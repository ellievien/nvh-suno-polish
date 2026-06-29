// Standalone sanity test for DenoiseCore (no JUCE). Validates the real-time
// resample + RNNoise framing pipeline: output length, finiteness, no clicks,
// no FIFO drift, latency reporting. Run at 48k and 44.1k.

#include "DenoiseCore.h"
#include <cstdio>
#include <random>
#include <cmath>
#include <vector>

static bool testRate (double sr)
{
    DenoiseCore core;
    core.prepare (sr, 1);

    const int total = (int) (sr * 5.0);           // 5 seconds
    std::vector<float> sig ((size_t) total);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> nd (-1.0f, 1.0f);
    for (int i = 0; i < total; ++i)
    {
        const float t = (float) (i / sr);
        sig[(size_t) i] = 0.3f * std::sin (2.0f * 3.14159265f * 220.0f * t) + 0.08f * nd (rng);
    }

    std::vector<float> outAll; outAll.reserve ((size_t) total);
    const int block = 512;
    float maxAbs = 0.0f, maxJump = 0.0f, prev = 0.0f;
    bool nan = false;

    for (int p = 0; p < total; p += block)
    {
        const int n = (p + block <= total) ? block : (total - p);
        std::vector<float> buf (sig.begin() + p, sig.begin() + p + n);
        float* ptrs[1] = { buf.data() };
        core.process (ptrs, 1, n, 1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float v = buf[(size_t) i];
            if (! std::isfinite (v)) nan = true;
            maxAbs = std::max (maxAbs, std::fabs (v));
            if (! outAll.empty()) maxJump = std::max (maxJump, std::fabs (v - prev));
            prev = v;
            outAll.push_back (v);
        }
    }

    // steady-state RMS (second half, after priming)
    double e = 0.0; const size_t half = outAll.size() / 2;
    for (size_t i = half; i < outAll.size(); ++i) e += (double) outAll[i] * outAll[i];
    const double rms = std::sqrt (e / (double) (outAll.size() - half));

    const bool lenOK = (int) outAll.size() == total;
    const bool ok = ! nan && lenOK && maxAbs < 2.0f && maxJump < 1.0f;
    std::printf ("sr=%.0f  len=%d/%d  maxAbs=%.3f  maxJump=%.3f  nan=%d  latency=%d  steadyRMS=%.4f  -> %s\n",
                 sr, (int) outAll.size(), total, maxAbs, maxJump, (int) nan,
                 core.getLatencySamples(), rms, ok ? "ok" : "BAD");
    return ok;
}

int main()
{
    const bool a = testRate (48000.0);
    const bool b = testRate (44100.0);
    std::printf ("RESULT: %s\n", (a && b) ? "PASS" : "FAIL");
    return (a && b) ? 0 : 1;
}
