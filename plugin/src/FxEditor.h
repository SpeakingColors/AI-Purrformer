// AI Purrformer FX plugin: editor. AGPL-3.0.
#pragma once
#include "FxProcessor.h"
#include "Theme.h"

namespace purr
{
class FxEditor : public juce::AudioProcessorEditor
{
public:
    explicit FxEditor (FxProcessor&);
    ~FxEditor() override;
    void resized() override;
    void refreshForScreenshot();               // draws a frame even when the window is not on screen

    static constexpr int baseW = 860, baseH = 810;

private:
    struct Content;
    NeonLook look;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::ResizableBorderComponent> border;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    FxProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxEditor)
};
} // namespace purr
