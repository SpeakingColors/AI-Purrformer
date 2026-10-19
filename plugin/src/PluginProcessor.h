// AI Purrformer plugin: processor. AGPL-3.0.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <map>
#include <memory>
#include "EngineClient.h"

namespace purr
{
struct Rendered
{
    juce::AudioBuffer<float> audio;   // at the host sample rate it was prepared for
    double sampleRate = 44100.0;
    double originBeats = 0.0;         // host beat that the first sample sits on
    double tempo = 120.0;             // tempo the job was rendered at
};

class PurrProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PurrProcessor() : PurrProcessor (defaultBuses()) {}
    ~PurrProcessor() override;

    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AI Purrformer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Melody
    void setCapturing (bool on);
    bool isCapturing() const { return capturing.load(); }
    void drainCapture();                       // message thread (or tests)
    void clearNotes();
    void setNotes (std::vector<NoteEv> n, double tempo);
    std::vector<NoteEv> getNotes() const;
    double getTempo() const { return noteTempo.load(); }
    double getHostTempo() const { return hostTempo.load(); }
    double getLastPpq() const { return lastPpq.load(); }

    // Rendering
    juce::String buildJobJson (double& originBeatsOut) const;
    void renderAsync();                        // runs on a worker thread
    bool renderBlocking (juce::String& err);   // used by tests
    std::shared_ptr<const Rendered> getRendered() const;
    std::atomic<int> renderState { 0 };        // 0 idle, 1 rendering, 2 done, 3 failed
    juce::String statusText() const;
    void setStatusText (const juce::String&);
    int renderCount() const { return renderCounter.load(); }

    // Engine
    EngineClient engine;
    std::atomic<bool> engineUp { false };
    juce::StringArray backendNames;            // guarded by metaLock
    juce::String getBackend() const;
    void setBackend (const juce::String& b);
    juce::String lyrics() const;
    void setLyrics (const juce::String&);
    std::array<juce::String, 3> getVoiceIds() const;
    void setVoiceId (int slot, const juce::String& id);
    void refreshHealth();
    void refreshVoices();
    std::vector<VoiceInfo> getVoices() const;

    juce::AudioProcessorValueTreeState apvts;

protected:
    explicit PurrProcessor (BusesProperties buses);
    static BusesProperties defaultBuses() { return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true); }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    struct Transport { double ppq = 0.0, bpm = 0.0; bool playing = false; };
    Transport readTransport();                 // audio thread
    void captureMidi (const juce::MidiBuffer&, const Transport&);   // audio thread, when capturing
    void playRendered (juce::AudioBuffer<float>&, const Transport&, float gain, bool replace);
    juce::var voicesVar() const;
    juce::var fxVar() const;
    bool installRendered (const juce::MemoryBlock& wav, double originBeats, double tempo, juce::String& err);
    double hostSr = 44100.0;
    juce::ThreadPool pool { 1 };      // renders
    juce::ThreadPool ioPool { 1 };    // health and voice list
    std::atomic<bool> capturing { false };
    std::atomic<double> hostTempo { 120.0 };
    std::atomic<double> lastPpq { 0.0 };

private:
    void timerCallback() override;

    struct CapEv { double ppq; std::uint8_t pitch, vel, on; };
    juce::AbstractFifo fifo { 8192 };
    std::array<CapEv, 8192> fifoData {};
    std::map<int, std::pair<double, int>> pending;   // pitch -> (start, velocity)

    mutable juce::CriticalSection notesLock;
    std::vector<NoteEv> notes;
    std::atomic<double> noteTempo { 120.0 };

    mutable juce::SpinLock renderLock;
    std::shared_ptr<const Rendered> rendered;

    mutable juce::CriticalSection metaLock;
    juce::String statusMsg { "Ready." }, backend { "tone" }, lyricsText;
    std::array<juce::String, 3> voiceIds { "test-sine", "test-saw", "test-square" };
    std::vector<VoiceInfo> voices;

    std::vector<std::shared_ptr<const Rendered>> retired;
    std::atomic<int> renderCounter { 0 };
    int healthTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PurrProcessor)
};
} // namespace purr
