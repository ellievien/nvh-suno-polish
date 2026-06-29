#pragma once
// Self-contained real-time vocal denoise core wrapping RNNoise.
// RNNoise runs at 48 kHz on 480-sample mono frames, so this class does
// drift-free streaming sample-rate conversion (cubic) around it, per channel,
// plus a latency-aligned dry/wet mix. No JUCE dependency -> unit-testable.

#include <vector>
#include <deque>
#include <cmath>
#include <cstdint>

extern "C" {
#include "rnnoise.h"
}

class DenoiseCore
{
public:
    DenoiseCore() = default;
    ~DenoiseCore() { releaseStates(); }

    // (re)configure for a given host sample rate and channel count
    void prepare (double hostSampleRate, int numChannels)
    {
        releaseStates();
        hostSR = hostSampleRate > 0 ? hostSampleRate : 48000.0;
        needResample = std::abs (hostSR - 48000.0) > 0.5;
        frameSize = rnnoise_get_frame_size();   // 480

        chans.clear();
        chans.resize ((size_t) juce_max (1, numChannels));
        for (auto& c : chans)
        {
            c.st = rnnoise_create (nullptr);
            c.up.reset();   c.up.ratio   = hostSR / 48000.0;   // host -> 48k
            c.down.reset(); c.down.ratio = 48000.0 / hostSR;   // 48k -> host
            c.in48.clear(); c.out48.clear();
            c.frame.assign ((size_t) frameSize, 0.0f);
            c.cleaned.assign ((size_t) frameSize, 0.0f);
        }

        // latency: one 48k frame mapped to host samples (+ small resampler warmup)
        latencyHost = (int) std::lround (frameSize * hostSR / 48000.0) + (needResample ? 8 : 0);
        for (auto& c : chans)
        {
            c.dry.assign ((size_t) juce_max (1, latencyHost), 0.0f);
            c.dryPos = 0;
        }
        primed = false;
    }

    int getLatencySamples() const { return latencyHost; }

    void reset()
    {
        for (auto& c : chans)
        {
            if (c.st) { rnnoise_destroy (c.st); c.st = rnnoise_create (nullptr); }
            c.up.reset(); c.down.reset();
            c.in48.clear(); c.out48.clear();
            std::fill (c.dry.begin(), c.dry.end(), 0.0f);
            c.dryPos = 0;
        }
    }

    // Process one block in place. mix 0..1 (wet amount), linear out gain.
    // Returns {inRms, outRms} for metering.
    struct Levels { float in, out; };
    Levels process (float* const* channelData, int numCh, int numSamples, float mix, float gain)
    {
        double inSq = 0.0, outSq = 0.0; int count = 0;
        const int nc = juce_min (numCh, (int) chans.size());

        for (int ch = 0; ch < nc; ++ch)
        {
            auto& c = chans[(size_t) ch];
            float* data = channelData[ch];

            // 1) host -> 48k (push all of this block, produce what we can)
            scratch.resize ((size_t) (numSamples * 2 + 16));
            c.upSrc.assign (data, data + numSamples);
            int up = c.up.run (c.upSrc, scratch.data(), (int) scratch.size());
            for (int i = 0; i < up; ++i) c.in48.push_back (scratch[(size_t) i]);

            // 2) RNNoise on full 480 frames
            while ((int) c.in48.size() >= frameSize)
            {
                for (int i = 0; i < frameSize; ++i)
                {
                    c.frame[(size_t) i] = c.in48.front() * 32768.0f; // RNNoise expects int16 range
                    c.in48.pop_front();
                }
                rnnoise_process_frame (c.st, c.cleaned.data(), c.frame.data());
                for (int i = 0; i < frameSize; ++i)
                    c.out48.push_back (c.cleaned[(size_t) i] / 32768.0f);
            }

            // 3) 48k -> host, produce exactly numSamples (zero-pad while priming)
            wet.resize ((size_t) numSamples);
            int got = c.down.run (c.out48, wet.data(), numSamples);
            for (int i = got; i < numSamples; ++i) wet[(size_t) i] = 0.0f;

            // 4) latency-aligned dry/wet mix + gain
            for (int i = 0; i < numSamples; ++i)
            {
                const float inSample = data[i];
                const float dryDelayed = c.dry[(size_t) c.dryPos];
                c.dry[(size_t) c.dryPos] = inSample;
                c.dryPos = (c.dryPos + 1) % (int) c.dry.size();

                const float w = wet[(size_t) i];
                float outSample = (mix * w + (1.0f - mix) * dryDelayed) * gain;
                data[i] = outSample;

                inSq  += (double) inSample * inSample;
                outSq += (double) outSample * outSample;
                ++count;
            }
        }

        // mirror processing for any extra channels beyond nc (just gain the dry)
        Levels lv { 0.0f, 0.0f };
        if (count > 0) { lv.in = (float) std::sqrt (inSq / count); lv.out = (float) std::sqrt (outSq / count); }
        return lv;
    }

private:
    static int juce_max (int a, int b) { return a > b ? a : b; }
    static int juce_min (int a, int b) { return a < b ? a : b; }

    // 4-point cubic-Hermite streaming resampler, pull model (drift-free).
    struct Resampler
    {
        double ratio = 1.0;   // inputRate / outputRate
        double frac  = 0.0;
        float  h[4]  = { 0, 0, 0, 0 };
        void reset() { frac = 0.0; h[0] = h[1] = h[2] = h[3] = 0.0f; }

        static float cubic (float y0, float y1, float y2, float y3, float t)
        {
            const float a0 = y3 - y2 - y0 + y1;
            const float a1 = y0 - y1 - a0;
            const float a2 = y2 - y0;
            const float a3 = y1;
            return ((a0 * t + a1) * t + a2) * t + a3;
        }

        // produce up to 'want' samples into out, pulling input from src; returns produced count
        int run (std::deque<float>& src, float* out, int want)
        {
            int n = 0;
            while (n < want)
            {
                while (frac >= 1.0)
                {
                    if (src.empty()) return n;
                    h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = src.front(); src.pop_front();
                    frac -= 1.0;
                }
                out[n++] = cubic (h[0], h[1], h[2], h[3], (float) frac);
                frac += ratio;
            }
            return n;
        }
    };

    struct Channel
    {
        DenoiseState* st = nullptr;
        Resampler up, down;
        std::deque<float> upSrc;       // transient per-block source for upsampler
        std::deque<float> in48, out48; // 48k FIFOs around RNNoise
        std::vector<float> frame, cleaned;
        std::vector<float> dry;        // latency-aligned dry delay line
        int dryPos = 0;
    };

    void releaseStates()
    {
        for (auto& c : chans) if (c.st) { rnnoise_destroy (c.st); c.st = nullptr; }
    }

    double hostSR = 48000.0;
    bool needResample = false;
    bool primed = false;
    int frameSize = 480;
    int latencyHost = 480;
    std::vector<Channel> chans;
    std::vector<float> scratch, wet;
};
