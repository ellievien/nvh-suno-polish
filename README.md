# nvh the noise

A real-time **vocal denoiser VST3** for Windows DAWs (built for Studio One / Fender Studio Pro 8, works in any VST3 host). Drop it on a vocal track and it removes background noise, hiss, hum, and room as the track plays — powered by [RNNoise](https://github.com/xiph/rnnoise) running in real time in the audio thread. No server, no GPU.

## Features

- 🎚️ **Denoise Amount** — dry/wet blend (100% = full clean)
- 🔊 **Output Gain**
- 📈 **In / Out level meters**
- ⚡ Real-time, fully self-contained (RNNoise compiled in)
- 🔁 Drift-free internal 48 kHz resampling, so it works at any project sample rate, with latency reported to the host for sample-accurate alignment

The DSP core lives in [`Source/DenoiseCore.h`](Source/DenoiseCore.h) — a self-contained, JUCE-free class (cubic streaming resamplers around RNNoise's 480-sample frames + latency-aligned dry/wet), unit-tested by [`test/test_core.cpp`](test/test_core.cpp).

## Roadmap — "Studio Process" (model-based)

An offline, file-based panel that reproduces a take at studio quality:
- **Enhance my take** — a restoration model (DeepFilterNet) keeps your performance and makes it studio-clean.
- **Regenerate in my cloned voice** — transcribe + re-speak in a cloned voice (via [ComfyUI-VoiceCloneUI](https://github.com/ellievien/ComfyUI-VoiceCloneUI)).

## Build (Windows)

Dependencies (not vendored here):
1. **JUCE** — `git clone https://github.com/juce-framework/JUCE.git`
2. **RNNoise** — `git clone https://github.com/xiph/rnnoise.git`, then download its model:
   ```sh
   cd rnnoise && ./download_model.sh   # fetches src/rnnoise_data.c
   ```
3. **Visual Studio 2022** with the C++ workload (provides MSVC + CMake + Ninja).

Edit the paths near the top of [`CMakeLists.txt`](CMakeLists.txt) (`JUCE` and `RN`/rnnoise) to where you cloned them, then:

```bat
build.bat
```

This configures with Ninja (Release) and builds. With `COPY_PLUGIN_AFTER_BUILD` it installs to `C:\Program Files\Common Files\VST3\nvh the noise.vst3`. Restart your DAW (or rescan VST3) to load it.

> Note: `CMakeLists.txt` currently uses absolute dependency paths (this started as a personal build). Adjust them for your machine.

## License

MIT — see [LICENSE](LICENSE). RNNoise is BSD-licensed and is **not** included in this repo; fetch it separately (see Build).
