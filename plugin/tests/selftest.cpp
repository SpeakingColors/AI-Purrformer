// Headless end to end check: MIDI capture, engine round trip, host-synced playback.
// Run with the engine up:  uvicorn purrformer.server:app --port 8765   (from engine/)
#include "../src/FxProcessor.h"
#include <cstdio>
#include <cstdlib>

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


    // 7. FX version: record what comes in while the host plays, render, play back in time
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("purr-selftest-data");
        tmp.createDirectory();
#ifdef _WIN32
        _putenv_s ("XDG_DATA_HOME", tmp.getFullPathName().toRawUTF8());
#else
        setenv ("XDG_DATA_HOME", tmp.getFullPathName().toRawUTF8(), 1);
#endif
        FxProcessor fx;
        FakeHost h2;
        fx.setPlayHead (&h2);
        fx.setRateAndBufferSizeDetails (44100.0, 512);
        fx.prepareToPlay (44100.0, 512);
        fx.refreshHealth(); fx.refreshVoices();
        fx.apvts.getParameter ("live_on")->setValueNotifyingHost (0.0f);    // studio render tests run with live off

        juce::AudioBuffer<float> io (2, 512);
        double phase = 0.0;
        auto blockIn = [&] (double ppq, bool playing, juce::MidiBuffer& m) {
            h2.ppq = ppq; h2.playing = playing;
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.3f * (float) std::sin (phase); phase += 2.0 * 3.14159265358979 * 220.0 / 44100.0;
                io.setSample (0, i, s); io.setSample (1, i, s);
            }
            fx.processBlock (io, m);
        };
        const double step = 512 * 120.0 / 60.0 / 44100.0;

        juce::MidiBuffer none;
        for (int k = 0; k < 4; ++k) blockIn (0.0, true, none);   // dry path is delayed by the reported latency
        CHECK (peak (io) > 0.25f && peak (io) < 0.35f, "FX passes the input through when nothing is rendered");

        fx.armRecord();
        CHECK (fx.recState() == FxProcessor::Armed, "record is armed and waits for the host");
        blockIn (0.0, false, none);
        CHECK (fx.recState() == FxProcessor::Armed, "armed record ignores a stopped host");
        double ppq = 8.0;
        juce::MidiBuffer withNote; withNote.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
        blockIn (ppq, true, withNote); ppq += step;
        CHECK (fx.recState() == FxProcessor::Recording, "record starts when the host plays");
        for (int i = 0; i < 120; ++i) { blockIn (ppq, true, none); ppq += step; }
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        blockIn (ppq, true, off); ppq += step;
        blockIn (ppq, false, none);                      // host stops
        CHECK (fx.recState() == FxProcessor::Done, "record stops when the host stops");
        fx.poll();
        CHECK (fx.recordedSeconds() > 1.3 && fx.recordedSeconds() < 1.5, "about 1.4 seconds were recorded");
        CHECK (fx.getNotes().size() == 1 && fx.getNotes()[0].pitch == 69, "MIDI note was captured during the recording");

        juce::String e3;
        CHECK (fx.renderBlockingFx (e3), ("FX render round trip: " + e3).toRawUTF8());
        CHECK (fx.resultFile().existsAsFile() && fx.resultFile().getSize() > 10000, "result .wav was saved for drag and drop");
        auto rr = fx.getRendered();
        CHECK (rr != nullptr && std::abs (rr->originBeats - 8.0) < 1e-9, "result is anchored where the recording started");

        auto playFx = [&] (double p, bool pl) { juce::MidiBuffer m; h2.ppq = p; h2.playing = pl; io.clear(); fx.processBlock (io, m); return peak (io); };
        CHECK (playFx (8.3, true) > 0.01f, "FX plays the result in time with the host");
        CHECK (playFx (1.0, true) == 0.0f, "FX is silent (original voice off) before the recording");

        // manual key + scale and grit travel to the engine and the render still works
        {
            auto setP = [&] (const char* id, float v) { fx.apvts.getParameter (id)->setValueNotifyingHost (fx.apvts.getParameter (id)->convertTo0to1 (v)); };
            setP ("key", 1); setP ("tonic", 2); setP ("scale", 2);          // Manual, D, dorian
            setP ("key_snap", 1.0f);
            setP ("grit_on", 1); setP ("grit_drive", 0.5f); setP ("grit_fuzz", 0.4f); setP ("grit_mix", 0.6f); setP ("grit_pos", 4);   // all three
            juce::String e4;
            CHECK (fx.renderBlockingFx (e4), ("FX render with manual key and grit: " + e4).toRawUTF8());
            auto r4 = fx.getRendered();
            CHECK (r4 != nullptr && r4->audio.getNumSamples() > 10000, "grit render has audio");
            setP ("grit_on", 0); setP ("key", 0);
        }

        // a loop jump ends the recording (the user recorded one pass)
        fx.armRecord();
        blockIn (16.0, true, none); blockIn (16.0 + step, true, none);
        blockIn (4.0, true, none);
        CHECK (fx.recState() == FxProcessor::Done, "a loop jump closes the recording");
        // window size survives a save and load
        fx.setEditorWidth (1290);
        juce::MemoryBlock st2; fx.getStateInformation (st2);
        FxProcessor fx2; fx2.setStateInformation (st2.getData(), (int) st2.getSize());
        CHECK (fx2.editorWidth() == 1290, "FX state remembers the window size");
        fx.discardRecording();
        CHECK (fx.recState() == FxProcessor::Idle, "discard resets the recorder");
    }


    // live pitch fix: a flat sung note lands on the key, MIDI can pick the note, latency is reported
    {
        FxProcessor lv;
        FakeHost h3;
        lv.setPlayHead (&h3);
        lv.setRateAndBufferSizeDetails (44100.0, 512);
        lv.prepareToPlay (44100.0, 512);
        auto setL = [&] (const char* id, float v) { lv.apvts.getParameter (id)->setValueNotifyingHost (lv.apvts.getParameter (id)->convertTo0to1 (v)); };
        setL ("key", 1); setL ("tonic", 0); setL ("scale", 0);               // Manual, C major
        setL ("live_snap", 1.0f); setL ("live_speed", 0.0f);
        CHECK (lv.getLatencySamples() > 0 && lv.getLatencySamples() < 4410, "live mode reports a small latency");

        auto freqOf = [] (const std::vector<float>& x) {                      // autocorrelation estimate
            int best = 0; double bv = -1e30;
            for (int lag = 60; lag < 800; ++lag)
            {
                double c = 0; for (size_t i = 0; i + lag < x.size(); ++i) c += (double) x[i] * x[i + lag];
                if (c > bv) { bv = c; best = lag; }
            }
            return 44100.0 / best; };
        auto runLive = [&] (double inHz, int note) {
            juce::AudioBuffer<float> b (2, 512); std::vector<float> out; double ph = 0.0;
            for (int blk = 0; blk < 120; ++blk)
            {
                for (int i = 0; i < 512; ++i) { const float v = 0.4f * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * inHz / 44100.0; b.setSample (0, i, v); b.setSample (1, i, v); }
                juce::MidiBuffer m;
                if (blk == 0 && note > 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                lv.processBlock (b, m);
                if (blk >= 60) for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
            }
            return freqOf (out); };
        const double f1 = runLive (212.0, 0);                                 // flat A3 (220 Hz) in C major
        CHECK (std::abs (f1 - 220.0) < 5.0, "live: a flat note is pulled onto the key");
        setL ("midi_follow", 1.0f);
        const double f2 = runLive (212.0, 67);                                // MIDI says G3 (196 Hz)
        CHECK (std::abs (f2 - 196.0) < 5.0, "live: MIDI note picks the target pitch");
    }

    std::printf (failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
