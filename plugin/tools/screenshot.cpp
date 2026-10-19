// Makes pictures of the FX editor for the manual and README. Needs the engine running (tone backend).
//   purr_screenshot <output folder>
#include "../src/FxProcessor.h"
#include "../src/FxEditor.h"
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

static FxEditor* gEd = nullptr;
static void shot (juce::Component& c, const juce::File& f)
{
    if (gEd != nullptr) gEd->refreshForScreenshot();
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.openedOk()) png.writeImageToStream (img, out);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory();
    dir.createDirectory();

    if (argc > 2 && juce::String (argv[2]) == "sing")
    {
        PurrProcessor sing;
        sing.setRateAndBufferSizeDetails (44100.0, 512);
        sing.prepareToPlay (44100.0, 512);
        sing.refreshHealth(); sing.refreshVoices();
        std::unique_ptr<juce::AudioProcessorEditor> e2 (sing.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
        shot (*e2, dir.getChildFile ("sing-1.png"));
        return 0;
    }

    FxProcessor fx;
    FakeHost host;
    fx.setPlayHead (&host);
    fx.setRateAndBufferSizeDetails (44100.0, 512);
    fx.prepareToPlay (44100.0, 512);
    fx.refreshHealth(); fx.refreshVoices();
    std::unique_ptr<juce::AudioProcessorEditor> ed (fx.createEditor());
    gEd = dynamic_cast<FxEditor*> (ed.get());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    shot (*ed, dir.getChildFile ("fx-1-start.png"));

    // record about 5 seconds of a gliding sine, with one MIDI note
    juce::AudioBuffer<float> io (2, 512);
    double phase = 0.0, ppq = 8.0, t = 0.0;
    const double step = 512 * host.bpm / 60.0 / 44100.0;
    fx.armRecord();
    juce::MidiBuffer note; note.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    for (int b = 0; b < 430; ++b)
    {
        const double f = 200.0 + 40.0 * std::sin (t * 2.0);
        for (int i = 0; i < 512; ++i)
        {
            const float s = 0.35f * (float) std::sin (phase); phase += 2.0 * 3.14159265358979 * f / 44100.0; t += 1.0 / 44100.0;
            io.setSample (0, i, s); io.setSample (1, i, s);
        }
        host.ppq = ppq; host.playing = true;
        juce::MidiBuffer m; if (b == 0) m = note;
        fx.processBlock (io, m);
        ppq += step;
        if (b == 200) { juce::MessageManager::getInstance()->runDispatchLoopUntil (120); shot (*ed, dir.getChildFile ("fx-2-recording.png")); }
    }
    host.playing = false; juce::MidiBuffer none; fx.processBlock (io, none);
    fx.poll();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    shot (*ed, dir.getChildFile ("fx-3-recorded.png"));

    {   // show the optional bits: a hand-picked key and some grit
        auto setP = [&] (const char* id, float v) { auto* p = fx.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        setP ("key", 1); setP ("tonic", 9); setP ("scale", 10);                         // Manual, A, minor pentatonic
        setP ("grit_on", 1); setP ("grit_drive", 0.35f); setP ("grit_trans", 0.25f); setP ("grit_fuzz", 0.15f); setP ("grit_mix", 0.4f); setP ("grit_pos", 3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    }
    juce::String err;
    if (! fx.renderBlockingFx (err)) { std::printf ("render failed: %s\n", err.toRawUTF8()); return 1; }
    fx.setStatusText ("Done. Drag the box onto a track, or press play to hear it here.");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    shot (*ed, dir.getChildFile ("fx-4-result.png"));
    return 0;
}
