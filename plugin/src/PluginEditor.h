// AI Purrformer plugin: editor. AGPL-3.0.
#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

namespace purr
{
class PurrEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PurrEditor (PurrProcessor&);
    ~PurrEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Impl;
    void timerCallback() override;
    std::unique_ptr<Impl> impl;
    NeonLook look;
    PurrProcessor& proc;
};
} // namespace purr
