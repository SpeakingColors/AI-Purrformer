#include "FxEditor.h"
#include <catui/CatLookAndFeel.h>
#include <catui/Logo.h>
#include <catui/RenderGate.h>

namespace purr
{
namespace
{
using namespace col;
constexpr int kCheek = 22;

juce::String dbText (float db) { return juce::String (juce::roundToInt (db)); }
float toDb (float lin) { return lin > 0.00001f ? juce::Decibels::gainToDecibels (lin, -90.0f) : -90.0f; }

// Level meter with a dB scale (-60 to 0), peak hold falls slowly.
struct Meter : juce::Component
{
    juce::String caption;
    float db = -90.0f, hold = -90.0f;
    Meter (juce::String c) : caption (std::move (c)) {}
    void feed (float lin)
    {
        const float d = toDb (lin);
        db = juce::jmax (d, db - 3.0f);
        hold = juce::jmax (d, hold - 0.6f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto bar = r.withTrimmedBottom (14.0f);
        drawWell (g, bar);
        auto inner = bar.reduced (3.0f);
        auto frac = [] (float d) { return juce::jlimit (0.0f, 1.0f, (d + 60.0f) / 60.0f); };
        auto fill = inner.withWidth (inner.getWidth() * frac (db));
        g.setGradientFill (juce::ColourGradient (mint, inner.getX(), 0, db > -6.0f ? pink : sun, inner.getRight(), 0, false));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (ink);
        g.fillRect (inner.getX() + inner.getWidth() * frac (hold) - 1.0f, inner.getY(), 2.0f, inner.getHeight());
        g.setFont (ui (9.5f));
        for (float t : { -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })
        {
            const float x = inner.getX() + inner.getWidth() * frac (t);
            g.setColour (mute.withAlpha (0.6f));
            g.drawLine (x, bar.getBottom() - 4, x, bar.getBottom(), 1.0f);
            g.setColour (mute);
            g.drawText (dbText (t), juce::Rectangle<float> (x - 12, bar.getBottom(), 24, 12).toNearestInt(), juce::Justification::centred);
        }
        g.setFont (ui (11.0f, true));
        g.setColour (bg); g.drawText (caption, bar.reduced (8, 0).translated (1, 1).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (ink); g.drawText (caption, bar.reduced (8, 0).toNearestInt(), juce::Justification::centredLeft);
    }
};

// The obvious drag and drop box: drag it onto any track in the host, or into a folder.
struct DropBox : juce::Component, juce::SettableTooltipClient
{
    FxProcessor& proc;
    std::vector<float> peaks;
    int shownRender = -1;
    float pulse = 0.0f;
    std::function<void()> onSave;

    explicit DropBox (FxProcessor& p) : proc (p) {}
    bool ready() const { return proc.resultFile().existsAsFile(); }

    void refreshPeaks()
    {
        if (proc.renderCount() == shownRender) return;
        shownRender = proc.renderCount();
        peaks.clear();
        if (auto r = proc.getRendered())
        {
            const int cols = 320, n = r->audio.getNumSamples();
            if (n > 0)
            {
                auto* d = r->audio.getReadPointer (0);
                for (int c = 0; c < cols; ++c)
                {
                    const int a = (int) ((juce::int64) n * c / cols), b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / cols));
                    float m = 0.0f;
                    for (int i = a; i < b; i += juce::jmax (1, (b - a) / 64)) m = juce::jmax (m, std::abs (d[i]));
                    peaks.push_back (m);
                }
            }
        }
        repaint();
    }
    void tick() { pulse += 0.12f; if (ready()) repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = ready();
        const float glow = on ? 0.5f + 0.5f * std::sin (pulse) : 0.0f;
        g.setColour (well); g.fillRoundedRectangle (r, 16.0f);
        if (on) { g.setColour (pink.withAlpha (0.10f + 0.12f * glow)); g.fillRoundedRectangle (r, 16.0f); }
        juce::Path box; box.addRoundedRectangle (r.reduced (3.0f), 14.0f);
        juce::Path dashed;
        const float dashes[] = { 10.0f, 7.0f };
        juce::PathStrokeType (on ? 3.0f : 2.0f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (on ? pink.interpolatedWith (sun, glow) : line);
        g.fillPath (dashed);

        auto area = r.reduced (14.0f);
        if (! peaks.empty() && on)
        {
            auto wv = area.withTrimmedTop (area.getHeight() * 0.50f);
            const float cx = wv.getCentreY(), hh = wv.getHeight() * 0.45f;
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const float x = wv.getX() + wv.getWidth() * (float) i / (float) peaks.size();
                g.setColour (teal.withAlpha (0.8f));
                g.fillRect (x, cx - peaks[i] * hh, wv.getWidth() / (float) peaks.size() + 0.5f, juce::jmax (1.0f, peaks[i] * hh * 2.0f));
            }
        }
        auto head = area.removeFromTop (area.getHeight() * (on ? 0.5f : 1.0f));
        // arrow pointing out of the box
        const float ax = head.getX() + 34.0f, ay = head.getCentreY();
        juce::Path arrow;
        arrow.addArrow ({ ax, ay - 22.0f, ax, ay + 24.0f + 4.0f * glow }, 8.0f, 26.0f, 18.0f);
        g.setColour (on ? sun : mute.withAlpha (0.5f)); g.fillPath (arrow);
        auto text = head.withTrimmedLeft (74.0f);
        g.setColour (on ? ink : mute);
        g.setFont (ui (on ? 26.0f : 17.0f, true));
        g.drawText (on ? "DRAG ME onto a track" : "Your result appears here", text.removeFromTop (text.getHeight() * 0.5f).toNearestInt(),
                    juce::Justification::bottomLeft);
        g.setFont (ui (13.0f)); g.setColour (on ? mint : mute);
        g.drawText (on ? proc.resultFile().getFileName() + "   (also saved on your computer)" : "Record, then press Render. Then drag this box into your DAW.",
                    text.toNearestInt(), juce::Justification::topLeft);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 6 || ! ready()) return;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ proc.resultFile().getFullPathName() }, false, this);
    }
    juce::MouseCursor getMouseCursor() override { return ready() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor; }
};

juce::String pctText (double v, int) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
double pctValue (const juce::String& t) { return t.getDoubleValue() / 100.0; }
} // namespace

struct FxEditor::Content : juce::Component
{
    FxProcessor& proc;
    FxEditor& owner;
    catui::RenderGate gate { *this };

    juce::TextButton recBtn { "RECORD" }, discardBtn { "Discard" }, renderBtn { "RENDER" }, saveBtn { "Save .wav..." },
                     tipsBtn { "Tips" };
    Meter inMeter { "Input (your voice)" }, scMeter { "Sidechain (the song)" };
    juce::ComboBox keyBox, voiceBox[3];
    juce::Slider snap, follow, w[3], outLevel, dry;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aSnap, aFollow, aW[3], aOut, aDry;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aKey;
    DropBox drop { proc };
    std::vector<VoiceInfo> shownVoices;
    std::unique_ptr<juce::FileChooser> chooser;
    int blink = 0;
    bool tips = true;

    Content (FxProcessor& p, FxEditor& o) : proc (p), owner (o)
    {
        auto setup = [this] (juce::Slider& s, juce::Colour c, const juce::String& tip) {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
            s.setColour (juce::Slider::trackColourId, c);
            s.setColour (juce::Slider::textBoxTextColourId, ink);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setTooltip (tip);
            addAndMakeVisible (s);
        };
        setup (snap, violet, "How strongly the voice is pulled onto the notes of the song's key. 0% keeps it as sung.");
        setup (follow, mint, "When MIDI is sent in, how closely the voice follows those notes. 100% = exactly the MIDI.");
        setup (outLevel, sun, "Volume of the new AI voice.");
        setup (dry, sky, "How much of your original recording you still hear next to the AI voice.");
        for (int i = 0; i < 3; ++i) setup (w[i], i == 0 ? pink : i == 1 ? teal : orange, "How much of this voice is in the mix. Mix up to three voices.");
        aSnap = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "key_snap", snap);
        aFollow = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "midi_follow", follow);
        aOut = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "out_gain", outLevel);
        aDry = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dry", dry);
        const char* wid[3] = { "w1", "w2", "w3" };
        for (int i = 0; i < 3; ++i)
        {
            aW[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, wid[i], w[i]);
            voiceBox[i].setTooltip ("Pick a voice. Add your own voices with the Voices tool of the engine.");
            addAndMakeVisible (voiceBox[i]);
            voiceBox[i].onChange = [this, i] {
                const int idx = voiceBox[i].getSelectedItemIndex();
                if (idx >= 0 && idx < (int) shownVoices.size()) proc.setVoiceId (i, shownVoices[(size_t) idx].id);
            };
        }
        for (auto* s : { &snap, &follow, &outLevel, &dry, &w[0], &w[1], &w[2] })
        {
            s->textFromValueFunction = [] (double v) { return pctText (v, 0); };
            s->valueFromTextFunction = [] (const juce::String& t) { return pctValue (t); };
            s->updateText();
        }
        keyBox.addItemList (proc.apvts.getParameter ("key")->getAllValueStrings(), 1);
        keyBox.setTooltip ("Auto listens to the sidechain (the song) and picks the key. Or choose it yourself. Off = no key help.");
        addAndMakeVisible (keyBox);
        aKey = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);

        recBtn.setTooltip ("Press, then press play in your host and sing. The plugin keeps what it hears while the host plays.");
        recBtn.onClick = [this] {
            const auto s = proc.recState();
            if (s == FxProcessor::Armed || s == FxProcessor::Recording) proc.stopRecord(); else proc.armRecord();
        };
        discardBtn.setTooltip ("Throw the recording away.");
        discardBtn.onClick = [this] { proc.discardRecording(); };
        renderBtn.setTooltip ("Send the recording to the engine on your computer. It sings it back in the voice you picked.");
        renderBtn.onClick = [this] { proc.renderRecording(); };
        saveBtn.setTooltip ("Save the result as a .wav file wherever you like.");
        saveBtn.onClick = [this] { saveAs(); };
        tipsBtn.setClickingTogglesState (true); tipsBtn.setToggleState (true, juce::dontSendNotification);
        tipsBtn.setTooltip ("Turn the hover hints on or off.");
        tipsBtn.onClick = [this] { tips = tipsBtn.getToggleState(); owner.tooltips->setMillisecondsBeforeTipAppears (tips ? 1000 : 100000000); };
        for (auto* b : { &recBtn, &discardBtn, &renderBtn, &saveBtn, &tipsBtn }) addAndMakeVisible (b);
        addAndMakeVisible (inMeter); addAndMakeVisible (scMeter); addAndMakeVisible (drop);
        drop.setTooltip ("Drag this box onto any track in your host to drop the AI voice there.");

        gate.onFrame = [this] { frame(); };
        gate.start (30);
        syncVoices();
    }

    void refreshNow() { frame(); }

    void saveAs()
    {
        auto f = proc.resultFile();
        if (! f.existsAsFile()) return;
        chooser = std::make_unique<juce::FileChooser> ("Save the AI voice", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (f.getFileName()), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [f] (const juce::FileChooser& fc) { auto dst = fc.getResult(); if (dst != juce::File()) f.copyFileTo (dst.withFileExtension ("wav")); });
    }

    void syncVoices()
    {
        auto v = proc.getVoices();
        if (v.size() == shownVoices.size() && std::equal (v.begin(), v.end(), shownVoices.begin(), [] (auto& a, auto& b) { return a.id == b.id; })) return;
        shownVoices = v;
        auto ids = proc.getVoiceIds();
        for (int i = 0; i < 3; ++i)
        {
            voiceBox[i].clear (juce::dontSendNotification);
            int sel = -1;
            for (size_t k = 0; k < v.size(); ++k)
            {
                voiceBox[i].addItem (v[k].name.isNotEmpty() ? v[k].name : v[k].id, (int) k + 1);
                if (v[k].id == ids[(size_t) i]) sel = (int) k;
            }
            if (sel >= 0) voiceBox[i].setSelectedItemIndex (sel, juce::dontSendNotification);
        }
    }

    void frame()
    {
        proc.poll();
        inMeter.feed (proc.takeInputPeak());
        scMeter.feed (proc.takeSidechainPeak());
        syncVoices();
        drop.refreshPeaks(); drop.tick();
        const auto s = proc.recState();
        ++blink;
        recBtn.setButtonText (s == FxProcessor::Armed ? "WAITING FOR PLAY... (click to cancel)"
                              : s == FxProcessor::Recording ? "STOP  " + juce::String (proc.recordedSeconds(), 1) + " s"
                              : s == FxProcessor::Done ? "RECORD AGAIN" : "RECORD");
        const juce::Colour rc = s == FxProcessor::Recording ? ((blink / 8) % 2 ? pink : orange) : s == FxProcessor::Armed ? sun : pink;
        recBtn.setColour (juce::TextButton::buttonColourId, rc);
        recBtn.setColour (juce::TextButton::textColourOffId, bg);
        discardBtn.setVisible (s == FxProcessor::Done);
        const bool busy = proc.renderState.load() == 1;
        renderBtn.setEnabled (proc.hasRecording() && proc.engineUp.load() && ! busy);
        renderBtn.setButtonText (busy ? "RENDERING..." : "RENDER");
        renderBtn.setColour (juce::TextButton::buttonColourId, renderBtn.isEnabled() ? mint : well);
        renderBtn.setColour (juce::TextButton::textColourOffId, renderBtn.isEnabled() ? bg : mute);
        saveBtn.setEnabled (drop.ready());
        repaint (getLocalBounds().withTrimmedBottom (0));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x < kCheek || e.x > getWidth() - kCheek) owner.setSize (baseW, baseH);   // wood cheek: back to 100%
    }

    void card (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& n, const juce::String& title, juce::Colour c)
    {
        drawPanel (g, r.toFloat());
        auto head = r.reduced (14, 8).removeFromTop (22);
        g.setColour (c); g.fillEllipse ((float) head.getX(), (float) head.getY(), 22.0f, 22.0f);
        g.setColour (bg); g.setFont (ui (13.0f, true)); g.drawText (n, head.removeFromLeft (22), juce::Justification::centred);
        label (g, title, head.withTrimmedLeft (8), ink, 14.0f);
    }

    // one place decides where everything sits; paint and resized both read it
    struct L
    {
        juce::Rectangle<int> hdr, c1, c2, c3, c4;
        juce::Rectangle<int> recRow, discardR, inM, scM, note1, note2;
        juce::Rectangle<int> keyInfo, keyL, keyR, snapL, snapR, folL, folR;
        juce::Rectangle<int> voiceRow[3], wL[3], wR[3], outL, outR, dryL, dryR;
        juce::Rectangle<int> renderR, statusR, saveR, dropR;
    } lay;

    void computeLayout()
    {
        auto r = getLocalBounds().reduced (kCheek, 0);
        lay.hdr = r.removeFromTop (46);
        const int m = 12;
        auto body = r.reduced (m, 0);
        body.removeFromTop (10);
        auto top = body.removeFromTop (380);
        body.removeFromTop (m);
        lay.c4 = body.withTrimmedBottom (m);
        auto left = top.removeFromLeft (top.getWidth() / 2 - m / 2);
        top.removeFromLeft (m);
        lay.c3 = top;
        lay.c1 = left.removeFromTop (190);
        left.removeFromTop (m);
        lay.c2 = left;

        auto in1 = lay.c1.reduced (14, 8); in1.removeFromTop (28);
        auto row = in1.removeFromTop (38);
        lay.discardR = row.removeFromRight (84).reduced (0, 2); row.removeFromRight (6);
        lay.recRow = row;
        in1.removeFromTop (6);
        lay.inM = in1.removeFromTop (34); lay.scM = in1.removeFromTop (34);
        lay.note1 = in1.removeFromTop (16); lay.note2 = in1.removeFromTop (16);

        auto in2 = lay.c2.reduced (14, 8); in2.removeFromTop (28);
        lay.keyInfo = in2.removeFromTop (40);
        auto line = [&] (juce::Rectangle<int>& lab, juce::Rectangle<int>& ctl) {
            auto rr = in2.removeFromTop (28); in2.removeFromTop (3);
            lab = rr.removeFromLeft (100); ctl = rr;
        };
        line (lay.keyL, lay.keyR); line (lay.snapL, lay.snapR); line (lay.folL, lay.folR);

        auto in3 = lay.c3.reduced (14, 8); in3.removeFromTop (28);
        for (int i = 0; i < 3; ++i)
        {
            lay.voiceRow[i] = in3.removeFromTop (26); in3.removeFromTop (2);
            auto rr = in3.removeFromTop (26); in3.removeFromTop (12);
            lay.wL[i] = rr.removeFromLeft (100); lay.wR[i] = rr;
        }
        in3.removeFromTop (4);
        auto rr = in3.removeFromTop (28); in3.removeFromTop (3); lay.outL = rr.removeFromLeft (130); lay.outR = rr;
        rr = in3.removeFromTop (28); lay.dryL = rr.removeFromLeft (130); lay.dryR = rr;

        auto in4 = lay.c4.reduced (14, 8); in4.removeFromTop (26);
        auto tr = in4.removeFromTop (34);
        lay.renderR = tr.removeFromLeft (150);
        lay.saveR = tr.removeFromRight (130);
        lay.statusR = tr.reduced (10, 0);
        in4.removeFromTop (6);
        lay.dropR = in4;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);
        catui::drawWoodCheeks (g, getLocalBounds(), kCheek);
        auto hd = lay.hdr;
        g.setColour (panelHi); g.fillRect (hd);
        catui::drawCatHead (g, hd.removeFromLeft (52).reduced (8, 6).toFloat(), pink, bg);
        label (g, "AI Purrformer", hd.withWidth (170), ink, 20.0f);
        label (g, "Studio 87", hd.withTrimmedLeft (172).withWidth (80), mute, 12.0f);
        const bool up = proc.engineUp.load();
        label (g, up ? "Engine: running" : "Engine NOT running. Start it from the Start menu (AI Purrformer Engine).",
               hd.withTrimmedLeft (260).withTrimmedRight (80), up ? mint : orange, 12.0f);

        card (g, lay.c1, "1", "Record", pink);
        card (g, lay.c2, "2", "Listen to the song", violet);
        card (g, lay.c3, "3", "Pick the voice", teal);
        card (g, lay.c4, "4", "Render, then drag the result out", sun);

        g.setFont (ui (11.5f)); g.setColour (mute);
        g.drawText (proc.hasSidechain() ? "Sidechain connected: the plugin hears the song."
                                        : "No sidechain yet: send a track or the master here to find the key.",
                    lay.note1, juce::Justification::centredLeft);
        g.drawText ("MIDI notes captured: " + juce::String ((int) proc.getNotes().size()) + " (optional, from another track)",
                    lay.note2, juce::Justification::centredLeft);

        const auto st = proc.analysisState();
        auto ki = lay.keyInfo;
        const auto kt = proc.keyText();
        auto kline = ki.removeFromTop (22);
        g.setFont (ui (12.0f, true)); g.setColour (mute);
        g.drawText ("Key found:", kline.removeFromLeft (80), juce::Justification::centredLeft);
        g.setColour (ink); g.setFont (ui (15.0f, true));
        g.drawText (kt.isNotEmpty() ? kt : (st.isNotEmpty() ? st : "after you record"), kline, juce::Justification::centredLeft);
        g.setColour (mint); g.setFont (ui (11.5f));
        g.drawText (proc.chordsText().isNotEmpty() ? "Chords: " + proc.chordsText() : "", ki, juce::Justification::centredLeft);

        auto lab = [&] (const juce::String& t, juce::Rectangle<int> r) { label (g, t, r, mute, 12.0f); };
        lab ("Key", lay.keyL); lab ("Snap to key", lay.snapL); lab ("Follow MIDI", lay.folL);
        for (int i = 0; i < 3; ++i) lab ("Voice " + juce::String (i + 1) + " amount", lay.wL[i]);
        lab ("AI voice level", lay.outL); lab ("Original voice", lay.dryL);
        label (g, proc.statusText(), lay.statusR, mute, 12.5f);
    }

    void resized() override
    {
        computeLayout();
        { auto hb = lay.hdr; tipsBtn.setBounds (hb.removeFromRight (70).reduced (6, 9)); }
        recBtn.setBounds (lay.recRow); discardBtn.setBounds (lay.discardR);
        inMeter.setBounds (lay.inM); scMeter.setBounds (lay.scM);
        keyBox.setBounds (lay.keyR); snap.setBounds (lay.snapR); follow.setBounds (lay.folR);
        for (int i = 0; i < 3; ++i) { voiceBox[i].setBounds (lay.voiceRow[i]); w[i].setBounds (lay.wR[i]); }
        outLevel.setBounds (lay.outR); dry.setBounds (lay.dryR);
        renderBtn.setBounds (lay.renderR); saveBtn.setBounds (lay.saveR);
        drop.setBounds (lay.dropR);
    }

    static constexpr int baseW = FxEditor::baseW, baseH = FxEditor::baseH;
};

FxEditor::FxEditor (FxProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 1000);
    content = std::make_unique<Content> (p, *this);
    addAndMakeVisible (*content);
    content->setSize (baseW, baseH);

    constrainer.setFixedAspectRatio ((double) baseW / baseH);
    constrainer.setSizeLimits (baseW / 2, baseH / 2, baseW * 3, baseH * 3);
    setConstrainer (&constrainer);
    setResizable (true, false);
    border = std::make_unique<juce::ResizableBorderComponent> (this, &constrainer);
    border->setBorderThickness (juce::BorderSize<int> (6));
    addAndMakeVisible (*border);
    setSize (baseW, baseH);
}

void FxEditor::refreshForScreenshot() { content->refreshNow(); }

FxEditor::~FxEditor()
{
    setConstrainer (nullptr);
    setLookAndFeel (nullptr);
}

void FxEditor::resized()
{
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW, (float) getHeight() / (float) baseH));
    content->setBounds (0, 0, baseW, baseH);
    border->setBounds (getLocalBounds());
    border->toFront (false);
}
} // namespace purr
