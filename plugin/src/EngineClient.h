// AI Purrformer plugin: talks to the local engine over HTTP. AGPL-3.0.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace purr
{
struct VoiceInfo
{
    juce::String id, name, rights;
    bool builtin = false;
    int clips = 0;
    double seconds = 0.0;
};

struct NoteEv
{
    double startBeats = 0, lengthBeats = 1;
    int pitch = 60, velocity = 100;
    juce::String lyric;
};

struct ModelStatus
{
    juce::String state = "unknown", step, error; // state: idle, downloading, done, error, unknown
    juce::int64 done = 0, total = 0;
    bool installed = false;
};

class EngineClient
{
public:
    explicit EngineClient (juce::String url = "http://127.0.0.1:8765") : base (std::move (url)) {}
    void setBaseUrl (const juce::String& u) { base = u.trimCharactersAtEnd ("/"); }
    juce::String getBaseUrl() const { return base; }

    bool health (juce::StringArray& backends, juce::String& err) const;
    bool listVoices (std::vector<VoiceInfo>& out, juce::String& err) const;
    bool parseMidi (const juce::MemoryBlock& midi, double& tempo, std::vector<NoteEv>& notes, juce::String& err) const;
    bool sing (const juce::String& jobJson, juce::MemoryBlock& wav, juce::String& err) const;
    // Audio-in path: body = 4-byte JSON length + job JSON + recording WAV (+ optional context WAV).
    bool convert (const juce::MemoryBlock& body, juce::MemoryBlock& wav, juce::String& err) const;
    bool analyze (const juce::MemoryBlock& wav, juce::String& keyText, juce::String& chordsText, juce::String& err) const;
    bool importVoice (const juce::String& name, const juce::String& rights, bool confirmed,
                      const juce::MemoryBlock& wav, juce::String& err) const;
    bool modelStatus (ModelStatus& out) const;
    bool startModelDownload (juce::String& err) const;
    bool deleteVoice (const juce::String& id, juce::String& err) const;

private:
    bool request (const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                  const juce::String& contentType, juce::MemoryBlock& out, int& status, int timeoutMs) const;
    static juce::String errorText (const juce::MemoryBlock& body, int status);
    juce::String base;
};
} // namespace purr
