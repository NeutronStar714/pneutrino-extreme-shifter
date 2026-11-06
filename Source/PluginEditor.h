#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Custom rotary-slider look-and-feel — Pneutrino series style
class ExtremeShifterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ExtremeShifterLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return juce::Font (juce::FontOptions ("Courier New", 20.0f, juce::Font::bold));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExtremeShifterLookAndFeel)
};

//==============================================================================
// Live mix bar graph: one 5 px bar per element (dry + 3 voices), x = its
// shift on the -24..+12 st axis, height = its loudness in the mix.
// Overlapping bars are z-ordered so the SHORTER one floats on top.
class MixGraph final : public juce::Component
{
public:
    // Note: JUCE_DECLARE_NON_COPYABLE below suppresses the implicit default
    // ctor, so it must be declared explicitly (the editor holds one as a member)
    MixGraph() = default;

    struct Bar
    {
        float shiftSt;      // x position on the semitone axis
        float level;        // 0..1 display height
        juce::Colour colour;
    };

    void setBars (const Bar* bars, int numBars)
    {
        this->bars.assign (bars, bars + numBars);
        repaint();
    }

    void paint (juce::Graphics&) override;

private:
    static constexpr float shiftMin = -24.0f;
    static constexpr float shiftMax = 12.0f;
    std::vector<Bar> bars;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixGraph)
};

//==============================================================================
class PolyShifterAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit PolyShifterAudioProcessorEditor (PolyShifterAudioProcessor&);
    ~PolyShifterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Reflects the current toggle state in the buttons' text
    void updateBlockButton();
    void updateStepButton();

    // "Latency: xx.x ms" for the active STFT block size
    void refreshLatencyLabel();

    // A rotary knob (value text box + name label underneath)
    class KnobControl  : public juce::Component
    {
    public:
        KnobControl (PolyShifterAudioProcessor&, ExtremeShifterLookAndFeel&,
                     const juce::String& paramID, const juce::String& displayName,
                     double defaultValue, std::function<juce::String (float)> valueFormatter,
                     int textBoxWidth = 48);

        void resized() override;
        void resetLookAndFeel();

        // Re-render the value text box through the formatter (used when a
        // format that depends on another parameter changes, e.g. STEP/SMOOTH)
        void refreshText();

    private:
        juce::Slider slider;
        juce::Label nameLabel;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
        std::function<juce::String (float)> formatValue;
    };

    PolyShifterAudioProcessor& processorRef;
    ExtremeShifterLookAndFeel lnf;

    // Voice 1
    KnobControl balance1Knob;
    KnobControl semitone1Knob;
    KnobControl fine1Knob;
    KnobControl tone1Knob;

    // Voice 2
    KnobControl balance2Knob;
    KnobControl semitone2Knob;
    KnobControl fine2Knob;
    KnobControl tone2Knob;

    // Voice 3 (sub octave: -24..0 st, default -12)
    KnobControl balance3Knob;
    KnobControl semitone3Knob;
    KnobControl fine3Knob;
    KnobControl tone3Knob;

    // Header & attribution
    juce::Label nameLabel;
    juce::Label attributionLabel;

    // Voice box captions
    juce::Label voiceHeaderLbl[3];

    // Pitch response toggle (STEP default / SMOOTH)
    juce::TextButton stepButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment stepAttachment;

    // STFT block size toggle (2048 default / 4096)
    juce::TextButton blockButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment blockAttachment;

    // Live latency readout between the two toggles
    juce::Label latencyLabel;

    // Live mix bar graph (dry + 3 voices) under the button row
    MixGraph mixGraph;

    // Cached parameter pointers for the graph's bar positions (STEP/SMOOTH
    // aware shift of each voice)
    std::atomic<float>* stepModeAtomic { nullptr };
    std::atomic<float>* semiAtomic[3]  { nullptr, nullptr, nullptr };
    std::atomic<float>* fineAtomic[3]  { nullptr, nullptr, nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyShifterAudioProcessorEditor)
};
