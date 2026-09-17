#include "../Source/PluginProcessor.h"
#include <cstdio>

static void setP (SplintProcessor& p, const char* id, float norm)
{
    if (auto* par = p.apvts.getParameter (id)) par->setValueNotifyingHost (norm);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    SplintProcessor p;
    const double sr = 44100.0;
    const int block = 512;
    p.prepareToPlay (sr, block);
    setP (p, "freerun", 1.0f);

    const int mode = argc > 1 ? atoi (argv[1]) : 0;
    if (mode == 1) { setP (p, "style", 1.0f); setP (p, "on_glitch", 1.0f); setP (p, "on_repeat", 1.0f); setP (p, "on_warp", 1.0f); setP (p, "on_crush", 1.0f); setP (p, "on_gate", 1.0f); setP (p, "on_flip", 1.0f); setP (p, "on_shift", 1.0f); setP (p, "on_drop", 1.0f); }
    if (mode == 2) { setP (p, "engine", 1.0f); setP (p, "autofill", 0.34f); }
    p.regenerate (false);
    printf ("pattern A hits: %d, B: %d, slices: %d\n", (int) p.getPattern (0).size(), (int) p.getPattern (1).size(), (int) p.getSlices().size());

    juce::AudioBuffer<float> buf (2, block);
    const int seconds = 8;
    juce::AudioBuffer<float> out (2, (int) (sr * seconds));
    out.clear();
    juce::MidiBuffer midi;
    int written = 0;
    for (int i = 0; written + block < out.getNumSamples(); ++i)
    {
        buf.clear();
        midi.clear();
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, written, buf, ch, 0, block);
        written += block;
    }
    float peak = 0.0f; double rms = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        peak = juce::jmax (peak, out.getMagnitude (ch, 0, written));
        rms += out.getRMSLevel (ch, 0, written);
    }
    printf ("peak %.3f  rms %.4f\n", peak, rms / 2.0);

    if (mode == 3)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
        juce::File pf ("/tmp/splint_ui.png");
        pf.deleteFile();
        juce::PNGImageFormat png;
        if (auto* os = pf.createOutputStream().release()) { png.writeImageToStream (img, *os); delete os; }
        printf ("ui %d x %d saved\n", ed->getWidth(), ed->getHeight());
    }

    juce::File f (juce::String ("/tmp/splint_test") + juce::String (mode) + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto* os = f.createOutputStream().release())
        if (auto* w = wav.createWriterFor (os, sr, 2, 16, {}, 0))
        { w->writeFromAudioSampleBuffer (out, 0, written); delete w; }
    printf ("written %s\n", f.getFullPathName().toRawUTF8());
    return 0;
}
