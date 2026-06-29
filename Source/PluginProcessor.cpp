#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout VocalCleanerAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Denoise Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gain", 1 }, "Output Gain",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

VocalCleanerAudioProcessor::VocalCleanerAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();
}

VocalCleanerAudioProcessor::~VocalCleanerAudioProcessor()
{
    pool.removeAllJobs (true, 5000);
}

void VocalCleanerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate;
    core.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (core.getLatencySamples());
}

bool VocalCleanerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VocalCleanerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // --- Studio Process audition: when a processed clip is playing, it takes
    //     over the output so you can hear the result on this track. ---
    if (previewFlag.exchange (false)) { playPos = 0.0; playing = true; }

    if (playing.load())
    {
        const juce::ScopedLock sl (clipLock);
        const int clipLen = clip.getNumSamples();
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();

        if (clipLen < 2) { playing = false; buffer.clear(); inLevel.store (0.0f); outLevel.store (0.0f); return; }

        const double ratio = (hostSampleRate > 0 ? clipSampleRate / hostSampleRate : 1.0);
        const float* src = clip.getReadPointer (0);
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float s = 0.0f;
            if (playPos < clipLen - 1)
            {
                const int idx = (int) playPos;
                const float frac = (float) (playPos - idx);
                s = src[idx] * (1.0f - frac) + src[idx + 1] * frac;
                playPos += ratio;
            }
            else { playing = false; }
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] = s;
            peak = juce::jmax (peak, std::abs (s));
        }
        inLevel.store (0.0f);
        outLevel.store (peak);
        return;
    }

    // --- Normal real-time denoise ---
    const float mix  = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load());

    auto lv = core.process (buffer.getArrayOfWritePointers(),
                            buffer.getNumChannels(), buffer.getNumSamples(), mix, gain);
    inLevel.store (lv.in);
    outLevel.store (lv.out);
}

//==============================================================================
void VocalCleanerAudioProcessor::postStatus (const juce::String& m, bool err)
{
    const juce::ScopedLock sl (statusLock);
    statusMsg = m;
    statusErr = err;
    statusVersion.fetch_add (1);
}

//==============================================================================
juce::String VocalCleanerAudioProcessor::httpPostJson (const juce::String& urlStr, const juce::String& json, int timeoutMs, int& status)
{
    juce::URL url = juce::URL (urlStr).withPOSTData (json);
    status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders ("Content-Type: application/json")
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    std::unique_ptr<juce::InputStream> in (url.createInputStream (opts));
    if (in == nullptr) return {};
    return in->readEntireStreamAsString();
}

juce::String VocalCleanerAudioProcessor::httpGet (const juce::String& urlStr, int timeoutMs, int& status)
{
    status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    std::unique_ptr<juce::InputStream> in (juce::URL (urlStr).createInputStream (opts));
    if (in == nullptr) return {};
    return in->readEntireStreamAsString();
}

juce::MemoryBlock VocalCleanerAudioProcessor::httpGetBin (const juce::String& urlStr, int timeoutMs, int& status)
{
    status = 0;
    juce::MemoryBlock mb;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status);
    std::unique_ptr<juce::InputStream> in (juce::URL (urlStr).createInputStream (opts));
    if (in != nullptr)
        in->readIntoMemoryBlock (mb);
    return mb;
}

juce::String VocalCleanerAudioProcessor::uploadFile (const juce::String& serverUrl, const juce::File& file, int& status)
{
    status = 0;
    juce::URL url = juce::URL (serverUrl + "/voiceclone/upload").withFileToUpload ("audio", file, "audio/wav");
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withConnectionTimeoutMs (60000)
                    .withStatusCode (&status);
    std::unique_ptr<juce::InputStream> in (url.createInputStream (opts));
    if (in == nullptr) return {};
    auto resp = in->readEntireStreamAsString();
    return juce::JSON::parse (resp).getProperty ("filename", "").toString();
}

juce::String VocalCleanerAudioProcessor::transcribe (const juce::String& serverUrl, const juce::String& filename,
                                                     const juce::String& language, int& status)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("filename", filename);
    body->setProperty ("language", language);
    auto resp = httpPostJson (serverUrl + "/voiceclone/transcribe",
                              juce::JSON::toString (juce::var (body.get())), 180000, status);
    if (status < 200 || status >= 300) return {};
    return juce::JSON::parse (resp).getProperty ("text", "").toString().trim();
}

static juce::var makeNodeRef (const juce::String& nodeId, int slot)
{
    juce::Array<juce::var> a;
    a.add (nodeId);
    a.add (slot);
    return a;
}

// Qwen3-TTS only accepts this fixed language set; anything else (e.g. Tagalog,
// which Whisper transcribes fine but the TTS node would reject) falls back to
// "Auto" so the model auto-detects from the transcript.
static juce::String qwenNodeLanguage (const juce::String& ui)
{
    static const juce::StringArray supported {
        "Auto", "Chinese", "English", "Japanese", "Korean", "French",
        "German", "Spanish", "Portuguese", "Russian", "Italian" };
    return supported.contains (ui) ? ui : "Auto";
}

//==============================================================================
void VocalCleanerAudioProcessor::startStudioProcess (const StudioParams& p)
{
    if (processing.exchange (true))
        return; // already running
    lastStudio = p;
    pool.addJob ([this, p] { runStudio (p); });
}

void VocalCleanerAudioProcessor::runStudio (StudioParams p)
{
    int st = 0;
    const juce::File take (p.takePath);
    if (! take.existsAsFile())
    {
        postStatus ("Load a take (audio file) first.", true);
        processing = false;
        return;
    }

    // 1) upload the take to the ComfyUI input dir
    postStatus ("Uploading take...", false);
    const juce::String upName = uploadFile (p.serverUrl, take, st);
    if (upName.isEmpty())
    {
        postStatus ("Upload failed - is ComfyUI running at " + p.serverUrl + " with the VoiceCloneUI node?", true);
        processing = false;
        return;
    }

    juce::var outAudio;

    if (p.mode == 0)
    {
        // ---- Enhance my take (DeepFilterNet, isolated venv) ----
        postStatus ("Enhancing (DeepFilterNet restoration)... first run loads the model.", false);
        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty ("filename", upName);
        body->setProperty ("keep_sr", true);
        if (p.attenLim > 0.0f) body->setProperty ("atten_lim", (double) p.attenLim);
        auto resp = httpPostJson (p.serverUrl + "/voiceclone/enhance",
                                  juce::JSON::toString (juce::var (body.get())), 900000, st);
        if (st < 200 || st >= 300)
        {
            auto err = juce::JSON::parse (resp).getProperty ("error", "").toString();
            postStatus ("Enhance failed: " + (err.isNotEmpty() ? err : juce::String ("status ") + juce::String (st)), true);
            processing = false;
            return;
        }
        outAudio = juce::JSON::parse (resp); // {filename, type, subfolder, metrics...}
    }
    else if (p.mode == 2)
    {
        // ---- Suno Polish (clean -> autotune -> production chain -> loud master) ----
        postStatus ("Suno Polish: clean -> tune -> produce -> master... first run loads the model.", false);
        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty ("filename", upName);
        body->setProperty ("clean", p.cleanFirst);
        body->setProperty ("autotune", p.autotune);
        body->setProperty ("key", p.key);
        body->setProperty ("scale", p.scale);
        body->setProperty ("reverb", (double) p.reverb);
        auto resp = httpPostJson (p.serverUrl + "/voiceclone/polish",
                                  juce::JSON::toString (juce::var (body.get())), 1800000, st);
        if (st < 200 || st >= 300)
        {
            auto err = juce::JSON::parse (resp).getProperty ("error", "").toString();
            postStatus ("Polish failed: " + (err.isNotEmpty() ? err : juce::String ("status ") + juce::String (st)), true);
            processing = false;
            return;
        }
        outAudio = juce::JSON::parse (resp);
    }
    else
    {
        // ---- Regenerate in my cloned voice ----
        // a) transcribe the take to get the words to re-speak
        postStatus ("Transcribing take (Whisper)...", false);
        juce::String words = transcribe (p.serverUrl, upName, p.language, st);
        if (words.isEmpty())
        {
            postStatus ("Could not transcribe the take - nothing to regenerate.", true);
            processing = false;
            return;
        }

        // b) fetch the stored "my voice" reference
        auto mv = juce::JSON::parse (httpGet (p.serverUrl + "/voiceclone/myvoice", 15000, st));
        const juce::String refName    = mv.getProperty ("filename", "").toString();
        const juce::String refTextStr = mv.getProperty ("ref_text", "").toString();
        const bool         refExists  = (bool) mv.getProperty ("exists", false);
        if (refName.isEmpty() || ! refExists)
        {
            postStatus ("No 'my voice' reference set. Click \"Set my voice...\" and pick a clean clip of you.", true);
            processing = false;
            return;
        }

        // c) build LoadAudio -> FB_Qwen3TTSVoiceClone -> SaveAudio
        postStatus ("Regenerating in your cloned voice: \"" + words.substring (0, 60) + (words.length() > 60 ? "..." : "") + "\"", false);
        const int seed = juce::Random::getSystemRandom().nextInt (1000000000);

        juce::DynamicObject::Ptr loadIn = new juce::DynamicObject();
        loadIn->setProperty ("audio", refName);
        juce::DynamicObject::Ptr load = new juce::DynamicObject();
        load->setProperty ("class_type", "LoadAudio");
        load->setProperty ("inputs", juce::var (loadIn.get()));

        juce::DynamicObject::Ptr vcIn = new juce::DynamicObject();
        vcIn->setProperty ("target_text", words);
        vcIn->setProperty ("model_choice", p.model);
        vcIn->setProperty ("device", "auto");
        vcIn->setProperty ("precision", "bf16");
        vcIn->setProperty ("language", qwenNodeLanguage (p.language));
        vcIn->setProperty ("ref_audio", makeNodeRef ("10", 0));
        vcIn->setProperty ("ref_text", refTextStr);
        // NB: transcription above used p.language (Whisper supports Tagalog);
        // the TTS node only accepts qwenNodeLanguage()'s set.
        vcIn->setProperty ("seed", seed);
        vcIn->setProperty ("max_new_tokens", 2048);
        vcIn->setProperty ("top_p", 0.8);
        vcIn->setProperty ("top_k", 20);
        vcIn->setProperty ("temperature", 1.0);
        vcIn->setProperty ("repetition_penalty", 1.05);
        vcIn->setProperty ("x_vector_only", false);
        vcIn->setProperty ("attention", "auto");
        vcIn->setProperty ("unload_model_after_generate", false);
        juce::DynamicObject::Ptr vc = new juce::DynamicObject();
        vc->setProperty ("class_type", "FB_Qwen3TTSVoiceClone");
        vc->setProperty ("inputs", juce::var (vcIn.get()));

        juce::DynamicObject::Ptr saveIn = new juce::DynamicObject();
        saveIn->setProperty ("audio", makeNodeRef ("20", 0));
        saveIn->setProperty ("filename_prefix", "nvhstudio");
        juce::DynamicObject::Ptr save = new juce::DynamicObject();
        save->setProperty ("class_type", "SaveAudio");
        save->setProperty ("inputs", juce::var (saveIn.get()));

        juce::DynamicObject::Ptr graph = new juce::DynamicObject();
        graph->setProperty ("10", juce::var (load.get()));
        graph->setProperty ("20", juce::var (vc.get()));
        graph->setProperty ("30", juce::var (save.get()));

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty ("prompt", juce::var (graph.get()));
        root->setProperty ("client_id", "nvhstudio");

        auto resp = httpPostJson (p.serverUrl + "/prompt", juce::JSON::toString (juce::var (root.get())), 30000, st);
        if (st < 200 || st >= 300 || resp.isEmpty())
        { postStatus ("Submit failed (status " + juce::String (st) + ").", true); processing = false; return; }
        const auto promptId = juce::JSON::parse (resp).getProperty ("prompt_id", "").toString();
        if (promptId.isEmpty())
        { postStatus ("Rejected by ComfyUI: " + resp.substring (0, 240), true); processing = false; return; }

        const int maxTries = 1500; // ~25 min @ 1s
        for (int i = 0; i < maxTries && processing.load(); ++i)
        {
            juce::Thread::sleep (1000);
            auto h = httpGet (p.serverUrl + "/history/" + promptId, 15000, st);
            if (h.isEmpty() || h == "{}") { if (i % 5 == 4) postStatus ("Generating... " + juce::String (i + 1) + "s", false); continue; }
            auto hv = juce::JSON::parse (h);
            auto* rootObj = hv.getDynamicObject();
            if (rootObj == nullptr) continue;
            juce::var entry;
            for (auto& kv : rootObj->getProperties())
                if (kv.name.toString() == promptId) { entry = kv.value; break; }
            if (! entry.isObject()) continue;
            const auto statusStr = entry.getProperty ("status", juce::var()).getProperty ("status_str", "").toString();
            if (statusStr == "error") { postStatus ("Generation error - see the ComfyUI console.", true); processing = false; return; }
            auto outputs = entry.getProperty ("outputs", juce::var());
            if (auto* outObj = outputs.getDynamicObject())
                for (auto& kv : outObj->getProperties())
                {
                    auto audioArr = kv.value.getProperty ("audio", juce::var());
                    if (audioArr.isArray() && audioArr.size() > 0) { outAudio = audioArr[0]; break; }
                }
            if (outAudio.isObject()) break;
        }
        if (! outAudio.isObject()) { postStatus ("Timed out waiting for audio.", true); processing = false; return; }
    }

    // download + decode the result
    const auto fn  = outAudio.getProperty ("filename", "").toString();
    const auto sub = outAudio.getProperty ("subfolder", "").toString();
    const auto typ = outAudio.getProperty ("type", "output").toString();
    if (fn.isEmpty()) { postStatus ("No output filename returned.", true); processing = false; return; }
    const juce::String viewUrl = p.serverUrl + "/view?filename=" + juce::URL::addEscapeChars (fn, true)
                               + "&subfolder=" + juce::URL::addEscapeChars (sub, true)
                               + "&type=" + juce::URL::addEscapeChars (typ, true);

    postStatus ("Downloading result...", false);
    auto bytes = httpGetBin (viewUrl, 60000, st);
    if (bytes.getSize() == 0) { postStatus ("Could not download the processed audio.", true); processing = false; return; }
    if (! decodeIntoClip (bytes)) { postStatus ("Processed audio could not be decoded.", true); processing = false; return; }

    lastResultName = fn;
    processing = false;
    const juce::String verb = p.mode == 0 ? "Enhanced: " : (p.mode == 2 ? "Polished: " : "Regenerated: ");
    postStatus (verb + fn + "  (" + juce::String (clipLengthSeconds(), 1)
                + "s) - press Preview, or Save As to export.", false);
}

//==============================================================================
void VocalCleanerAudioProcessor::setMyVoice (const juce::String& serverUrl, const juce::File& file)
{
    if (processing.exchange (true))
        return;
    pool.addJob ([this, serverUrl, file]
    {
        int st = 0;
        if (! file.existsAsFile()) { postStatus ("Reference file not found.", true); processing = false; return; }
        postStatus ("Uploading 'my voice' reference...", false);
        const juce::String name = uploadFile (serverUrl, file, st);
        if (name.isEmpty()) { postStatus ("Upload failed - is ComfyUI running?", true); processing = false; return; }

        postStatus ("Transcribing 'my voice' reference (Whisper)...", false);
        juce::String refText = transcribe (serverUrl, name, "English", st); // best-effort

        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty ("filename", name);
        body->setProperty ("ref_text", refText);
        auto resp = httpPostJson (serverUrl + "/voiceclone/myvoice",
                                  juce::JSON::toString (juce::var (body.get())), 30000, st);
        if (st < 200 || st >= 300 || ! (bool) juce::JSON::parse (resp).getProperty ("ok", false))
        { postStatus ("Could not store the reference (status " + juce::String (st) + ").", true); processing = false; return; }

        { const juce::ScopedLock sl (statusLock); myVoiceName = name; }
        myVoiceVersion.fetch_add (1);
        processing = false;
        postStatus ("'My voice' set: " + name + (refText.isNotEmpty() ? "  (transcript captured)" : ""), false);
    });
}

void VocalCleanerAudioProcessor::queryMyVoice (const juce::String& serverUrl)
{
    pool.addJob ([this, serverUrl]
    {
        int st = 0;
        auto mv = juce::JSON::parse (httpGet (serverUrl + "/voiceclone/myvoice", 10000, st));
        const juce::String name = (bool) mv.getProperty ("exists", false)
                                    ? mv.getProperty ("filename", "").toString() : juce::String();
        { const juce::ScopedLock sl (statusLock); myVoiceName = name; }
        myVoiceVersion.fetch_add (1);
    });
}

//==============================================================================
bool VocalCleanerAudioProcessor::decodeIntoClip (const juce::MemoryBlock& data)
{
    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, true)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> tmp ((int) juce::jmax ((juce::uint32) 1, reader->numChannels), len);
    reader->read (&tmp, 0, len, 0, true, true);

    juce::AudioBuffer<float> mono (1, len);
    mono.clear();
    const float g = 1.0f / (float) tmp.getNumChannels();
    for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
        mono.addFrom (0, 0, tmp, ch, 0, len, g);

    {
        const juce::ScopedLock sl (clipLock);
        clip = std::move (mono);
        clipSampleRate = reader->sampleRate > 0 ? reader->sampleRate : 48000.0;
        playPos = 0.0;
        playing = false;
    }
    return true;
}

bool VocalCleanerAudioProcessor::saveClipTo (const juce::File& file)
{
    juce::AudioBuffer<float> snapshot;
    double sr;
    {
        const juce::ScopedLock sl (clipLock);
        if (clip.getNumSamples() < 1) return false;
        snapshot.makeCopyOf (clip);
        sr = clipSampleRate;
    }
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (os.get(), sr, 1, 24, {}, 0));
    if (writer == nullptr) return false;
    os.release(); // writer owns the stream now
    writer->writeFromAudioSampleBuffer (snapshot, 0, snapshot.getNumSamples());
    return true;
}

//==============================================================================
void VocalCleanerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("nvhState");
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (new juce::XmlElement (*params));

    auto* s = root.createNewChildElement ("studio");
    s->setAttribute ("serverUrl", lastStudio.serverUrl);
    s->setAttribute ("takePath",  lastStudio.takePath);
    s->setAttribute ("mode",      lastStudio.mode);
    s->setAttribute ("attenLim",  lastStudio.attenLim);
    s->setAttribute ("model",     lastStudio.model);
    s->setAttribute ("language",  lastStudio.language);
    s->setAttribute ("key",       lastStudio.key);
    s->setAttribute ("scale",     lastStudio.scale);
    s->setAttribute ("autotune",  lastStudio.autotune);
    s->setAttribute ("cleanFirst", lastStudio.cleanFirst);
    s->setAttribute ("reverb",    lastStudio.reverb);

    copyXmlToBinary (root, destData);
}

void VocalCleanerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    if (xml->hasTagName ("nvhState"))
    {
        if (auto* params = xml->getChildByName ("PARAMS"))
            apvts.replaceState (juce::ValueTree::fromXml (*params));
        if (auto* s = xml->getChildByName ("studio"))
        {
            lastStudio.serverUrl = s->getStringAttribute ("serverUrl", lastStudio.serverUrl);
            lastStudio.takePath  = s->getStringAttribute ("takePath",  lastStudio.takePath);
            lastStudio.mode      = s->getIntAttribute    ("mode",      lastStudio.mode);
            lastStudio.attenLim  = (float) s->getDoubleAttribute ("attenLim", lastStudio.attenLim);
            lastStudio.model     = s->getStringAttribute ("model",     lastStudio.model);
            lastStudio.language  = s->getStringAttribute ("language",  lastStudio.language);
            lastStudio.key       = s->getStringAttribute ("key",       lastStudio.key);
            lastStudio.scale     = s->getStringAttribute ("scale",     lastStudio.scale);
            lastStudio.autotune  = s->getBoolAttribute   ("autotune",  lastStudio.autotune);
            lastStudio.cleanFirst = s->getBoolAttribute  ("cleanFirst", lastStudio.cleanFirst);
            lastStudio.reverb    = (float) s->getDoubleAttribute ("reverb", lastStudio.reverb);
        }
    }
    else if (xml->hasTagName ("PARAMS"))
    {
        // legacy state (params only)
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessorEditor* VocalCleanerAudioProcessor::createEditor()
{
    return new VocalCleanerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalCleanerAudioProcessor();
}
