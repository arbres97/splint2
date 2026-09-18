#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// Спектральное облако: сигнал раскладывается на частоты, магнитуды затухают
// медленно, фазы размазываются, есть сдвиг спектра и заморозка.
class SpectralReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : ch)
        {
            c.in.assign ((size_t) N, 0.0f);
            c.out.assign ((size_t) N, 0.0f);
            c.mag.assign ((size_t) (N / 2 + 1), 0.0f);
            c.phase.assign ((size_t) (N / 2 + 1), 0.0f);
            c.wp = 0;
        }
        fftData.assign ((size_t) (2 * N), 0.0f);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
        counter = 0;
    }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f);
            std::fill (c.out.begin(), c.out.end(), 0.0f);
            std::fill (c.mag.begin(), c.mag.end(), 0.0f);
        }
    }

    // decay 0..1, blur 0..1, shiftSemis -12..12, freeze
    void process (float* l, float* r, int numSamples, float decay, float blur, float shiftSemis, bool freeze,
                  float* wetL, float* wetR)
    {
        const float dec = juce::jlimit (0.0f, 0.9995f, 0.5f + decay * 0.4995f);
        const double ratio = std::pow (2.0, shiftSemis / 12.0);

        for (int i = 0; i < numSamples; ++i)
        {
            ch[0].in[(size_t) ch[0].wp] = l[i];
            ch[1].in[(size_t) ch[1].wp] = r[i];
            wetL[i] = ch[0].out[(size_t) ch[0].wp];
            wetR[i] = ch[1].out[(size_t) ch[1].wp];
            ch[0].out[(size_t) ch[0].wp] = 0.0f;
            ch[1].out[(size_t) ch[1].wp] = 0.0f;
            ch[0].wp = (ch[0].wp + 1) % N;
            ch[1].wp = (ch[1].wp + 1) % N;

            if (++counter >= hop)
            {
                counter = 0;
                for (int c = 0; c < 2; ++c) processFrame (ch[(size_t) c], dec, blur, ratio, freeze);
            }
        }
    }

private:
    static constexpr int order = 11;
    static constexpr int N = 1 << order;
    static constexpr int hop = N / 4;

    struct Chan
    {
        std::vector<float> in, out, mag, phase;
        int wp = 0;
    };

    void processFrame (Chan& c, float dec, float blur, double ratio, bool freeze)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            fftData[(size_t) i] = c.in[(size_t) ((c.wp + i) % N)] * window[(size_t) i];

        fft.performRealOnlyForwardTransform (fftData.data(), true);

        const int bins = N / 2 + 1;
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int b = 0; b < bins; ++b)
        {
            const float re = fftData[(size_t) (2 * b)];
            const float im = fftData[(size_t) (2 * b + 1)];
            const float m = std::sqrt (re * re + im * im);
            float acc = c.mag[(size_t) b] * dec;
            if (! freeze) acc = juce::jmax (acc, m);
            c.mag[(size_t) b] = acc;
            c.phase[(size_t) b] += twoPi * (float) b * (float) hop / (float) N
                                 + (rnd.nextFloat() - 0.5f) * blur * twoPi;
            if (c.phase[(size_t) b] > twoPi) c.phase[(size_t) b] -= twoPi * std::floor (c.phase[(size_t) b] / twoPi);
        }

        for (int b = 0; b < bins; ++b)
        {
            float outMag = c.mag[(size_t) b];
            if (std::abs (ratio - 1.0) > 0.001)
            {
                const int srcBin = (int) std::round (b / ratio);
                if (srcBin >= 0 && srcBin < bins) outMag += 0.7f * c.mag[(size_t) srcBin];
            }
            outMag *= 0.5f;
            fftData[(size_t) (2 * b)]     = outMag * std::cos (c.phase[(size_t) b]);
            fftData[(size_t) (2 * b + 1)] = outMag * std::sin (c.phase[(size_t) b]);
        }

        fft.performRealOnlyInverseTransform (fftData.data());

        const float norm = 2.0f / 3.0f;
        for (int i = 0; i < N; ++i)
            c.out[(size_t) ((c.wp + i) % N)] += fftData[(size_t) i] * window[(size_t) i] * norm;
    }

    juce::dsp::FFT fft { order };
    std::vector<float> fftData, window;
    Chan ch[2];
    int counter = 0;
    double sr = 44100.0;
    juce::Random rnd;
};
