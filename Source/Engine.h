#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace splint {

struct Hit
{
    double start = 0.0;   // в шагах (1/16) от начала квадрата
    double len   = 2.0;   // длина в шагах
    double pos   = 0.0;   // точка старта в семпле, 0..1
    float  gain  = 0.9f;
    float  rate  = 1.0f;
    float  pan   = 0.0f;
    int    pitch = 0;
    int    rat   = 1;
    bool   rev   = false;
};

using Pattern = std::vector<Hit>;

struct Rng
{
    uint32_t a;
    explicit Rng (uint32_t seed = 1u) : a (seed) {}
    double next()
    {
        a += 0x6D2B79F5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (1u | t);
        t ^= t + (t ^ (t >> 7)) * (61u | t);
        return (double) ((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0;
    }
    int   index (int n)               { return n > 0 ? (int) (next() * n) % n : 0; }
    int   pick (const std::vector<int>& v) { return v.empty() ? 0 : v[(size_t) index ((int) v.size())]; }
    double pick (const std::vector<double>& v) { return v.empty() ? 0.0 : v[(size_t) index ((int) v.size())]; }
};

inline double hash01 (int i, int s)
{
    uint32_t x = (uint32_t) ((uint32_t) (i + 1) * 2654435761u) ^ (uint32_t) ((uint32_t) (s + 13) * 40503u);
    x ^= x >> 15; x *= 2246822507u; x ^= x >> 13; x *= 3266489909u; x ^= x >> 16;
    return (double) x / 4294967296.0;
}

struct Style
{
    const char* name;
    double bpm, swing;
    int    hits;
    double air;
    int    onset[16];
    double repeatP, nextP;
    std::vector<int> pitches;
    double pan;
};

inline const std::vector<Style>& styles()
{
    static const std::vector<Style> s = {
        { "UK garage / 2-step",   132.0, 62.0, 4, 0.15, {10,0,2,5, 1,1,6,2, 3,5,3,1, 1,3,5,2}, 0.35, 0.40, {5,7,12,-5}, 0.30 },
        { "Dubstep / halftime",   140.0, 50.0, 3, 0.25, {10,0,0,3, 0,0,5,0, 8,0,3,0, 1,4,0,4}, 0.45, 0.35, {-12,-7,-5,7}, 0.20 },
        { "Jungle / drum & bass", 172.0, 52.0, 5, 0.10, {10,1,4,2, 2,3,2,5, 4,4,8,2, 3,4,4,5}, 0.30, 0.50, {12,-12,3,7}, 0.40 },
        { "Footwork / juke",      160.0, 50.0, 6, 0.20, {10,1,2,7, 1,2,8,1, 2,7,1,2, 8,1,4,5}, 0.70, 0.20, {-2,5,-12}, 0.20 },
        { "Grime / eskibeat",     140.0, 50.0, 4, 0.30, {10,0,2,5, 0,2,7,0, 2,0,6,2, 0,5,2,4}, 0.50, 0.30, {-12,7,12}, 0.20 },
        { "UK funky / bassline",  128.0, 57.0, 5, 0.15, {10,1,4,2, 5,1,4,5, 2,4,1,5, 7,1,5,2}, 0.35, 0.45, {3,5,7,-5}, 0.30 },
        { "IDM / glitch",         104.0, 50.0, 6, 0.20, {10,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3}, 0.25, 0.25, {-12,-7,-5,-3,2,5,7,12,19}, 0.80 },
        { "Фигуры и филлы",       132.0, 50.0, 4, 0.00, {10,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1}, 0.60, 0.30, {12,-12}, 0.20 },
    };
    return s;
}
inline int figureStyleIndex() { return (int) styles().size() - 1; }

// ---------------- нарезка ----------------

inline std::vector<float> rmsEnvelope (const juce::AudioBuffer<float>& b, int hop, int win)
{
    std::vector<float> env;
    if (b.getNumSamples() <= win) return env;
    const float* d = b.getReadPointer (0);
    const int frames = (b.getNumSamples() - win) / hop;
    env.resize ((size_t) juce::jmax (0, frames));
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        const int o = f * hop;
        for (int i = 0; i < win; i += 2) s += (double) d[o + i] * d[o + i];
        env[(size_t) f] = (float) std::sqrt (s / (win / 2));
    }
    return env;
}

// точки атак, отсортированы по силе
inline std::vector<double> transientCandidates (const juce::AudioBuffer<float>& b)
{
    std::vector<double> out;
    const int hop = 256;
    auto env = rmsEnvelope (b, hop, 512);
    const int fr = (int) env.size();
    if (fr < 10) return out;
    std::vector<float> flux ((size_t) fr, 0.0f);
    for (int f = 3; f < fr; ++f)
        flux[(size_t) f] = juce::jmax (0.0f, env[(size_t) f] - (env[(size_t) f - 1] + env[(size_t) f - 2] + env[(size_t) f - 3]) / 3.0f * 1.1f);
    std::vector<std::pair<float, int>> peaks;
    for (int f = 4; f < fr - 4; ++f)
    {
        if (flux[(size_t) f] <= 0.0f) continue;
        bool ok = true;
        for (int k = -4; k <= 4 && ok; ++k) if (k != 0 && flux[(size_t) (f + k)] > flux[(size_t) f]) ok = false;
        if (ok) peaks.push_back ({ flux[(size_t) f], f });
    }
    std::sort (peaks.begin(), peaks.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    const double sr = b.getNumSamples() > 0 ? 1.0 : 1.0;
    juce::ignoreUnused (sr);
    for (auto& p : peaks)
        out.push_back (juce::jmax (0.0, (double) p.second * hop - 0.004 * 44100.0) / (double) b.getNumSamples());
    return out;
}

inline void fillGrid (std::vector<double>& pos, int count, double gap)
{
    for (int k = 0; (int) pos.size() < count && k < count * 8; ++k)
    {
        const double c = std::fmod (k * 0.61803, 1.0);
        bool ok = true;
        for (auto p : pos) if (std::abs (p - c) <= gap) { ok = false; break; }
        if (ok) pos.push_back (c);
    }
}

inline std::vector<double> detectTransients (const juce::AudioBuffer<float>& b, double durSeconds, int count)
{
    std::vector<double> pos;
    const double gap = 0.07 / juce::jmax (0.001, durSeconds);
    for (auto p : transientCandidates (b))
    {
        bool ok = true;
        for (auto q : pos) if (std::abs (q - p) < gap) { ok = false; break; }
        if (ok) pos.push_back (p);
        if ((int) pos.size() >= count) break;
    }
    bool hasStart = false;
    for (auto p : pos) if (p < 0.02) hasStart = true;
    if (! hasStart) { pos.push_back (0.0); std::sort (pos.begin(), pos.end()); if ((int) pos.size() > count) pos.pop_back(); }
    fillGrid (pos, count, 0.5 / juce::jmax (1, count));
    std::sort (pos.begin(), pos.end());
    return pos;
}

inline std::vector<double> detectPhrases (const juce::AudioBuffer<float>& b, double durSeconds, int target)
{
    const int hop = 256;
    auto env = rmsEnvelope (b, hop, 512);
    std::vector<double> pos;
    if (env.empty()) { pos.push_back (0.0); return pos; }
    float pk = 0.0f;
    for (auto v : env) pk = juce::jmax (pk, v);
    const float thr = pk * 0.1f;
    const int minSil = (int) std::round (0.05 * 44100.0 / hop);
    const double L = (double) b.getNumSamples();
    std::vector<double> ons;
    int silent = minSil;
    for (int f = 0; f < (int) env.size(); ++f)
    {
        if (env[(size_t) f] < thr) ++silent;
        else { if (silent >= minSil) ons.push_back (juce::jmax (0.0, (double) f * hop - 0.01 * 44100.0) / L); silent = 0; }
    }
    if (ons.empty()) ons.push_back (0.0);
    const double minGap = 0.15 / juce::jmax (0.001, durSeconds);
    for (auto p : ons) if (pos.empty() || p - pos.back() >= minGap) pos.push_back (p);

    const int tgt = target > 0 ? target : juce::jlimit (6, 16, (int) pos.size());
    if ((int) pos.size() < tgt)
    {
        const double gap = 0.2 / juce::jmax (0.001, durSeconds);
        for (auto p : transientCandidates (b))
        {
            if ((int) pos.size() >= tgt) break;
            bool ok = true;
            for (auto q : pos) if (std::abs (q - p) <= gap) { ok = false; break; }
            if (ok) pos.push_back (p);
        }
        std::sort (pos.begin(), pos.end());
    }
    while ((int) pos.size() > tgt)
    {
        size_t bi = 1; double bd = 1e9;
        for (size_t i = 1; i < pos.size(); ++i) { const double d = pos[i] - pos[i - 1]; if (d < bd) { bd = d; bi = i; } }
        pos.erase (pos.begin() + (long) bi);
    }
    if ((int) pos.size() < tgt) fillGrid (pos, tgt, 0.3 / juce::jmax (1, tgt));
    std::sort (pos.begin(), pos.end());
    return pos;
}

inline std::vector<double> sliceGrid (int count)
{
    std::vector<double> v;
    for (int i = 0; i < count; ++i) v.push_back ((double) i / count);
    return v;
}

inline int sliceIndexOf (const std::vector<double>& slices, double pos)
{
    int k = 0;
    for (int i = 0; i < (int) slices.size(); ++i) if (slices[(size_t) i] <= pos + 1e-6) k = i;
    return k;
}

// ---------------- фигуры ----------------

enum class FigKind { quarter, eighth, sixteenth, everyThird, trip8, trip16, roll32, tresillo, gallop, twoStep, accel, decel };
inline const char* figureName (FigKind k)
{
    switch (k)
    {
        case FigKind::quarter:    return "Прямые 1/4";
        case FigKind::eighth:     return "Прямые 1/8";
        case FigKind::sixteenth:  return "Прямые 1/16";
        case FigKind::everyThird: return "Каждая третья";
        case FigKind::trip8:      return "Триоли 1/8";
        case FigKind::trip16:     return "Триоли 1/16";
        case FigKind::roll32:     return "Дробь 1/32";
        case FigKind::tresillo:   return "3 + 3 + 2";
        case FigKind::gallop:     return "Галоп";
        case FigKind::twoStep:    return "2-step сбивка";
        case FigKind::accel:      return "Разгон";
        case FigKind::decel:      return "Торможение";
    }
    return "";
}

inline std::vector<double> evenly (double L, double g)
{
    std::vector<double> o;
    for (double x = 0.0; x < L - 1e-6; x += g) o.push_back (x);
    return o;
}
inline std::vector<double> cellsOf (double L, double cell, const std::vector<double>& offs)
{
    std::vector<double> o;
    for (double b = 0.0; b < L - 1e-6; b += cell)
        for (auto x : offs) if (b + x < L - 1e-6) o.push_back (b + x);
    return o;
}
inline std::vector<double> rampOffsets (double L, int dir)
{
    const double q = L / 4.0;
    std::vector<double> gaps = { q, q / 2, q / 4, q / 8 };
    if (dir < 0) std::reverse (gaps.begin(), gaps.end());
    std::vector<double> o;
    for (int k = 0; k < 4; ++k)
    {
        const double g = juce::jmax (0.125, gaps[(size_t) k]);
        for (double x = 0.0; x < q - 1e-6; x += g) o.push_back (k * q + x);
    }
    return o;
}
inline std::vector<double> figureOffsets (FigKind k, double L)
{
    switch (k)
    {
        case FigKind::quarter:    return evenly (L, 4.0);
        case FigKind::eighth:     return evenly (L, 2.0);
        case FigKind::sixteenth:  return evenly (L, 1.0);
        case FigKind::everyThird: return evenly (L, 3.0);
        case FigKind::trip8:      return evenly (L, 4.0 / 3.0);
        case FigKind::trip16:     return evenly (L, 2.0 / 3.0);
        case FigKind::roll32:     return evenly (L, 0.5);
        case FigKind::tresillo:   return cellsOf (L, 8.0, { 0.0, 3.0, 6.0 });
        case FigKind::gallop:     return cellsOf (L, 4.0, { 0.0, 2.0, 3.0 });
        case FigKind::twoStep:    return cellsOf (L, 16.0, { 0.0, 3.0, 6.0, 10.0, 13.0 });
        case FigKind::accel:      return rampOffsets (L, 1);
        case FigKind::decel:      return rampOffsets (L, -1);
    }
    return evenly (L, 2.0);
}

struct FigParams
{
    FigKind kind = FigKind::eighth;
    int where = 1;     // 0 весь квадрат, 1 последний такт, 2 последние 2 доли, 3 последняя доля
    int source = 0;    // 0 повтор, 1 фраза дальше, 2 фрагменты подряд, 3 с волны
    int pitchMode = 0; // 0 ровно, 1 вверх, 2 вниз
    int velMode = 0;   // 0 ровно, 1 нарастание, 2 спад
};

inline void figureRegion (const FigParams& fp, int nSteps, double& start, double& len)
{
    switch (fp.where)
    {
        case 0: start = 0.0;                     len = (double) nSteps; break;
        case 1: start = juce::jmax (0, nSteps - 16); len = juce::jmin (16, nSteps); break;
        case 2: start = juce::jmax (0, nSteps - 8);  len = juce::jmin (8, nSteps); break;
        default: start = juce::jmax (0, nSteps - 4); len = juce::jmin (4, nSteps); break;
    }
}

// advancePerStep: какая доля семпла проходит за один шаг при нормальной скорости
inline Pattern buildFigure (double start, double L, double pos0, const std::vector<double>& slices,
                            const FigParams& fp, const Hit& proto, double advancePerStep)
{
    Pattern out;
    auto offs = figureOffsets (fp.kind, L);
    const int n = (int) offs.size();
    const int ns = juce::jmax (1, (int) slices.size());
    for (int k = 0; k < n; ++k)
    {
        const double o = offs[(size_t) k];
        const double gap = (k + 1 < n ? offs[(size_t) k + 1] : L) - o;
        const double f = n > 1 ? (double) k / (n - 1) : 0.0;
        double pos = pos0;
        if (fp.source == 1) pos = juce::jlimit (0.0, 0.999, pos0 + o * advancePerStep * proto.rate * (proto.rev ? -1.0 : 1.0));
        else if (fp.source == 2) pos = slices[(size_t) ((sliceIndexOf (slices, pos0) + k) % ns)];
        Hit h;
        h.start = start + o;
        h.len   = juce::jmax (0.125, gap);
        h.pos   = pos;
        h.rate  = proto.rate;
        h.pan   = proto.pan;
        h.rev   = fp.source == 1 ? false : proto.rev;
        h.pitch = proto.pitch + (fp.pitchMode == 1 ? (int) std::round (f * 12.0) : fp.pitchMode == 2 ? -(int) std::round (f * 12.0) : 0);
        h.gain  = fp.velMode == 1 ? (float) (0.35 + 0.6 * f) : fp.velMode == 2 ? (float) (0.95 - 0.6 * f) : 0.9f;
        out.push_back (h);
    }
    return out;
}

// ---------------- генератор паттернов ----------------

struct GenParams { int hits = 4; double air = 0.15, madness = 0.12, pitchVar = 0.0; };

inline std::vector<int> pickOnsets (Rng& r, const Style& st, int count)
{
    std::vector<int> chosen { 0 }, cand;
    for (int i = 1; i < 16; ++i) cand.push_back (i);
    while ((int) chosen.size() < count && ! cand.empty())
    {
        double tot = 0.0;
        std::vector<double> ws;
        for (auto i : cand) { const double w = (st.onset[i] + 0.3) * (0.5 + r.next()); ws.push_back (w); tot += w; }
        double u = r.next() * tot;
        size_t k = 0;
        while (k + 1 < ws.size() && u > ws[k]) { u -= ws[k]; ++k; }
        chosen.push_back (cand[k]);
        cand.erase (cand.begin() + (long) k);
    }
    std::sort (chosen.begin(), chosen.end());
    return chosen;
}

inline void ornament (Rng& r, const Style& st, Hit& h, double M, double pv)
{
    if (h.len <= 2.0 && r.next() < M * 0.6) h.rat = M > 0.6 ? r.pick (std::vector<int> { 2, 3, 4, 6, 8 }) : r.pick (std::vector<int> { 2, 2, 3, 4 });
    if (r.next() < M * 0.35) h.rev = true;
    if (r.next() < M * 0.30) h.rate = (float) r.pick (std::vector<double> { 0.5, 0.75, 1.5, 2.0 });
    if (r.next() < pv) h.pitch = r.pick (st.pitches);
    h.pan = (float) ((r.next() - 0.5) * st.pan * (0.3 + M));
}

inline Pattern generatePattern (uint32_t seed, int styleIdx, int bars, const std::vector<double>& slices,
                                const GenParams& gp, const FigParams& fp, double advancePerStep)
{
    const auto& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, styleIdx)];
    const int ns = juce::jmax (1, (int) slices.size());
    const int nSteps = bars * 16;
    Rng r (seed == 0 ? 1u : seed);

    if (styleIdx == figureStyleIndex())
    {
        double start = 0.0, len = 16.0;
        figureRegion (fp, nSteps, start, len);
        const double pos0 = fp.source == 3 ? (slices.empty() ? 0.0 : slices[0]) : slices[(size_t) r.index (ns)];
        Hit proto; proto.pos = pos0;
        Pattern out = buildFigure (start, len, pos0, slices, fp, proto, advancePerStep);
        if (start > 0.0)
        {
            Hit lead; lead.start = 0.0; lead.len = start; lead.pos = pos0;
            out.insert (out.begin(), lead);
        }
        return out;
    }

    struct Src { int slice; double pos; };
    auto nextSource = [&] (const Src* prev) -> Src
    {
        if (prev == nullptr) { const int sl = r.index (juce::jmax (1, (ns + 1) / 2)); return { sl, slices[(size_t) sl] }; }
        const double u = r.next();
        if (u < st.repeatP) return *prev;
        if (u < st.repeatP + st.nextP) { const int sl = (prev->slice + 1) % ns; return { sl, slices[(size_t) sl] }; }
        const int sl = r.index (ns);
        return { sl, slices[(size_t) sl] };
    };

    const int count = juce::jlimit (1, 16, gp.hits);
    auto baseOn = pickOnsets (r, st, count);
    std::vector<Src> baseSrc;
    {
        Src prev {}; bool has = false;
        for (size_t i = 0; i < baseOn.size(); ++i) { prev = nextSource (has ? &prev : nullptr); has = true; baseSrc.push_back (prev); }
    }
    auto altOn = pickOnsets (r, st, count);

    struct Raw { int start; double pos; int key; bool fill; int bar; };
    std::vector<Raw> raw;
    for (int b = 0; b < bars; ++b)
    {
        const bool isVar = (b % 2) == 1;
        const bool isFill = bars > 1 && b == bars - 1;
        std::vector<std::pair<int, Src>> ons;
        std::vector<int> keys;
        for (size_t j = 0; j < baseOn.size(); ++j)
            if (! isVar || baseOn[j] < 8) { ons.push_back ({ baseOn[j], baseSrc[j] }); keys.push_back ((int) j); }
        if (isVar)
        {
            Src prev = ons.empty() ? Src { 0, slices[0] } : ons.back().second;
            int j = 0;
            for (auto o : altOn)
                if (o >= 8) { prev = nextSource (&prev); ons.push_back ({ o, prev }); keys.push_back (100 + j++); }
        }
        std::vector<bool> fills (ons.size(), false);
        if (isFill)
        {
            const int extra = 1 + r.index (2);
            for (int e = 0; e < extra; ++e)
            {
                const int o = 10 + r.index (6);
                bool exists = false;
                for (auto& p : ons) if (p.first == o) exists = true;
                if (exists) continue;
                Src before = ons.empty() ? baseSrc[0] : ons[0].second;
                for (auto& p : ons) if (p.first < o) before = p.second;
                ons.push_back ({ o, before }); keys.push_back (200 + o); fills.push_back (true);
            }
        }
        std::vector<size_t> order (ons.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort (order.begin(), order.end(), [&] (size_t x, size_t y) { return ons[x].first < ons[y].first; });
        for (auto i : order)
            raw.push_back ({ b * 16 + ons[i].first, ons[i].second.pos, keys[i], i < fills.size() ? fills[i] : false, b });
    }
    std::sort (raw.begin(), raw.end(), [] (const Raw& x, const Raw& y) { return x.start < y.start; });

    Pattern out;
    for (size_t j = 0; j < raw.size(); ++j)
    {
        const int nx = j + 1 < raw.size() ? raw[j + 1].start : raw[0].start + nSteps;
        double len = nx - raw[j].start;
        Rng r2 ((uint32_t) (seed * 31u + (uint32_t) raw[j].key * 7u + (raw[j].fill ? (uint32_t) raw[j].bar * 1000u : 0u)));
        if (len >= 2.0 && r2.next() < gp.air) len = juce::jmax (1.0, std::round (len * (0.35 + r2.next() * 0.4)));
        Hit h;
        h.start = raw[j].start;
        h.len   = len;
        h.pos   = raw[j].pos;
        h.gain  = raw[j].fill ? 0.85f : 0.9f;
        ornament (r2, st, h, raw[j].fill ? juce::jmin (1.0, gp.madness + 0.3) : gp.madness, gp.pitchVar);
        out.push_back (h);
    }
    return out;
}

// что звучит на шаге i: кусок, который тут начинается, или продолжение предыдущего
inline bool soundAt (const Pattern& p, double i, int nSteps, double advancePerStep, Hit& result)
{
    for (const auto& h : p)
        if (h.start >= i - 1e-9 && h.start < i + 1.0 - 1e-9) { result = h; return true; }
    const Hit* best = nullptr; double bestE = 1e9;
    for (const auto& h : p)
    {
        double e = i - h.start;
        if (e < 0) e += nSteps;
        if (e > 0 && e < h.len && e < bestE) { bestE = e; best = &h; }
    }
    if (best == nullptr) return false;
    result = *best;
    result.pos = juce::jlimit (0.0, 0.999, best->pos + bestE * advancePerStep * best->rate * (best->rev ? -1.0 : 1.0));
    result.len = best->len - bestE;
    result.rat = 1;
    return true;
}

} // namespace splint
