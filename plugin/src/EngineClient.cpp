#include "EngineClient.h"

namespace purr
{
bool EngineClient::request (const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                            const juce::String& contentType, juce::MemoryBlock& out, int& status, int timeoutMs) const
{
    juce::URL url (base + path);
    auto handling = juce::URL::ParameterHandling::inAddress;
    if (body != nullptr)
    {
        url = url.withPOSTData (*body);
        handling = juce::URL::ParameterHandling::inPostData;
    }
    status = 0;
    const juce::String headers = body != nullptr ? "Content-Type: " + contentType + "\r\n" : juce::String();
    auto opts = juce::URL::InputStreamOptions (handling)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withHttpRequestCmd (method)
                    .withStatusCode (&status)
                    .withExtraHeaders (headers);

    auto in = url.createInputStream (opts);
    if (in == nullptr)
        return false;
    out.reset();
    in->readIntoMemoryBlock (out);
    return true;
}

juce::String EngineClient::errorText (const juce::MemoryBlock& body, int status)
{
    auto v = juce::JSON::parse (body.toString());
    if (auto* o = v.getDynamicObject())
        if (o->hasProperty ("detail"))
            return o->getProperty ("detail").toString();
    return "engine answered HTTP " + juce::String (status);
}

bool EngineClient::health (juce::StringArray& backends, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("GET", "/health", nullptr, {}, out, status, 1500))
    {
        err = "engine not reachable at " + base;
        return false;
    }
    if (status != 200) { err = errorText (out, status); return false; }
    backends.clear();
    auto v = juce::JSON::parse (out.toString());
    if (auto* arr = v.getProperty ("backends", {}).getArray())
        for (auto& b : *arr)
            if ((bool) b.getProperty ("available", false))
                backends.add (b.getProperty ("name", "").toString());
    return true;
}

bool EngineClient::modelStatus (ModelStatus& m) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("GET", "/models", nullptr, {}, out, status, 1500) || status != 200) return false;
    auto v = juce::JSON::parse (out.toString());
    m.state = v.getProperty ("state", "unknown").toString();
    m.step = v.getProperty ("step", "").toString();
    m.error = v.getProperty ("error", "").toString();
    m.done = (juce::int64) v.getProperty ("done", 0);
    m.total = (juce::int64) v.getProperty ("total", 0);
    m.installed = (bool) v.getProperty ("installed", false);
    return true;
}

bool EngineClient::startModelDownload (juce::String& err) const
{
    juce::MemoryBlock out, body; int status = 0;
    body.append ("{}", 2);
    if (! request ("POST", "/models/download", &body, "application/json", out, status, 3000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    return true;
}

bool EngineClient::listVoices (std::vector<VoiceInfo>& outV, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("GET", "/voices", nullptr, {}, out, status, 3000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    outV.clear();
    auto v = juce::JSON::parse (out.toString());
    if (auto* arr = v.getProperty ("voices", {}).getArray())
        for (auto& e : *arr)
        {
            VoiceInfo vi;
            vi.id = e.getProperty ("id", "").toString();
            vi.name = e.getProperty ("name", vi.id).toString();
            vi.rights = e.getProperty ("rights", "").toString();
            vi.builtin = e.getProperty ("kind", "").toString() == "builtin";
            if (auto* clips = e.getProperty ("clips", {}).getArray())
            {
                vi.clips = clips->size();
                for (auto& c : *clips)
                    vi.seconds += (double) c.getProperty ("duration_s", 0.0);
            }
            outV.push_back (vi);
        }
    return true;
}

bool EngineClient::parseMidi (const juce::MemoryBlock& midi, double& tempo, std::vector<NoteEv>& notes, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("POST", "/midi/parse", &midi, "application/octet-stream", out, status, 5000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    auto v = juce::JSON::parse (out.toString());
    tempo = (double) v.getProperty ("tempo_bpm", 120.0);
    notes.clear();
    if (auto* arr = v.getProperty ("notes", {}).getArray())
        for (auto& n : *arr)
        {
            NoteEv e;
            e.startBeats = (double) n.getProperty ("start_beats", 0.0);
            e.lengthBeats = (double) n.getProperty ("length_beats", 1.0);
            e.pitch = (int) n.getProperty ("pitch", 60);
            e.velocity = (int) n.getProperty ("velocity", 100);
            e.lyric = n.getProperty ("lyric", "").toString();
            notes.push_back (e);
        }
    return true;
}

bool EngineClient::sing (const juce::String& jobJson, juce::MemoryBlock& wav, juce::String& err) const
{
    juce::MemoryBlock body (jobJson.toRawUTF8(), jobJson.getNumBytesAsUTF8());
    int status = 0;
    if (! request ("POST", "/sing", &body, "application/json", wav, status, 120000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (wav, status); return false; }
    return true;
}

bool EngineClient::convert (const juce::MemoryBlock& body, juce::MemoryBlock& wav, juce::String& err) const
{
    int status = 0;
    if (! request ("POST", "/convert", &body, "application/octet-stream", wav, status, 600000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (wav, status); return false; }
    return true;
}

bool EngineClient::analyze (const juce::MemoryBlock& wav, juce::String& keyText, juce::String& chordsText, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("POST", "/analyze", &wav, "application/octet-stream", out, status, 60000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    auto v = juce::JSON::parse (out.toString());
    auto key = v["key"];
    if (key["tonic"].isVoid() || key["tonic"].toString().isEmpty()) keyText = "no clear key";
    else keyText = key["tonic"].toString() + " " + key["mode"].toString()
                   + " (" + juce::String (juce::roundToInt ((double) key["confidence"] * 100.0)) + "% sure)";
    juce::StringArray names;
    if (auto* arr = v["chords"].getArray())
        for (auto& c : *arr) { auto n = c["chord"].toString(); if (names.isEmpty() || names[names.size() - 1] != n) names.add (n); }
    chordsText = names.joinIntoString (" ");
    return true;
}

bool EngineClient::importVoice (const juce::String& name, const juce::String& rights, bool confirmed,
                                const juce::MemoryBlock& wav, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    auto path = "/voices?name=" + juce::URL::addEscapeChars (name, true)
              + "&rights=" + juce::URL::addEscapeChars (rights, true)
              + "&rights_confirmed=" + juce::String (confirmed ? "true" : "false");
    if (! request ("POST", path, &wav, "application/octet-stream", out, status, 30000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    return true;
}

bool EngineClient::deleteVoice (const juce::String& id, juce::String& err) const
{
    juce::MemoryBlock out; int status = 0;
    if (! request ("DELETE", "/voices/" + juce::URL::addEscapeChars (id, false), nullptr, {}, out, status, 3000)) { err = "engine not reachable"; return false; }
    if (status != 200) { err = errorText (out, status); return false; }
    return true;
}
} // namespace purr
