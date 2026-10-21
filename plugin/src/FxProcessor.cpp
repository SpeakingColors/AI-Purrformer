#include "FxProcessor.h"
#include "FxEditor.h"

namespace purr
{
FxProcessor::FxProcessor()
    : PurrProcessor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

FxProcessor::~FxProcessor() {}

bool FxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (l.getMainInputChannelSet() != out) return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

void FxProcessor::prepareToPlay (double sr, int blockSize)
{
    PurrProcessor::prepareToPlay (sr, blockSize);
    const int cap = (int) (sr * maxSeconds);
    if (rec.getNumSamples() != cap) rec.setSize (1, cap);
    if (side.getNumSamples() != cap) side.setSize (1, cap);
    liveVoice.prepare (sr, 2);
    setLatencySamples (liveVoice.latencySamples());
    sidechainOn = getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
}

juce::AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor (*this); }

void FxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("editorW", editorW.load(), nullptr);
    PurrProcessor::getStateInformation (dest);
}

void FxProcessor::setStateInformation (const void* data, int size)
{
    PurrProcessor::setStateInformation (data, size);
    editorW = juce::jlimit (0, 4000, (int) apvts.state.getProperty ("editorW", 0));
}

// ---- audio thread -------------------------------------------------------------------------------

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t = readTransport();
    const int n = buffer.getNumSamples();

    auto mainIn = getBusBuffer (buffer, true, 0);
    const int inCh = mainIn.getNumChannels();
    float pk = 0.0f;
    for (int c = 0; c < inCh; ++c) pk = juce::jmax (pk, mainIn.getMagnitude (c, 0, n));
    if (pk > inPeak.load()) inPeak.store (pk);

    const bool scOn = sidechainOn.load() && getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
    if (scOn)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        float sp = 0.0f;
        for (int c = 0; c < sc.getNumChannels(); ++c) sp = juce::jmax (sp, sc.getMagnitude (c, 0, n));
        if (sp > scPeak.load()) scPeak.store (sp);
    }

    int st = recStateA.load();
    if (st == Armed && t.playing)
    {
        recOrigin = t.ppq;
        recTempo = t.bpm > 0.0 ? t.bpm : 120.0;
        expectedPpq = t.ppq;
        recLen = 0; sideLen = 0;
        recStateA = st = Recording;
    }
    if (st == Recording)
    {
        const bool jumped = std::abs (t.ppq - expectedPpq) > 0.05 && t.bpm > 0.0;
        const int len = recLen.load();
        if (! t.playing || jumped || len + n > rec.getNumSamples())
            recStateA = st = Done;                       // stop, loop wrap or full: keep what we have
        else
        {
            auto* dst = rec.getWritePointer (0) + len;
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                for (int c = 0; c < inCh; ++c) s += mainIn.getReadPointer (c)[i];
                dst[i] = inCh > 0 ? s / (float) inCh : 0.0f;
            }
            if (scOn)
            {
                auto sc = getBusBuffer (buffer, true, 1);
                auto* sd = side.getWritePointer (0) + len;
                const int sch = sc.getNumChannels();
                for (int i = 0; i < n; ++i)
                {
                    float s = 0.0f;
                    for (int c = 0; c < sch; ++c) s += sc.getReadPointer (c)[i];
                    sd[i] = sch > 0 ? s / (float) sch : 0.0f;
                }
                sideLen = len + n;
            }
            recLen = len + n;
            if (t.bpm > 0.0) expectedPpq += n * t.bpm / 60.0 / hostSr;
            captureMidi (midi, t.bpm > 0.0 ? t : Transport { t.ppq, recTempo, t.playing });
        }
    }

    // MIDI held right now (the live pitch fix follows it)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) heldNote = m.getNoteNumber();
        else if ((m.isNoteOff() || m.isAllNotesOff()) && (m.isAllNotesOff() || m.getNoteNumber() == heldNote)) heldNote = -1;
    }

    // output: the live voice (or the original, delayed the same so timing never changes), then the rendered result
    const bool recording = recStateA.load() == Recording || recStateA.load() == Armed;
    std::shared_ptr<const Rendered> r = getRendered();
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    live::Settings ls;
    ls.on = P ("live_on") > 0.5f;
    float origGain = 1.0f;
    if (recording) origGain = P ("monitor") > 0.5f ? 1.0f : 0.0f;
    else if (r != nullptr) origGain = P ("dry");
    if (ls.on)
    {
        ls.snap = P ("live_snap"); ls.speed = P ("live_speed"); ls.level = P ("live_level"); ls.dry = P ("live_dry");
        const int keyMode = (int) P ("key");                                  // 0 Auto, 1 Manual, 2 Off
        ls.autoKey = keyMode == 0;
        ls.mask = keyMode == 1 ? live::scaleMask ((int) P ("tonic"), (int) P ("scale")) : 0;
        ls.heldNote = heldNote; ls.follow = P ("midi_follow");
        ls.gritOn = P ("grit_on") > 0.5f; ls.drive = P ("grit_drive"); ls.trans = P ("grit_trans"); ls.fuzz = P ("grit_fuzz");
        ls.gritMix = P ("grit_mix"); ls.gritPos = (int) P ("grit_pos");
    }
    else { ls.level = 0.0f; ls.dry = origGain; ls.snap = 0.0f; }
    if (inCh > 0)
    {
        float* chans[2] = { mainIn.getWritePointer (0), inCh > 1 ? mainIn.getWritePointer (1) : mainIn.getWritePointer (0) };
        liveVoice.process (chans, juce::jmin (inCh, 2), n, ls);
        if (inCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in, stereo out
    }
    if (! recording)
    {
        auto tl = t;                                           // the host delays other tracks by our latency, so place the result later
        if (tl.bpm > 0.0) tl.ppq -= liveVoice.latencySamples() / hostSr * tl.bpm / 60.0;
        playRendered (mainIn, tl, P ("out_gain"), false);
    }
    midi.clear();
}

// ---- record control (message thread) --------------------------------------------------------------

void FxProcessor::armRecord()
{
    clearNotes();
    recLen = 0; sideLen = 0; analysed = false; notesClosed = false;
    { const juce::ScopedLock sl (fxLock); keyTxt.clear(); chordsTxt.clear(); analysisTxt.clear(); }
    setCapturing (true);
    recStateA = Armed;
    setStatusText ("Armed. Press play in your host and sing or play the track.");
}

void FxProcessor::stopRecord()
{
    const int st = recStateA.load();
    if (st == Recording) recStateA = Done;
    else if (st == Armed) { recStateA = Idle; setCapturing (false); setStatusText ("Ready."); }
}

void FxProcessor::discardRecording()
{
    recStateA = Idle; recLen = 0; sideLen = 0;
    setCapturing (false);
    clearNotes();
    setStatusText ("Ready.");
}

void FxProcessor::poll()
{
    if (recState() == Done && ! notesClosed)
    {
        notesClosed = true;
        setCapturing (false);                 // closes held notes
        setNotes (getNotes(), recTempo);
        setStatusText (recLen.load() > 0 ? "Recorded " + juce::String (recordedSeconds(), 1) + " s. Press Render."
                                         : "Nothing was recorded. Arm Record, then press play.");
        if (recLen.load() > 0) startAnalysis();
    }
}

// ---- analysis and render -------------------------------------------------------------------------

juce::MemoryBlock FxProcessor::toWav16 (const float* mono, int n, double sr)
{
    juce::MemoryBlock mb;
    const juce::uint32 dataBytes = (juce::uint32) n * 2, rate = (juce::uint32) sr;
    auto w32 = [&] (juce::uint32 v) { mb.append (&v, 4); };
    auto w16 = [&] (juce::uint16 v) { mb.append (&v, 2); };
    mb.append ("RIFF", 4); w32 (36 + dataBytes); mb.append ("WAVEfmt ", 8);
    w32 (16); w16 (1); w16 (1); w32 (rate); w32 (rate * 2); w16 (2); w16 (16);
    mb.append ("data", 4); w32 (dataBytes);
    std::vector<juce::int16> pcm ((size_t) n);
    for (int i = 0; i < n; ++i) pcm[(size_t) i] = (juce::int16) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, mono[i]) * 32767.0f);
    mb.append (pcm.data(), (size_t) n * 2);
    return mb;                                  // WAV is little endian; JUCE targets are little endian
}

void FxProcessor::startAnalysis()
{
    if (analysed) return;
    analysed = true;
    { const juce::ScopedLock sl (fxLock); analysisTxt = "Listening for the key..."; }
    const bool useSide = sideLen.load() > 0;
    const int n = useSide ? sideLen.load() : recLen.load();
    auto wav = std::make_shared<juce::MemoryBlock> (toWav16 ((useSide ? side : rec).getReadPointer (0), n, hostSr));
    ioPool.addJob ([this, wav] {
        juce::String k, c, err;
        const bool ok = engine.analyze (*wav, k, c, err);
        const juce::ScopedLock sl (fxLock);
        if (ok) { keyTxt = k; chordsTxt = c; analysisTxt = "done"; }
        else analysisTxt = "key unknown: " + err;
    });
}

bool FxProcessor::buildBody (juce::MemoryBlock& body, juce::String& err)
{
    const int n = recLen.load();
    if (recState() != Done || n < (int) (hostSr * 0.2)) { err = "Nothing recorded yet. Arm Record, press play, sing."; return false; }
    auto P = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };

    auto* job = new juce::DynamicObject();
    job->setProperty ("version", 1);
    job->setProperty ("backend", getBackend());
    job->setProperty ("sample_rate", (int) hostSr);
    job->setProperty ("tempo_bpm", juce::jlimit (20.0, 400.0, recTempo));
    job->setProperty ("voices", voicesVar());
    job->setProperty ("fx", fxVar());
    const auto keyChoice = apvts.getParameter ("key")->getCurrentValueAsText();
    job->setProperty ("key", keyChoice == "Auto" ? juce::String ("auto") : keyChoice == "Off" ? juce::String ("off")
                             : apvts.getParameter ("tonic")->getCurrentValueAsText() + " " + apvts.getParameter ("scale")->getCurrentValueAsText());
    {
        static const char* const pos[5] = { "before", "between", "after", "both", "all" };
        auto* gr = new juce::DynamicObject();
        gr->setProperty ("enabled", P ("grit_on") > 0.5);
        gr->setProperty ("drive", P ("grit_drive"));
        gr->setProperty ("transistor", P ("grit_trans"));
        gr->setProperty ("fuzz", P ("grit_fuzz"));
        gr->setProperty ("mix", P ("grit_mix"));
        gr->setProperty ("position", juce::String (pos[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("grit_pos")->load())]));
        job->setProperty ("grit", juce::var (gr));
    }
    job->setProperty ("snap_strength", P ("key_snap"));
    job->setProperty ("midi_strength", P ("midi_follow"));
    juce::Array<juce::var> jn;
    for (auto& nt : getNotes())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("start_beats", juce::jmax (0.0, nt.startBeats - recOrigin));
        o->setProperty ("length_beats", juce::jmax (0.01, nt.lengthBeats));
        o->setProperty ("pitch", nt.pitch);
        o->setProperty ("velocity", juce::jlimit (1, 127, nt.velocity));
        jn.add (juce::var (o));
    }
    job->setProperty ("notes", jn);

    auto voiceWav = toWav16 (rec.getReadPointer (0), n, hostSr);
    juce::MemoryBlock ctxWav;
    if (sideLen.load() > 0) ctxWav = toWav16 (side.getReadPointer (0), sideLen.load(), hostSr);
    job->setProperty ("context_bytes", (int) ctxWav.getSize());

    const auto json = juce::JSON::toString (juce::var (job), true);
    const juce::uint32 jlen = (juce::uint32) json.getNumBytesAsUTF8();
    body.reset();
    body.append (&jlen, 4);
    body.append (json.toRawUTF8(), jlen);
    body.append (voiceWav.getData(), voiceWav.getSize());
    body.append (ctxWav.getData(), ctxWav.getSize());
    return true;
}

juce::File FxProcessor::dataRoot()
{
#if JUCE_LINUX
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    auto base = xdg.isNotEmpty() ? juce::File (xdg) : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
#else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif
    return base.getChildFile ("Studio 87").getChildFile ("AI Purrformer");
}

juce::File FxProcessor::saveResult (const juce::MemoryBlock& wav)
{
    auto dir = dataRoot().getChildFile ("results");
    dir.createDirectory();
    auto f = dir.getChildFile ("AI Purrformer " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    f.replaceWithData (wav.getData(), wav.getSize());
    // keep the folder small: only the newest 20 results stay
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    for (int i = 0; i < files.size() - 20; ++i) files.getReference (i).deleteFile();
    return f;
}

bool FxProcessor::renderBlockingFx (juce::String& err)
{
    juce::MemoryBlock body, wav;
    if (! buildBody (body, err)) return false;
    if (! engine.convert (body, wav, err)) return false;
    if (! installRendered (wav, recOrigin, recTempo, err)) return false;
    auto f = saveResult (wav);
    const juce::ScopedLock sl (fxLock);
    lastResult = f;
    return true;
}

void FxProcessor::renderRecording()
{
    if (renderState.load() == 1) return;
    renderState = 1;
    setStatusText ("Rendering... a long clip takes a while, the engine works on your computer.");
    pool.addJob ([this] {
        juce::String err;
        const bool ok = renderBlockingFx (err);
        setStatusText (ok ? "Done. Drag the box onto a track, or press play to hear it here." : err);
        renderState = ok ? 2 : 3;
    });
}

juce::File FxProcessor::resultFile() const { const juce::ScopedLock sl (fxLock); return lastResult; }
juce::String FxProcessor::keyText() const { const juce::ScopedLock sl (fxLock); return keyTxt; }
juce::String FxProcessor::chordsText() const { const juce::ScopedLock sl (fxLock); return chordsTxt; }
juce::String FxProcessor::analysisState() const { const juce::ScopedLock sl (fxLock); return analysisTxt; }
} // namespace purr
