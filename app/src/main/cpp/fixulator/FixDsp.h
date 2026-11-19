#pragma once
// Fixulator DSP core: plain C++17, no JUCE, so it can be tested natively and used in the plugin.
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>
#include <string>
#include <numeric>

namespace fx {

constexpr float kPi = 3.14159265358979f;
constexpr double kPiD = 3.14159265358979323846;
constexpr int kNumBands = 24;
inline const std::array<float, kNumBands>& bandCentres()
{
    static const std::array<float, kNumBands> c { 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                                   1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    return c;
}
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float powToDb (double p) { return (float) (10.0 * std::log10 (std::max (p, 1.0e-20))); }

// ---------------------------------------------------------------- recipe
struct Recipe
{
    std::string name = "Reference podcast";
    std::string source;
    std::array<float, kNumBands> profile {}; // dB, relative to the mean over all bands
    float sibPeak = -8.0f;                   // typical 1-s peak of sibilance-band / full-band energy, dB
    bool valid = false;
};

// tolerance window per band: tighter in the core speech range, looser at the extremes
inline float bandTolerance (int b)
{
    const float f = bandCentres()[(size_t) b];
    if (f >= 100.0f && f <= 250.0f) return 2.0f;          // body zone: voices differ most here
    return (f >= 315.0f && f <= 5000.0f) ? 1.0f : 1.5f;
}

// ---------------------------------------------------------------- FFT (radix 2)
class Fft
{
public:
    explicit Fft (int order) : n (1 << order), rev ((size_t) n), tw ((size_t) n / 2)
    {
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, -2.0f * kPi * (float) i / (float) n);
    }
    int size() const { return n; }
    void forward (std::vector<std::complex<float>>& x) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[(size_t) i], x[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto u = x[(size_t) (i + j)], v = x[(size_t) (i + j + half)] * tw[(size_t) (j * step)];
                    x[(size_t) (i + j)] = u + v; x[(size_t) (i + j + half)] = u - v;
                }
        }
    }
private:
    int n; std::vector<int> rev; std::vector<std::complex<float>> tw;
};

// ---------------------------------------------------------------- biquad (RBJ)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};
    inline float process (float x, int ch)
    {
        float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    void peak (double sr, double f, double q, double db)
    { double A = std::pow (10.0, db / 40.0), w = 2 * kPiD * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
      set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowpass (double sr, double f, double q)
    { double w = 2 * kPiD * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
      set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void highpass (double sr, double f, double q)
    { double w = 2 * kPiD * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
      set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void bandpass (double sr, double f, double q) // 0 dB peak gain
    { double w = 2 * kPiD * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
      set (al, 0, -al, 1 + al, -2 * c, 1 - al); }
    void highShelf (double sr, double f, double q, double db)
    { double A = std::pow (10.0, db / 40.0), w = 2 * kPiD * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w), s = 2 * std::sqrt (A) * al;
      set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
           (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s); }
    // magnitude response in dB at frequency f
    double responseDb (double sr, double f) const
    {
        const std::complex<double> z = std::polar (1.0, -2 * kPiD * f / sr), z2v = z * z;
        auto num = (double) b0 + (double) b1 * z + (double) b2 * z2v;
        auto den = 1.0 + (double) a1 * z + (double) a2 * z2v;
        return 20.0 * std::log10 (std::max (std::abs (num / den), 1e-9));
    }
};

// simple sample delay line per channel
struct Delay
{
    std::vector<float> buf[2]; int len = 0, pos = 0;
    void prepare (int samples) { len = std::max (1, samples + 1); for (auto& b : buf) b.assign ((size_t) len, 0.0f); pos = 0; }
    // push x, return the sample from `delay` samples ago (delay <= len-1)
    inline float push (float x, int ch, int delay)
    {
        auto& b = buf[ch]; b[(size_t) pos] = x;
        int r = pos - delay; if (r < 0) r += len;
        return b[(size_t) r];
    }
    inline void advance() { if (++pos >= len) pos = 0; }
};

// ---------------------------------------------------------------- voice activity (own speaker vs bleed)
struct VoiceDetector
{
    float peakDb = -100, floorDb = -60; int hang = 0, strongHang = 0, liftHang = 0; double acc = 0; int cnt = 0, frameLen = 480; bool voiced = false, strong = false, lift = false;
    void prepare (double sr) { frameLen = (int) (sr * 0.01); acc = 0; cnt = 0; peakDb = -100; floorDb = -60; hang = 0; strongHang = 0; liftHang = 0; voiced = false; strong = false; lift = false; }
    // feed one mono sample; returns true when a new 10 ms frame decision was made
    inline bool push (float x)
    {
        acc += (double) x * x;
        if (++cnt < frameLen) return false;
        const float lvl = powToDb (acc / cnt); acc = 0; cnt = 0;
        if (lvl > peakDb) peakDb = lvl; else peakDb -= 0.0015f;        // ~0.15 dB/s decay: bleed stays below the speaker's level
        if (lvl < floorDb) floorDb = lvl; else floorDb += 0.02f;       // ~2 dB/s rise
        floorDb = std::min (floorDb, peakDb - 20.0f);
        const bool active = lvl > peakDb - 15.0f && lvl > floorDb + 12.0f && lvl > -62.0f;
        if (active) hang = 15; else if (hang > 0) --hang;
        voiced = active || hang > 0;
        if (active && lvl > peakDb - 10.0f) strongHang = 30; else if (strongHang > 0) --strongHang;
        strong = strongHang > 0;   // clearly the speaker, not bleed or mumbling
        if (active && lvl > peakDb - 18.0f) liftHang = 20; else if (liftHang > 0) --liftHang;
        lift = liftHang > 0;       // the speaker, also softly (short answers, "mm"), still well above bleed
        return true;
    }
};

// ---------------------------------------------------------------- speaker pitch (median fundamental of strong speech)
struct PitchTracker
{
    static constexpr int kBins = 64; static constexpr float kLo = 60.0f, kHi = 450.0f;
    Biquad lp; int dec = 4, decCnt = 0; double fs = 12000; float decAcc = 0;
    std::vector<float> ring; int pos = 0, sinceLast = 0, win = 480, hop = 240;
    std::array<int, kBins> hist {}; int count = 0; bool flagged = false; int flaggedCnt = 0;
    void prepare (double sr)
    {
        dec = std::max (1, (int) std::round (sr / 12000.0)); fs = sr / dec;
        lp.lowpass (sr, 900, 0.707); lp.reset(); decCnt = 0; decAcc = 0;
        win = (int) (fs * 0.04); hop = (int) (fs * 0.02);
        ring.assign ((size_t) win, 0.0f); pos = 0; sinceLast = 0; flaggedCnt = 0;
    }
    void reset() { hist.fill (0); count = 0; }
    // feed one mono sample; `use` = this is clearly the speaker
    inline void push (float x, bool use)
    {
        const float y = lp.process (x, 0);
        if (use) ++flaggedCnt;
        if (++decCnt < dec) return;
        decCnt = 0;
        ring[(size_t) pos] = y; if (++pos >= win) pos = 0;
        if (++sinceLast < hop) return;
        sinceLast = 0;
        const bool ok = flaggedCnt > hop * dec * 3 / 4; flaggedCnt = 0;
        if (! ok) return;
        // autocorrelation of the last 40 ms
        float buf[1024]; const int n = std::min (win, 1024);
        for (int k = 0; k < n; ++k) buf[k] = ring[(size_t) ((pos + k) % win)];
        double r0 = 0; for (int k = 0; k < n; ++k) r0 += (double) buf[k] * buf[k];
        if (r0 < 1e-9) return;
        const int lagLo = (int) (fs / kHi), lagHi = std::min ((int) (fs / kLo), n - 40);
        double best = 0; int bestLag = 0;
        for (int L = lagLo; L <= lagHi; ++L)
        {
            double r = 0; for (int k = 0; k + L < n; ++k) r += (double) buf[k] * buf[k + L];
            r /= (double) (n - L) / n;
            if (r > best) { best = r; bestLag = L; }
        }
        if (bestLag == 0 || best < 0.55 * r0) return;
        const float f0 = (float) (fs / bestLag);
        const int bin = (int) std::floor (std::log (f0 / kLo) / std::log (kHi / kLo) * kBins);
        if (bin < 0 || bin >= kBins) return;
        ++hist[(size_t) bin]; ++count;
    }
    // median fundamental, 0 if not enough material
    float median (int minCount = 60) const
    {
        if (count < minCount) return 0;
        int acc = 0;
        for (int b = 0; b < kBins; ++b) { acc += hist[(size_t) b]; if (acc * 2 >= count) return kLo * std::pow (kHi / kLo, (b + 0.5f) / kBins); }
        return 0;
    }
};

// ---------------------------------------------------------------- band analyser (FFT 4096, hop 2048)
struct BandAnalyser
{
    static constexpr int order = 12;
    Fft fft { order };
    std::vector<float> fifo, window; std::vector<std::complex<float>> buf;
    int fill = 0; double sr = 48000;
    std::vector<int> binBand; // band index per bin or -1
    void prepare (double sampleRate)
    {
        sr = sampleRate; const int n = fft.size();
        fifo.assign ((size_t) n, 0.0f); window.resize ((size_t) n); buf.resize ((size_t) n); fill = 0;
        for (int i = 0; i < n; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2 * kPi * (float) i / (float) n);
        binBand.assign ((size_t) n / 2, -1);
        const float k = std::pow (2.0f, 1.0f / 6.0f);
        for (int b = 0; b < kNumBands; ++b)
        {
            const float lo = bandCentres()[(size_t) b] / k, hi = bandCentres()[(size_t) b] * k;
            int nearest = (int) std::lround (bandCentres()[(size_t) b] * n / sr); bool any = false;
            for (int i = 1; i < n / 2; ++i) { float f = (float) (i * sr / n); if (f >= lo && f < hi) { binBand[(size_t) i] = b; any = true; } }
            if (! any && nearest > 0 && nearest < n / 2) binBand[(size_t) nearest] = b;
        }
    }
    // push a mono sample; when a frame completes, writes band powers and returns true
    inline bool push (float x, std::array<double, kNumBands>& out)
    {
        const int n = fft.size();
        fifo[(size_t) fill++] = x;
        if (fill < n) return false;
        for (int i = 0; i < n; ++i) buf[(size_t) i] = { fifo[(size_t) i] * window[(size_t) i], 0.0f };
        fft.forward (buf);
        out.fill (0.0);
        for (int i = 1; i < n / 2; ++i) { int b = binBand[(size_t) i]; if (b >= 0) out[(size_t) b] += (double) std::norm (buf[(size_t) i]); }
        std::copy (fifo.begin() + n / 2, fifo.end(), fifo.begin()); fill = n / 2;
        return true;
    }
};

// deviation of a profile from the recipe, with the overall offset removed by the median (robust to a big boost in one region)
inline std::array<float, kNumBands> deviationFrom (const std::array<float, kNumBands>& prof, const std::array<float, kNumBands>& rec)
{
    std::array<float, kNumBands> d {}; for (int b = 0; b < kNumBands; ++b) d[(size_t) b] = prof[(size_t) b] - rec[(size_t) b];
    auto tmp = d; std::nth_element (tmp.begin(), tmp.begin() + kNumBands / 2, tmp.end());
    const float med = 0.5f * (tmp[kNumBands / 2] + *std::max_element (tmp.begin(), tmp.begin() + kNumBands / 2));
    for (auto& v : d) v -= med;
    return d;
}

inline std::array<float, kNumBands> powersToProfile (const std::array<double, kNumBands>& p)
{
    std::array<float, kNumBands> db {}; float mean = 0;
    for (int b = 0; b < kNumBands; ++b) { db[(size_t) b] = powToDb (p[(size_t) b]); mean += db[(size_t) b]; }
    mean /= kNumBands;
    for (auto& v : db) v -= mean;
    return db;
}

// sibilance metric helper: per 10 ms frame ratio of 6.5 kHz band energy to full energy
struct SibMeter
{
    Biquad bp; double sib = 0, full = 0; int cnt = 0, frameLen = 480; float winMax = -60; int winFrames = 0; float value = -60; bool primed = false;
    void prepare (double sr) { bp.bandpass (sr, 6500, 0.9); bp.reset(); frameLen = (int) (sr * 0.01); sib = full = 0; cnt = 0; winMax = -60; winFrames = 0; value = -60; primed = false; }
    // returns true when a 1-second window closed (value updated)
    inline bool push (float x, bool voiced)
    {
        const float s = bp.process (x, 0); sib += (double) s * s; full += (double) x * x;
        if (++cnt < frameLen) return false;
        const float r = (float) (10.0 * std::log10 ((sib + 1e-12) / (full + 1e-12)));
        if (voiced) winMax = std::max (winMax, r);
        sib = full = 0; cnt = 0;
        if (++winFrames < 100) return false;
        winFrames = 0;
        if (winMax > -59.0f) { value = primed ? value + 0.25f * (winMax - value) : winMax; primed = true; }
        winMax = -60; return true;
    }
};

// ---------------------------------------------------------------- offline recipe analysis (for Add reference)
inline Recipe analyseReference (const float* mono, size_t n, double sr, const std::string& name, const std::string& source)
{
    Recipe r; r.name = name; r.source = source;
    VoiceDetector vad; vad.prepare (sr);
    BandAnalyser an; an.prepare (sr);
    SibMeter sm; sm.prepare (sr);
    std::array<double, kNumBands> acc {}, frame {}; int frames = 0, voicedSub = 0, totalSub = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = mono[i];
        if (vad.push (x)) { ++totalSub; if (vad.voiced) ++voicedSub; }
        sm.push (x, vad.voiced);
        if (an.push (x, frame))
        {
            if (totalSub > 0 && voicedSub >= (int) (0.7 * totalSub)) { for (int b = 0; b < kNumBands; ++b) acc[(size_t) b] += frame[(size_t) b]; ++frames; }
            voicedSub = totalSub = 0;
        }
    }
    if (frames < 20) return r; // not enough speech
    r.profile = powersToProfile (acc);
    r.sibPeak = sm.primed ? sm.value : -8.0f;
    r.valid = true;
    return r;
}

// ---------------------------------------------------------------- the engine
enum class ColorType { Opto = 0, Tape = 1, Clean = 2 };
enum Light { Boom = 0, Mud, Box, Hard, Sharp, Sss, Thin, Dull, NumLights };
// hand-adjustable tone regions shown as lights (Sss is separate: it drives the de-esser)
enum Tone { TLow = 0, TMud, TBox, THard, TSharp, TAir, NumTones };
inline float toneWeight (int tone, float f)
{
    auto w = [f] (std::initializer_list<float> full, std::initializer_list<float> half)
    { for (float c : full) if (std::abs (f - c) < 1) return 1.0f; for (float c : half) if (std::abs (f - c) < 1) return 0.5f; return 0.0f; };
    switch (tone)
    {
        case TLow:   return w ({ 63, 80, 100, 125, 160, 200 }, { 250 });
        case TMud:   return w ({ 200, 250, 315, 400 }, { 160, 500 });
        case TBox:   return w ({ 500, 630, 800 }, { 400, 1000 });
        case THard:  return w ({ 1600, 2000, 2500, 3150 }, { 1250, 4000 });
        case TSharp: return w ({ 4000, 5000, 6300 }, { 3150, 8000 });
        case TAir:   return w ({ 8000, 10000, 12500 }, { 6300 });
        default:     return 0.0f;
    }
}
inline std::pair<float, float> toneRegion (int tone)
{
    switch (tone) { case TLow: return { 63, 200 }; case TMud: return { 200, 400 }; case TBox: return { 500, 800 };
                    case THard: return { 1600, 3150 }; case TSharp: return { 4000, 6300 }; default: return { 8000, 12500 }; }
}
inline float toneLimit (int tone) { return (tone == TLow || tone == TAir) ? 4.0f : 3.0f; }

struct Params
{
    float fix = 50, plosive = 50, color = 0, deess = 50, levelTarget = -18, tilt = 0, body = 0;
    std::array<float, NumTones> tone {};   // manual EQ per region, dB
    ColorType colorType = ColorType::Opto;
    bool levelOn = true, slowMode = false, bypassAB = false;
    std::array<bool, NumLights> lightOn { true, true, true, true, true, true, true, true };
};

struct Meters
{
    std::atomic<int> light[NumLights] {};          // 0 ok, 1 near, 2 over, 3 no data
    std::atomic<float> inX { 0 }, inY { 0 }, outX { 0 }, outY { 0 };
    std::atomic<float> inLufs { -70 }, outLufs { -70 };
    std::atomic<float> learnProgress { 0 };        // 0..1
    std::atomic<bool> learned { false }, hasData { false }, voiced { false };
    std::atomic<int> tone[NumTones] {};      // 0 ok, 1 near, 2 over
    std::atomic<int> toneDir[NumTones] {};   // +1 too much, -1 too little
    std::atomic<float> autoGainDb { 0 };
};

class Engine
{
public:
    Meters meters;

    void setRecipe (const Recipe& r) { recipe = r; correctionDirty = true; }
    const Recipe& getRecipe() const { return recipe; }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        lookPlosive = (int) (sr * 0.005); lookDeess = (int) (sr * 0.002); lookLim = (int) (sr * 0.003); lookLev = (int) (sr * 0.3);
        latency = lookPlosive + lookDeess + lookLim + lookLev;
        dLev.prepare (lookLev); pitch.prepare (sr); if (! restored) pitch.reset();
        dPlos.prepare (lookPlosive); dDeess.prepare (lookDeess); dLim.prepare (lookLim); dDry.prepare (latency);
        limGainBuf.assign ((size_t) lookLim + 1, 1.0f); limPos = 0;
        plosLP.lowpass (sr, 150, 0.707); plosLP.reset();
        deBP.bandpass (sr, 6500, 0.9); deBP.reset();
        for (int i = 0; i < 2; ++i) { kShelf[i].highShelf (sr, 1681, 0.707, 4.0); kShelf[i].reset(); kHp[i].highpass (sr, 38, 0.5); kHp[i].reset(); }
        tapeBump.peak (sr, 90, 0.8, 0); tapeHf.highShelf (sr, 9000, 0.707, 0); tapeBump.reset(); tapeHf.reset();
        dcPrev[0] = dcPrev[1] = dcOut[0] = dcOut[1] = 0;
        for (auto& bq : eq) bq.reset();
        eqGainDb.fill (0); eqTargetDb.fill (0); applyEqCoeffs();
        vad.prepare (sr); anIn.prepare (sr); anOut.prepare (sr); sibOut.prepare (sr);
        plEnvFast = plEnvSlow = plGain = 1e-9f; plGain = 1;
        deEnvSib = deEnvFull = 0; deGain = 1;
        colEnv = 0; colSlowLvl = -30; grFast = grSlow = 0;
        levelGainDb = 0; levSettled = false; matchGainDb = 0;
        agEnergy = 0; agBlockAcc = 0; agBlocks = 0; agBlockCnt = 0; agBlockLen = (int) (sr * 0.1);
        if (! restoredGain) { inGainDb = inGainTarget = 0; } abGainDb = 0; limGain = 1;
        loudIn.reset (sr, 30); loudPre.reset (sr, 30); loudOut.reset (sr, 30); loudLevIn.reset (sr, 6); colMakeupDb = 0;
        inProfileEma.fill (0); outProfileEma.fill (0); inEmaPrimed = outEmaPrimed = false;
        learnAcc.fill (0); learnFrames = 0; framesPerSec = (float) (sr / 2048.0); learnTargetFrames = (int) (30.0f * framesPerSec);
        if (restored) { learning = false; profileValid = true; meters.learned = true; meters.learnProgress = 1; }
        else { learning = true; profileValid = false; meters.learned = false; meters.learnProgress = 0; }
        correctionDirty = true;
        hopVoiced = hopTotal = 0;
        finishPrepare();
    }
    int getLatency() const { return latency; }
    float getLevelGainDb() const { return levelGainDb; }
    float getInputGainDb() const { return inGainDb; }
    void setInputGainDb (float g) { inGainDb = inGainTarget = g; restoredGain = true; meters.autoGainDb = g; }
    std::array<float, kNumBands> getEqTarget() const { return eqTargetDb; }
    std::array<float, kNumBands> getLearnedProfile() const { return learnedProfile; }

    void requestLearn() { learnRequested.store (true); }
    float getSpeakerF0() const { return f0Speaker; }
    void setSpeakerF0 (float f) { f0Speaker = f; correctionDirty = true; }
    // persisted learned state
    bool getLearnedProfile (std::array<float, kNumBands>& out) const { if (! profileValid || learning) return false; out = learnedProfile; return true; }
    void setLearnedProfile (const std::array<float, kNumBands>& p) { learnedProfile = p; restored = true; profileValid = true; learning = false; correctionDirty = true; meters.learned = true; meters.learnProgress = 1; }

    void process (float* const* io, int numCh, int n, const Params& p)
    {
        if (learnRequested.exchange (false)) { learning = true; restored = false; restoredGain = false; agEnergy = 0; agBlocks = 0; learnAcc.fill (0); learnFrames = 0; meters.learned = false; meters.learnProgress = 0; pitch.reset(); f0Speaker = 0; }
        if (p.fix != lastFix || p.lightOn != lastLights || p.tilt != lastTilt || p.body != lastBody || p.tone != lastTone) { lastFix = p.fix; lastLights = p.lightOn; lastTilt = p.tilt; lastBody = p.body; lastTone = p.tone; correctionDirty = true; }
        lastLevelTarget = p.levelTarget;
        slowModeActive = p.slowMode;
        if (correctionDirty && profileValid) recomputeCorrection();
        if (p.colorType != lastColorType || std::abs (p.color - lastColor) > 0.5f) { setupColorFilters (p); lastColorType = p.colorType; lastColor = p.color; }

        updateEqSmoothing (n);
        const int chs = std::min (numCh, 2);
        const float levTarget = p.levelTarget;
        const float plosAmt = p.plosive / 100.0f, deAmt = p.deess / 100.0f, colAmt = p.color / 100.0f;

        for (int i = 0; i < n; ++i)
        {
            float in[2] = { io[0][i], chs > 1 ? io[1][i] : io[0][i] };
            const float mono = 0.5f * (in[0] + in[1]);

            // ---- analysis on the raw input: voice detection, learning, input profile, input loudness
            if (vad.push (mono)) { ++hopTotal; if (vad.voiced) ++hopVoiced; meters.voiced = vad.voiced; }
            pitch.push (mono, vad.strong && (learning || slowModeActive || f0Speaker <= 0));
            if (anIn.push (mono, frame)) onInputFrame();
            loudIn.push (in, chs, vad.voiced, kShelf, kHp, 0);

            // ---- 0. auto input gain: measured on the speaker's strong speech while learning, then held
            {
                double e = 0;
                for (int c = 0; c < chs; ++c) { float k = kHpE[c].process (kShelfE[c].process (in[c], 0), 0); e += (double) k * k; }
                if (chs < 2) e *= 2;
                agBlockAcc += e;
                if (++agBlockCnt >= agBlockLen)
                {
                    if (learning && vad.strong)
                    {
                        agEnergy += agBlockAcc / agBlockCnt; ++agBlocks;
                        if (agBlocks >= 20)
                        {
                            const float L = (float) (-0.691 + 10 * std::log10 (std::max (agEnergy / agBlocks, 1e-12)));
                            inGainTarget = std::clamp (lastLevelTarget - L, -12.0f, 30.0f);
                        }
                    }
                    agBlockAcc = 0; agBlockCnt = 0;
                }
                inGainDb += (inGainTarget - inGainDb) * relCoef (0.5f);
            }
            const float gIn = dbToGain (inGainDb);
            float x[2] = { in[0] * gIn, in[1] * gIn };
            float dry[2];
            for (int c = 0; c < 2; ++c) dry[c] = dDry.push (in[c], c, latency);
            dDry.advance();

            // ---- 1. plosive guard (lookahead): reduce the band under 150 Hz on sudden bursts
            {
                const float low = plosLP.process (mono, 0);
                const float a = std::abs (low);
                plEnvFast = a > plEnvFast ? a : plEnvFast + (a - plEnvFast) * relCoef (0.04f);
                plEnvSlow += (a * a - plEnvSlow) * relCoef (0.4f);
                float target = 1.0f;
                if (plosAmt > 0.001f)
                {
                    const float slowRms = std::sqrt (plEnvSlow) + 1e-6f;
                    const float overDb = gainToDb (plEnvFast / slowRms) - (14.0f - 6.0f * plosAmt);
                    if (overDb > 0) target = dbToGain (-std::min (overDb * 1.5f, 18.0f * plosAmt));
                }
                plGain = target < plGain ? plGain + (target - plGain) * relCoef (0.0005f) : plGain + (target - plGain) * relCoef (0.06f);
                for (int c = 0; c < 2; ++c)
                {
                    const float xd = dPlos.push (x[c], c, lookPlosive);
                    const float lowD = plosLPd[c].process (xd);
                    x[c] = xd - (1.0f - plGain) * lowD;
                }
                dPlos.advance();
            }

            // ---- 2. balance EQ toward the recipe
            for (int c = 0; c < 2; ++c) for (auto& bq : eq) x[c] = bq.process (x[c], c);

            // ---- 3. color
            if (colAmt > 0.001f) applyColor (x, p.colorType, colAmt);

            // ---- 4. de-esser (lookahead)
            {
                const float m = 0.5f * (x[0] + x[1]);
                const float s = deBP.process (m, 0);
                const float as = std::abs (s), af = std::abs (m);
                deEnvSib = as > deEnvSib ? deEnvSib + (as - deEnvSib) * relCoef (0.0003f) : deEnvSib + (as - deEnvSib) * relCoef (0.03f);
                deEnvFull = af > deEnvFull ? deEnvFull + (af - deEnvFull) * relCoef (0.001f) : deEnvFull + (af - deEnvFull) * relCoef (0.06f);
                float target = 1.0f;
                if (deAmt > 0.001f && p.lightOn[Sss] && deEnvFull > 1e-4f)
                {
                    const float ratioDb = gainToDb (deEnvSib / (deEnvFull + 1e-9f));
                    const float thr = -4.0f - 6.0f * deAmt;
                    if (ratioDb > thr) target = dbToGain (-std::min ((ratioDb - thr) * 1.5f, 12.0f * deAmt));
                }
                deGain = target < deGain ? deGain + (target - deGain) * relCoef (0.0003f) : deGain + (target - deGain) * relCoef (0.04f);
                for (int c = 0; c < 2; ++c)
                {
                    const float xd = dDeess.push (x[c], c, lookDeess);
                    const float sd = deBPd[c].process (xd);
                    x[c] = xd - (1.0f - deGain) * sd;
                }
                dDeess.advance();
            }

            // ---- 5. leveller: rides words and short answers, looks 300 ms ahead so it is already there when they start
            loudLevIn.push (x, chs, vad.voiced, kShelfB, kHpB, 0);
            if (p.levelOn)
            {
                if (vad.voiced && loudLevIn.valid)
                {
                    // 0.6 s loudness window centred on the sound (thanks to the lookahead), 2 dB dead zone
                    const float want = std::clamp (levTarget - loudLevIn.lufs, -12.0f, 12.0f);
                    const float err = want - levelGainDb;
                    if (std::abs (err) <= 2.0f) levSettled = true;
                    if (std::abs (err) > 2.0f && (err < 0 || vad.lift))   // only raise on the speaker, never on bleed
                    {
                        const float toward = err > 0 ? want - 2.0f : want + 2.0f;
                        levelGainDb += (toward - levelGainDb) * relCoef (! levSettled ? 0.15f : (err < 0 ? 0.25f : 0.35f));
                    }
                }
            }
            for (int c = 0; c < 2; ++c) x[c] = dLev.push (x[c], c, lookLev);
            dLev.advance();
            const float levDb = p.levelOn ? levelGainDb : 0.0f;
            // ---- 6. gain match when the leveller is off (output follows input loudness)
            loudPre.push (x, chs, vad.voiced, kShelfC, kHpC, levDb);
            if (! p.levelOn)
            {
                if (vad.voiced && loudPre.valid && loudIn.valid)
                {
                    const float want = std::clamp (loudIn.lufs + inGainDb - loudPre.lufs, -12.0f, 12.0f);
                    matchGainDb += (want - matchGainDb) * relCoef (2.0f);
                }
            }
            const float g = dbToGain (levDb + (p.levelOn ? 0.0f : matchGainDb));
            for (int c = 0; c < 2; ++c) x[c] *= g;

            // ---- 7. safety limiter, ceiling -1 dBFS, 3 ms lookahead
            {
                const float peak = std::max (std::abs (x[0]), std::abs (x[1]));
                const float ceil = 0.891f; const float req = peak > ceil ? ceil / peak : 1.0f;
                limGainBuf[(size_t) limPos] = req; limPos = (limPos + 1) % (int) limGainBuf.size();
                float mn = 1.0f; for (float v : limGainBuf) mn = std::min (mn, v);
                limGain = mn < limGain ? mn : limGain + (mn - limGain) * relCoef (0.06f);
                for (int c = 0; c < 2; ++c) { float y = dLim.push (x[c], c, lookLim) * limGain; x[c] = std::clamp (y, -ceil, ceil); }
                dLim.advance();
            }

            // ---- output analysis (lights, balance dot, loudness)
            const float om = 0.5f * (x[0] + x[1]);
            if (anOut.push (om, frameOut)) onOutputFrame (p);
            sibOut.push (om, vad.voiced);
            loudOut.push (x, chs, vad.voiced, kShelfD, kHpD, 0);
            if (vad.voiced && loudOut.valid && loudIn.valid)
                abGainDb += (std::clamp (loudOut.lufs - loudIn.lufs, -24.0f, 24.0f) - abGainDb) * relCoef (2.0f);

            // ---- A/B: bypass at matched loudness
            if (p.bypassAB) { const float ga = dbToGain (abGainDb); x[0] = dry[0] * ga; x[1] = dry[1] * ga; }

            io[0][i] = x[0];
            if (numCh > 1) io[1][i] = x[1];
        }
        meters.autoGainDb = inGainDb;
        meters.inLufs = loudIn.valid ? loudIn.lufs : -70.0f;
        meters.outLufs = loudOut.valid ? loudOut.lufs : -70.0f;
        updateSibLight();
    }

private:
    // short-term loudness (K-weighted, 400 ms window, voiced frames only)
    struct Loudness
    {
        double acc = 0; int cnt = 0, blockLen = 4800, nb = 4; double blocks[32] {}; int bi = 0, filled = 0; float lufs = -70; bool valid = false;
        void reset (double sr, int numBlocks = 4) { nb = numBlocks; blockLen = (int) (sr * 0.1); acc = 0; cnt = 0; bi = 0; filled = 0; lufs = -70; valid = false; for (auto& b : blocks) b = 0; }
        inline void push (const float* x, int chs, bool voiced, Biquad* shelf, Biquad* hp, float preGainDb)
        {
            const float g = preGainDb != 0 ? dbToGain (preGainDb) : 1.0f;
            double e = 0;
            for (int c = 0; c < std::max (chs, 1); ++c) { float k = hp[c].process (shelf[c].process (x[c] * g, 0), 0); e += (double) k * k; }
            if (chs < 2) e *= 2;
            acc += e;
            if (++cnt < blockLen) return;
            const double ms = acc / cnt; acc = 0; cnt = 0;
            if (! voiced) return;
            blocks[bi] = ms; bi = (bi + 1) % nb; filled = std::min (filled + 1, nb);
            if (filled >= std::min (nb, 4)) { double m = 0; for (int k = 0; k < filled; ++k) m += blocks[k]; m /= filled; lufs = (float) (-0.691 + 10 * std::log10 (std::max (m, 1e-12))); valid = true; }
        }
    };

    inline float relCoef (float seconds) const { return 1.0f - std::exp (-1.0f / (float) (seconds * sr)); }

    void onInputFrame()
    {
        const bool voicedFrame = hopTotal > 0 && hopVoiced >= (int) (0.7 * hopTotal);
        hopVoiced = hopTotal = 0;
        if (! voicedFrame) return;
        const auto prof = powersToProfile (frame);
        emaProfile (inProfileEma, prof, inEmaPrimed, 8.0f * framesPerSec);
        if (learning)
        {
            for (int b = 0; b < kNumBands; ++b) learnAcc[(size_t) b] += frame[(size_t) b];
            ++learnFrames;
            meters.learnProgress = std::min (1.0f, (float) learnFrames / (float) learnTargetFrames);
            if (learnFrames >= 40) { learnedProfile = powersToProfile (learnAcc); profileValid = true; correctionDirty = true; }
            if (learnFrames >= learnTargetFrames) { learning = false; meters.learned = true; restored = true; restoredGain = true; }   // keep it through bounces/re-prepare
        }
        else if (slowModeActive)
        {
            emaProfile (learnedProfile, prof, profileValid, 30.0f * framesPerSec); correctionDirty = true;
        }
        if (learning || slowModeActive || f0Speaker <= 0)
        {
            const float f = pitch.median();
            if (f > 0 && std::abs (f - f0Speaker) > 3.0f) { f0Speaker = f; correctionDirty = true; }
        }
        if (correctionDirty) recomputeCorrection(); else updatePredicted();
    }

    void onOutputFrame (const Params& p)
    {
        const bool voicedFrame = vad.voiced;
        if (! voicedFrame || ! recipe.valid) return;
        const auto prof = powersToProfile (frameOut);
        emaProfile (outProfileEma, prof, outEmaPrimed, 8.0f * framesPerSec);

        // region deviations vs recipe
        const auto dev = deviationFrom (outProfileEma, recipe.profile);
        auto region = [&] (float lo, float hi) { float s = 0; int k = 0; for (int b = 0; b < kNumBands; ++b) { float f = bandCentres()[(size_t) b]; if (f >= lo && f <= hi) { s += dev[(size_t) b]; ++k; } } return k ? s / k : 0.0f; };
        auto rate = [] (float dev, float lim) { return dev > lim ? 2 : (dev > 0.6f * lim ? 1 : 0); };
        meters.light[Boom] = p.lightOn[Boom] ? rate (region (80, 200), 4.0f) : 4;
        meters.light[Mud] = p.lightOn[Mud] ? rate (region (200, 400), 3.0f) : 4;
        meters.light[Box] = p.lightOn[Box] ? rate (region (400, 900), 3.0f) : 4;
        meters.light[Hard] = p.lightOn[Hard] ? rate (region (1600, 4000), 3.0f) : 4;
        meters.light[Sharp] = p.lightOn[Sharp] ? rate (region (4000, 7000), 3.0f) : 4;
        meters.light[Thin] = p.lightOn[Thin] ? rate (-region (100, 300), 4.0f) : 4;
        meters.light[Dull] = p.lightOn[Dull] ? rate (-region (6000, 12500), 4.0f) : 4;
        sssOn = p.lightOn[Sss];
    }

    void updateSibLight()
    {
        if (! sssOn) { meters.light[Sss] = 4; return; }
        if (! sibOut.primed || ! recipe.valid) { meters.light[Sss] = 0; return; }
        const float d = sibOut.value - recipe.sibPeak;
        meters.light[Sss] = d > 5 ? 2 : (d > 2 ? 1 : 0);
    }

    static void emaProfile (std::array<float, kNumBands>& ema, const std::array<float, kNumBands>& v, bool& primed, float frames)
    {
        if (! primed) { ema = v; primed = true; return; }
        const float a = 1.0f / frames;
        for (int b = 0; b < kNumBands; ++b) ema[(size_t) b] += a * (v[(size_t) b] - ema[(size_t) b]);
    }

    // balance dot: x = dark(-1) .. bright(+1), y = thin(-1) .. boomy(+1), relative to the recipe
    std::pair<float, float> dotFrom (const std::array<float, kNumBands>& prof) const
    {
        if (! recipe.valid) return { 0, 0 };
        auto avg = [&] (float lo, float hi) { float s = 0; int k = 0; for (int b = 0; b < kNumBands; ++b) { float f = bandCentres()[(size_t) b]; if (f >= lo && f <= hi) { s += prof[(size_t) b] - recipe.profile[(size_t) b]; ++k; } } return k ? s / k : 0.0f; };
        const float bright = avg (2000, 8000) - avg (125, 500);
        const float boomy = avg (63, 250) - avg (500, 2000);
        return { std::clamp (bright / 12.0f, -1.0f, 1.0f), std::clamp (boomy / 12.0f, -1.0f, 1.0f) };
    }

    void recomputeCorrection()
    {
        correctionDirty = false;
        if (! recipe.valid) { eqTargetDb.fill (0); return; }
        const float fix = profileValid ? lastFix / 100.0f : 0.0f;
        std::array<float, kNumBands> want {};
        const auto dev = deviationFrom (learnedProfile, recipe.profile);
        const float tolScale = 1.0f - 0.85f * fix * fix;              // Full = (almost) no tolerance window
        for (int b = 0; b < kNumBands; ++b)
        {
            const float d = dev[(size_t) b];
            const float tol = bandTolerance (b) * tolScale;
            const float excess = std::abs (d) > tol ? (d > 0 ? d - tol : d + tol) : 0.0f;
            want[(size_t) b] = -fix * excess;
        }
        // smooth across neighbouring bands
        std::array<float, kNumBands> sm {};
        for (int b = 0; b < kNumBands; ++b)
        {
            const float l = want[(size_t) std::max (b - 1, 0)], r = want[(size_t) std::min (b + 1, kNumBands - 1)];
            sm[(size_t) b] = 0.25f * l + 0.5f * want[(size_t) b] + 0.25f * r;
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = bandCentres()[(size_t) b];
            float v = sm[(size_t) b];
            if (f >= 100 && f <= 250) v *= 0.75f + 0.25f * fix;  // body zone gentler, fully corrected at Full
            // light toggles: an "off" light stops that region being fixed
            auto inR = [f] (float lo, float hi) { return f >= lo && f <= hi; };
            if (v < 0)
            {
                if (! lastLights[Boom] && inR (63, 200)) v = 0;
                if (! lastLights[Mud] && inR (200, 400)) v = 0;
                if (! lastLights[Box] && inR (400, 900)) v = 0;
                if (! lastLights[Hard] && inR (1600, 4000)) v = 0;
                if (! lastLights[Sharp] && inR (4000, 7000)) v = 0;
            }
            else
            {
                if (! lastLights[Thin] && inR (100, 300)) v = 0;
                if (! lastLights[Dull] && inR (6000, 12500)) v = 0;
            }
            // manual trims from dragging the dot: tilt pivots at 1 kHz, body lifts/cuts the lows
            const float oct = std::clamp (std::log2 (f / 1000.0f) / 3.0f, -1.0f, 1.0f);
            v += 0.5f * lastTilt * oct;
            if (f <= 250) v += lastBody; else if (f <= 500) v += 0.5f * lastBody;
            // automatic boosts never invent a bottom the voice doesn't have: nothing under ~0.7 x the speaker's
            // fundamental (that is only rumble, handling and the other mic's bleed), little around it
            float boostCap = 9.0f;
            if (f0Speaker > 0) { if (f < 0.7f * f0Speaker) boostCap = 0.0f; else if (f < 1.1f * f0Speaker) boostCap = 3.0f; }
            v = std::clamp (v, -18.0f, boostCap);                    // automatic part stays sensible
            for (int t = 0; t < NumTones; ++t) v += lastTone[(size_t) t] * toneWeight (t, f);   // by ear: no cap
            if (f > sr * 0.45) v = 0;
            sm[(size_t) b] = std::clamp (v, -36.0f, 36.0f);
        }
        // compensate for overlap between neighbouring peaking filters (few Jacobi steps)
        std::array<float, kNumBands> set = sm;
        for (int it = 0; it < 4; ++it)
        {
            std::array<Biquad, kNumBands> tmp;
            for (int b = 0; b < kNumBands; ++b) tmp[(size_t) b].peak (sr, std::min ((double) bandCentres()[(size_t) b], sr * 0.45), kQ, set[(size_t) b]);
            for (int b = 0; b < kNumBands; ++b)
            {
                double resp = 0; const double f = std::min ((double) bandCentres()[(size_t) b], sr * 0.45);
                for (int k = 0; k < kNumBands; ++k) resp += tmp[(size_t) k].responseDb (sr, f);
                set[(size_t) b] = std::clamp (set[(size_t) b] + 0.6f * (sm[(size_t) b] - (float) resp), -40.0f, 40.0f);
            }
        }
        eqTargetDb = set;
        // actual response of the final curve at each band: used to predict the output instantly
        {
            std::array<Biquad, kNumBands> tmp;
            for (int b = 0; b < kNumBands; ++b) tmp[(size_t) b].peak (sr, std::min ((double) bandCentres()[(size_t) b], sr * 0.45), kQ, set[(size_t) b]);
            for (int b = 0; b < kNumBands; ++b)
            {
                double resp = 0; const double f = std::min ((double) bandCentres()[(size_t) b], sr * 0.45);
                for (int k = 0; k < kNumBands; ++k) resp += tmp[(size_t) k].responseDb (sr, f);
                predResp[(size_t) b] = (float) resp;
            }
        }
        updatePredicted();
    }

    // lights and the output dot from (input profile + EQ curve): reacts instantly to every control
    void updatePredicted()
    {
        if (! recipe.valid || ! (profileValid || inEmaPrimed)) return;
        // base = the learned long-term voice (stable); before that exists, the running input average
        const auto& base = profileValid ? learnedProfile : inProfileEma;
        { const auto [ix, iy] = dotFrom (base); meters.inX = ix; meters.inY = iy; }
        std::array<float, kNumBands> pred {};
        for (int b = 0; b < kNumBands; ++b) pred[(size_t) b] = base[(size_t) b] + predResp[(size_t) b];
        const auto dev = deviationFrom (pred, recipe.profile);
        const auto baseDev = deviationFrom (base, recipe.profile);
        for (int t = 0; t < NumTones; ++t)
        {
            const auto [lo, hi] = toneRegion (t);
            float s = 0; int k = 0;
            const float below = f0Speaker > 0 ? 0.7f * f0Speaker : 0.0f;   // under the voice: not part of its tone
            for (int b = 0; b < kNumBands; ++b) { float f = bandCentres()[(size_t) b]; if (f >= lo && f <= hi && (f >= below || dev[(size_t) b] > 0)) { s += dev[(size_t) b]; ++k; } }
            const float d = k ? s / k : 0.0f, lim = toneLimit (t);
            meters.tone[t] = std::abs (d) > lim ? 2 : (std::abs (d) > 0.6f * lim ? 1 : 0);
            meters.toneDir[t] = d >= 0 ? 1 : -1;
            // the source has almost nothing in this region: say so instead of nagging (boosting mostly adds hiss)
            float bs = 0; int bk = 0;
            for (int b = 0; b < kNumBands; ++b) { float f = bandCentres()[(size_t) b]; if (f >= lo && f <= hi) { bs += baseDev[(size_t) b]; ++bk; } }
            if (d < 0 && bk && bs / bk < -10.0f && meters.tone[t] == 2) { meters.tone[t] = 1; meters.toneDir[t] = -2; }
        }
        const auto [x, y] = dotFrom (pred);
        meters.outX = x; meters.outY = y; meters.hasData = true;
    }

    void updateEqSmoothing (int n)
    {
        const float a = 1.0f - std::exp (-(float) n / (float) (0.3 * sr));
        bool changed = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float d = eqTargetDb[(size_t) b] - eqGainDb[(size_t) b];
            if (std::abs (d) > 0.005f) { eqGainDb[(size_t) b] += a * d; changed = true; }
        }
        if (changed) applyEqCoeffs();
    }
    void applyEqCoeffs()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            float z1[2] = { eq[(size_t) b].z1[0], eq[(size_t) b].z1[1] }, z2[2] = { eq[(size_t) b].z2[0], eq[(size_t) b].z2[1] };
            eq[(size_t) b].peak (sr, std::min ((double) bandCentres()[(size_t) b], sr * 0.45), kQ, eqGainDb[(size_t) b]);
            eq[(size_t) b].z1[0] = z1[0]; eq[(size_t) b].z1[1] = z1[1]; eq[(size_t) b].z2[0] = z2[0]; eq[(size_t) b].z2[1] = z2[1];
        }
    }

    void setupColorFilters (const Params& p)
    {
        const float a = p.color / 100.0f;
        if (p.colorType == ColorType::Tape) { tapeBump.peak (sr, 90, 0.8, 1.5 * a); tapeHf.highShelf (sr, 9000, 0.707, -2.0 * a); }
        else { tapeBump.peak (sr, 90, 0.8, 0); tapeHf.highShelf (sr, 9000, 0.707, 0); }
    }

    inline void applyColor (float* x, ColorType t, float amt)
    {
        const float m = std::max (std::abs (x[0]), std::abs (x[1]));
        // level detector (10 ms RMS) and a slow running speech level (voiced only)
        colEnv += (m * m - colEnv) * relCoef (0.01f);
        const float lvl = powToDb (colEnv);
        if (vad.voiced) colSlowLvl += (lvl - colSlowLvl) * relCoef (2.0f);
        float thr, ratio, att, relF, relS;
        if (t == ColorType::Opto) { thr = colSlowLvl - (1.0f + 5.0f * amt); ratio = 3.0f; att = 0.01f; relF = 0.06f; relS = 1.5f; }
        else if (t == ColorType::Tape) { thr = colSlowLvl - (1.0f + 3.0f * amt); ratio = 1.6f; att = 0.002f; relF = 0.1f; relS = 0.1f; }
        else { thr = colSlowLvl - (1.0f + 5.0f * amt); ratio = 2.0f + 2.0f * amt; att = 0.002f; relF = 0.12f; relS = 0.12f; }
        const float knee = 6.0f; const float over = lvl - thr;
        float gr = 0;
        if (over > knee / 2) gr = over * (1 - 1 / ratio);
        else if (over > -knee / 2) { const float o = over + knee / 2; gr = (1 - 1 / ratio) * o * o / (2 * knee); }
        grFast = gr > grFast ? grFast + (gr - grFast) * relCoef (att) : grFast + (gr - grFast) * relCoef (relF);
        grSlow = gr > grSlow ? grSlow + (gr - grSlow) * relCoef (att * 20) : grSlow + (gr - grSlow) * relCoef (relS);
        const float grDb = t == ColorType::Opto ? 0.5f * grFast + 0.5f * grSlow : grFast;
        // automatic make-up: the average gain reduction while talking is given back, so Color never drops the level
        if (vad.voiced) colMakeupDb += (grDb - colMakeupDb) * relCoef (0.6f);
        const float g = dbToGain (colMakeupDb - grDb);
        for (int c = 0; c < 2; ++c)
        {
            float y = x[c] * g;
            if (t == ColorType::Opto)
            {   // tube: gentle even harmonics, DC removed
                const float h = y + 0.6f * amt * y * y;
                dcOut[c] = h - dcPrev[c] + 0.9995f * dcOut[c]; dcPrev[c] = h; y = dcOut[c];
            }
            else if (t == ColorType::Tape)
            {
                const float k = 1.0f + 2.5f * amt;
                y = std::tanh (k * y) / k;
                y = tapeHf.process (tapeBump.process (y, c), c);
            }
            x[c] = y;
        }
    }


    // state
    double sr = 48000; int latency = 0, lookPlosive = 0, lookDeess = 0, lookLim = 0, lookLev = 0;
    PitchTracker pitch; float f0Speaker = 0;
    static constexpr double kQ = 4.3;
    Recipe recipe;
    std::array<Biquad, kNumBands> eq; std::array<float, kNumBands> eqGainDb {}, eqTargetDb {};
    Delay dPlos, dDeess, dLim, dDry, dLev;
    Biquad plosLP; struct LP2 { Biquad b; float process (float x) { return b.process (x, 0); } };
    struct PerCh { Biquad b; int ch = 0; float process (float x) { return b.process (x, ch); } };
    PerCh plosLPd[2], deBPd[2];
    Biquad deBP; Biquad kShelf[2], kHp[2], kShelfB[2], kHpB[2], kShelfC[2], kHpC[2], kShelfD[2], kHpD[2];
    Biquad tapeBump, tapeHf; float dcPrev[2] {}, dcOut[2] {};
    float plEnvFast = 0, plEnvSlow = 0, plGain = 1, deEnvSib = 0, deEnvFull = 0, deGain = 1;
    float colEnv = 0, colSlowLvl = -30, grFast = 0, grSlow = 0, colMakeupDb = 0;
    float inGainDb = 0, inGainTarget = 0; double agEnergy = 0, agBlockAcc = 0; int agBlocks = 0, agBlockCnt = 0, agBlockLen = 4800; Biquad kShelfE[2], kHpE[2];
    bool levSettled = false; float levelGainDb = 0, matchGainDb = 0, abGainDb = 0, limGain = 1;
    std::vector<float> limGainBuf; int limPos = 0;
    VoiceDetector vad; BandAnalyser anIn, anOut; SibMeter sibOut;
    std::array<double, kNumBands> frame {}, frameOut {};
    Loudness loudIn, loudPre, loudOut, loudLevIn;
    std::array<float, kNumBands> inProfileEma {}, outProfileEma {}, learnedProfile {};
    bool inEmaPrimed = false, outEmaPrimed = false;
    std::array<double, kNumBands> learnAcc {}; int learnFrames = 0, learnTargetFrames = 700;
    bool learning = true, profileValid = false, correctionDirty = true, slowModeActive = false, sssOn = true;
    std::atomic<bool> learnRequested { false };
    int hopVoiced = 0, hopTotal = 0; float framesPerSec = 23.4f; bool restored = false, restoredGain = false;
    float lastFix = -1, lastColor = -1, lastTilt = 0, lastBody = 0, lastLevelTarget = -18; std::array<float, NumTones> lastTone {}; std::array<float, kNumBands> predResp {}; ColorType lastColorType = ColorType::Clean;
    std::array<bool, NumLights> lastLights {};

public:
    // must be called after prepare(): set up per-channel copies of filters
    void finishPrepare()
    {
        for (int c = 0; c < 2; ++c)
        {
            plosLPd[c].b.lowpass (sr, 150, 0.707); plosLPd[c].b.reset(); plosLPd[c].ch = c;
            deBPd[c].b.bandpass (sr, 6500, 0.9); deBPd[c].b.reset(); deBPd[c].ch = c;
            kShelfE[c] = kShelf[c]; kHpE[c] = kHp[c]; kShelfE[c].reset(); kHpE[c].reset();
            kShelfB[c] = kShelf[c]; kHpB[c] = kHp[c]; kShelfC[c] = kShelf[c]; kHpC[c] = kHp[c]; kShelfD[c] = kShelf[c]; kHpD[c] = kHp[c];
        }
        lastLights = { true, true, true, true, true, true, true, true };
    }
};

} // namespace fx
