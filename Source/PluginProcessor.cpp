#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace splint;

static const char* kDecoIds[8]   = { "repeat", "glitch", "warp", "shift", "flip", "gate", "crush", "drop" };
static const char* kDecoNames[8] = { "Битрипит", "Глитч", "Варп", "Сдвиг", "Переворот", "Гейт", "Краш", "Сбой" };
const char* splintDecoName (int i) { return kDecoNames[juce::jlimit (0, 7, i)]; }

//==============================================================================
void Voice::startRelease (int samplesFromNow, int relLen)
{
    if (! active) return;
    if (delay >= samplesFromNow) { active = false; return; }
    releaseDelay = samplesFromNow;
    releaseLen = juce::jmax (16, relLen);
}

static inline float readInterp (const juce::AudioBuffer<float>& src, int ch, double p)
{
    const int len = src.getNumSamples();
    if (p < 0.0 || p >= (double) (len - 2)) return 0.0f;
    const int i0 = (int) p;
    const float fr = (float) (p - i0);
    const float* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
    return d[i0] * (1.0f - fr) + d[i0 + 1] * fr;
}

void Voice::render (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& src, int numSamples)
{
    const int outLen = out.getNumSamples();
    float* oL = out.getWritePointer (0);
    float* oR = out.getWritePointer (juce::jmin (1, out.getNumChannels() - 1));

    for (int i = 0; i < numSamples && i < outLen; ++i)
    {
        if (! active) return;
        if (delay > 0) { --delay; continue; }

        if (releaseDelay > 0) --releaseDelay;
        else if (releaseDelay == 0) { releasing = true; releaseLeft = releaseLen; releaseDelay = -1; }

        float env;
        if (releasing)
        {
            env = (float) releaseLeft / (float) releaseLen;
            if (--releaseLeft <= 0) { active = false; return; }
        }
        else
        {
            const int remain = total - played;
            if (remain <= 0) { active = false; return; }
            env = 1.0f;
            if (played < fadeIn)  env = (float) played / (float) fadeIn;
            if (remain < fadeOut) env = juce::jmin (env, (float) remain / (float) fadeOut);
        }

        if (gatePeriod > 4.0)
        {
            const double ph = std::fmod ((double) played, gatePeriod) / gatePeriod;
            const double edge = 0.03;
            float g = 0.0f;
            if (ph < 0.5) g = (float) juce::jmin (1.0, juce::jmin (ph, 0.5 - ph) / edge);
            env *= g;
        }

        float s0 = 0.0f, s1 = 0.0f;
        if (stretch)
        {
            for (int k = 0; k < 2; ++k)
            {
                if (gPhase[k] >= grainLen) { gPhase[k] = 0.0; gStart[k] = scanPos; }
                const double rp = gStart[k] + gPhase[k] * pitchRatio * (rev ? -1.0 : 1.0);
                const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * gPhase[k] / grainLen);
                s0 += (float) w * readInterp (src, 0, rp);
                s1 += (float) w * readInterp (src, 1, rp);
                gPhase[k] += 1.0;
            }
            scanPos += scanInc;
            if (scanPos < 1.0 || scanPos >= (double) (src.getNumSamples() - 2)) { active = false; return; }
        }
        else
        {
            s0 = readInterp (src, 0, pos);
            s1 = readInterp (src, 1, pos);
            double incNow = incEnd;
            if (ramp && rampLen > 0 && played < rampLen) incNow = inc + (incEnd - inc) * ((double) played / (double) rampLen);
            else if (! ramp) incNow = inc;
            if (wobDepth > 0.0)
            {
                wobPhase += wobInc;
                incNow *= 1.0 + wobDepth * std::sin (wobPhase);
            }
            pos += incNow;
            if (pos < 1.0 || pos >= (double) (src.getNumSamples() - 2)) { active = false; return; }
        }

        if (crushHold > 1)
        {
            if (crushCnt <= 0) { crushCnt = crushHold; crushV[0] = std::round (s0 * crushQ) / crushQ; crushV[1] = std::round (s1 * crushQ) / crushQ; }
            --crushCnt;
            s0 = crushV[0]; s1 = crushV[1];
        }

        const float a = env * gain;
        oL[i] += s0 * a * panL;
        oR[i] += s1 * a * panR;
        ++played;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SplintProcessor::makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const char* s) { return ParameterID { s, 1 }; };

    StringArray styleNames;
    for (auto& s : styles()) styleNames.add (String::fromUTF8 (s.name));
    l.add (std::make_unique<AudioParameterChoice> (pid ("style"), juce::String::fromUTF8 ("Стиль"), styleNames, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("bars"), juce::String::fromUTF8 ("Тактов"), StringArray { "1", "2", "4" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid ("slicemode"), juce::String::fromUTF8 ("Нарезка"), StringArray { juce::String::fromUTF8 ("Фразы"), juce::String::fromUTF8 ("Атаки"), juce::String::fromUTF8 ("Сетка") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("slicecount"), juce::String::fromUTF8 ("Кусков"), StringArray { juce::String::fromUTF8 ("Авто"), "4", "8", "16" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("engine"), juce::String::fromUTF8 ("Движок"), StringArray { juce::String::fromUTF8 ("Сэмплер"), juce::String::fromUTF8 ("Стретч") }, 0));

    l.add (std::make_unique<AudioParameterFloat> (pid ("morph"), juce::String::fromUTF8 ("Морф A-B"), NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("swing"), juce::String::fromUTF8 ("Свинг"), NormalisableRange<float> (50.0f, 75.0f), 56.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("pitch"), juce::String::fromUTF8 ("Питч"), NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("speed"), juce::String::fromUTF8 ("Скорость"), NormalisableRange<float> (0.5f, 2.0f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("vol"), juce::String::fromUTF8 ("Громкость"), NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    l.add (std::make_unique<AudioParameterBool> (pid ("mono"), juce::String::fromUTF8 ("Моно-глушение"), true));
    l.add (std::make_unique<AudioParameterBool> (pid ("freerun"), juce::String::fromUTF8 ("Играть без транспорта"), false));

    l.add (std::make_unique<AudioParameterFloat> (pid ("cutoff"), juce::String::fromUTF8 ("Фильтр"), NormalisableRange<float> (60.0f, 18000.0f, 1.0f, 0.3f), 18000.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("res"), juce::String::fromUTF8 ("Резонанс"), NormalisableRange<float> (0.0f, 1.0f), 0.1f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("drive"), juce::String::fromUTF8 ("Драйв"), NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("dmix"), juce::String::fromUTF8 ("Эхо"), NormalisableRange<float> (0.0f, 1.0f), 0.12f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("dtime"), juce::String::fromUTF8 ("Время эха"), NormalisableRange<float> (1.0f, 8.0f, 1.0f), 3.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("dfb"), juce::String::fromUTF8 ("Повторы эха"), NormalisableRange<float> (0.0f, 0.9f), 0.3f));

    l.add (std::make_unique<AudioParameterFloat> (pid ("hits"), juce::String::fromUTF8 ("Кусков в такте"), NormalisableRange<float> (1.0f, 10.0f, 1.0f), 4.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("air"), juce::String::fromUTF8 ("Паузы"), NormalisableRange<float> (0.0f, 1.0f), 0.15f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("mad"), juce::String::fromUTF8 ("Безумие"), NormalisableRange<float> (0.0f, 1.0f), 0.12f));
    l.add (std::make_unique<AudioParameterFloat> (pid ("pvar"), juce::String::fromUTF8 ("Питч-вариации"), NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    StringArray figNames;
    for (int i = 0; i < 12; ++i) figNames.add (String::fromUTF8 (figureName ((FigKind) i)));
    l.add (std::make_unique<AudioParameterChoice> (pid ("fig"), juce::String::fromUTF8 ("Фигура"), figNames, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid ("figwhere"), juce::String::fromUTF8 ("Где"), StringArray { juce::String::fromUTF8 ("Весь квадрат"), juce::String::fromUTF8 ("Последний такт"), juce::String::fromUTF8 ("Последние 2 доли"), juce::String::fromUTF8 ("Последняя доля") }, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid ("figsrc"), juce::String::fromUTF8 ("Звук фигуры"), StringArray { juce::String::fromUTF8 ("Повтор"), juce::String::fromUTF8 ("Фраза дальше"), juce::String::fromUTF8 ("Фрагменты подряд"), juce::String::fromUTF8 ("Первый фрагмент") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("figpitch"), juce::String::fromUTF8 ("Питч фигуры"), StringArray { juce::String::fromUTF8 ("Ровно"), juce::String::fromUTF8 ("Вверх"), juce::String::fromUTF8 ("Вниз") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("figvel"), juce::String::fromUTF8 ("Громкость фигуры"), StringArray { juce::String::fromUTF8 ("Ровно"), juce::String::fromUTF8 ("Нарастание"), juce::String::fromUTF8 ("Спад") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("autofill"), juce::String::fromUTF8 ("Авто-филл"), StringArray { juce::String::fromUTF8 ("Выкл"), juce::String::fromUTF8 ("Каждый круг"), juce::String::fromUTF8 ("Каждый 2-й"), juce::String::fromUTF8 ("Каждый 4-й") }, 0));

    l.add (std::make_unique<AudioParameterFloat> (pid ("chaos"), juce::String::fromUTF8 ("Хаос"), NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    for (int i = 0; i < 8; ++i)
    {
        l.add (std::make_unique<AudioParameterBool> (pid ((juce::String ("on_") + kDecoIds[i]).toRawUTF8()),
                                                     juce::String::fromUTF8 (kDecoNames[i]), false));
        l.add (std::make_unique<AudioParameterFloat> (pid ((juce::String ("amt_") + kDecoIds[i]).toRawUTF8()),
                                                      juce::String::fromUTF8 (kDecoNames[i]) + juce::String::fromUTF8 (" сила"),
                                                      NormalisableRange<float> (0.0f, 1.0f), 0.35f));
    }
    l.add (std::make_unique<AudioParameterChoice> (pid ("repsize"), juce::String::fromUTF8 ("Размер битрипита"), StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (pid ("replen"), juce::String::fromUTF8 ("Длина битрипита"), StringArray { "2/16", "4/16", "8/16" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid ("repmode"), juce::String::fromUTF8 ("Ход битрипита"), StringArray { juce::String::fromUTF8 ("Ровно"), juce::String::fromUTF8 ("Разгон"), juce::String::fromUTF8 ("Вверх"), juce::String::fromUTF8 ("Затухание") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("glsize"), juce::String::fromUTF8 ("Дробление глитча"), StringArray { "1/32", "1/64", "1/128" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid ("warpmode"), juce::String::fromUTF8 ("Тип варпа"), StringArray { juce::String::fromUTF8 ("Всё"), juce::String::fromUTF8 ("Стоп"), juce::String::fromUTF8 ("Раскрутка"), juce::String::fromUTF8 ("Плавание") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("shiftset"), juce::String::fromUTF8 ("Интервалы сдвига"), StringArray { juce::String::fromUTF8 ("Октавы"), juce::String::fromUTF8 ("Квинты"), juce::String::fromUTF8 ("Полутона") }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid ("gaterate"), juce::String::fromUTF8 ("Шаг гейта"), StringArray { "1/16", juce::String::fromUTF8 ("Триоли"), "1/32", "1/64" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (pid ("crushhard"), juce::String::fromUTF8 ("Жёсткость краша"), NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    return l;
}

//==============================================================================
SplintProcessor::SplintProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SPLINT", makeLayout())
{
    formats.registerBasicFormats();
    for (auto& a : decoHeldUi) a.store (false);
    for (auto& a : decoFire) a.store (0);
    makeDemoSample();
}

bool SplintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SplintProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 3.0));
    delayLine.reset();
    for (auto& f : delayHP)
    {
        f.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 250.0f);
    }
    voiceBuf.setSize (2, juce::jmax (64, samplesPerBlock));
    for (auto& v : voices) v.active = false;
    rep = RepState();
    fill = FillState();
}

int SplintProcessor::numSteps() const
{
    const int b = choiceOf ("bars");
    return (b == 0 ? 1 : b == 1 ? 2 : 4) * 16;
}

double SplintProcessor::getSampleSeconds() const
{
    juce::SpinLock::ScopedLockType lock (dataLock);
    return sample ? sample->getNumSamples() / sampleSR : 0.0;
}

double SplintProcessor::advancePerStep (double samplesPerStep) const
{
    if (! sample || sample->getNumSamples() < 2) return 0.0;
    const double pitchRatio = std::pow (2.0, valOf ("pitch") / 12.0);
    const double ratio = sampleSR / sr;
    const double inc = valOf ("speed") * (choiceOf ("engine") == 1 ? 1.0 : pitchRatio) * ratio;
    return samplesPerStep * inc / (double) sample->getNumSamples();
}

double SplintProcessor::probOf (int idx) const
{
    if (decoHeldUi[(size_t) idx].load() || midiDecoHeld[idx]) return 1.0;
    const juce::String on = juce::String ("on_") + kDecoIds[idx];
    if (apvts.getRawParameterValue (on)->load() < 0.5f) return 0.0;
    const juce::String amt = juce::String ("amt_") + kDecoIds[idx];
    return juce::jlimit (0.0, 1.0, (double) apvts.getRawParameterValue (amt)->load() * (double) valOf ("chaos"));
}

//==============================================================================
Voice* SplintProcessor::findVoice()
{
    for (auto& v : voices) if (! v.active) return &v;
    Voice* oldest = &voices[0];
    for (auto& v : voices) if (v.played > oldest->played) oldest = &v;
    return oldest;
}

void SplintProcessor::chokeAt (int offset)
{
    if (! boolOf ("mono")) { numLastVoices = 0; return; }
    for (int i = 0; i < numLastVoices; ++i)
        for (auto& v : voices)
            if (v.active && v.id == lastVoiceIds[i]) v.startRelease (offset, (int) (sr * 0.012));
    numLastVoices = 0;
}

void SplintProcessor::spawnVoice (const splint::Hit& h, int offset, int lengthSamples, double semis, float amp,
                                  int warp, bool crush, double gatePeriod)
{
    if (! sample || lengthSamples < 32) return;
    auto* v = findVoice();
    *v = Voice();
    v->id = ++voiceCounter;
    v->active = true;
    v->delay = juce::jmax (0, offset);
    v->total = lengthSamples;
    v->gain = juce::jlimit (0.0f, 2.0f, amp);
    const float pan = juce::jlimit (-1.0f, 1.0f, h.pan);
    v->panL = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
    v->panR = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
    v->rev = h.rev;
    v->gatePeriod = gatePeriod;

    const double D = (double) sample->getNumSamples();
    const double ratioSR = sampleSR / sr;
    const double pitchRatio = std::pow (2.0, semis / 12.0);
    const double base = juce::jlimit (0.05, 8.0, (double) h.rate * (double) valOf ("speed"));
    const bool stretch = choiceOf ("engine") == 1 && warp == 0;
    const double startPos = juce::jlimit (2.0, D - 4.0, h.pos * D);

    if (stretch)
    {
        v->stretch = true;
        v->pitchRatio = pitchRatio * ratioSR;
        v->scanInc = base * ratioSR * (h.rev ? -1.0 : 1.0);
        v->scanPos = startPos;
        v->grainLen = juce::jmax (256.0, 0.08 * sr);
        v->gPhase[0] = 0.0;
        v->gPhase[1] = v->grainLen * 0.5;
        v->gStart[0] = v->gStart[1] = startPos;
    }
    else
    {
        v->pos = startPos;
        const double incBase = base * pitchRatio * ratioSR * (h.rev ? -1.0 : 1.0);
        v->inc = incBase;
        v->incEnd = incBase;
        if (warp == 1) { v->ramp = true; v->incEnd = incBase * 0.05; v->rampLen = lengthSamples; }
        else if (warp == 2) { v->ramp = true; v->inc = incBase * 0.25; v->incEnd = incBase; v->rampLen = juce::jmin (lengthSamples, (int) (0.35 * sr)); }
        else if (warp == 3) { v->wobDepth = 0.14; v->wobInc = 2.0 * juce::MathConstants<double>::pi * (4.0 + rnd.nextDouble() * 5.0) / sr; }
    }

    if (crush)
    {
        const double hard = valOf ("crushhard");
        v->crushHold = (int) std::round (2.0 + hard * 14.0);
        v->crushQ = (float) std::pow (2.0, std::round (8.0 - hard * 5.0) - 1.0);
    }
    v->fadeIn = juce::jmin (juce::jmax (16, lengthSamples / 8), (int) (sr * 0.004));
    v->fadeOut = juce::jmin (juce::jmax (32, lengthSamples / 4), (int) (sr * 0.012));

    if (numLastVoices < 8) lastVoiceIds[numLastVoices++] = v->id;
}

void SplintProcessor::startHit (const splint::Hit& h, int offset, double samplesPerStep,
                                int warp, bool glitch, bool crush, double gatePeriod)
{
    if (! sample) return;
    const int gate = juce::jmax (64, (int) (h.len * samplesPerStep));
    chokeAt (offset);
    const double semisBase = valOf ("pitch") + h.pitch;

    if (glitch)
    {
        const int glChoice = choiceOf ("glsize");
        const double sizeSteps = glChoice == 0 ? 0.5 : glChoice == 1 ? 0.25 : 0.125;
        const int chunk = juce::jmax (256, (int) (sizeSteps * samplesPerStep));
        splint::Hit frag = h;
        for (int t = 0, k = 0; t < gate - 64 && k < 96; t += chunk, ++k)
        {
            if (k == 0 || rnd.nextDouble() > 0.45)
            {
                frag = h;
                frag.pos = juce::jlimit (0.0, 0.999, h.pos + (rnd.nextDouble() - 0.5) * 0.6 / juce::jmax (0.1, getSampleSecondsUnlocked()));
                frag.rev = rnd.nextDouble() < 0.3;
            }
            const double semis = semisBase + (rnd.nextDouble() < 0.2 ? (rnd.nextDouble() < 0.5 ? 12.0 : -12.0) : 0.0);
            spawnVoice (frag, offset + t, juce::jmin (chunk - 32, gate - t), semis, h.gain * 0.9f, 0, crush, gatePeriod);
        }
        return;
    }

    const int n = juce::jlimit (1, 8, h.rat);
    const int sub = gate / n;
    for (int k = 0; k < n; ++k)
    {
        const double semis = semisBase;
        const float amp = h.gain * (1.0f - juce::jmin (0.5f, k * 0.08f)) * (n > 1 ? 0.9f : 1.0f);
        spawnVoice (h, offset + k * sub, n > 1 ? juce::jmax (64, (int) (sub * 0.92)) : gate, semis, amp, warp, crush, gatePeriod);
    }
}

double SplintProcessor::getSampleSecondsUnlocked() const
{
    return sample ? sample->getNumSamples() / sampleSR : 1.0;
}

void SplintProcessor::playDeco (const splint::Hit& hIn, int offset, double samplesPerStep)
{
    splint::Hit h = hIn;
    const double pDrop = probOf (7);
    if (pDrop > 0.0 && rnd.nextDouble() < pDrop * 0.55) { chokeAt (offset); decoFire[7].store (1); return; }
    if (pDrop > 0.0 && rnd.nextDouble() < pDrop * 0.35) { offset += (int) (rnd.nextDouble() * 0.5 * samplesPerStep); decoFire[7].store (1); }

    if (rnd.nextDouble() < probOf (3))
    {
        const int set = choiceOf ("shiftset");
        static const int oct[4] { 12, -12, 24, -12 };
        static const int fifth[5] { 7, -5, 12, 5, -7 };
        static const int chrom[7] { 1, -1, 2, -2, 3, -3, 6 };
        h.pitch += set == 0 ? oct[rnd.nextInt (4)] : set == 1 ? fifth[rnd.nextInt (5)] : chrom[rnd.nextInt (7)];
        decoFire[3].store (1);
    }
    if (rnd.nextDouble() < probOf (4))
    {
        const double span = h.len * samplesPerStep * (sampleSR / sr) / juce::jmax (1.0, (double) sample->getNumSamples());
        h.pos = juce::jlimit (0.0, 0.999, h.rev ? h.pos - span : h.pos + span);
        h.rev = ! h.rev;
        decoFire[4].store (1);
    }
    int warp = 0;
    if (rnd.nextDouble() < probOf (2))
    {
        const int m = choiceOf ("warpmode");
        warp = m == 0 ? 1 + rnd.nextInt (3) : m;
        decoFire[2].store (1);
    }
    bool glitch = false;
    if (rnd.nextDouble() < probOf (1)) { glitch = true; decoFire[1].store (1); }
    double gatePeriod = 0.0;
    if (rnd.nextDouble() < probOf (5))
    {
        const int g = choiceOf ("gaterate");
        const double steps = g == 0 ? 1.0 : g == 1 ? 2.0 / 3.0 : g == 2 ? 0.5 : 0.25;
        gatePeriod = steps * samplesPerStep;
        decoFire[5].store (1);
    }
    bool crush = false;
    if (rnd.nextDouble() < probOf (6)) { crush = true; decoFire[6].store (1); }

    startHit (h, offset, samplesPerStep, warp, glitch, crush, gatePeriod);
}

void SplintProcessor::scheduleStep (int i, long long abs, int offset, double samplesPerStep, double morph)
{
    const int n = numSteps();
    const double adv = advancePerStep (samplesPerStep);
    auto laneFor = [&] (int beat) -> const splint::Pattern&
    {
        if (morph <= 0.001) return patterns[0];
        if (morph >= 0.999) return patterns[1];
        return hash01 (beat, 777) < morph ? patterns[1] : patterns[0];
    };
    const bool repeatHeld = decoHeldUi[0].load() || midiDecoHeld[0];

    // битрипит
    if (rep.active && (abs >= rep.until || (rep.hold && ! repeatHeld))) rep.active = false;
    if (! rep.active && (repeatHeld || (i % 4 == 0 && rnd.nextDouble() < probOf (0) * 0.6)))
    {
        splint::Hit snap;
        if (soundAt (laneFor (i / 4), i, n, adv, snap))
        {
            const int sc = choiceOf ("repsize");
            const int lc = choiceOf ("replen");
            rep.active = true;
            rep.hold = repeatHeld;
            rep.start = abs;
            rep.size = sc == 0 ? 4.0 : sc == 1 ? 2.0 : sc == 2 ? 1.0 : 0.5;
            rep.until = repeatHeld ? (long long) 1e15 : abs + (lc == 0 ? 2 : lc == 1 ? 4 : 8);
            rep.mode = choiceOf ("repmode");
            rep.snap = snap;
            rep.snap.rat = 1;
            decoFire[0].store (1);
        }
    }
    if (rep.active)
    {
        const double r = (double) (abs - rep.start);
        double size = rep.size;
        if (rep.mode == 1)
        {
            const double total = rep.hold ? 16.0 : (double) (rep.until - rep.start);
            const int k = (int) juce::jlimit (0.0, 3.0, std::floor (rep.hold ? r / 4.0 : (r / total) * 3.0));
            size = rep.size / std::pow (2.0, k);
        }
        size = juce::jmax (0.125, size);
        const double totalSteps = rep.hold ? 16.0 : (double) (rep.until - rep.start);
        for (int k = (int) std::ceil (r / size - 1e-9); k * size < r + 1.0 - 1e-9; ++k)
        {
            splint::Hit x = rep.snap;
            x.len = size * 0.95;
            x.rat = 1;
            if (rep.mode == 2) x.pitch += juce::jmin (12, k);
            if (rep.mode == 3) x.gain *= (float) juce::jmax (0.12, 1.0 - (k * size) / juce::jmax (1.0, totalSteps));
            startHit (x, offset + (int) ((k * size - r) * samplesPerStep), samplesPerStep, 0, false, false, 0.0);
        }
        return;
    }

    // филлы
    const bool fillHeld = fillHeldUi.load() || midiFillHeld;
    splint::FigParams fp;
    fp.kind = (FigKind) choiceOf ("fig");
    fp.where = choiceOf ("figwhere");
    fp.source = choiceOf ("figsrc");
    fp.pitchMode = choiceOf ("figpitch");
    fp.velMode = choiceOf ("figvel");

    bool haveFill = false;
    if (fillHeld)
    {
        if (! fill.active || ! fill.cyclic)
        {
            splint::Hit proto;
            const bool has = soundAt (laneFor (i / 4), i, n, adv, proto);
            fill.active = true; fill.cyclic = true; fill.start = abs; fill.len = 16.0;
            fill.hits = buildFigure (0.0, 16.0, has ? proto.pos : (slices.empty() ? 0.0 : slices[0]), slices, fp, proto, adv);
        }
        haveFill = true;
    }
    else
    {
        if (fill.active && fill.cyclic) fill.active = false;
        const int af = choiceOf ("autofill");
        if (af > 0)
        {
            const int every = af == 1 ? 1 : af == 2 ? 2 : 4;
            const long long loopNo = (long long) std::floor ((double) abs / n);
            if (((loopNo + 1) % every) == 0)
            {
                double rs = 0.0, rl = 16.0;
                figureRegion (fp, n, rs, rl);
                if ((double) i == rs)
                {
                    splint::Hit proto;
                    const bool has = soundAt (laneFor (i / 4), i, n, adv, proto);
                    fill.active = true; fill.cyclic = false; fill.start = abs; fill.len = rl;
                    fill.hits = buildFigure (0.0, rl, has ? proto.pos : (slices.empty() ? 0.0 : slices[0]), slices, fp, proto, adv);
                }
                if (fill.active && ! fill.cyclic && (double) i >= rs && (double) i < rs + rl && (double) (abs - fill.start) < fill.len)
                    haveFill = true;
            }
        }
    }
    if (haveFill && fill.active)
    {
        double r = (double) (abs - fill.start);
        if (fill.cyclic) r = std::fmod (r, fill.len);
        for (const auto& h : fill.hits)
            if (h.start >= r - 1e-9 && h.start < r + 1.0 - 1e-9)
                playDeco (h, offset + (int) ((h.start - r) * samplesPerStep), samplesPerStep);
        return;
    }

    // обычный паттерн
    const auto& P = laneFor (i / 4);
    bool played = false;
    for (const auto& h : P)
        if (h.start >= i - 1e-9 && h.start < i + 1.0 - 1e-9)
        {
            playDeco (h, offset + (int) ((h.start - i) * samplesPerStep), samplesPerStep);
            played = true;
        }
    if (played || i % 4 != 0) return;

    // морф сменил дорожку на новой доле: подхватываем то, что звучало
    const int prevBeat = (int) std::floor ((double) (((i - 1) + n) % n) / 4.0);
    const bool sameLane = (&laneFor (prevBeat) == &P);
    if (sameLane) return;
    splint::Hit cont;
    if (soundAt (P, i, n, adv, cont)) startHit (cont, offset, samplesPerStep, 0, false, false, 0.0);
    else chokeAt (offset);
}

//==============================================================================
void SplintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int nn = m.getNoteNumber();
            if (nn >= 36 && nn < 44) midiDecoHeld[nn - 36] = true;
            else if (nn == 44) midiFillHeld = true;
        }
        else if (m.isNoteOff())
        {
            const int nn = m.getNoteNumber();
            if (nn >= 36 && nn < 44) midiDecoHeld[nn - 36] = false;
            else if (nn == 44) midiFillHeld = false;
        }
    }
    midi.clear();

    double bpm = 120.0, ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto p = ph->getPosition())
        {
            if (auto b = p->getBpm()) bpm = *b;
            if (auto q = p->getPpqPosition()) ppq = *q;
            hostPlaying = p->getIsPlaying();
        }
    const double samplesPerStep = juce::jmax (16.0, sr * 60.0 / bpm / 4.0);
    const bool running = hostPlaying || boolOf ("freerun");
    const int n = numSteps();

    double startStep = hostPlaying ? ppq * 4.0 : freeStep;
    const double endStep = startStep + numSamples / samplesPerStep;

    {
        const juce::SpinLock::ScopedTryLockType lock (dataLock);
        if (lock.isLocked())
        {
            activeSample = sample;
            if (running && sample && sample->getNumSamples() > 1000)
            {
                const double swingOff = (valOf ("swing") / 100.0 - 0.5) * 2.0;
                const double morph = valOf ("morph");
                long long s = (long long) std::ceil (startStep - 1e-9);
                int guard = 0;
                while ((double) s < endStep && guard++ < 64)
                {
                    if (s != lastAbsStep)
                    {
                        const double t = (double) s + ((s % 2 != 0) ? swingOff : 0.0);
                        int offset = (int) std::floor ((t - startStep) * samplesPerStep);
                        offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), offset);
                        const int i = (int) (((s % n) + n) % n);
                        scheduleStep (i, s, offset, samplesPerStep, morph);
                        lastAbsStep = s;
                    }
                    ++s;
                }
            }
        }
    }
    freeStep = running ? endStep : startStep;
    currentStep.store (std::fmod (std::fmod (startStep, (double) n) + n, (double) n));

    voiceBuf.setSize (2, juce::jmax (numSamples, 64), false, false, true);
    voiceBuf.clear();
    if (activeSample && activeSample->getNumSamples() > 8)
        for (auto& v : voices)
            if (v.active) v.render (voiceBuf, *activeSample, numSamples);

    // эффекты
    filter.setCutoffFrequency (juce::jlimit (40.0f, (float) (sr * 0.45), valOf ("cutoff")));
    filter.setResonance (juce::jlimit (0.1f, 8.0f, 0.7f + valOf ("res") * 7.0f));
    delayLine.setDelay ((float) juce::jlimit (32.0, sr * 2.5, valOf ("dtime") * samplesPerStep));

    const float drive = valOf ("drive");
    const float driveK = 1.0f + drive * 20.0f;
    const float dryG = 1.0f - drive * 0.3f;
    const float wet = valOf ("dmix");
    const float fb = valOf ("dfb");
    const float vol = valOf ("vol");

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));
    const float* vL = voiceBuf.getReadPointer (0);
    const float* vR = voiceBuf.getReadPointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        float l = vL[i], r = vR[i];
        if (drive > 0.01f)
        {
            const float norm = std::tanh (driveK);
            l = std::tanh (l * driveK) / norm;
            r = std::tanh (r * driveK) / norm;
        }
        l = filter.processSample (0, l);
        r = filter.processSample (1, r);

        float dl = delayLine.popSample (0);
        float dr = delayLine.popSample (1);
        dl = delayHP[0].processSample (dl);
        dr = delayHP[1].processSample (dr);
        delayLine.pushSample (0, l + dl * fb);
        delayLine.pushSample (1, r + dr * fb);

        float oL = l * dryG + dl * wet;
        float oR = r * dryG + dr * wet;
        oL = std::tanh (oL * 1.1f) * vol;
        oR = std::tanh (oR * 1.1f) * vol;
        outL[i] = oL;
        if (outR != outL) outR[i] = oR;
    }
}

//==============================================================================
static void normaliseBuffer (juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) peak = juce::jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
    if (peak > 1.0e-5f) b.applyGain (0.95f / peak);
}

void SplintProcessor::makeDemoSample()
{
    const double srOut = 44100.0;
    const double len = 5.4;
    auto buf = std::make_shared<juce::AudioBuffer<float>> (1, (int) (srOut * len));
    buf->clear();
    float* d = buf->getWritePointer (0);

    struct Syl { const char* v; double f, dur; };
    struct Phrase { double t; std::vector<Syl> syl; };
    auto formants = [] (const juce::String& v) -> std::array<double, 3>
    {
        if (v == "a") return { 800.0, 1150.0, 2900.0 };
        if (v == "o") return { 450.0, 800.0, 2830.0 };
        if (v == "i") return { 270.0, 2140.0, 2950.0 };
        if (v == "e") return { 400.0, 1700.0, 2600.0 };
        return { 325.0, 700.0, 2530.0 };
    };
    const std::vector<Phrase> phrases = {
        { 0.05, { { "a", 220, 0.34 }, { "o", 247, 0.30 }, { "i", 294, 0.26 }, { "e", 262, 0.55 } } },
        { 1.80, { { "u", 196, 0.30 }, { "a", 330, 0.32 }, { "i", 294, 0.24 }, { "o", 247, 0.62 } } },
        { 3.60, { { "e", 220, 0.40 }, { "a", 262, 0.36 }, { "o", 220, 0.90 } } },
    };

    for (const auto& ph : phrases)
    {
        double t = ph.t;
        double total = 0.0;
        for (const auto& s : ph.syl) total += s.dur;
        const double t1 = ph.t + total;
        double phase = 0.0;
        for (const auto& s : ph.syl)
        {
            const auto F = formants (juce::String (s.v));
            const int start = (int) (t * srOut), end = juce::jmin (buf->getNumSamples(), (int) ((t + s.dur) * srOut));
            for (int i = start; i < end; ++i)
            {
                const double time = (double) i / srOut;
                const double vib = 1.0 + 0.012 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.3 * time) * juce::jlimit (0.0, 1.0, (time - ph.t) / juce::jmax (0.1, total));
                const double f0 = s.f * vib;
                phase += f0 / srOut;
                double v = 0.0;
                for (int hnum = 1; hnum <= 40; ++hnum)
                {
                    const double fh = f0 * hnum;
                    if (fh > 5000.0) break;
                    double amp = 1.0 / hnum;
                    double res = 0.0;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double bw = 90.0 + k * 60.0;
                        const double x = (fh - F[(size_t) k]) / bw;
                        res += (k == 0 ? 1.0 : k == 1 ? 0.6 : 0.35) / (1.0 + x * x);
                    }
                    v += amp * res * std::sin (2.0 * juce::MathConstants<double>::pi * hnum * phase);
                }
                // огибающая: атака, провалы между слогами, спад в конце фразы
                double e = 1.0;
                const double fromStart = time - ph.t, toEnd = t1 - time;
                e *= juce::jlimit (0.0, 1.0, fromStart / 0.04);
                e *= juce::jlimit (0.0, 1.0, toEnd / 0.12);
                const double sylPos = time - t;
                if (sylPos < 0.05) e *= 0.45 + 0.55 * (sylPos / 0.05);
                d[i] += (float) (v * 0.12 * e);
            }
            t += s.dur;
        }
    }
    normaliseBuffer (*buf);

    const juce::SpinLock::ScopedLockType lock (dataLock);
    sample = buf;
    sampleSR = srOut;
    sampleName = juce::String::fromUTF8 ("Демо-вокал");
    samplePath = {};
    computeSlices();
    computePeaks();
    regenerateLocked (false);
}

bool SplintProcessor::loadSampleFile (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) return false;
    const int maxLen = (int) juce::jmin ((juce::int64) (reader->sampleRate * 60.0), (juce::int64) reader->lengthInSamples);
    if (maxLen < 100) return false;
    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jmin ((unsigned) 2, reader->numChannels), maxLen);
    reader->read (buf.get(), 0, maxLen, 0, true, true);
    normaliseBuffer (*buf);

    const juce::SpinLock::ScopedLockType lock (dataLock);
    sample = buf;
    sampleSR = reader->sampleRate;
    sampleName = f.getFileName();
    samplePath = f.getFullPathName();
    computeSlices();
    computePeaks();
    regenerateLocked (false);
    return true;
}

void SplintProcessor::computeSlices()
{
    if (! sample) { slices = { 0.0 }; return; }
    const double dur = sample->getNumSamples() / sampleSR;
    const int cc = choiceOf ("slicecount");
    const int count = cc == 0 ? 8 : cc == 1 ? 4 : cc == 2 ? 8 : 16;
    const int mode = choiceOf ("slicemode");
    if (mode == 2) slices = sliceGrid (count);
    else if (mode == 1) slices = detectTransients (*sample, dur, count);
    else slices = detectPhrases (*sample, dur, cc == 0 ? 0 : count);
    if (slices.empty()) slices = { 0.0 };
}

void SplintProcessor::computePeaks()
{
    peaks.assign (2000, 0.0f);
    if (! sample) return;
    const int N = sample->getNumSamples();
    const double spb = (double) N / 2000.0;
    float mx = 1.0e-6f;
    for (int b = 0; b < 2000; ++b)
    {
        const int s0 = (int) (b * spb), s1 = juce::jmin (N, (int) ((b + 1) * spb));
        float m = 0.0f;
        for (int ch = 0; ch < sample->getNumChannels(); ++ch)
        {
            const float* d = sample->getReadPointer (ch);
            for (int i = s0; i < s1; i += 8) m = juce::jmax (m, std::abs (d[i]));
        }
        peaks[(size_t) b] = m;
        mx = juce::jmax (mx, m);
    }
    for (auto& v : peaks) v /= mx;
}

void SplintProcessor::regenerateLocked (bool newSeed)
{
    if (newSeed) seed = 1 + rnd.nextInt (99999);
    GenParams gp;
    gp.hits = (int) valOf ("hits");
    gp.air = valOf ("air");
    gp.madness = valOf ("mad");
    gp.pitchVar = valOf ("pvar");
    FigParams fp;
    fp.kind = (FigKind) choiceOf ("fig");
    fp.where = choiceOf ("figwhere");
    fp.source = choiceOf ("figsrc");
    fp.pitchMode = choiceOf ("figpitch");
    fp.velMode = choiceOf ("figvel");
    const double spStep = sr * 60.0 / 132.0 / 4.0;
    const double adv = advancePerStep (spStep);
    const int bars = numSteps() / 16;
    patterns[0] = generatePattern ((uint32_t) seed, choiceOf ("style"), bars, slices, gp, fp, adv);
    patterns[1] = generatePattern ((uint32_t) (seed + 101), choiceOf ("style"), bars, slices, gp, fp, adv);
}

void SplintProcessor::regenerate (bool newSeed)
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    regenerateLocked (newSeed);
}

void SplintProcessor::reslice()
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    computeSlices();
    regenerateLocked (false);
}

void SplintProcessor::insertFigure (int lane)
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    if (! sample) return;
    FigParams fp;
    fp.kind = (FigKind) choiceOf ("fig");
    fp.where = choiceOf ("figwhere");
    fp.source = choiceOf ("figsrc");
    fp.pitchMode = choiceOf ("figpitch");
    fp.velMode = choiceOf ("figvel");
    const int n = numSteps();
    double start = 0.0, len = 16.0;
    figureRegion (fp, n, start, len);
    const double adv = advancePerStep (sr * 60.0 / 132.0 / 4.0);
    auto& P = patterns[juce::jlimit (0, 1, lane)];
    splint::Hit proto;
    const bool has = soundAt (P, start, n, adv, proto);
    const double pos0 = has ? proto.pos : (slices.empty() ? 0.0 : slices[0]);
    auto figure = buildFigure (start, len, pos0, slices, fp, proto, adv);
    splint::Pattern keep;
    for (auto h : P)
    {
        if (h.start >= start && h.start < start + len) continue;
        if (h.start < start && h.start + h.len > start) h.len = juce::jmax (0.25, start - h.start);
        keep.push_back (h);
    }
    for (auto& h : figure) keep.push_back (h);
    P = keep;
}

std::vector<float> SplintProcessor::getPeaks()
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return peaks;
}
std::vector<double> SplintProcessor::getSlices()
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return slices;
}
splint::Pattern SplintProcessor::getPattern (int lane)
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return patterns[juce::jlimit (0, 1, lane)];
}

//==============================================================================
static juce::String patternToString (const splint::Pattern& p)
{
    juce::StringArray parts;
    for (const auto& h : p)
        parts.add (juce::String (h.start) + "," + juce::String (h.len) + "," + juce::String (h.pos) + ","
                   + juce::String (h.gain) + "," + juce::String (h.rate) + "," + juce::String (h.pan) + ","
                   + juce::String (h.pitch) + "," + juce::String (h.rat) + "," + juce::String (h.rev ? 1 : 0));
    return parts.joinIntoString (";");
}
static splint::Pattern patternFromString (const juce::String& s)
{
    splint::Pattern p;
    auto rows = juce::StringArray::fromTokens (s, ";", "");
    for (auto& r : rows)
    {
        auto f = juce::StringArray::fromTokens (r, ",", "");
        if (f.size() < 9) continue;
        splint::Hit h;
        h.start = f[0].getDoubleValue(); h.len = f[1].getDoubleValue(); h.pos = f[2].getDoubleValue();
        h.gain = (float) f[3].getDoubleValue(); h.rate = (float) f[4].getDoubleValue(); h.pan = (float) f[5].getDoubleValue();
        h.pitch = f[6].getIntValue(); h.rat = f[7].getIntValue(); h.rev = f[8].getIntValue() != 0;
        p.push_back (h);
    }
    return p;
}

void SplintProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("SPLINT_EXTRA");
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        extra.setProperty ("seed", seed, nullptr);
        extra.setProperty ("samplePath", samplePath, nullptr);
        extra.setProperty ("A", patternToString (patterns[0]), nullptr);
        extra.setProperty ("B", patternToString (patterns[1]), nullptr);
    }
    state.appendChild (extra, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SplintProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid()) return;
    auto extra = tree.getChildWithName ("SPLINT_EXTRA").createCopy();
    tree.removeChild (tree.getChildWithName ("SPLINT_EXTRA"), nullptr);
    apvts.replaceState (tree);
    if (! extra.isValid()) return;
    const juce::String path = extra.getProperty ("samplePath", "").toString();
    if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSampleFile (juce::File (path));
    const juce::SpinLock::ScopedLockType lock (dataLock);
    seed = (int) extra.getProperty ("seed", 4127);
    auto a = patternFromString (extra.getProperty ("A", "").toString());
    auto b = patternFromString (extra.getProperty ("B", "").toString());
    if (! a.empty()) patterns[0] = a;
    if (! b.empty()) patterns[1] = b;
}

juce::AudioProcessorEditor* SplintProcessor::createEditor() { return new SplintEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SplintProcessor(); }
