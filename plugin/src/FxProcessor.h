// AI Purrformer FX plugin: audio in, sidechain in, MIDI in. AGPL-3.0.
// Workflow: arm Record, play the host, sing (or play the track); the plugin keeps the audio, the
// sidechain audio (for the key) and any MIDI. Render sends it to the local engine; the result plays
// back in time with the host and can be dragged out as a .wav onto any track.
#pragma once
#include "PluginProcessor.h"

namespace purr
{
class FxProcessor : public PurrProcessor
{
public:
    FxProcessor();
    ~FxProcessor() override;

    const juce::String getName() const override { return "AI Purrformer"; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    enum RecState { Idle = 0, Armed = 1, Recording = 2, Done = 3 };
    // message thread
    void armRecord();                          // wait for the host to play, then record
    void stopRecord();
    void discardRecording();
    void poll();                               // call from the editor timer: closes notes, starts analysis
    RecState recState() const { return (RecState) recStateA.load(); }
    double recordedSeconds() const { return recLen.load() / juce::jmax (1.0, hostSr); }
    float takeInputPeak() { return inPeak.exchange (0.0f); }
    bool hasSidechain() const { return sidechainOn.load(); }
    float takeSidechainPeak() { return scPeak.exchange (0.0f); }
    bool hasRecording() const { return recState() == Done && recLen.load() > 0; }

    void renderRecording();                    // worker thread
    juce::File resultFile() const;             // empty until something was rendered
    juce::String keyText() const;
    juce::String chordsText() const;
    juce::String analysisState() const;
    static juce::File dataRoot();

    // for tests
    bool renderBlockingFx (juce::String& err);
    static juce::MemoryBlock toWav16 (const float* mono, int n, double sr);

private:
    bool buildBody (juce::MemoryBlock& body, juce::String& err);
    void startAnalysis();
    juce::File saveResult (const juce::MemoryBlock& wav);

    static constexpr int maxSeconds = 300;
    juce::AudioBuffer<float> rec, side;        // mono, allocated in prepareToPlay
    std::atomic<int> recStateA { Idle };
    std::atomic<int> recLen { 0 }, sideLen { 0 };
    std::atomic<float> inPeak { 0.0f }, scPeak { 0.0f };
    std::atomic<bool> sidechainOn { false };
    double recOrigin = 0.0, recTempo = 120.0, expectedPpq = 0.0;   // set by the audio thread before Recording
    bool analysed = false, notesClosed = false;

    mutable juce::CriticalSection fxLock;
    juce::File lastResult;
    juce::String keyTxt, chordsTxt, analysisTxt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxProcessor)
};
} // namespace purr
