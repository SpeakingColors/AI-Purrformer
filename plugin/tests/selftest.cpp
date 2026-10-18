// Headless end to end check: MIDI capture, engine round trip, host-synced playback.
// Run with the engine up:  uvicorn purrformer.server:app --port 8765   (from engine/)
#include "../src/PluginProcessor.h"
#include <cstdio>

using namespace purr;

struct FakeHost : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setPpqPosition (ppq); i.setBpm (bpm); i.setIsPlaying (playing); return i;
    }
};

static int failures = 0;
#define CHECK(c, msg) do { if (! (c)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

static float peak (const juce::AudioBuffer<float>& b) { return b.getMagnitude (0, b.getNumSamples()); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    PurrProcessor proc;
    FakeHost host;
    proc.setPlayHead (&host);
    proc.setRateAndBufferSizeDetails (44100.0, 512);
    proc.prepareToPlay (44100.0, 512);
    juce::AudioBuffer<float> buf (2, 512);

    // 1. engine reachable
    proc.refreshHealth(); proc.refreshVoices();
    CHECK (proc.engineUp.load(), "engine is reachable");
    CHECK (proc.getVoices().size() >= 3, "voice list has the built-in test voices");

    // 2. capture notes from host MIDI: C4 for 1 beat at beat 4, E4 for 1 beat at beat 5
    proc.setCapturing (true);
    auto feed = [&] (double ppq, int note, bool on) {
        juce::MidiBuffer mb; host.ppq = ppq;
        mb.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), 0);
        proc.processBlock (buf, mb);
    };
    feed (4.0, 60, true); feed (5.0, 60, false); feed (5.0, 64, true); feed (6.0, 64, false);
    proc.setCapturing (false);
    auto notes = proc.getNotes();
    CHECK (notes.size() == 2, "two notes captured");
    if (notes.size() == 2)
    {
        CHECK (std::abs (notes[0].startBeats - 4.0) < 1e-6 && std::abs (notes[0].lengthBeats - 1.0) < 1e-6, "first note timing is 4.0 + 1.0 beats");
        CHECK (notes[1].pitch == 64, "second note is E4");
    }

    // 3. render through the engine (tone backend) with robot on
    proc.apvts.getParameter ("robot_on")->setValueNotifyingHost (1.0f);
    juce::String err;
    CHECK (proc.renderBlocking (err), ("render round trip: " + err).toRawUTF8());
    auto r = proc.getRendered();
    CHECK (r != nullptr && r->audio.getNumSamples() > 40000, "rendered audio covers the two beats (about one second)");
    CHECK (r != nullptr && std::abs (r->originBeats - 4.0) < 1e-9, "origin is the bar where the melody starts");

    // 4. playback follows the host position
    juce::AudioBuffer<float> out (2, 512);
    auto play = [&] (double ppq, bool playing) { host.ppq = ppq; host.playing = playing; juce::MidiBuffer m; out.clear(); proc.processBlock (out, m); return peak (out); };
    CHECK (play (4.2, true) > 0.01f, "audio plays when the host is at the melody");
    CHECK (play (4.2, false) == 0.0f, "silent when the host is stopped");
    CHECK (play (1.0, true) == 0.0f, "silent before the melody starts");
    CHECK (play (40.0, true) == 0.0f, "silent after the melody ends");

    // 5. state round trip keeps notes, lyrics, parameters
    proc.setLyrics ("neon lights");
    juce::MemoryBlock state; proc.getStateInformation (state);
    PurrProcessor other; other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.getNotes().size() == 2 && other.lyrics() == "neon lights", "state save and load keeps notes and lyrics");
    CHECK (other.apvts.getRawParameterValue ("robot_on")->load() > 0.5f, "state keeps parameters");

    // 6. bad input is reported, not crashed
    PurrProcessor empty; juce::String e2;
    CHECK (! empty.renderBlocking (e2) && e2.isNotEmpty(), "render without a melody gives a clear message");

    std::printf (failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
