#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"
#include <atomic>
#include <memory>

using SampleBufferPtr = std::shared_ptr<juce::AudioBuffer<float>>;

struct Voice
{
    bool   active = false;
    int    delay = 0;
    double pos = 0.0, inc = 1.0, incEnd = 1.0;
    bool   ramp = false;
    double wobDepth = 0.0, wobPhase = 0.0, wobInc = 0.0;
    int    total = 0, played = 0, fadeIn = 64, fadeOut = 192;
    float  gain = 1.0f, panL = 1.0f, panR = 1.0f;
    double gatePeriod = 0.0;
    int    crushHold = 0, crushCnt = 0;
    float  crushQ = 0.0f, crushV[2] { 0.0f, 0.0f };
    bool   stretch = false, rev = false;
    double grainLen = 0.0, scanInc = 0.0, pitchRatio = 1.0, scanPos = 0.0;
    double gPhase[2] { 0.0, 0.0 }, gStart[2] { 0.0, 0.0 };
    bool   releasing = false;
    int    releaseLeft = 0, releaseLen = 128, releaseDelay = -1, rampLen = 0;
    int    id = 0;

    void startRelease (int samplesFromNow, int relLen);
    void render (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& src, int numSamples);
};

class SplintProcessor : public juce::AudioProcessor
{
public:
    SplintProcessor();
    ~SplintProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Splint"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- интерфейс для редактора ---
    bool loadSampleFile (const juce::File& f);
    void makeDemoSample();
    void regenerate (bool newSeed);
    void reslice();
    void insertFigure (int lane);
    juce::String getSampleName() const { return sampleName; }
    double getSampleSeconds() const;
    std::vector<float> getPeaks();
    std::vector<double> getSlices();
    splint::Pattern getPattern (int lane);
    void setFillHeld (bool v) { fillHeldUi.store (v); }
    void setDecoHeld (int idx, bool v) { if (idx >= 0 && idx < 8) decoHeldUi[(size_t) idx].store (v); }
    std::atomic<double> currentStep { 0.0 };
    std::atomic<int>    decoFire[8];
    int getSeed() const { return seed; }
    int numSteps() const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

private:
    int    choiceOf (const char* id) const { return (int) apvts.getRawParameterValue (id)->load(); }
    float  valOf (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool   boolOf (const char* id) const { return apvts.getRawParameterValue (id)->load() > 0.5f; }

    void   scheduleStep (int stepInLoop, long long absStep, int offset, double samplesPerStep, double morph);
    void   playDeco (const splint::Hit& h, int offset, double samplesPerStep);
    void   startHit (const splint::Hit& h, int offset, double samplesPerStep, int warp, bool glitch, bool crush, double gatePeriod);
    void   spawnVoice (const splint::Hit& h, int offset, int lengthSamples, double semis, float amp,
                       int warp, bool crush, double gatePeriod);
    Voice* findVoice();
    void   chokeAt (int offset);
    void   regenerateLocked (bool newSeed);
    double getSampleSecondsUnlocked() const;
    double advancePerStep (double samplesPerStep) const;
    double probOf (int idx) const;
    void   computeSlices();
    void   computePeaks();

    juce::AudioFormatManager formats;
    mutable juce::SpinLock dataLock;
    SampleBufferPtr sample, activeSample;
    double sampleSR = 44100.0;
    std::vector<double> slices;
    std::vector<float> peaks;
    splint::Pattern patterns[2];
    juce::String sampleName, samplePath;
    int seed = 4127;

    std::array<Voice, 32> voices;
    int voiceCounter = 0;
    int lastVoiceIds[8] {};
    int numLastVoices = 0;

    // состояние деконструкции и филлов
    struct RepState { bool active = false; long long start = 0, until = 0; double size = 1.0; int mode = 0; bool hold = false; splint::Hit snap; };
    RepState rep;
    struct FillState { bool active = false; long long start = 0; double len = 16.0; bool cyclic = false; splint::Pattern hits; };
    FillState fill;

    std::atomic<bool> fillHeldUi { false };
    std::array<std::atomic<bool>, 8> decoHeldUi;
    bool midiDecoHeld[8] {};
    bool midiFillHeld = false;

    double sr = 44100.0;
    double freeStep = 0.0;
    long long lastAbsStep = -1;
    juce::Random rnd;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 192000 };
    juce::dsp::IIR::Filter<float> delayHP[2];
    juce::AudioBuffer<float> voiceBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplintProcessor)
};
