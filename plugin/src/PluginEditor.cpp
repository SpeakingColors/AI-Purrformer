#include "PluginEditor.h"
#include "Mascot.h"

namespace purr
{
namespace
{
using namespace col;

// ---------- helpers --------------------------------------------------------------------------
float pv (PurrProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

void setP (PurrProcessor& p, const char* id, float real)
{
    auto* prm = p.apvts.getParameter (id);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    prm->endChangeGesture();
}

void hashIn (std::uint64_t& h, double v) { h = h * 1099511628211ull + (std::uint64_t) (std::int64_t) std::llround (v * 1000.0) + 1469598103934665603ull; }

int countSyllables (const juce::String& wordIn)
{
    auto w = wordIn.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz");
    if (w.isEmpty()) return 0;
    if (w.length() > 2 && w.endsWithChar ('e') && ! juce::String ("aeiouy").containsChar (w[w.length() - 2])) w = w.dropLastCharacters (1);
    auto vowel = [] (juce::juce_wchar c) { return juce::String ("aeiouy").containsChar (c); };
    int n = 0;
    for (int i = 0; i < w.length(); ++i)
    {
        if (vowel (w[i]) && (i == 0 || ! vowel (w[i - 1]))) ++n;
        else if (i > 0 && vowel (w[i]) && vowel (w[i - 1]))
        {
            auto pair = w.substring (i - 1, i + 1);
            if (pair == "eo" || pair == "ia" || pair == "ua" || pair == "uo" || (pair == "io" && w.substring (i + 1, i + 2) != "n")) ++n;
        }
    }
    return juce::jmax (1, n);
}

int countLine (const juce::String& line)
{
    int n = 0;
    for (auto& w : juce::StringArray::fromTokens (line, " \t", "")) n += countSyllables (w);
    return n;
}

// Splits into chip texts (roughly one per syllable) for the Sing page.
juce::StringArray chipsFor (const juce::String& text)
{
    juce::StringArray out;
    for (auto& w : juce::StringArray::fromTokens (text, " \t\r\n", ""))
    {
        int s = countSyllables (w);
        if (s <= 1) { out.add (w); continue; }
        int per = (int) std::ceil (w.length() / (double) s);
        for (int i = 0; i < s; ++i)
            out.add (w.substring (i * per, i == s - 1 ? w.length() : (i + 1) * per));
    }
    return out;
}

struct Live : juce::Component, private juce::Timer
{
    std::uint64_t last = ~0ull;
    virtual std::uint64_t signature() = 0;
    void visibilityChanged() override { if (isShowing()) startTimerHz (15); else stopTimer(); }
    void timerCallback() override { auto s = signature(); if (s != last) { last = s; repaint(); } }
};

// ---------- small drawn icons --------------------------------------------------------------------
enum class Ico { mic, sliders, pen, users, robot, layers, flame, wave, note };
void drawIcon (juce::Graphics& g, Ico i, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    auto st = juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    auto cx = r.getCentreX(), cy = r.getCentreY(), u = r.getWidth() / 24.0f;
    auto X = [&] (float x) { return r.getX() + x * u; };
    auto Y = [&] (float y) { return r.getY() + y * u; };
    switch (i)
    {
        case Ico::mic:
            p.addRoundedRectangle (X (9), Y (2), 6 * u, 12 * u, 3 * u);
            p.startNewSubPath (X (5), Y (11)); p.addCentredArc (X (12), Y (11), 7 * u, 7 * u, 0, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, false);
            p.startNewSubPath (X (12), Y (18)); p.lineTo (X (12), Y (22)); break;
        case Ico::sliders:
            for (float x : { 5.0f, 12.0f, 19.0f }) { p.startNewSubPath (X (x), Y (3)); p.lineTo (X (x), Y (21)); }
            p.addEllipse (X (3), Y (14), 4 * u, 4 * u); p.addEllipse (X (10), Y (6), 4 * u, 4 * u); p.addEllipse (X (17), Y (11), 4 * u, 4 * u); break;
        case Ico::pen:
            p.startNewSubPath (X (4), Y (20)); p.lineTo (X (5), Y (16)); p.lineTo (X (17), Y (4)); p.lineTo (X (20), Y (7)); p.lineTo (X (8), Y (19)); p.closeSubPath(); break;
        case Ico::users:
            p.addEllipse (X (5), Y (3), 8 * u, 8 * u); p.startNewSubPath (X (2), Y (21)); p.lineTo (X (2), Y (19)); p.lineTo (X (3), Y (15)); p.lineTo (X (15), Y (15)); p.lineTo (X (16), Y (19)); p.lineTo (X (16), Y (21)); break;
        case Ico::robot:
            p.addRoundedRectangle (X (4), Y (8), 16 * u, 12 * u, 3 * u); p.startNewSubPath (X (12), Y (8)); p.lineTo (X (12), Y (3));
            p.addEllipse (X (8), Y (12), 2 * u, 2 * u); p.addEllipse (X (14), Y (12), 2 * u, 2 * u); break;
        case Ico::layers:
            p.startNewSubPath (X (12), Y (3)); p.lineTo (X (21), Y (8)); p.lineTo (X (12), Y (13)); p.lineTo (X (3), Y (8)); p.closeSubPath();
            p.startNewSubPath (X (3), Y (13)); p.lineTo (X (12), Y (18)); p.lineTo (X (21), Y (13)); break;
        case Ico::flame:
            p.startNewSubPath (X (12), Y (22)); p.cubicTo (X (4), Y (22), X (4), Y (13), X (9), Y (8)); p.cubicTo (X (9), Y (12), X (12), Y (11), X (12), Y (3));
            p.cubicTo (X (18), Y (8), X (20), Y (14), X (18), Y (18)); p.cubicTo (X (17), Y (21), X (15), Y (22), X (12), Y (22)); break;
        case Ico::wave:
            for (float x : { 3.0f, 7.0f, 11.0f, 15.0f, 19.0f }) { float h = x == 11 ? 8.0f : (x == 7 || x == 15 ? 5.0f : 3.0f); p.startNewSubPath (X (x), cy - h * u); p.lineTo (X (x), cy + h * u); } break;
        case Ico::note:
            p.startNewSubPath (X (9), Y (18)); p.lineTo (X (9), Y (5)); p.lineTo (X (21), Y (3)); p.lineTo (X (21), Y (16));
            p.addEllipse (X (3), Y (15), 6 * u, 6 * u); p.addEllipse (X (15), Y (13), 6 * u, 6 * u); break;
    }
    juce::ignoreUnused (cx);
    g.strokePath (p, st);
}

// ---------- live views ---------------------------------------------------------------------------
struct PianoRoll : Live
{
    PurrProcessor& p;
    explicit PianoRoll (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override
    {
        std::uint64_t h = 7; auto n = p.getNotes(); hashIn (h, (double) n.size()); hashIn (h, p.isCapturing());
        if (! n.empty()) { hashIn (h, n.back().startBeats); hashIn (h, n.back().pitch); }
        return h;
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawWell (g, r);
        auto notes = p.getNotes();
        if (notes.empty())
        {
            label (g, p.isCapturing() ? "Recording: press play in your host..." : "No melody yet. Capture from the host, or open a .mid file.",
                   getLocalBounds().reduced (10), mute, 12.0f, false, juce::Justification::centred);
            return;
        }
        double lo = 1e9, hi = -1e9; int pl = 127, ph = 0;
        for (auto& n : notes) { lo = juce::jmin (lo, n.startBeats); hi = juce::jmax (hi, n.startBeats + n.lengthBeats); pl = juce::jmin (pl, n.pitch); ph = juce::jmax (ph, n.pitch); }
        double origin = std::floor (lo / 4.0) * 4.0, span = juce::jmax (4.0, std::ceil ((hi - origin) / 4.0) * 4.0);
        pl -= 2; ph += 2; if (ph - pl < 11) ph = pl + 11;
        auto in = r.reduced (6.0f);
        g.setColour (line.withAlpha (0.5f));
        for (double b = 0; b <= span; b += 4.0) g.drawVerticalLine ((int) (in.getX() + in.getWidth() * (float) (b / span)), in.getY(), in.getBottom());
        const juce::Colour pal[] = { mint, sky, violet, pink, teal };
        for (auto& n : notes)
        {
            float x = in.getX() + in.getWidth() * (float) ((n.startBeats - origin) / span);
            float w = juce::jmax (4.0f, in.getWidth() * (float) (n.lengthBeats / span) - 2.0f);
            float h = in.getHeight() / (float) (ph - pl + 1);
            float y = in.getBottom() - (n.pitch - pl + 1) * h;
            auto c = pal[n.pitch % 5];
            g.setColour (c.withAlpha (0.3f)); g.fillRoundedRectangle (x - 2, y - 2, w + 4, h + 4, h / 2);
            g.setColour (c); g.fillRoundedRectangle (x, y, w, juce::jmax (4.0f, h - 1.0f), h / 2);
        }
    }
};

struct WaveView : Live
{
    PurrProcessor& p;
    explicit WaveView (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override
    {
        std::uint64_t h = 3; hashIn (h, p.renderCount()); hashIn (h, p.renderState.load());
        auto r = p.getRendered();
        if (r != nullptr) hashIn (h, juce::roundToInt ((p.getLastPpq() - r->originBeats) * 8.0));
        return h;
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); drawWell (g, r);
        auto rend = p.getRendered();
        if (rend == nullptr)
        {
            g.setColour (line); const float dash[] = { 4.0f, 6.0f };
            g.drawDashedLine (juce::Line<float> (8.0f, r.getCentreY(), r.getRight() - 8.0f, r.getCentreY()), dash, 2, 1.5f);
            label (g, p.renderState.load() == 1 ? "rendering..." : "press Render", getLocalBounds().reduced (10), mute, 11.0f, false, juce::Justification::centredRight);
            return;
        }
        const int n = rend->audio.getNumSamples();
        auto in = r.reduced (6.0f, 6.0f);
        const int cols = (int) in.getWidth();
        auto* d = rend->audio.getReadPointer (0);
        juce::Path path;
        g.setGradientFill (juce::ColourGradient (mint, in.getX(), 0, pink, in.getRight(), 0, false));
        for (int x = 0; x < cols; ++x)
        {
            int a = (int) ((juce::int64) x * n / cols), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / cols));
            float mx = 0; for (int i = a; i < b; i += juce::jmax (1, (b - a) / 16)) mx = juce::jmax (mx, std::abs (d[i]));
            float h = mx * in.getHeight() * 0.5f;
            g.fillRect (in.getX() + x, in.getCentreY() - h, 1.0f, juce::jmax (1.0f, h * 2));
        }
        double sec = (p.getLastPpq() - rend->originBeats) * 60.0 / rend->tempo;
        double frac = sec * rend->sampleRate / n;
        if (frac >= 0 && frac <= 1.0) { g.setColour (juce::Colours::white); g.fillRect (in.getX() + (float) frac * in.getWidth(), r.getY() + 2, 2.0f, r.getHeight() - 4); }
    }
};

struct BlendBar : Live
{
    PurrProcessor& p;
    explicit BlendBar (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override { std::uint64_t h = 1; for (auto* id : { "w1", "w2", "w3" }) hashIn (h, pv (p, id)); return h; }
    void paint (juce::Graphics& g) override
    {
        float w[3] = { pv (p, "w1"), pv (p, "w2"), pv (p, "w3") }, tot = w[0] + w[1] + w[2];
        const juce::Colour c[3] = { sun, pink, teal };
        auto r = getLocalBounds().toFloat(); float x = r.getX();
        juce::Path clip; clip.addRoundedRectangle (r, r.getHeight() / 2); g.saveState(); g.reduceClipRegion (clip);
        for (int i = 0; i < 3; ++i) { float ww = tot > 0 ? r.getWidth() * w[i] / tot : 0; g.setColour (c[i]); g.fillRect (x, r.getY(), ww, r.getHeight()); x += ww; }
        g.restoreState();
    }
};

struct RobotView : Live
{
    PurrProcessor& p; explicit RobotView (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override { std::uint64_t h = 5; hashIn (h, pv (p, "robot_speed")); hashIn (h, pv (p, "robot_mix")); hashIn (h, pv (p, "robot_on")); return h; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); drawWell (g, r); auto in = r.reduced (6.0f);
        const float lv[6] = { 44, 30, 18, 30, 52, 38 }; float rs = pv (p, "robot_speed"), mix = pv (p, "robot_mix");
        auto Y = [&] (float v) { return in.getY() + in.getHeight() * v / 70.0f; };
        g.setColour (line); for (float y : { 18.0f, 30.0f, 38.0f, 44.0f, 52.0f }) g.drawHorizontalLine ((int) Y (y), in.getX(), in.getRight());
        juce::Path nat, snap;
        for (int x = 0; x <= 300; x += 5)
        {
            int i = juce::jmin (5, x / 50); float y = lv[i] + 7 * std::sin (x / 11.0f) + 4 * std::sin (x / 4.3f) + (i % 2 ? 5 : -4);
            auto pt = juce::Point<float> (in.getX() + in.getWidth() * x / 300.0f, Y (y)); x ? nat.lineTo (pt) : nat.startNewSubPath (pt);
        }
        for (int i = 0; i < 6; ++i)
        {
            float x0 = i * 50.0f, pr = i ? lv[i - 1] : lv[0], gl = (1 - rs) * 42 + 1;
            auto P = [&] (float x, float v) { return juce::Point<float> (in.getX() + in.getWidth() * x / 300.0f, Y (v)); };
            if (i == 0) snap.startNewSubPath (P (0, lv[0]));
            snap.lineTo (P (x0, pr)); snap.lineTo (P (x0 + gl, lv[i])); snap.lineTo (P (x0 + 50, lv[i]));
        }
        float on = pv (p, "robot_on") > 0.5f ? 1.0f : 0.4f;
        g.setColour (mute.withAlpha ((1 - mix) * on)); g.strokePath (nat, juce::PathStrokeType (2.0f));
        g.setColour (sky.withAlpha ((0.15f + 0.85f * mix) * on)); g.strokePath (snap, juce::PathStrokeType (3.0f));
    }
};

struct SawView : Live
{
    PurrProcessor& p; explicit SawView (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override { std::uint64_t h = 5; for (auto* id : { "saw_copies", "saw_detune", "saw_mix", "saw_spread", "saw_widen", "saw_on" }) hashIn (h, pv (p, id)); return h; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); drawWell (g, r);
        int n = (int) std::lround (pv (p, "saw_copies")); float sp = pv (p, "saw_spread"), de = pv (p, "saw_detune"), wd = pv (p, "saw_widen"), mx = pv (p, "saw_mix");
        float on = pv (p, "saw_on") > 0.5f ? 1.0f : 0.4f;
        label (g, "L", getLocalBounds().reduced (8, 4), mute, 10.0f, false); label (g, "R", getLocalBounds().reduced (8, 4), mute, 10.0f, false, juce::Justification::topRight);
        for (int i = 0; i < n; ++i)
        {
            float o = -1.0f + 2.0f * i / (float) (n - 1), a = std::abs (o);
            auto c = a < 0.01f ? mint : (a < 0.5f ? juce::Colour (0xff6A2EFF) : (a < 0.8f ? violet : pink));
            float x = r.getCentreX() + o * sp * r.getWidth() * 0.44f, h = 26 + (1 - a) * 34, bw = 6 + wd * 8;
            auto bar = juce::Rectangle<float> (x - bw / 2, r.getBottom() - 18 - h, bw, h);
            float al = (0.35f + 0.65f * mx) * on;
            g.setColour (c.withAlpha (al * 0.3f)); g.fillRoundedRectangle (bar.expanded (3), 6);
            g.setColour (c.withAlpha (al)); g.fillRoundedRectangle (bar, 4);
            label (g, juce::String (juce::roundToInt (o * 50 * de)), juce::Rectangle<int> ((int) x - 16, (int) r.getBottom() - 16, 32, 14), mute, 9.0f, false, juce::Justification::centred);
        }
    }
};

struct DirtView : Live
{
    PurrProcessor& p; explicit DirtView (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override { std::uint64_t h = 5; for (auto* id : { "dirt_drive", "dirt_crush", "dirt_arts", "dirt_mix", "dirt_on" }) hashIn (h, pv (p, id)); return h; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); drawWell (g, r); auto in = r.reduced (6.0f);
        float dr = pv (p, "dirt_drive"), cr = pv (p, "dirt_crush"), ar = pv (p, "dirt_arts"), dm = pv (p, "dirt_mix"), on = pv (p, "dirt_on") > 0.5f ? 1.0f : 0.4f;
        float gain = 1 + dr * 9, L = cr < 0.02f ? 1000.0f : std::pow (2.0f, std::round (6 - 5 * cr));
        juce::Path dry, wet;
        for (int x = 0; x <= 300; x += 3)
        {
            float s = 0.9f * std::sin (x / 300.0f * juce::MathConstants<float>::pi * 4), y = std::tanh (gain * s) / std::tanh (gain);
            y = std::round (y * L) / L;
            for (auto pr : { std::pair<int, float> { 60, 1.0f }, { 150, 1.6f }, { 225, 1.2f } })
                if (ar > 0.08f && std::abs (x - pr.first) < ar * 14 * pr.second) y = ((x / 3) % 2 ? 1.0f : -1.0f) * 0.1f * ar;
            float w = s * (1 - dm) + y * dm, px = in.getX() + in.getWidth() * x / 300.0f;
            juce::Point<float> a (px, in.getCentreY() - s * in.getHeight() * 0.43f), b (px, in.getCentreY() - w * in.getHeight() * 0.43f);
            x ? dry.lineTo (a) : dry.startNewSubPath (a); x ? wet.lineTo (b) : wet.startNewSubPath (b);
        }
        g.setColour (mute.withAlpha (0.6f)); g.strokePath (dry, juce::PathStrokeType (1.5f));
        g.setColour (pink.withAlpha (on)); g.strokePath (wet, juce::PathStrokeType (2.5f));
    }
};

struct Chips : Live
{
    PurrProcessor& p; explicit Chips (PurrProcessor& pr) : p (pr) {}
    std::uint64_t signature() override { return (std::uint64_t) p.lyrics().hashCode64() ^ (std::uint64_t) p.getNotes().size(); }
    void paint (juce::Graphics& g) override
    {
        auto chips = chipsFor (p.lyrics()); int x = 0, y = 0;
        for (int i = 0; i < juce::jmin (24, chips.size()); ++i)
        {
            int w = juce::jmax (44, (int) ui (13.0f, true).getStringWidthFloat (chips[i]) + 22);
            if (x + w > getWidth()) { x = 0; y += 38; }
            if (y + 32 > getHeight()) break;
            auto cell = juce::Rectangle<float> ((float) x, (float) y, (float) w, 32.0f);
            bool beyond = i >= (int) p.getNotes().size() && ! p.getNotes().empty();
            g.setColour (well); g.fillRoundedRectangle (cell, 10); g.setColour (beyond ? pink : sky); g.drawRoundedRectangle (cell.reduced (0.5f), 10, 1.2f);
            label (g, chips[i], cell.toNearestInt(), ink, 13.0f, true, juce::Justification::centred);
            x += w + 5;
        }
    }
};

// ---------- knob row ----------------------------------------------------------------------------
struct Knob : juce::Component
{
    juce::Slider s { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::SliderParameterAttachment> att;
    juce::String title, hint; juce::Colour acc; bool pct;
    Knob (PurrProcessor& p, const char* id, juce::String t, juce::String h, juce::Colour c, bool asPct = true)
        : title (std::move (t)), hint (std::move (h)), acc (c), pct (asPct)
    {
        addAndMakeVisible (s); s.setColour (juce::Slider::trackColourId, acc);
        att = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (id), s);
        s.onValueChange = [this] { repaint(); };
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        label (g, title, { 0, 0, getWidth() - 70, 20 }, ink, 13.0f);
        label (g, hint, { 0, 19, getWidth() - 70, 16 }, mute, 11.0f, false);
        label (g, pct ? juce::String (juce::roundToInt (s.getValue() * 100)) + "%" : juce::String (juce::roundToInt (s.getValue())),
               { getWidth() - 64, 0, 64, 20 }, acc, 13.0f, true, juce::Justification::centredRight);
    }
    void resized() override { s.setBounds (0, getHeight() - 28, getWidth(), 24); }
};

struct Page : juce::Component { virtual void sync() {} };

struct Panel : juce::Component
{
    juce::String title; juce::Colour acc; Ico ico;
    Panel (juce::String t, juce::Colour c, Ico i) : title (std::move (t)), acc (c), ico (i) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat());
        drawIcon (g, ico, { 14.0f, 10.0f, 18.0f, 18.0f }, acc);
        label (g, title, { 38, 8, getWidth() - 140, 22 }, acc, 11.5f);
    }
};

void styleButton (juce::TextButton& b, juce::Colour fill = raise, juce::Colour text = ink)
{
    b.setColour (juce::TextButton::buttonColourId, fill); b.setColour (juce::TextButton::buttonOnColourId, mint);
    b.setColour (juce::TextButton::textColourOffId, text); b.setColour (juce::TextButton::textColourOnId, bg);
}

// ---------- pages -----------------------------------------------------------------------------------
struct Plain : juce::Component { void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds().toFloat()); } };

struct SingPage : Page
{
    Plain bottom;
    PurrProcessor& p;
    Panel melody { "MELODY", mint, Ico::note }, lyr { "LYRICS AND TIMING", sky, Ico::pen }, blend { "VOICE BLEND", sun, Ico::users };
    Seg mode { { "Melody leads", "Voice leads" }, mint }, blendMode { { "Layer (choir)", "One new voice (test)" }, violet };
    PianoRoll roll { p }; WaveView wave { p }; BlendBar bar { p }; Chips chips { p };
    juce::TextButton capture { "Capture from host" }, openMidi { "Open .mid..." }, clear { "Clear" }, render { "Render" };
    juce::TextEditor lyrics;
    juce::ComboBox voice[3]; juce::Slider w[3];
    std::unique_ptr<juce::SliderParameterAttachment> wAtt[3];
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::Drawable> mascot;
    std::vector<VoiceInfo> shown;

    explicit SingPage (PurrProcessor& pr) : p (pr)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &melody, &lyr, &blend, &bottom, &mode, &blendMode, &roll, &wave, &bar, &chips, &capture, &openMidi, &clear, &render, &lyrics })
            addAndMakeVisible (c);
        mode.onChange = [this] (int i) { setP (p, "mode", (float) i); };
        blendMode.onChange = [this] (int i) { setP (p, "blend", (float) i); };
        capture.setClickingTogglesState (true); styleButton (capture); styleButton (openMidi); styleButton (clear);
        styleButton (render, mint, bg);
        capture.onClick = [this] { p.setCapturing (capture.getToggleState()); capture.setButtonText (capture.getToggleState() ? "Stop capturing" : "Capture from host");
                                   if (capture.getToggleState()) p.setStatusText ("Capturing: press play in your host and play or loop your MIDI clip."); else p.setStatusText ("Melody captured."); };
        clear.onClick = [this] { p.clearNotes(); };
        openMidi.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Open a MIDI file", juce::File(), "*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (! f.existsAsFile()) return;
                juce::MemoryBlock mb; f.loadFileAsData (mb);
                juce::Component::SafePointer<SingPage> self (this);
                juce::Thread::launch ([self, mb, this] {
                    double tempo = 120; std::vector<NoteEv> notes; juce::String err;
                    bool ok = p.engine.parseMidi (mb, tempo, notes, err);
                    juce::MessageManager::callAsync ([self, ok, tempo, notes, err, this] { if (self == nullptr) return;
                        if (ok) { p.setNotes (notes, tempo); p.setStatusText (juce::String (notes.size()) + " notes loaded from the file."); }
                        else p.setStatusText (err); });
                });
            });
        };
        render.onClick = [this] { p.renderAsync(); };
        lyrics.setMultiLine (true, true); lyrics.setReturnKeyStartsNewLine (true); lyrics.setFont (ui (15.0f));
        lyrics.setTextToShowWhenEmpty ("Type or paste a lyric line...", mute);
        lyrics.setText (p.lyrics(), false);
        lyrics.onTextChange = [this] { p.setLyrics (lyrics.getText()); };
        for (int i = 0; i < 3; ++i)
        {
            addAndMakeVisible (voice[i]); addAndMakeVisible (w[i]);
            const juce::Colour cc[3] = { sun, pink, teal };
            w[i].setSliderStyle (juce::Slider::LinearHorizontal); w[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            w[i].setColour (juce::Slider::trackColourId, cc[i]);
            wAtt[i] = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (i == 0 ? "w1" : (i == 1 ? "w2" : "w3")), w[i]);
            w[i].onValueChange = [this] { repaint(); };
            voice[i].onChange = [this, i] { int id = voice[i].getSelectedId(); if (id > 0 && id <= (int) shown.size()) p.setVoiceId (i, shown[(size_t) id - 1].id); };
        }
        if (auto xml = juce::XmlDocument::parse (mascotSvg)) mascot = juce::Drawable::createFromSVG (*xml);
        sync();
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        label (g, "MODE", { 14, 4, 50, 28 }, mute, 11.0f);
        label (g, mode.index == 0 ? "Your MIDI melody decides the notes. The singer follows it. Your own pitch is never used."
                                  : "The singer invents phrasing around your lyrics. The melody still comes from MIDI, never from your voice.",
               { 340, 4, 900, 28 }, mute, 12.0f, false);
        auto notes = p.getNotes(); double end = 0;
        for (auto& n : notes) end = juce::jmax (end, n.startBeats + n.lengthBeats);
        label (g, "Tempo " + juce::String (p.getHostTempo(), 0) + " BPM (host)   Notes " + juce::String ((int) notes.size()) + "   Length " + juce::String (end / 4.0, 1) + " bars",
               { 26, 220, 380, 20 }, mute, 11.0f, false);
        g.setColour (mute); g.setFont (ui (11.0f));
        g.drawFittedText ("Capture: tick the button, press play in your host, and play or loop your MIDI clip once. Or open a .mid file. The melody always comes from here, never from your voice.",
                          { 26, 340, 380, 60 }, juce::Justification::topLeft, 4);
        auto ns = (int) chipsFor (p.lyrics()).size(); auto cnt = (int) notes.size();
        juce::String fit = cnt == 0 ? "No melody yet: syllables are checked against the notes once you have them."
                                    : (ns == cnt ? juce::String (ns) + " syllables, " + juce::String (cnt) + " notes: fits"
                                                 : juce::String (ns) + " syllables, " + juce::String (cnt) + " notes: " + (ns < cnt ? juce::String (cnt - ns) + " short" : juce::String (ns - cnt) + " too many"));
        label (g, fit, { 436, 176, 480, 22 }, cnt == 0 ? mute : (ns == cnt ? mint : (ns < cnt ? sun : pink)), 12.0f);
        label (g, juce::String (juce::roundToInt (w[0].getValue() * 100)) + "% / " + juce::String (juce::roundToInt (w[1].getValue() * 100)) + "% / " + juce::String (juce::roundToInt (w[2].getValue() * 100)) + "%  (normalised by the engine)",
               { 948, 470, 300, 20 }, mute, 11.0f, false);
        g.setColour (mute); g.setFont (ui (11.0f));
        g.drawFittedText (blendMode.index == 0 ? "Layer: each voice sings the whole part, then they are mixed by weight. Works now."
                                               : "One new voice: the engine merges the voices. Experimental, may not work yet.",
                          { 948, 520, 296, 40 }, juce::Justification::topLeft, 3);
        label (g, p.statusText(), { 190, 618, 760, 24 }, mute, 12.0f, false);
        label (g, "Render, then press play in your host.", { 948, 590, 284, 20 }, mute, 11.0f, false, juce::Justification::centredRight);
        if (mascot != nullptr) mascot->drawWithin (g, { 1018.0f, 168.0f, 112.0f, 112.0f }, juce::RectanglePlacement::centred, 1.0f);
        const char* names[3] = { "Voice 1", "Voice 2", "Voice 3" };
        for (int i = 0; i < 3; ++i) label (g, names[i], { 948, 296 + i * 56, 80, 14 }, mute, 10.0f);
    }
    void resized() override
    {
        mode.setBounds (68, 6, 250, 26);
        melody.setBounds (0, 40, 416, 500); lyr.setBounds (424, 40, 504, 500); blend.setBounds (936, 40, 320, 500);
        // children use page coordinates (page is 1280 wide, 700 tall)
        roll.setBounds (12, 74, 392, 140);
        capture.setBounds (12, 246, 150, 30); openMidi.setBounds (168, 246, 130, 30); clear.setBounds (304, 246, 100, 30);
        chips.setBounds (436, 74, 480, 96);
        lyrics.setBounds (436, 204, 480, 300);
        bar.setBounds (948, 76, 296, 14);
        for (int i = 0; i < 3; ++i) { voice[i].setBounds (1028, 292 + i * 56, 216, 24); w[i].setBounds (948, 318 + i * 56, 296, 22); }
        blendMode.setBounds (948, 492, 296, 26);
        bottom.setBounds (0, 548, 1256, 100);
        render.setBounds (14, 576, 160, 44); wave.setBounds (190, 564, 760, 52);
    }
    void sync() override
    {
        mode.setIndex ((int) pv (p, "mode")); blendMode.setIndex ((int) pv (p, "blend"));
        auto v = p.getVoices();
        bool changed = v.size() != shown.size();
        for (size_t i = 0; ! changed && i < v.size(); ++i) changed = v[i].id != shown[i].id;
        if (changed)
        {
            shown = v; auto ids = p.getVoiceIds();
            for (int i = 0; i < 3; ++i)
            {
                voice[i].clear (juce::dontSendNotification);
                for (size_t k = 0; k < shown.size(); ++k) voice[i].addItem (shown[k].name, (int) k + 1);
                for (size_t k = 0; k < shown.size(); ++k) if (shown[k].id == ids[(size_t) i]) voice[i].setSelectedId ((int) k + 1, juce::dontSendNotification);
            }
        }
        render.setEnabled (p.renderState.load() != 1);
        render.setButtonText (p.renderState.load() == 1 ? "Rendering..." : "Render");
        capture.setToggleState (p.isCapturing(), juce::dontSendNotification);
        repaint (0, 0, 1280, 40);
    }
};

struct FxPage : Page
{
    PurrProcessor& p;
    Panel robotP { "ROBOT VOICE", sky, Ico::robot }, sawP { "SUPERSAW VOICE", violet, Ico::layers }, dirtP { "DIRT", pink, Ico::flame };
    Seg robotOn { { "Off", "On" }, sky }, sawOn { { "Off", "On" }, violet }, dirtOn { { "Off", "On" }, pink };
    RobotView robotV { p }; SawView sawV { p }; DirtView dirtV { p };
    std::vector<std::unique_ptr<Knob>> knobs;

    Knob* add (const char* id, const char* t, const char* h, juce::Colour c, juce::Rectangle<int> r, bool pct = true)
    {
        knobs.push_back (std::make_unique<Knob> (p, id, t, h, c, pct)); addAndMakeVisible (*knobs.back()); knobs.back()->setBounds (r); return knobs.back().get();
    }
    explicit FxPage (PurrProcessor& pr) : p (pr)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &robotP, &sawP, &dirtP, &robotOn, &sawOn, &dirtOn, &robotV, &sawV, &dirtV }) addAndMakeVisible (c);
        robotOn.onChange = [this] (int i) { setP (p, "robot_on", (float) i); };
        sawOn.onChange = [this] (int i) { setP (p, "saw_on", (float) i); };
        dirtOn.onChange = [this] (int i) { setP (p, "dirt_on", (float) i); };
        const int cw = 404, kx = 14, kw = cw - 28; int y0 = 120;
        robotP.setBounds (0, 0, cw, 600); sawP.setBounds (cw + 8, 0, cw, 600); dirtP.setBounds (2 * (cw + 8), 0, cw, 600);
        robotOn.setBounds (cw - 110, 6, 96, 26); sawOn.setBounds (cw + 8 + cw - 110, 6, 96, 26); dirtOn.setBounds (2 * (cw + 8) + cw - 110, 6, 96, 26);
        robotV.setBounds (14, 42, cw - 28, 90); sawV.setBounds (cw + 8 + 14, 42, cw - 28, 90); dirtV.setBounds (2 * (cw + 8) + 14, 42, cw - 28, 90);
        y0 = 144;
        add ("robot_speed", "Retune speed", "Left: natural glide. Right: instant, hard robot", sky, { kx, y0, kw, 64 });
        add ("robot_mix", "Dry / wet", "Left: clean AI voice. Right: all robot", sky, { kx, y0 + 66, kw, 64 });
        int sx = cw + 8 + kx;
        add ("saw_copies", "Copies", "Voices stacked, 3 to 9", violet, { sx, y0, kw, 64 }, false);
        add ("saw_detune", "Detune", "Up to 50 cents on the outer copies", violet, { sx, y0 + 66, kw, 64 });
        add ("saw_spread", "Spread", "Pan the copies across left and right", violet, { sx, y0 + 132, kw, 64 });
        add ("saw_widen", "Widen", "Extra side level for a bigger image", violet, { sx, y0 + 198, kw, 64 });
        add ("saw_mix", "Stack mix", "Left: untouched. Right: all stack", violet, { sx, y0 + 264, kw, 64 });
        int dx = 2 * (cw + 8) + kx;
        add ("dirt_drive", "Drive (tube)", "Warm saturation up to heavy distortion", pink, { dx, y0, kw, 64 });
        add ("dirt_crush", "Bitcrush", "Fewer bits and a lower sample rate", pink, { dx, y0 + 66, kw, 64 });
        add ("dirt_arts", "Artifacts", "Stutters, dropouts, buzz bursts", pink, { dx, y0 + 132, kw, 64 });
        add ("dirt_mix", "Dirt mix", "Left: untouched. Right: all dirt", pink, { dx, y0 + 198, kw, 64 });
        sync();
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (mute); g.setFont (ui (11.0f));
        g.drawFittedText ("The robot sound comes from snapping every note exactly and removing the natural wobble. Dry / wet blends it with the clean singer.", { 14, 480, 376, 60 }, juce::Justification::topLeft, 3);
        g.drawFittedText ("Artifacts repeat the same way every render, so a render you like can be redone exactly.", { 2 * 412 + 14, 420, 376, 40 }, juce::Justification::topLeft, 2);
        g.setColour (mute); g.drawFittedText ("These settings are applied by the engine when you press Render on the Sing tab.", { 412 + 14, 500, 376, 40 }, juce::Justification::topLeft, 2);
    }
    void sync() override { robotOn.setIndex (pv (p, "robot_on") > 0.5f); sawOn.setIndex (pv (p, "saw_on") > 0.5f); dirtOn.setIndex (pv (p, "dirt_on") > 0.5f); }
};

struct LyricsPage : Page
{
    PurrProcessor& p; Panel pad { "NOTEPAD", sky, Ico::pen }, check { "LINE CHECK", mint, Ico::wave };
    juce::TextEditor ed;
    explicit LyricsPage (PurrProcessor& pr) : p (pr)
    {
        addAndMakeVisible (pad); addAndMakeVisible (check); addAndMakeVisible (ed);
        pad.setBounds (0, 0, 840, 600); check.setBounds (848, 0, 420, 600);
        ed.setMultiLine (true, false); ed.setReturnKeyStartsNewLine (true); ed.setFont (ui (19.0f));
        ed.setTextToShowWhenEmpty ("One line per phrase. Type or paste your lyrics...", mute);
        ed.setBounds (14, 40, 812, 500); ed.setText (p.lyrics(), false);
        ed.onTextChange = [this] { p.setLyrics (ed.getText()); repaint(); };
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        auto lines = juce::StringArray::fromLines (ed.getText());
        int totalSyl = 0, y = 44;
        for (int i = 0; i < lines.size() && i < 14; ++i)
        {
            int n = countLine (lines[i]); totalSyl += n;
            auto row = juce::Rectangle<int> (862, y, 392, 34);
            g.setColour (well); g.fillRoundedRectangle (row.toFloat(), 10); g.setColour (line); g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 10, 1.0f);
            label (g, juce::String (i + 1), row.withWidth (30), mute, 12.0f, true, juce::Justification::centred);
            label (g, lines[i].isEmpty() ? "(empty)" : lines[i], row.withTrimmedLeft (32).withTrimmedRight (90), ink, 13.0f, false);
            label (g, juce::String (n) + " syl", row.withTrimmedLeft (row.getWidth() - 84), sky, 12.0f, true, juce::Justification::centredRight);
            y += 38;
        }
        auto cnt = (int) p.getNotes().size();
        label (g, cnt == 0 ? "Total " + juce::String (totalSyl) + " syllables. No melody yet."
                           : "Total " + juce::String (totalSyl) + " syllables, " + juce::String (cnt) + " notes: " + (totalSyl == cnt ? "fits" : (totalSyl < cnt ? "short" : "too many")),
               { 862, 590 - 40, 392, 24 }, cnt && totalSyl == cnt ? mint : sun, 12.0f);
        g.setColour (mute); g.setFont (ui (11.0f));
        g.drawFittedText ("AI helper (continue a line, rhymes, fit to the notes) needs the local model. It is on the roadmap and not wired yet.", { 862, 480, 392, 40 }, juce::Justification::topLeft, 3);
    }
    size_t lastNotes = 0;
    void sync() override { if (! ed.hasKeyboardFocus (true) && ed.getText() != p.lyrics()) ed.setText (p.lyrics(), false); if (p.getNotes().size() != lastNotes) { lastNotes = p.getNotes().size(); repaint(); } }
};

struct VoicesPage : Page
{
    PurrProcessor& p; Panel lib { "VOICE LIBRARY", sun, Ico::users }, det { "VOICE DETAILS", mint, Ico::mic };
    juce::TextButton import { "Import .wav..." }, del { "Delete selected" }, refresh { "Refresh" }, use[3];
    std::unique_ptr<juce::FileChooser> chooser; int sel = 0;
    explicit VoicesPage (PurrProcessor& pr) : p (pr)
    {
        addAndMakeVisible (lib); addAndMakeVisible (det);
        for (auto* b : { &import, &del, &refresh }) { addAndMakeVisible (b); styleButton (*b); }
        styleButton (import, mint, bg);
        lib.setBounds (0, 0, 620, 600); det.setBounds (628, 0, 628, 600);
        import.setBounds (14, 548, 150, 32); del.setBounds (172, 548, 150, 32); refresh.setBounds (330, 548, 110, 32);
        for (int i = 0; i < 3; ++i) { addAndMakeVisible (use[i]); styleButton (use[i]); use[i].setButtonText ("Use as voice " + juce::String (i + 1)); use[i].setBounds (642 + i * 204, 300, 192, 32);
            use[i].onClick = [this, i] { auto v = p.getVoices(); if (sel < (int) v.size()) { p.setVoiceId (i, v[(size_t) sel].id); repaint(); } }; }
        refresh.onClick = [this] { juce::Component::SafePointer<VoicesPage> s (this); juce::Thread::launch ([s, this] { p.refreshVoices(); juce::MessageManager::callAsync ([s] { if (s) s->repaint(); }); }); };
        del.onClick = [this] {
            auto v = p.getVoices(); if (sel >= (int) v.size() || v[(size_t) sel].builtin) { p.setStatusText ("Built-in test voices cannot be deleted."); repaint(); return; }
            auto id = v[(size_t) sel].id; juce::Component::SafePointer<VoicesPage> s (this);
            juce::Thread::launch ([s, this, id] { juce::String err; if (! p.engine.deleteVoice (id, err)) p.setStatusText (err); p.refreshVoices();
                juce::MessageManager::callAsync ([s] { if (s) { s->sel = 0; s->repaint(); } }); }); };
        import.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Choose a clean .wav clip (1 to 60 seconds)", juce::File(), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (! f.existsAsFile()) return;
                auto* a = new juce::AlertWindow ("Add a voice", "Only add voices you own or have permission or a licence for.", juce::MessageBoxIconType::NoIcon);
                a->addTextEditor ("name", f.getFileNameWithoutExtension(), "Name");
                a->addComboBox ("rights", { "own-voice", "permission", "licensed", "cc0", "cc-by", "public-domain" }, "Rights");
                a->addTextBlock ("Tick the box below only if this is true.");
                auto* tick = new juce::ToggleButton ("I have the rights to this voice, or the permission of the person whose voice it is.");
                tick->setSize (480, 24); a->addCustomComponent (tick);
                a->addButton ("Add voice", 1, juce::KeyPress (juce::KeyPress::returnKey)); a->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                juce::Component::SafePointer<VoicesPage> s (this);
                a->enterModalState (true, juce::ModalCallbackFunction::create ([s, this, a, tick, f] (int r) {
                    if (r != 1 || s == nullptr) return;
                    auto name = a->getTextEditorContents ("name"); auto rights = a->getComboBoxComponent ("rights")->getText(); bool ok = tick->getToggleState();
                    juce::MemoryBlock mb; f.loadFileAsData (mb);
                    juce::Thread::launch ([s, this, name, rights, ok, mb] { juce::String err; bool good = p.engine.importVoice (name, rights, ok, mb, err);
                        p.setStatusText (good ? "Voice added." : err); p.refreshVoices(); juce::MessageManager::callAsync ([s] { if (s) s->repaint(); }); });
                }), true);
            });
        };
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto v = p.getVoices(); int row = (e.y - 44) / 60;
        if (e.x < 620 && e.y >= 44 && row >= 0 && row < (int) v.size()) { sel = row; repaint(); }
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        auto v = p.getVoices();
        if (v.empty()) label (g, "No voices yet: is the engine running?", { 20, 50, 580, 30 }, mute, 13.0f, false);
        const juce::Colour cc[] = { sun, pink, teal, sky, violet, mint };
        for (size_t i = 0; i < v.size() && i < 8; ++i)
        {
            auto row = juce::Rectangle<int> (14, 44 + (int) i * 60, 592, 52); auto c = v[i].builtin ? sky : cc[i % 6];
            g.setColour (well); g.fillRoundedRectangle (row.toFloat(), 12);
            g.setColour ((int) i == sel ? c : line); g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 12, (int) i == sel ? 2.0f : 1.0f);
            g.setColour (c); g.fillEllipse ((float) row.getX() + 8, (float) row.getY() + 8, 36, 36);
            drawIcon (g, v[i].builtin ? Ico::wave : Ico::mic, { (float) row.getX() + 17, (float) row.getY() + 17, 18, 18 }, bg);
            label (g, v[i].name, { row.getX() + 54, row.getY() + 6, 330, 22 }, ink, 14.0f);
            label (g, v[i].builtin ? "synthetic, no clips" : juce::String (v[i].clips) + " clips, " + juce::String (juce::roundToInt (v[i].seconds)) + " s", { row.getX() + 54, row.getY() + 28, 330, 18 }, mute, 11.0f, false);
            label (g, v[i].rights, { row.getRight() - 150, row.getY(), 140, row.getHeight() }, c, 12.0f, true, juce::Justification::centredRight);
        }
        label (g, "AI Purrformer ships no one else's voice. Add your own, or voices you have permission or a licence for.", { 14, 510, 592, 32 }, mute, 11.0f, false);
        if (sel < (int) v.size() && ! v.empty())
        {
            auto& s = v[(size_t) sel];
            label (g, s.name, { 650, 48, 580, 28 }, ink, 18.0f);
            label (g, "rights: " + s.rights + (s.builtin ? "   (built-in test voice)" : "") + "   clips: " + juce::String (s.clips), { 650, 80, 580, 22 }, mint, 12.0f);
            label (g, "id: " + s.id, { 650, 106, 580, 20 }, mute, 11.0f, false);
        }
        auto ids = p.getVoiceIds(); int y = 350;
        label (g, "VOICES IN THE BLEND (set weights on the Sing tab)", { 650, y - 4, 580, 20 }, violet, 11.5f);
        for (int i = 0; i < 3; ++i)
        {
            juce::String nm = ids[(size_t) i]; for (auto& x : v) if (x.id == ids[(size_t) i]) nm = x.name;
            label (g, "Voice " + juce::String (i + 1) + ": " + (nm.isEmpty() ? "(empty)" : nm), { 650, y + 24 + i * 26, 580, 22 }, ink, 13.0f, false);
        }
        label (g, "Voice folder (engine side): ~/.local/share/purrformer/voices", { 650, 520, 580, 20 }, mute, 11.0f, false);
        label (g, p.statusText(), { 650, 548, 580, 24 }, sun, 12.0f, false);
    }
    std::uint64_t lastSig = 0;
    void sync() override
    {
        std::uint64_t h = 11; auto v = p.getVoices(); hashIn (h, (double) v.size()); hashIn (h, sel);
        for (auto& x : v) { hashIn (h, (double) x.id.hashCode()); hashIn (h, x.clips); }
        hashIn (h, (double) p.statusText().hashCode()); for (auto& id : p.getVoiceIds()) hashIn (h, (double) id.hashCode());
        if (h != lastSig) { lastSig = h; repaint(); }
    }
};

// ---------- header tabs ---------------------------------------------------------------------------
struct Tab : juce::Component
{
    juce::String text; Ico ico; bool on = false; std::function<void()> onClick;
    Tab (juce::String t, Ico i) : text (std::move (t)), ico (i) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (on) { g.setGradientFill (juce::ColourGradient (violet, r.getX(), 0, pink, r.getRight(), 0, false)); g.fillRoundedRectangle (r, r.getHeight() / 2); }
        else { g.setColour (panel); g.fillRoundedRectangle (r, r.getHeight() / 2); g.setColour (line); g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f); }
        drawIcon (g, ico, { r.getX() + 12, r.getCentreY() - 8, 16, 16 }, on ? bg : mute);
        label (g, text, getLocalBounds().withTrimmedLeft (34), on ? bg : mute, 12.5f);
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
};
} // namespace

struct PurrEditor::Impl
{
    PurrProcessor& p;
    struct Content : juce::Component
    {
        PurrProcessor& p; std::unique_ptr<juce::Drawable> mascot;
        explicit Content (PurrProcessor& pr) : p (pr) { if (auto xml = juce::XmlDocument::parse (mascotSvg)) mascot = juce::Drawable::createFromSVG (*xml); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (col::bg);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3A1B9A), 640.0f, -60.0f, col::bg, 640.0f, 380.0f, true)); g.fillRect (getLocalBounds());
            for (int side = 0; side < 2; ++side)
            {
                int x0 = side ? 1270 : 0;
                for (int i = 0; i < 10; ++i) { g.setColour (juce::Colour (i % 4 == 0 ? 0xff5E4029 : (i % 4 == 1 ? 0xff7B5736 : 0xff6B4A2F))); g.fillRect (x0 + i, 0, 1, getHeight()); }
            }
            if (mascot) mascot->drawWithin (g, { 16.0f, 8.0f, 48.0f, 48.0f }, juce::RectanglePlacement::centred, 1.0f);
            g.setGradientFill (juce::ColourGradient (col::violet, 72.0f, 0.0f, col::sun, 240.0f, 0.0f, false));
            g.setFont (ui (22.0f, true)); g.drawText ("AI Purrformer", 72, 12, 190, 36, juce::Justification::centredLeft);
            bool up = p.engineUp.load();
            auto pill = juce::Rectangle<float> (790.0f, 16.0f, 200.0f, 30.0f); auto c = up ? col::mint : col::pink;
            g.setColour (c.withAlpha (0.12f)); g.fillRoundedRectangle (pill, 15.0f); g.setColour (c.withAlpha (0.6f)); g.drawRoundedRectangle (pill, 15.0f, 1.0f);
            g.setColour (c); g.fillEllipse (pill.getX() + 12, pill.getCentreY() - 4, 8, 8);
            label (g, up ? "engine connected" : "engine offline", pill.toNearestInt().withTrimmedLeft (28), c, 12.0f);
        }
    } content;

    Tab tabs[4] = { { "Sing", Ico::mic }, { "Voice FX", Ico::sliders }, { "Lyrics", Ico::pen }, { "Voices", Ico::users } };
    SingPage sing; FxPage fx; LyricsPage lyr; VoicesPage voices;
    Page* pages[4];
    juce::ComboBox backend; juce::StringArray lastBackends;
    int current = 0;

    explicit Impl (PurrProcessor& pr) : p (pr), content (pr), sing (pr), fx (pr), lyr (pr), voices (pr)
    {
        pages[0] = &sing; pages[1] = &fx; pages[2] = &lyr; pages[3] = &voices;
        for (int i = 0; i < 4; ++i)
        {
            content.addAndMakeVisible (tabs[i]); tabs[i].setBounds (250 + i * 124, 14, 116, 34);
            tabs[i].onClick = [this, i] { select (i); };
            content.addAndMakeVisible (*pages[i]); pages[i]->setBounds (12, 64, 1256, 650);
        }
        content.addAndMakeVisible (backend); backend.setBounds (1010, 16, 160, 30);
        backend.onChange = [this] { p.setBackend (backend.getText()); };
        select (juce::jlimit (0, 3, juce::SystemStats::getEnvironmentVariable ("PURR_TAB", "0").getIntValue()));
    }
    void select (int i) { current = i; for (int k = 0; k < 4; ++k) { pages[k]->setVisible (k == i); tabs[k].on = (k == i); tabs[k].repaint(); } }
};

PurrEditor::PurrEditor (PurrProcessor& pr) : AudioProcessorEditor (pr), impl (std::make_unique<Impl> (pr)), proc (pr)
{
    setLookAndFeel (&look);
    addAndMakeVisible (impl->content);
    impl->content.setSize (1280, 720);
    setResizable (true, true); setResizeLimits (640, 360, 2560, 1440);
    getConstrainer()->setFixedAspectRatio (1280.0 / 720.0);
    setSize (1000, 563);
    startTimerHz (8);
}

PurrEditor::~PurrEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PurrEditor::resized()
{
    float s = (float) getWidth() / 1280.0f;
    impl->content.setTransform (juce::AffineTransform::scale (s));
}

void PurrEditor::paint (juce::Graphics& g)
{
    // woodgrain side cheeks go on top of the scaled content in paintOverChildren-like fashion: draw backdrop here
    g.fillAll (col::bg);
}

void PurrEditor::timerCallback()
{
    auto& I = *impl;
    I.pages[I.current]->sync();
    juce::StringArray b; { b = proc.backendNames; }
    if (b != I.lastBackends)
    {
        I.lastBackends = b; I.backend.clear (juce::dontSendNotification);
        int id = 1, pick = 0;
        for (auto& n : b) { I.backend.addItem (n, id); if (n == proc.getBackend()) pick = id; ++id; }
        if (pick) I.backend.setSelectedId (pick, juce::dontSendNotification);
    }
    I.content.repaint (0, 0, 1280, 64);
}

// Header painting lives on the content component through a lambda-free subclass trick: we paint on the editor itself.
} // namespace purr
