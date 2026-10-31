// Live pitch fix for the AI Purrformer FX plugin. AGPL-3.0.
// Real time, low delay, no engine needed: it tracks your pitch (YIN), pulls it toward the key or
// toward the notes of a MIDI track, and shifts the voice with a formant-preserving pitch-synchronous overlap-add shifter (PSOLA).
// Optional grit (Drive, Transistor, Fuzz from the PurrOne synth) before and after.
// Everything on the audio thread uses buffers made in prepare(); no allocation while processing.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace purr::live
{
constexpr double kPi = 3.14159265358979323846;
inline float hzToMidi (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }
inline float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

// ---- scales (same order as the "scale" parameter and engine/purrformer/analysis.py SCALES) ----------
inline const std::array<std::vector<int>, 13>& scaleSteps()
{
    static const std::array<std::vector<int>, 13> s = {{
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } }};
    return s;
}
inline int scaleMask (int tonic, int scale)
{
    int m = 0;
    for (int st : scaleSteps()[(size_t) std::clamp (scale, 0, 12)]) m |= 1 << ((tonic + st) % 12);
    return m;
}
// move a (fractional) MIDI pitch toward the nearest note of the scale; strength 0..1
inline float snapToMask (float midi, int mask, float strength)
{
    if (mask == 0 || mask == 0xFFF || strength <= 0.0f) return midi;
    const int base = (int) std::floor (midi / 12.0f) * 12;
    float best = midi; float bd = 1e9f;
    for (int o = -12; o <= 12; o += 12)
        for (int pc = 0; pc < 12; ++pc)
            if (mask & (1 << pc))
            {
                const float c = (float) (base + o + pc), d = std::abs (c - midi);
                if (d < bd) { bd = d; best = c; }
            }
    return midi + (best - midi) * std::clamp (strength, 0.0f, 1.0f);
}

// ---- grit: the PurrOne shaper (see engine/purrformer/grit.py, same numbers) -----------------------
struct Shaper
{
    static float ss (float t) { t = std::clamp (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
    static float asym (float u, float nl) { return u >= 0.0f ? std::tanh (u) : nl * std::tanh (u / nl); }
    static float shape (float x, float drive, float xs, float fuzz)
    {
        const float dn = ss ((drive - 0.8f) * 5.0f), xn = ss ((xs - 0.8f) * 5.0f), fn = ss ((fuzz - 0.8f) * 5.0f);
        float y = x;
        if (drive > 0.001f)
        {
            y = std::tanh (x * (1.0f + drive * 5.0f + dn * 30.0f));
            if (dn > 0.0f) y += dn * (std::tanh (y * (2.0f + 10.0f * dn)) - y);
        }
        if (xs > 0.001f)
        {
            const float nl = 1.0f - 0.3f * xs - 0.25f * xn;
            y = asym (y * (1.0f + xs * 5.0f + xn * 30.0f), nl);
            y += (0.3f * xs + 0.25f * xn) * y * y;
            if (xn > 0.0f) { const float y2 = asym (y * (2.0f + 10.0f * xn) + 0.15f * xn, nl); y += xn * (y2 - y); }
        }
        if (fuzz > 0.001f)
        {
            const float fz = 1.0f + fuzz * 14.0f + fn * 70.0f, bf = 0.5f * fuzz;
            y = std::tanh (y * fz + bf) - std::tanh (bf);
            if (fn > 0.0f)
            {
                const float r = std::sqrt (y * y + 0.02f) - 0.1414f;
                y += fn * 0.7f * (r - 0.3f);
                y = std::tanh (y * (1.0f + 0.8f * fn));
            }
        }
        return y;
    }
    static float makeup (float d, float t, float f)
    {
        double a = 0, b = 0;
        for (int i = 0; i < 256; ++i)
        {
            const float s = 0.9f * std::sin (6.2831853f * (float) i / 64.0f), y = shape (s, d, t, f);
            a += (double) s * s; b += (double) y * y;
        }
        return b > 1e-9 ? std::clamp ((float) std::sqrt (a / b), 0.25f, 4.0f) : 1.0f;
    }
};

// 4x oversampled grit for one channel. A fixed 12 sample delay is always in the path (also when
// the grit is off) so switching it on or off never changes the timing.
class GritStage
{
public:
    static constexpr int kTaps = 47, kDelay = 12;
    void prepare (double sr)
    {
        sampleRate = sr;
        buildFir();
        reset();
    }
    void reset()
    {
        xin.fill (0.0f); s4.fill (0.0f); dly.fill (0.0f);
        xp = sp = dp = 0; env = 0.0f; hpx = hpy = 0.0f;
    }
    // amount scales the three knobs (0.4 = the light "before" stage). mix 0..1
    void set (bool on, float drive, float trans, float fuzz, float mix, float amount)
    {
        if (! on || amount <= 0.0f || (drive <= 0 && trans <= 0 && fuzz <= 0) || mix <= 0.0f) { active = false; return; }
        const float d = drive * amount, t = trans * amount, f = fuzz * amount;
        if (d != cd || t != ct || f != cf) { cd = d; ct = t; cf = f; make = Shaper::makeup (d, t, f); }
        gmix = mix; active = true;
    }
    float process (float x)
    {
        // dry path delayed by 11.5 samples to line up with the oversampler
        dly[(size_t) dp] = x;
        auto at = [this] (int k) { return dly[(size_t) ((dp - k + 64) & 63)]; };
        const float dryD = 0.5f * (at (kDelay - 1) + at (kDelay));
        dp = (dp + 1) & 63;
        if (! active) return dryD;

        env = std::max (std::abs (x), env * relCoef);              // slow peak follower: knobs behave the same at any level
        const float g = 0.9f / std::max (env, 0.05f);
        xp = (xp + 1) % 13; xin[(size_t) xp] = x * g * 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            float y = 0.0f;
            for (int j = 0; k + 4 * j < kTaps; ++j) y += fir[(size_t) (k + 4 * j)] * xin[(size_t) ((xp - j + 13) % 13)];
            sp = (sp + 1) % kTaps;
            s4[(size_t) sp] = Shaper::shape (y, cd, ct, cf) * make;
        }
        float o = 0.0f;
        for (int i = 0; i < kTaps; ++i) o += fir[(size_t) i] * s4[(size_t) ((sp - i + kTaps) % kTaps)];
        hpy = o - hpx + hpa * hpy; hpx = o;                          // DC blocker (the transistor stage is lopsided)
        const float wet = hpy / g;
        return (1.0f - gmix) * dryD + gmix * wet;
    }
private:
    void buildFir()
    {
        const double fc = 0.5 / 4.0 * 0.92, c = (kTaps - 1) / 2.0; double sum = 0;
        std::array<double, kTaps> h {};
        for (int i = 0; i < kTaps; ++i)
        {
            const double t = i - c, sinc = std::abs (t) < 1e-9 ? 2 * fc : std::sin (2 * kPi * fc * t) / (kPi * t);
            h[(size_t) i] = sinc * (0.5 - 0.5 * std::cos (2 * kPi * i / (kTaps - 1)));
            sum += h[(size_t) i];
        }
        for (int i = 0; i < kTaps; ++i) fir[(size_t) i] = (float) (h[(size_t) i] / sum);
        hpa = 1.0f - 6.2831853f * 20.0f / (float) sampleRate;
        relCoef = std::exp (-1.0f / (float) (sampleRate * 0.2));
    }
    double sampleRate = 44100;
    std::array<float, kTaps> fir {}, s4 {};
    std::array<float, 13> xin {};
    std::array<float, 64> dly {};
    int xp = 0, sp = 0, dp = 0;
    bool active = false;
    float cd = -1, ct = -1, cf = -1, make = 1, gmix = 0.5f, env = 0, hpx = 0, hpy = 0, hpa = 0.999f, relCoef = 0.9999f;
};

// ---- the voice -----------------------------------------------------------------------------------
// Formant-preserving pitch shifter (pitch-synchronous overlap-add, "PSOLA"): the voice is cut into
// one-period grains that follow the singer's pitch, and the grains are laid back down closer together
// or further apart. The shape of the grain (the vowel, the formants) is untouched, so a corrected note
// still sounds like the same person. Optional Formant knob scales the grain read speed on purpose.
struct Settings
{
    bool on = true;
    float snap = 0.8f;          // 0..1 pull toward the scale
    float speed = 0.2f;         // 0 = instant (robot), 1 = slow and natural
    int mask = 0;               // allowed pitch classes (bit per note); 0 or 0xFFF = no key
    bool autoKey = false;       // learn the key from what is sung
    int heldNote = -1;          // MIDI note held right now (or -1)
    float follow = 1.0f;        // how closely to follow the held MIDI note
    float level = 1.0f, dry = 0.0f;
    float formant = 0.0f;       // semitones: shifts the vowel colour (formants) up or down, 0 = keep the singer's own
    bool gritOn = false;
    float drive = 0, trans = 0, fuzz = 0, gritMix = 0.5f;
    int gritPos = 2;            // 0 before, 1 between, 2 after, 3 before+after, 4 all (live has one voice: between = after)
};

struct Info                       // read by the editor
{
    std::atomic<float> sung { -1.0f }, target { -1.0f };
    std::atomic<int> keyTonic { -1 }, keyMinor { 0 };
};

class Voice
{
public:
    static constexpr double latencySeconds = 0.022;
    int latencySamples() const { return lat + 2 * GritStage::kDelay; }

    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        nch = std::clamp (channels, 1, 2);
        lat = (int) std::lround (sr * latencySeconds);
        const int need = lat + 6 * (int) (sr / 60.0) + 64;
        int n = 256; while (n < 2 * need) n <<= 1;
        mask = n - 1;
        for (auto& c : ch) { c.buf.assign ((size_t) n, 0.0f); c.acc.assign ((size_t) n, 0.0f); c.dry.assign ((size_t) n, 0.0f); c.pre.prepare (sr); c.post.prepare (sr); }
        mbuf.assign ((size_t) n, 0.0f);
        dec = std::max (1, (int) std::floor (sr / 16000.0));
        dsr = (float) sr / (float) dec;
        tauMin = std::max (2, (int) (dsr / 900.0f)); tauMax = (int) (dsr / 70.0f);
        win = 2 * tauMax;
        dbuf.assign ((size_t) (win + 8), 0.0f); d.assign ((size_t) tauMax + 2, 0.0f); cm.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& c : ch) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); std::fill (c.acc.begin(), c.acc.end(), 0.0f); std::fill (c.dry.begin(), c.dry.end(), 0.0f); c.pre.reset(); c.post.reset(); }
        std::fill (mbuf.begin(), mbuf.end(), 0.0f);
        std::fill (dbuf.begin(), dbuf.end(), 0.0f);
        w = 0; ratio = 1.0f; corr = 0.0f; decAcc = 0.0f; decN = 0; hopCount = 0; dpos = 0;
        hist.fill (0.0f); keyAge = 0; autoMask = 0; autoScore = -1.0f; sungMidi = -1.0f; voiced = false;
        pitchHist = {}; hpos = 0; hcount = 0; holdLeft = 0;
        streaming = false; firstGrain = true; sNext = 0.0; aPrev = 0; restartAfter = 0; hops = 0; vGain = 0.0f; blend = 0.0f;
        heldTarget = -1; sinceVoiced = 0;
        info.sung = -1.0f; info.target = -1.0f; info.keyTonic = -1;
    }

    Info info;

    // io = channels, modified in place: voice * level + dry (delayed to match) * dry
    void process (float* const* io, int channels, int n, const Settings& s)
    {
        const int nc = std::min (channels, nch);
        const float hopSec = (float) kHop / dsr;
        const float aCorr = s.speed <= 0.0f ? 0.0f : std::exp (-hopSec / (0.003f + 0.4f * s.speed * s.speed));
        const float aRatio = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
        const float aV = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));
        const float aB = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
        const bool preOn = s.gritOn && (s.gritPos == 0 || s.gritPos == 3 || s.gritPos == 4);
        const bool postOn = s.gritOn && (s.gritPos == 1 || s.gritPos == 2 || s.gritPos == 3 || s.gritPos == 4);
        for (int c = 0; c < nc; ++c)
        {
            ch[(size_t) c].pre.set (preOn, s.drive, s.trans, s.fuzz, s.gritMix, 0.4f);
            ch[(size_t) c].post.set (postOn, s.drive, s.trans, s.fuzz, s.gritMix, s.gritPos == 1 ? 0.7f : 1.0f);
        }
        const int mk = s.autoKey ? autoMask : s.mask;
        phi = std::exp2 ((double) std::clamp (s.formant, -12.0f, 12.0f) / 12.0);
        const bool formantOn = std::abs (s.formant) > 0.01f;
        const int dd = latencySamples();                                        // original voice delayed like the wet path

        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < nc; ++c) mono += io[c][i];
            mono /= (float) nc;
            // detector runs on a decimated copy (about 16 kHz)
            decAcc += mono;
            if (++decN == dec)
            {
                dbuf[(size_t) w] = decAcc / (float) dec; w = (w + 1) % (int) dbuf.size(); decAcc = 0.0f; decN = 0;
                if (++hopCount == kHop) { hopCount = 0; analyse (s, mk, aCorr); }
            }
            const float rTarget = std::exp2 (corr / 12.0f);
            ratio += (rTarget - ratio) * aRatio;

            float mpre = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                auto& cc = ch[(size_t) c];
                const float x = io[c][i];
                const float pre = cc.pre.process (x);
                cc.buf[(size_t) (dpos & mask)] = pre;
                cc.dry[(size_t) (dpos & mask)] = x;
                mpre += pre;
            }
            mbuf[(size_t) (dpos & mask)] = mpre / (float) nc;

            const long long tOut = dpos - lat;
            schedule (tOut, nc);
            vGain += ((streaming ? 1.0f : 0.0f) - vGain) * aV;
            const float bT = formantOn ? 1.0f : std::clamp (std::abs (corr) / 0.12f, 0.0f, 1.0f);   // tiny corrections stay bit-near the original
            blend += (bT - blend) * aB;
            const float mixW = vGain * blend;

            for (int c = 0; c < nc; ++c)
            {
                auto& cc = ch[(size_t) c];
                const size_t ti = (size_t) (tOut & mask);
                const float y = cc.acc[ti]; cc.acc[ti] = 0.0f;
                const float aligned = cc.buf[ti];
                const float v = aligned + mixW * (y - aligned);
                const float out = cc.post.process (v);
                io[c][i] = out * s.level + cc.dry[(size_t) ((dpos - dd) & mask)] * s.dry;
            }
            ++dpos;
        }
    }

private:
    static constexpr int kHop = 128;   // decimated samples between pitch estimates (about 8 ms)
    struct Chan { std::vector<float> buf, acc, dry; GritStage pre, post; };
    struct PitchEntry { long long stamp = -1; float T = 0.0f; bool voiced = false; };

    static float cubic (const std::vector<float>& b, int m, double pos)
    {
        const double fl = std::floor (pos); const long long i = (long long) fl; const float t = (float) (pos - fl);
        const float p0 = b[(size_t) ((i - 1) & m)], p1 = b[(size_t) (i & m)], p2 = b[(size_t) ((i + 1) & m)], p3 = b[(size_t) ((i + 2) & m)];
        return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    }

    const PitchEntry* look (long long t) const
    {
        const PitchEntry* oldest = nullptr;
        for (int k = 0; k < hcount; ++k)
        {
            const PitchEntry& e = pitchHist[(size_t) ((hpos - 1 - k + kHist) % kHist)];
            if (e.stamp <= t) return &e;
            oldest = &e;
        }
        return oldest;
    }
    int halfWidth (float T) const
    {
        const float cap = ((float) lat - 0.75f * T) * 0.5f / (float) std::max (1.0, phi);
        return std::max (16, (int) std::min (T, cap));
    }

    // lay down every grain that still touches the output sample we are about to play
    void schedule (long long tOut, int nc)
    {
        if (! streaming)
        {
            if (tOut < restartAfter) return;
            const PitchEntry* e = look (tOut);
            if (e == nullptr || ! e->voiced || e->T < 8.0f) return;
            const int hw0 = halfWidth (e->T);
            const PitchEntry* e2 = look (tOut + hw0);                            // the voice must still be there where the first grain lands
            if (e2 == nullptr || ! e2->voiced || e2->T < 8.0f) return;
            streaming = true; firstGrain = true;
            sNext = (double) (tOut + hw0);
        }
        for (int guard = 0; guard < 8; ++guard)
        {
            const PitchEntry* e = look ((long long) sNext);
            if (e == nullptr || ! e->voiced || e->T < 8.0f) { streaming = false; restartAfter = tOut + lat / 4; return; }
            const float T = e->T; const int hw = halfWidth (T);
            if (sNext - (double) hw > (double) tOut) return;
            addGrain (T, hw, nc);
        }
    }

    float mb (long long i) const { return mbuf[(size_t) (i & mask)]; }

    // the strongest peak within half a period around `c`: grains are centred on it (one pulse per grain)
    long long epochNear (long long c, int Ti, long long maxA) const
    {
        long long best = c; float bv = -1.0f;
        for (long long p = c - Ti / 2; p <= c + Ti / 2 && p <= maxA; ++p)
        {
            const float v = std::abs (mb (p - 1)) + 2.0f * std::abs (mb (p)) + std::abs (mb (p + 1));
            if (v > bv) { bv = v; best = p; }
        }
        return best;
    }

    void addGrain (float T, int hw, int nc)
    {
        const int Ti = std::max (8, (int) T);
        long long a;
        const int hwSrc = (int) std::ceil ((double) hw * phi);
        const long long maxA = dpos - std::max (hwSrc + 4, Ti / 2 + 2);
        if (firstGrain) { a = epochNear (std::min ((long long) std::llround (sNext), maxA), Ti, maxA); firstGrain = false; }
        else
        {
            const long long k = std::llround ((sNext - (double) aPrev) / (double) T);
            if (k < 1) a = aPrev;                                                // synthesis is behind: repeat the grain
            else
            {
                const long long ideal = aPrev + (long long) std::llround ((double) k * (double) T);
                const int half = Ti / 2, Td = std::max (1, Ti / 4);
                double er = 0.0;
                for (int j = -half; j <= half; j += 2) { const double r = mb (aPrev + j); er += r * r; }
                double bestC = -2.0; long long bestA = std::min (ideal, maxA);
                // line the new grain up with the waveform of the last one (same phase of the period)
                for (int dl = -Td; dl <= Td; ++dl)
                {
                    const long long ac = ideal + dl; if (ac > maxA) break;
                    double cc = 0.0, ec = 0.0;
                    for (int j = -half; j <= half; j += 2) { const double r = mb (aPrev + j), c = mb (ac + j); cc += r * c; ec += c * c; }
                    const double v = cc / std::sqrt (er * ec + 1e-9);
                    if (v > bestC) { bestC = v; bestA = ac; }
                }
                // a clear match keeps the chain; a poor match (new note, new vowel) finds the pulse again
                a = bestC > 0.6 ? bestA : epochNear (std::min (ideal, maxA), Ti, maxA);
            }
        }
        const double Tout = (double) T / (double) std::max (ratio, 0.25f);
        hw = std::max (16, std::min (hw, (int) (0.9 * Tout)));       // measured: narrower than one output period keeps the vowel colour best
        const double s0d = std::floor (sNext + 0.5); const long long s0 = (long long) s0d; const double fr = sNext - s0d;
        const float g = (float) std::pow (Tout / (double) hw, 0.25);          // measured: keeps pulse loudness and tail energy closest at +-3 semitones
        for (int i = -hw; i <= hw; ++i)
        {
            const float wgt = g * 0.5f * (1.0f + std::cos (3.14159265f * (float) i / (float) hw));
            const double pos = (double) a + ((double) i - fr) * phi;
            for (int c = 0; c < nc; ++c)
                ch[(size_t) c].acc[(size_t) ((s0 + i) & mask)] += wgt * cubic (ch[(size_t) c].buf, mask, pos);
        }
        aPrev = a; sNext += Tout;
    }

    void analyse (const Settings& s, int mk, float aCorr)
    {
        const int sz = (int) dbuf.size();
        auto x = [&] (int k) { return dbuf[(size_t) ((w - win + k + 2 * sz) % sz)]; };      // k = 0 oldest of the last `win` samples
        float e = 0.0f;
        for (int k = win / 2; k < win; ++k) e += x (k) * x (k);
        const bool loud = std::sqrt (e / (float) (win / 2)) > 0.006f;
        float f0 = 0.0f;
        if (loud)
        {
            d[0] = 0.0f;
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                float sum = 0.0f;
                for (int j = 0; j < tauMax; ++j) { const float v = x (j) - x (j + tau); sum += v * v; }
                d[(size_t) tau] = sum;
            }
            cm[0] = 1.0f; float run = 0.0f;
            for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = d[(size_t) tau] * (float) tau / std::max (run, 1e-12f); }
            int t = tauMin, found = 0;
            while (t < tauMax)
            {
                if (cm[(size_t) t] < 0.15f) { while (t + 1 < tauMax && cm[(size_t) t + 1] < cm[(size_t) t]) ++t; found = t; break; }
                ++t;
            }
            if (found > 1 && found < tauMax)
            {
                const float a = cm[(size_t) found - 1], b = cm[(size_t) found], c = cm[(size_t) found + 1], den = a - 2 * b + c;
                f0 = dsr / ((float) found + (std::abs (den) > 1e-12f ? 0.5f * (a - c) / den : 0.0f));
            }
        }
        if (++hops * kHop < win + kHop) f0 = 0.0f;                              // the first window is mostly silence from before we started
        voiced = f0 > 0.0f;
        // pitch history for the shifter: when and how long the period was (a lost estimate is held for two hops)
        {
            PitchEntry pe; pe.stamp = dpos - (long long) (win / 2) * dec;
            if (voiced) { pe.T = (float) sampleRate / f0; pe.voiced = true; holdLeft = 2; lastT = pe.T; }
            else if (holdLeft > 0 && lastT > 0.0f) { --holdLeft; pe.T = lastT; pe.voiced = true; }
            pitchHist[(size_t) hpos] = pe; hpos = (hpos + 1) % kHist; hcount = std::min (hcount + 1, kHist);
        }
        float wantCorr = 0.0f;
        if (voiced)
        {
            const float sung = hzToMidi (f0);
            sungMidi = sung;
            float target = sung;
            if (s.heldNote >= 0 && s.follow > 0.0f)
            {
                float n = (float) s.heldNote;
                while (n - sung > 6.0f) n -= 12.0f;
                while (sung - n > 6.0f) n += 12.0f;
                target = sung + (n - sung) * s.follow;
            }
            else
            {
                // note hysteresis: stay on the note you are on until you really move toward the next one
                if (mk != 0 && mk != 0xFFF && s.snap > 0.0f)
                {
                    float full = snapToMask (sung, mk, 1.0f);
                    if (sinceVoiced > 40) heldTarget = -1;
                    bool keep = false;
                    if (heldTarget >= 0 && (mk & (1 << (((heldTarget % 12) + 12) % 12))) != 0)
                    {
                        // gap to the next allowed note on the side we are leaning toward; leave the note only a quarter semitone past the midpoint
                        int gap = 1; const int dirn = sung >= (float) heldTarget ? 1 : -1;
                        while (gap < 12 && (mk & (1 << ((((heldTarget + dirn * gap) % 12) + 12) % 12))) == 0) ++gap;
                        keep = std::abs (sung - (float) heldTarget) < 0.5f * (float) gap + 0.25f;
                    }
                    if (keep) full = (float) heldTarget; else heldTarget = (int) std::lround (full);
                    target = sung + (full - sung) * std::clamp (s.snap, 0.0f, 1.0f);
                }
                else { target = snapToMask (sung, mk, s.snap); heldTarget = -1; }
            }
            sinceVoiced = 0;
            wantCorr = std::clamp (target - sung, -7.0f, 7.0f);
            // learn the key from the singing: pitch classes, slow decay
            for (auto& h : hist) h *= 0.9985f;
            hist[(size_t) (((int) std::lround (sung) % 12 + 12) % 12)] += 1.0f;
            if (++keyAge >= 32) { keyAge = 0; learnKey(); }
            info.sung = sung; info.target = sung + wantCorr;
        }
        else { info.sung = -1.0f; info.target = -1.0f; ++sinceVoiced; }
        corr = corr * aCorr + wantCorr * (1.0f - aCorr);
    }

    void learnKey()
    {
        static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float mnr[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        float tot = 0; for (float h : hist) tot += h;
        if (tot < 40.0f) return;                                   // need about a third of a second of singing
        float mean = tot / 12.0f, best = -2.0f, cur = -2.0f; int bt = 0, bm = 0;
        for (int t = 0; t < 12; ++t)
            for (int m = 0; m < 2; ++m)
            {
                const float* prof = m ? mnr : maj;
                float pm = 0; for (int i = 0; i < 12; ++i) pm += prof[i]; pm /= 12.0f;
                float nu = 0, da = 0, db = 0;
                for (int i = 0; i < 12; ++i)
                {
                    const float a = hist[(size_t) ((t + i) % 12)] - mean, b = prof[i] - pm;
                    nu += a * b; da += a * a; db += b * b;
                }
                const float c = da > 0 && db > 0 ? nu / std::sqrt (da * db) : 0.0f;
                if (c > best) { best = c; bt = t; bm = m; }
                if (autoMask != 0 && t == autoTonic && m == autoMinor) cur = c;
            }
        if (autoMask == 0 || best > cur + 0.03f) { autoTonic = bt; autoMinor = bm; autoMask = scaleMask (bt, bm == 1 ? 1 : 0); }
        autoScore = best;
        info.keyTonic = autoTonic; info.keyMinor = autoMinor;
    }

    static constexpr int kHist = 128;
    double sampleRate = 44100; int nch = 2, lat = 970, mask = 4095, dec = 3, tauMin = 18, tauMax = 228, win = 456;
    float dsr = 16000.0f;
    std::array<Chan, 2> ch;
    std::vector<float> dbuf, d, cm, mbuf;
    int w = 0, decN = 0, hopCount = 0, keyAge = 0, autoMask = 0, autoTonic = 0, autoMinor = 0;
    long long dpos = 0;
    float ratio = 1, corr = 0, decAcc = 0, autoScore = -1.0f, sungMidi = -1.0f;
    bool voiced = false;
    std::array<PitchEntry, kHist> pitchHist {};
    int hpos = 0, hcount = 0, holdLeft = 0, heldTarget = -1, sinceVoiced = 0;
    float lastT = 0.0f, vGain = 0.0f, blend = 0.0f;
    bool streaming = false, firstGrain = true;
    double sNext = 0.0, phi = 1.0;
    long long aPrev = 0, restartAfter = 0; int hops = 0;
    std::array<float, 12> hist {};
};
} // namespace purr::live
