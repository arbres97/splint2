#include "PluginEditor.h"

using namespace splint;

static juce::Colour sliceColour (int k)
{
    return juce::Colour::fromHSV ((float) std::fmod (k * 0.381966 + 0.57, 1.0), 0.55f, 0.85f, 1.0f);
}

void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1b1e24));
    g.fillRoundedRectangle (r, 6.0f);

    auto peaks = proc.getPeaks();
    auto slices = proc.getSlices();
    if (peaks.empty()) return;

    const float w = r.getWidth(), h = r.getHeight();
    for (size_t k = 0; k < slices.size(); ++k)
    {
        const float x0 = (float) slices[k] * w;
        const float x1 = (k + 1 < slices.size() ? (float) slices[k + 1] : 1.0f) * w;
        g.setColour (sliceColour ((int) k).withAlpha (0.16f));
        g.fillRect (x0, 0.0f, x1 - x0, h);
        g.setColour (sliceColour ((int) k).withAlpha (0.9f));
        g.fillRect (x0, 0.0f, 1.5f, h);
        g.setFont (11.0f);
        g.drawText (juce::String ((int) k + 1), (int) x0 + 3, (int) h - 16, 20, 14, juce::Justification::left);
    }
    g.setColour (juce::Colour (0xffd8dce2));
    const float mid = h * 0.5f;
    for (int x = 0; x < (int) w; ++x)
    {
        const float v = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (x / w * peaks.size()))];
        g.fillRect ((float) x, mid - v * mid * 0.85f, 1.0f, juce::jmax (1.0f, v * mid * 1.7f));
    }
}

void LaneView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1b1e24));
    g.fillRoundedRectangle (r, 6.0f);

    const int n = proc.numSteps();
    const float w = r.getWidth() / (float) n, h = r.getHeight();
    for (int b = 0; b < n / 4; ++b)
        if (b % 2)
        {
            g.setColour (juce::Colour (0xff23262d));
            g.fillRect (b * 4 * w, 0.0f, 4 * w, h);
        }
    g.setColour (juce::Colour (0xff3a3f48));
    for (int i = 0; i < n; i += 4) g.fillRect (i * w, 0.0f, 1.0f, h);
    for (int b = 16; b < n; b += 16) g.fillRect (b * w, 0.0f, 2.0f, h);

    auto slices = proc.getSlices();
    auto pattern = proc.getPattern (lane);
    auto peaks = proc.getPeaks();
    for (const auto& hit : pattern)
    {
        const int k = sliceIndexOf (slices, hit.pos);
        const double first = juce::jmin (hit.len, (double) n - hit.start);
        struct Seg { double s, l, off; };
        std::vector<Seg> segs { { hit.start, first, 0.0 } };
        if (hit.len > first) segs.push_back ({ 0.0, juce::jmin (hit.len - first, (double) n), first });
        for (auto& sg : segs)
        {
            const float x = (float) sg.s * w + 1.0f;
            const float bw = juce::jmax (3.0f, (float) sg.l * w - 2.0f);
            auto col = sliceColour (k);
            g.setColour (col.withAlpha (0.28f));
            g.fillRoundedRectangle (x, 6.0f, bw, h - 20.0f, 3.0f);
            g.setColour (col.withAlpha (0.95f));
            g.drawRoundedRectangle (x, 6.0f, bw, h - 20.0f, 3.0f, 1.0f);
            if (! peaks.empty())
            {
                const double span = hit.len * 0.02;
                const float midY = 6.0f + (h - 20.0f) * 0.5f;
                for (int c = 0; c < (int) bw; ++c)
                {
                    const double frac = (sg.off + (c / (double) bw) * sg.l) / juce::jmax (0.001, hit.len);
                    const double p = hit.rev ? hit.pos - frac * span : hit.pos + frac * span;
                    if (p < 0.0 || p >= 1.0) continue;
                    const float v = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (p * peaks.size()))] * (h - 26.0f) * 0.4f;
                    g.fillRect (x + c, midY - v, 1.0f, juce::jmax (1.0f, v * 2.0f));
                }
            }
            if (sg.off < 0.001 && bw > 16.0f)
            {
                juce::String lbl (k + 1);
                if (hit.rev) lbl += " <";
                if (hit.rat > 1) lbl += " x" + juce::String (hit.rat);
                if (hit.pitch != 0) lbl += (hit.pitch > 0 ? " +" : " ") + juce::String (hit.pitch);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (11.0f);
                g.drawText (lbl, (int) x + 3, 8, (int) bw - 5, 14, juce::Justification::left);
            }
        }
    }
    g.setColour (juce::Colour (0xffe0286f));
    const float px = (float) proc.currentStep.load() * w;
    g.fillRect (px, 0.0f, 2.0f, h);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (12.0f);
    g.drawText (lane == 0 ? "A" : "B", 6, (int) h - 18, 20, 14, juce::Justification::left);
}

//==============================================================================
SplintEditor::SplintEditor (SplintProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    auto addBtn = [this] (juce::Button& b) { addAndMakeVisible (b); };
    loadBtn.setButtonText (juce::String::fromUTF8 ("Загрузить семпл"));
    demoBtn.setButtonText (juce::String::fromUTF8 ("Демо"));
    genBtn.setButtonText (juce::String::fromUTF8 ("Сгенерировать A и B"));
    insABtn.setButtonText (juce::String::fromUTF8 ("Фигуру в A"));
    insBBtn.setButtonText (juce::String::fromUTF8 ("Фигуру в B"));
    addBtn (loadBtn); addBtn (demoBtn); addBtn (genBtn); addBtn (insABtn); addBtn (insBBtn);
    fillBtn.setButtonText (juce::String::fromUTF8 ("Филл, пока держишь"));
    addAndMakeVisible (fillBtn);
    addAndMakeVisible (wave);
    addAndMakeVisible (laneA);
    addAndMakeVisible (laneB);

    sampleLabel.setText (proc.getSampleName(), juce::dontSendNotification);
    sampleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (sampleLabel);
    hint.setText (juce::String::fromUTF8 ("Пэды 1-8 и филл также играются нотами C1-G#1 и A1. Плагин идёт по транспорту Live."), juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    hint.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (hint);

    for (auto id : { "style", "bars", "slicemode", "slicecount", "engine", "fig", "figwhere", "figsrc", "figpitch", "figvel", "autofill",
                     "repsize", "replen", "repmode", "glsize", "warpmode", "shiftset", "gaterate" })
        addCombo (id, {});
    for (auto id : { "morph", "swing", "pitch", "speed", "vol", "cutoff", "res", "drive", "dmix", "dtime", "dfb",
                     "hits", "air", "mad", "pvar", "chaos", "crushhard" })
        addSlider (id, {});
    addToggle ("mono", juce::String::fromUTF8 ("Моно-глушение"));
    addToggle ("freerun", juce::String::fromUTF8 ("Играть без транспорта"));

    for (int i = 0; i < 8; ++i)
    {
        pads[(size_t) i].setButtonText (juce::String::fromUTF8 (splintDecoName (i)) + "  " + juce::String (i + 1));
        pads[(size_t) i].setClickingTogglesState (false);
        addAndMakeVisible (pads[(size_t) i]);
        pads[(size_t) i].onPress = [this, i] (bool down)
        {
            const juce::String pid = juce::String ("on_") + juce::StringArray { "repeat", "glitch", "warp", "shift", "flip", "gate", "crush", "drop" }[i];
            auto* param = proc.apvts.getParameter (pid);
            if (param == nullptr) return;
            if (down)
            {
                const bool on = param->getValue() > 0.5f;
                param->setValueNotifyingHost (on ? 0.0f : 1.0f);
                if (! on) proc.setDecoHeld (i, true);
            }
            else
            {
                proc.setDecoHeld (i, false);
            }
        };
        auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
        s->setNumDecimalPlacesToDisplay (2);
        addAndMakeVisible (*s);
        padAmtAtt[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.apvts, juce::String ("amt_") + juce::StringArray { "repeat", "glitch", "warp", "shift", "flip", "gate", "crush", "drop" }[i], *s);
        s->setNumDecimalPlacesToDisplay (2);
        padAmt[(size_t) i] = std::move (s);
    }

    fillBtn.onPress = [this] (bool down) { proc.setFillHeld (down); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Выбери аудиофайл"), juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile() && proc.loadSampleFile (f))
                                      sampleLabel.setText (proc.getSampleName(), juce::dontSendNotification);
                              });
    };
    demoBtn.onClick = [this] { proc.makeDemoSample(); sampleLabel.setText (proc.getSampleName(), juce::dontSendNotification); };
    genBtn.onClick = [this] { proc.regenerate (true); };
    insABtn.onClick = [this] { proc.insertFigure (0); };
    insBBtn.onClick = [this] { proc.insertFigure (1); };

    if (auto* c = dynamic_cast<juce::ComboBox*> (find ("slicemode"))) c->onChange = [this] { juce::Timer::callAfterDelay (10, [this] { proc.reslice(); }); };
    if (auto* c = dynamic_cast<juce::ComboBox*> (find ("slicecount"))) c->onChange = [this] { juce::Timer::callAfterDelay (10, [this] { proc.reslice(); }); };
    if (auto* c = dynamic_cast<juce::ComboBox*> (find ("style"))) c->onChange = [this] { juce::Timer::callAfterDelay (10, [this] { proc.regenerate (false); }); };
    if (auto* c = dynamic_cast<juce::ComboBox*> (find ("bars"))) c->onChange = [this] { juce::Timer::callAfterDelay (10, [this] { proc.regenerate (false); }); };

    setSize (1060, 900);
    startTimerHz (24);
}

void SplintEditor::addSlider (const char* paramId, const juce::String&)
{
    auto* s = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 16);
    s->setNumDecimalPlacesToDisplay (2);
    sliders.add (s);
    addAndMakeVisible (s);
    sliderAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, paramId, *s));
    s->setNumDecimalPlacesToDisplay (2);
    auto* l = new juce::Label();
    l->setText (proc.apvts.getParameter (paramId)->getName (30), juce::dontSendNotification);
    l->setFont (juce::Font (juce::FontOptions (12.0f)));
    l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    sliderLabels.add (l);
    addAndMakeVisible (l);
    byId.set (paramId, s);
}

void SplintEditor::addCombo (const char* paramId, const juce::String&)
{
    auto* c = new juce::ComboBox();
    combos.add (c);
    addAndMakeVisible (c);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        c->addItemList (ch->choices, 1);
    comboAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, paramId, *c));
    auto* l = new juce::Label();
    l->setText (proc.apvts.getParameter (paramId)->getName (30), juce::dontSendNotification);
    l->setFont (juce::Font (juce::FontOptions (12.0f)));
    l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    comboLabels.add (l);
    addAndMakeVisible (l);
    byId.set (paramId, c);
}

void SplintEditor::addToggle (const char* paramId, const juce::String& name)
{
    auto* t = new juce::ToggleButton (name);
    toggles.add (t);
    addAndMakeVisible (t);
    toggleAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, paramId, *t));
    byId.set (paramId, t);
}

void SplintEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2e35));
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText ("Splint", 16, 8, 200, 34, juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText (juce::String::fromUTF8 ("чоп-ресемплер"), 16, 40, 200, 16, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    for (int y : { 294, 486, 612 }) g.fillRect (12, y, getWidth() - 24, 1);
}

void SplintEditor::resized()
{
    auto place = [this] (const char* id, int x, int y, int w, int h)
    {
        if (auto* c = find (id))
        {
            c->setBounds (x, y, w, h);
            for (auto* l : sliderLabels) if (l->getText() == proc.apvts.getParameter (id)->getName (30)) l->setBounds (x, y - 14, w, 14);
            for (auto* l : comboLabels) if (l->getText() == proc.apvts.getParameter (id)->getName (30)) l->setBounds (x, y - 14, w, 14);
        }
    };

    loadBtn.setBounds (230, 14, 140, 26);
    demoBtn.setBounds (376, 14, 70, 26);
    sampleLabel.setBounds (454, 14, 240, 26);
    place ("freerun", 700, 16, 200, 22);

    wave.setBounds (16, 56, getWidth() - 32, 110);

    place ("style", 16, 196, 190, 24);
    place ("bars", 214, 196, 70, 24);
    place ("slicemode", 292, 196, 100, 24);
    place ("slicecount", 400, 196, 80, 24);
    place ("engine", 488, 196, 100, 24);
    genBtn.setBounds (596, 196, 170, 24);
    place ("swing", 776, 196, 260, 24);

    place ("hits", 16, 250, 230, 22);
    place ("air", 262, 250, 230, 22);
    place ("mad", 508, 250, 230, 22);
    place ("pvar", 754, 250, 282, 22);

    laneA.setBounds (16, 308, getWidth() - 32, 62);
    laneB.setBounds (16, 374, getWidth() - 32, 62);
    place ("morph", 16, 458, 460, 22);

    // фигуры
    place ("fig", 16, 512, 150, 24);
    place ("figwhere", 174, 512, 140, 24);
    place ("figsrc", 322, 512, 150, 24);
    place ("figpitch", 480, 512, 90, 24);
    place ("figvel", 578, 512, 110, 24);
    insABtn.setBounds (696, 512, 100, 24);
    insBBtn.setBounds (802, 512, 100, 24);
    fillBtn.setBounds (908, 512, 136, 24);
    place ("autofill", 16, 566, 140, 24);
    place ("chaos", 174, 566, 250, 22);
    hint.setBounds (440, 562, 610, 24);

    // деконструкция
    const int px0 = 16, py0 = 636, pw = (getWidth() - 40) / 4, ph = 26;
    for (int i = 0; i < 8; ++i)
    {
        const int cx = px0 + (i % 4) * (pw + 4);
        const int cy = py0 + (i / 4) * 62;
        pads[(size_t) i].setBounds (cx, cy, pw, ph);
        padAmt[(size_t) i]->setBounds (cx, cy + ph + 2, pw, 18);
    }

    // мелкие настройки модулей и эффекты
    const int fy = 762;
    place ("repsize", 16, fy, 70, 20);
    place ("replen", 92, fy, 70, 20);
    place ("repmode", 168, fy, 90, 20);
    place ("glsize", 264, fy, 70, 20);
    place ("warpmode", 340, fy, 90, 20);
    place ("shiftset", 436, fy, 90, 20);
    place ("gaterate", 532, fy, 80, 20);
    place ("crushhard", 618, fy, 130, 20);

    place ("pitch", 16, 812, 200, 22);
    place ("speed", 232, 812, 200, 22);
    place ("cutoff", 448, 812, 200, 22);
    place ("res", 664, 812, 180, 22);
    place ("drive", 860, 812, 180, 22);
    place ("dmix", 16, 856, 200, 22);
    place ("dtime", 232, 856, 160, 22);
    place ("dfb", 408, 856, 200, 22);
    place ("vol", 624, 856, 200, 22);
    place ("mono", 840, 854, 200, 22);

}

void SplintEditor::timerCallback()
{
    laneA.repaint();
    laneB.repaint();
    for (int i = 0; i < 8; ++i)
    {
        const juce::String pid = juce::String ("on_") + juce::StringArray { "repeat", "glitch", "warp", "shift", "flip", "gate", "crush", "drop" }[i];
        const bool on = proc.apvts.getRawParameterValue (pid)->load() > 0.5f;
        const bool fired = proc.decoFire[i].exchange (0) != 0;
        auto col = on ? juce::Colour (0xff2b39f0) : juce::Colour (0xff3a3f48);
        if (fired) col = juce::Colour (0xffe0286f);
        pads[(size_t) i].setColour (juce::TextButton::buttonColourId, col);
        pads[(size_t) i].repaint();
    }
}
