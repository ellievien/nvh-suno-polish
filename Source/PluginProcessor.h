#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DenoiseCore.h"
#include <atomic>

// Settings for the offline "Studio Process" pass.
struct StudioParams
{
    juce::String serverUrl { "http://127.0.0.1:8188" };
    juce::String takePath;            // local file the user loaded
    int    mode      { 0 };           // 0 = Enhance (DeepFilterNet), 1 = Regenerate (clone), 2 = Suno Polish
    float  attenLim  { 0.0f };        // dB, 0 = full denoise (Enhance mode); higher keeps more ambience
    juce::String model    { "1.7B" }; // clone model (Regenerate mode)
    juce::String language { "English" };
    // Suno Polish (mode 2)
    juce::String key      { "C" };    // musical key for autotune
    juce::String scale    { "major" };// major | minor | chromatic
    bool   autotune   { true };       // snap pitch to scale
    bool   cleanFirst  { true };      // DeepFilterNet denoise before the chain
    float  reverb     { 0.35f };      // reverb amount 0..1 (the Reverb knob)
};

class VocalCleanerAudioProcessor : public juce::AudioProcessor
{
public:
    VocalCleanerAudioProcessor();
    ~VocalCleanerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override               { return true; }
    const juce::String getName() const override   { return "nvh suno polish"; }
    bool acceptsMidi() const override             { return false; }
    bool producesMidi() const override            { return false; }
    bool isMidiEffect() const override            { return false; }
    double getTailLengthSeconds() const override  { return 0.0; }
    int getNumPrograms() override                 { return 1; }
    int getCurrentProgram() override              { return 0; }
    void setCurrentProgram (int) override         {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };

    // ---- Studio Process control (called from the editor) ----
    void startStudioProcess (const StudioParams& p);
    void setMyVoice (const juce::String& serverUrl, const juce::File& file); // store the clone reference
    void queryMyVoice (const juce::String& serverUrl);                       // refresh stored-reference name
    void triggerPreview()        { previewFlag = true; }
    void stopPlayback()          { playing = false; }
    bool isBusy() const          { return processing.load(); }
    bool hasClip()               { const juce::ScopedLock sl (clipLock); return clip.getNumSamples() > 0; }
    double clipLengthSeconds()   { const juce::ScopedLock sl (clipLock); return clipSampleRate > 0 ? clip.getNumSamples() / clipSampleRate : 0.0; }
    bool saveClipTo (const juce::File& file);

    // ---- status (polled by the editor's timer; thread-safe) ----
    int getStatusVersion() const    { return statusVersion.load(); }
    juce::String getStatusMessage() { const juce::ScopedLock sl (statusLock); return statusMsg; }
    bool getStatusIsError()         { const juce::ScopedLock sl (statusLock); return statusErr; }

    int getMyVoiceVersion() const   { return myVoiceVersion.load(); }
    juce::String getMyVoiceName()   { const juce::ScopedLock sl (statusLock); return myVoiceName; }

    StudioParams lastStudio;        // persisted so the editor can restore fields
    juce::String lastResultName;    // last processed output filename

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    DenoiseCore core;

    // ---- Studio Process internals ----
    void runStudio (StudioParams p);
    bool decodeIntoClip (const juce::MemoryBlock& data);
    void postStatus (const juce::String& m, bool err);

    static juce::String      httpPostJson  (const juce::String& url, const juce::String& json, int timeoutMs, int& status);
    static juce::String      httpGet       (const juce::String& url, int timeoutMs, int& status);
    static juce::MemoryBlock httpGetBin    (const juce::String& url, int timeoutMs, int& status);
    static juce::String      uploadFile    (const juce::String& serverUrl, const juce::File& file, int& status);
    static juce::String      transcribe    (const juce::String& serverUrl, const juce::String& filename,
                                             const juce::String& language, int& status);

    juce::AudioFormatManager formatManager;

    juce::CriticalSection clipLock;
    juce::AudioBuffer<float> clip;       // mono, stored at its native sample rate
    double clipSampleRate = 48000.0;

    double hostSampleRate = 44100.0;
    std::atomic<bool> playing { false };
    std::atomic<bool> previewFlag { false };
    double playPos = 0.0;                 // fractional read position into clip

    std::atomic<bool> processing { false };

    juce::CriticalSection statusLock;
    juce::String statusMsg { "Studio idle. Load a take, pick a mode, then Process." };
    bool statusErr = false;
    std::atomic<int> statusVersion { 0 };

    juce::String myVoiceName;             // guarded by statusLock
    std::atomic<int> myVoiceVersion { 0 };

    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCleanerAudioProcessor)
};
