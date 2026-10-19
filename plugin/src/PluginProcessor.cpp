#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace purr
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

Layout PurrProcessor::makeLayout()
{
    using P = juce::ParameterID;
    Layout l;
    auto flt = [&] (const char* id, const char* name, float def) {
        l.add (std::make_unique<juce::AudioParameterFloat> (P { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def));
    };
    auto bl = [&] (const char* id, const char* name, bool def) { l.add (std::make_unique<juce::AudioParameterBool> (P { id, 1 }, name, def)); };
    bl ("robot_on", "Robot on", false);   flt ("robot_speed", "Retune speed", 1.0f);  flt ("robot_mix", "Robot dry/wet", 0.5f);
    bl ("saw_on", "Supersaw on", false);
    l.add (std::make_unique<juce::AudioParameterInt> (P { "saw_copies", 1 }, "Supersaw copies", 3, 9, 7));
    flt ("saw_detune", "Supersaw detune", 0.4f); flt ("saw_mix", "Supersaw mix", 0.6f);
    flt ("saw_spread", "Supersaw spread", 0.8f); flt ("saw_widen", "Supersaw widen", 0.3f);
    bl ("dirt_on", "Dirt on", false);     flt ("dirt_drive", "Drive", 0.0f);        flt ("dirt_crush", "Bitcrush", 0.0f);
    flt ("dirt_arts", "Artifacts", 0.0f); flt ("dirt_mix", "Dirt mix", 1.0f);
    flt ("w1", "Voice 1 weight", 0.5f);   flt ("w2", "Voice 2 weight", 0.3f);       flt ("w3", "Voice 3 weight", 0.2f);
    flt ("out_gain", "Output level", 0.8f);
    // audio-in (FX) version
    flt ("key_snap", "Key snap", 0.0f);   flt ("midi_follow", "Follow MIDI", 1.0f);
    flt ("dry", "Original voice level", 0.0f);
    bl ("monitor", "Hear input while recording", true);
    juce::StringArray keys { "Auto", "Off" };
    for (auto* t : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }) keys.add (juce::String (t) + " major");
    for (auto* t : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }) keys.add (juce::String (t) + " minor");
    l.add (std::make_unique<juce::AudioParameterChoice> (P { "key", 1 }, "Key", keys, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (P { "mode", 1 }, "Mode", juce::StringArray { "melody_leads", "voice_leads" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (P { "blend", 1 }, "Blend", juce::StringArray { "layer", "embedding" }, 0));
    return l;
}

PurrProcessor::PurrProcessor (BusesProperties buses)
    : AudioProcessor (std::move (buses)),
      apvts (*this, nullptr, "PURR", makeLayout())
{
    startTimerHz (4);
    ioPool.addJob ([this] { refreshHealth(); refreshVoices(); });
}

PurrProcessor::~PurrProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 3000);
    ioPool.removeAllJobs (true, 3000);
}

bool PurrProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PurrProcessor::prepareToPlay (double sr, int) { hostSr = sr; }

// ---- audio thread -------------------------------------------------------------------------------

PurrProcessor::Transport PurrProcessor::readTransport()
{
    Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.ppq = pos->getPpqPosition().orFallback (0.0);
            t.bpm = pos->getBpm().orFallback (0.0);
            t.playing = pos->getIsPlaying();
        }
    if (t.bpm > 0.0) hostTempo.store (t.bpm);
    lastPpq.store (t.ppq);
    return t;
}

void PurrProcessor::captureMidi (const juce::MidiBuffer& midi, const Transport& t)
{
    for (const auto meta : midi)
    {
        auto m = meta.getMessage();
        if (! (m.isNoteOn() || m.isNoteOff())) continue;
        int s1, b1, s2, b2;
        fifo.prepareToWrite (1, s1, b1, s2, b2);
        if (b1 > 0)
        {
            fifoData[(size_t) s1] = { t.ppq + meta.samplePosition * t.bpm / 60.0 / hostSr, (std::uint8_t) m.getNoteNumber(),
                                      (std::uint8_t) m.getVelocity(), (std::uint8_t) (m.isNoteOn() ? 1 : 0) };
            fifo.finishedWrite (1);
        }
    }
}

// Plays the rendered result at the host position. replace = overwrite the buffer, else add to it.
void PurrProcessor::playRendered (juce::AudioBuffer<float>& buffer, const Transport& t, float gain, bool replace)
{
    std::shared_ptr<const Rendered> r;
    {
        juce::SpinLock::ScopedTryLockType lock (renderLock);
        if (lock.isLocked()) r = rendered;
    }
    if (r == nullptr || ! t.playing || r->tempo <= 0.0) return;
    const auto startSample = (juce::int64) std::llround ((t.ppq - r->originBeats) * 60.0 / r->tempo * r->sampleRate);
    const int total = r->audio.getNumSamples();
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* dst = buffer.getWritePointer (ch);
        auto* src = r->audio.getReadPointer (juce::jmin (ch, r->audio.getNumChannels() - 1));
        for (int i = 0; i < n; ++i)
        {
            auto idx = startSample + i;
            if (idx >= 0 && idx < total) dst[i] = (replace ? 0.0f : dst[i]) + src[idx] * gain;
        }
    }
}

void PurrProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const auto t = readTransport();
    if (capturing.load() && t.playing && t.bpm > 0.0) captureMidi (midi, t);
    if (! capturing.load())
        playRendered (buffer, t, apvts.getRawParameterValue ("out_gain")->load(), true);
    midi.clear();
}

// ---- melody -------------------------------------------------------------------------------------

void PurrProcessor::setCapturing (bool on)
{
    if (! on && capturing.load())
    {
        capturing = false;
        drainCapture();
        // close notes that are still held
        const juce::ScopedLock sl (notesLock);
        for (auto& [p, st] : pending)
            notes.push_back ({ st.first, juce::jmax (0.1, lastPpq.load() - st.first), p, st.second, {} });
        pending.clear();
        noteTempo = hostTempo.load();
    }
    else capturing = on;
}

void PurrProcessor::drainCapture()
{
    const juce::ScopedLock sl (notesLock);
    int n = fifo.getNumReady();
    int s1, b1, s2, b2;
    fifo.prepareToRead (n, s1, b1, s2, b2);
    auto handle = [&] (const CapEv& e) {
        if (e.on)
            pending[e.pitch] = { e.ppq, e.vel };
        else if (auto it = pending.find (e.pitch); it != pending.end())
        {
            notes.push_back ({ it->second.first, juce::jmax (0.05, e.ppq - it->second.first), (int) e.pitch, it->second.second, {} });
            pending.erase (it);
        }
    };
    for (int i = 0; i < b1; ++i) handle (fifoData[(size_t) (s1 + i)]);
    for (int i = 0; i < b2; ++i) handle (fifoData[(size_t) (s2 + i)]);
    fifo.finishedRead (b1 + b2);
}

void PurrProcessor::clearNotes()
{
    const juce::ScopedLock sl (notesLock);
    notes.clear(); pending.clear();
}

void PurrProcessor::setNotes (std::vector<NoteEv> n, double tempo)
{
    const juce::ScopedLock sl (notesLock);
    notes = std::move (n);
    noteTempo = tempo;
}

std::vector<NoteEv> PurrProcessor::getNotes() const
{
    const juce::ScopedLock sl (notesLock);
    return notes;
}

// ---- rendering ----------------------------------------------------------------------------------

juce::var PurrProcessor::voicesVar() const
{
    auto P = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    auto obj = [] { return new juce::DynamicObject(); };
    juce::Array<juce::var> jv;
    const char* wid[3] = { "w1", "w2", "w3" };
    auto ids = getVoiceIds();
    for (int i = 0; i < 3; ++i)
        if (ids[(size_t) i].isNotEmpty() && P (wid[i]) > 0.0)
        {
            auto* o = obj();
            o->setProperty ("id", ids[(size_t) i]);
            o->setProperty ("weight", P (wid[i]));
            jv.add (juce::var (o));
        }
    if (jv.isEmpty() && ids[0].isNotEmpty())
    {
        auto* o = obj(); o->setProperty ("id", ids[0]); o->setProperty ("weight", 1.0); jv.add (juce::var (o));
    }
    return juce::var (jv);
}

juce::var PurrProcessor::fxVar() const
{
    auto P = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    auto obj = [] { return new juce::DynamicObject(); };
    auto* robot = obj();
    robot->setProperty ("enabled", P ("robot_on") > 0.5);
    robot->setProperty ("retune_speed", P ("robot_speed"));
    robot->setProperty ("mix", P ("robot_mix"));
    auto* saw = obj();
    saw->setProperty ("enabled", P ("saw_on") > 0.5);
    saw->setProperty ("copies", (int) std::lround (P ("saw_copies")));
    saw->setProperty ("detune", P ("saw_detune"));
    saw->setProperty ("mix", P ("saw_mix"));
    saw->setProperty ("spread", P ("saw_spread"));
    saw->setProperty ("widen", P ("saw_widen"));
    auto* dirt = obj();
    dirt->setProperty ("enabled", P ("dirt_on") > 0.5);
    dirt->setProperty ("drive", P ("dirt_drive"));
    dirt->setProperty ("bitcrush", P ("dirt_crush"));
    dirt->setProperty ("artifacts", P ("dirt_arts"));
    dirt->setProperty ("mix", P ("dirt_mix"));
    auto* fx = obj();
    fx->setProperty ("robot", juce::var (robot));
    fx->setProperty ("supersaw", juce::var (saw));
    fx->setProperty ("dirt", juce::var (dirt));
    return juce::var (fx);
}

juce::String PurrProcessor::buildJobJson (double& originOut) const
{
    auto notesCopy = getNotes();
    double minStart = 0.0;
    if (! notesCopy.empty())
    {
        minStart = notesCopy.front().startBeats;
        for (auto& n : notesCopy) minStart = juce::jmin (minStart, n.startBeats);
    }
    const double origin = std::floor (minStart / 4.0) * 4.0;
    originOut = origin;

    auto obj = [] { return new juce::DynamicObject(); };

    auto* job = obj();
    job->setProperty ("version", 1);
    job->setProperty ("mode", apvts.getParameter ("mode")->getCurrentValueAsText());
    job->setProperty ("backend", getBackend());
    job->setProperty ("sample_rate", (int) hostSr);
    job->setProperty ("tempo_bpm", juce::jlimit (20.0, 400.0, hostTempo.load()));
    job->setProperty ("blend_mode", apvts.getParameter ("blend")->getCurrentValueAsText());

    juce::Array<juce::var> jn;
    for (auto& n : notesCopy)
    {
        auto* o = obj();
        o->setProperty ("start_beats", n.startBeats - origin);
        o->setProperty ("length_beats", n.lengthBeats);
        o->setProperty ("pitch", n.pitch);
        o->setProperty ("velocity", juce::jlimit (1, 127, n.velocity));
        if (n.lyric.isNotEmpty()) o->setProperty ("lyric", n.lyric);
        jn.add (juce::var (o));
    }
    job->setProperty ("notes", jn);

    auto text = lyrics();
    if (text.isNotEmpty()) job->setProperty ("lyrics", text);

    job->setProperty ("voices", voicesVar());
    job->setProperty ("fx", fxVar());
    return juce::JSON::toString (juce::var (job), true);
}

bool PurrProcessor::installRendered (const juce::MemoryBlock& wav, double origin, double tempo, juce::String& err)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (wav, false)));
    if (reader == nullptr) { err = "engine sent audio the plugin cannot read"; return false; }

    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> raw (2, len);
    reader->read (&raw, 0, len, 0, true, true);
    if (reader->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);

    auto out = std::make_shared<Rendered>();
    if (std::abs (reader->sampleRate - hostSr) < 1.0)
        out->audio = std::move (raw);
    else
    {
        const double ratio = reader->sampleRate / hostSr;
        const int outLen = (int) (len / ratio);
        out->audio.setSize (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator li;
            li.process (ratio, raw.getReadPointer (ch), out->audio.getWritePointer (ch), outLen);
        }
    }
    out->sampleRate = hostSr; out->originBeats = origin; out->tempo = tempo;

    std::shared_ptr<const Rendered> old;
    {
        const juce::SpinLock::ScopedLockType lock (renderLock);
        old = rendered;
        rendered = out;
    }
    retired.push_back (old);                  // keep the old one alive so the audio thread never frees it
    if (retired.size() > 2) retired.erase (retired.begin());
    ++renderCounter;
    return true;
}

bool PurrProcessor::renderBlocking (juce::String& err)
{
    if (getNotes().empty()) { err = "No melody yet. Capture from the host or open a .mid file."; return false; }
    double origin = 0.0;
    auto json = buildJobJson (origin);
    const double tempo = juce::jlimit (20.0, 400.0, hostTempo.load());
    juce::MemoryBlock wav;
    if (! engine.sing (json, wav, err)) return false;
    return installRendered (wav, origin, tempo, err);
}

void PurrProcessor::renderAsync()
{
    if (renderState.load() == 1) return;
    renderState = 1;
    setStatusText ("Rendering...");
    pool.addJob ([this] {
        juce::String err;
        const bool ok = renderBlocking (err);
        setStatusText (ok ? "Rendered. Press play in your host." : err);
        renderState = ok ? 2 : 3;
    });
}

std::shared_ptr<const Rendered> PurrProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType lock (renderLock);
    return rendered;
}

// ---- engine, text -------------------------------------------------------------------------------

juce::String PurrProcessor::statusText() const { const juce::ScopedLock sl (metaLock); return statusMsg; }
void PurrProcessor::setStatusText (const juce::String& s) { const juce::ScopedLock sl (metaLock); statusMsg = s; }
juce::String PurrProcessor::getBackend() const { const juce::ScopedLock sl (metaLock); return backend; }
void PurrProcessor::setBackend (const juce::String& b) { const juce::ScopedLock sl (metaLock); backend = b; }
juce::String PurrProcessor::lyrics() const { const juce::ScopedLock sl (metaLock); return lyricsText; }
void PurrProcessor::setLyrics (const juce::String& t) { const juce::ScopedLock sl (metaLock); lyricsText = t; }
std::array<juce::String, 3> PurrProcessor::getVoiceIds() const { const juce::ScopedLock sl (metaLock); return voiceIds; }
void PurrProcessor::setVoiceId (int slot, const juce::String& id) { const juce::ScopedLock sl (metaLock); voiceIds[(size_t) slot] = id; }
std::vector<VoiceInfo> PurrProcessor::getVoices() const { const juce::ScopedLock sl (metaLock); return voices; }

void PurrProcessor::refreshHealth()
{
    juce::StringArray b; juce::String err;
    const bool ok = engine.health (b, err);
    engineUp = ok;
    const juce::ScopedLock sl (metaLock);
    backendNames = b;
}

void PurrProcessor::refreshVoices()
{
    std::vector<VoiceInfo> v; juce::String err;
    if (engine.listVoices (v, err))
    {
        const juce::ScopedLock sl (metaLock);
        voices = std::move (v);
    }
}

void PurrProcessor::timerCallback()
{
    drainCapture();
    if (++healthTick >= 20)
    {
        healthTick = 0;
        if (ioPool.getNumJobs() == 0)
            ioPool.addJob ([this] { refreshHealth(); });
    }
}

// ---- state --------------------------------------------------------------------------------------

void PurrProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("lyrics", lyrics(), nullptr);
    state.setProperty ("backend", getBackend(), nullptr);
    state.setProperty ("engineUrl", engine.getBaseUrl(), nullptr);
    auto ids = getVoiceIds();
    state.setProperty ("voiceIds", ids[0] + "," + ids[1] + "," + ids[2], nullptr);
    juce::Array<juce::var> jn;
    for (auto& n : getNotes())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("s", n.startBeats); o->setProperty ("l", n.lengthBeats);
        o->setProperty ("p", n.pitch); o->setProperty ("v", n.velocity);
        jn.add (juce::var (o));
    }
    state.setProperty ("notes", juce::JSON::toString (juce::var (jn), true), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void PurrProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    setLyrics (state.getProperty ("lyrics").toString());
    if (state.hasProperty ("backend")) setBackend (state.getProperty ("backend").toString());
    if (state.hasProperty ("engineUrl")) engine.setBaseUrl (state.getProperty ("engineUrl").toString());
    auto ids = juce::StringArray::fromTokens (state.getProperty ("voiceIds").toString(), ",", "");
    for (int i = 0; i < 3 && i < ids.size(); ++i) setVoiceId (i, ids[i]);
    std::vector<NoteEv> v;
    auto parsed = juce::JSON::parse (state.getProperty ("notes").toString());
    if (auto* arr = parsed.getArray())
        for (auto& e : *arr)
            v.push_back ({ (double) e["s"], (double) e["l"], (int) e["p"], (int) e["v"], {} });
    setNotes (std::move (v), hostTempo.load());
}

juce::AudioProcessorEditor* PurrProcessor::createEditor() { return new PurrEditor (*this); }
} // namespace purr

#ifdef PURR_FX
#include "FxProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new purr::FxProcessor(); }
#else
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new purr::PurrProcessor(); }
#endif
