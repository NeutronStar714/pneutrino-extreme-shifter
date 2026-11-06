#include "PluginEditor.h"

#include <cmath>

//==============================================================================
// Colour palette — Pneutrino series layout, Extreme Shifter's warm theme.
// (Role mapping from Dynamic Resonator: accent drives the knob arc/tick,
// button fill and box borders; panelBg fills the recessed wells; muteText
// outlines the knobs; the header uses the lightest accent tint.)
static const juce::Colour accentColour   { 0xFF543526 };   // theme: button off fill, stroke (voice 3)
static const juce::Colour accentMid      { 0xFF6E4833 };   // stroke (voice 2) - midpoint of V1/V3 strokes
static const juce::Colour accentBright   { 0xFF8A5A43 };   // knob arc/tick, stroke (voice 1), attribution, button-on text
static const juce::Colour accentLightest { 0xFFA5754F };   // header name
static const juce::Colour darkBg         { 0xFF1A0F09 };   // window background
static const juce::Colour panelBg        { 0xFF241710 };   // knob well, button-on fill
static const juce::Colour textColour     { 0xFFEDE0CC };   // primary text (cream)
static const juce::Colour muteText       { 0xFF9C8674 };   // secondary text, knob outline
static const juce::Colour gridColour     { 0xFF33231A };   // graph panel border/baseline

// Voice panel strokes walk the theme family bright -> deep
static const juce::Colour panelStrokes[3] = { accentBright, accentMid, accentColour };

// Mix-graph bar colours = the panel strokes; dry is a brown brighter than
// voice 1's stroke
static const juce::Colour dryBarColour { 0xFFC08A6A };

//==============================================================================
// ExtremeShifterLookAndFeel
//==============================================================================
ExtremeShifterLookAndFeel::ExtremeShifterLookAndFeel()
    : LookAndFeel_V4()
{
    setColour (juce::Slider::rotarySliderFillColourId,     accentBright);
    setColour (juce::Slider::rotarySliderOutlineColourId,  muteText);
    setColour (juce::Slider::textBoxTextColourId,          textColour);
    setColour (juce::Slider::textBoxBackgroundColourId,    darkBg);
    setColour (juce::Slider::textBoxOutlineColourId,       darkBg);
    setColour (juce::Label::textColourId,                  textColour);
}

void ExtremeShifterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                      const juce::Colour& /*backgroundColour*/,
                                                      bool /*shouldDrawButtonAsHighlighted*/,
                                                      bool /*shouldDrawButtonAsDown*/)
{
    // Fill from the toggle state, border always in the theme's bright copper -
    // the default LookAndFeel_V4 derives the border as a contrast colour,
    // which falls outside the theme family.
    const auto bounds = button.getLocalBounds().toFloat();
    g.setColour (button.getToggleState() ? panelBg : accentColour);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (accentBright);
    g.drawRoundedRectangle (bounds, 6.0f, 2.5f);
}

void ExtremeShifterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                                  int width, int height,
                                                  float sliderPos,
                                                  float rotaryStartAngle,
                                                  float rotaryEndAngle,
                                                  juce::Slider& /*s*/)
{
    const float diameter = float (juce::jmin (width, height)) - 8.0f;
    const float cx       = float (x) + float (width)  * 0.5f;
    const float cy       = float (y) + float (height) * 0.5f;
    const float radius   = diameter * 0.5f * 0.75f;
    const float lineW    = radius * 0.17f;
    const float borderW  = lineW * 0.5f;

    // Recessed well
    g.setColour (panelBg);
    g.fillEllipse (cx - radius, cy - radius, diameter * 0.75f, diameter * 0.75f);

    g.setColour (muteText);
    g.drawEllipse (cx - radius, cy - radius, diameter * 0.75f, diameter * 0.75f, borderW);

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Value arc
    juce::Path arc;
    arc.addCentredArc (cx, cy, radius - borderW, radius - borderW,
                       0.0f, rotaryStartAngle, angle, true);
    g.setColour (accentBright);
    g.strokePath (arc, juce::PathStrokeType (borderW, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));

    // Tick (pointer)
    juce::Path tick;
    tick.startNewSubPath (cx + radius * 0.25f * std::sin (angle),
                          cy - radius * 0.25f * std::cos (angle));
    tick.lineTo (cx + (radius - borderW - 1.0f) * std::sin (angle),
                 cy - (radius - borderW - 1.0f) * std::cos (angle));
    const float tickW = lineW * 0.9f;
    g.setColour (accentBright);
    g.strokePath (tick, juce::PathStrokeType (tickW, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));
}

//==============================================================================
// MixGraph
//==============================================================================
void MixGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (panelBg);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (gridColour);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);

    const float plotLeft   = area.getX() + 26.0f;
    const float plotRight  = area.getRight() - 26.0f;
    const float plotTop    = area.getY() + 8.0f;
    const float plotBottom = area.getBottom() - 14.0f;
    const float plotW      = plotRight - plotLeft;
    const float plotH      = plotBottom - plotTop;

    auto stToX = [&] (float st)
    {
        return plotLeft + (st - shiftMin) / (shiftMax - shiftMin) * plotW;
    };

    // Baseline + number line: one tick per st (labelled every 6 st), all in
    // the same length and the same muted colour as the labels
    g.setColour (muteText);
    g.drawHorizontalLine (int (plotBottom), plotLeft, plotRight);
    const juce::Font axisFont (juce::FontOptions (9.0f));
    g.setFont (axisFont);
    for (float st = shiftMin; st <= shiftMax + 0.01f; st += 1.0f)
    {
        const float x = stToX (st);
        g.drawVerticalLine (int (x), plotBottom, plotBottom + 3.0f);
        if (int (st) % 3 == 0)
        {
            // Centre each label on its tick - drawSingleLineText is
            // left-aligned, so labels of different widths would drift
            // right if all started at the same offset
            const int stInt = int (st);
            const juce::String label = juce::String (stInt >= 0 ? "+" : "") + juce::String (stInt);
            const float labelW = juce::GlyphArrangement::getStringWidth (axisFont, label);
            g.drawSingleLineText (label, int (x - labelW * 0.5f), int (plotBottom + 12.0f));
        }
    }

    // Bars: 5 px wide, height = level * plot height. Drawn tallest-first so
    // overlapping shorter bars land on top (shorter wins and floats).
    std::vector<const Bar*> sorted;
    sorted.reserve (bars.size());
    for (const auto& bar : bars)
        sorted.push_back (&bar);
    std::sort (sorted.begin(), sorted.end(),
               [] (const Bar* a, const Bar* b) { return a->level > b->level; });

    const float barW = 5.0f;
    for (const Bar* bar : sorted)
    {
        const float h = juce::jlimit (0.0f, 1.0f, bar->level) * plotH;
        if (h < 0.5f)
            continue;
        const float x = stToX (bar->shiftSt);
        g.setColour (bar->colour);
        g.fillRect (x - barW * 0.5f, plotBottom - h, barW, h);
    }
}

//==============================================================================
PolyShifterAudioProcessorEditor::KnobControl::KnobControl (PolyShifterAudioProcessor& processor,
                                                          ExtremeShifterLookAndFeel& lnf,
                                                          const juce::String& paramID,
                                                          const juce::String& displayName,
                                                          double defaultValue,
                                                          std::function<juce::String (float)> valueFormatter,
                                                          int textBoxWidth)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (processor.apvts, paramID, slider),
      formatValue (std::move (valueFormatter))
{
    slider.setName (displayName);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 16);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setWantsKeyboardFocus (true);
    slider.textFromValueFunction = [this] (double v) { return formatValue ((float) v); };
    slider.setLookAndFeel (&lnf);

    // The attachment (constructed above, in the init list) already pushed the
    // current value while textFromValueFunction was still null, so the text
    // box was painted with the raw float format. Re-render it through the
    // formatter now - this is what keeps restored values ("92%", "-9 st"...)
    // readable on every open, not just while dragging.
    slider.updateText();

    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setFont (juce::Font (juce::FontOptions ("Helvetica", 10.0f, juce::Font::bold)));
    nameLabel.setColour (juce::Label::textColourId, muteText);
    nameLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (slider);
    addAndMakeVisible (nameLabel);
}

void PolyShifterAudioProcessorEditor::KnobControl::refreshText()
{
    slider.updateText();
}

void PolyShifterAudioProcessorEditor::KnobControl::resized()
{
    auto bounds = getLocalBounds();
    nameLabel.setBounds (bounds.removeFromBottom (14));
    slider.setBounds (bounds);
}

void PolyShifterAudioProcessorEditor::KnobControl::resetLookAndFeel()
{
    slider.setLookAndFeel (nullptr);
}

//==============================================================================
PolyShifterAudioProcessorEditor::PolyShifterAudioProcessorEditor (PolyShifterAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      balance1Knob  (p, lnf, "V1BALANCE",  "Balance",  0.5,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      semitone1Knob (p, lnf, "V1SEMITONE", "Semitone", 12.0,
                     [step = p.apvts.getRawParameterValue ("STEPMODE")] (float v)
                     {
                         if (step->load() < 0.5f)   // STEP: whole semitones
                             return juce::String::formatted ("%+d st", juce::roundToInt (v));
                         return juce::String::formatted ("%+.2f st", v);   // SMOOTH: 2 d.p.
                     }, 64),
      fine1Knob     (p, lnf, "V1FINE",     "Fine",     0.0,
                     [] (float v) { return juce::String::formatted ("%+d ct", juce::roundToInt (v)); }),
      tone1Knob     (p, lnf, "V1TONE",     "Tone",     1.0,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      balance2Knob  (p, lnf, "V2BALANCE",  "Balance",  0.0,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      semitone2Knob (p, lnf, "V2SEMITONE", "Semitone", 0.0,
                     [step = p.apvts.getRawParameterValue ("STEPMODE")] (float v)
                     {
                         if (step->load() < 0.5f)   // STEP: whole semitones
                             return juce::String::formatted ("%+d st", juce::roundToInt (v));
                         return juce::String::formatted ("%+.2f st", v);   // SMOOTH: 2 d.p.
                     }, 64),
      fine2Knob     (p, lnf, "V2FINE",     "Fine",     0.0,
                     [] (float v) { return juce::String::formatted ("%+d ct", juce::roundToInt (v)); }),
      tone2Knob     (p, lnf, "V2TONE",     "Tone",     1.0,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      balance3Knob  (p, lnf, "V3BALANCE",  "Balance",  0.0,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      semitone3Knob (p, lnf, "V3SEMITONE", "Semitone", -12.0,
                     [step = p.apvts.getRawParameterValue ("STEPMODE")] (float v)
                     {
                         if (step->load() < 0.5f)   // STEP: whole semitones
                             return juce::String::formatted ("%+d st", juce::roundToInt (v));
                         return juce::String::formatted ("%+.2f st", v);   // SMOOTH: 2 d.p.
                     }, 64),
      fine3Knob     (p, lnf, "V3FINE",     "Fine",     0.0,
                     [] (float v) { return juce::String::formatted ("%+d ct", juce::roundToInt (v)); }),
      tone3Knob     (p, lnf, "V3TONE",     "Tone",     1.0,
                     [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }),
      stepAttachment (p.apvts, "STEPMODE", stepButton),
      blockAttachment (p.apvts, "BLOCK", blockButton)
{
    // Cached params for the mix graph's bar positions
    stepModeAtomic = p.apvts.getRawParameterValue ("STEPMODE");
    semiAtomic[0]  = p.apvts.getRawParameterValue ("V1SEMITONE");
    semiAtomic[1]  = p.apvts.getRawParameterValue ("V2SEMITONE");
    semiAtomic[2]  = p.apvts.getRawParameterValue ("V3SEMITONE");
    fineAtomic[0]  = p.apvts.getRawParameterValue ("V1FINE");
    fineAtomic[1]  = p.apvts.getRawParameterValue ("V2FINE");
    fineAtomic[2]  = p.apvts.getRawParameterValue ("V3FINE");

    // --- Header ---
    nameLabel.setText ("Extreme Shifter", juce::dontSendNotification);
    nameLabel.setFont (juce::Font (juce::FontOptions ("Courier New", 30.0f, juce::Font::bold)));
    nameLabel.setColour (juce::Label::textColourId, accentLightest);
    nameLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (nameLabel);

    attributionLabel.setText ("from Pneutrino by NeutronStar714", juce::dontSendNotification);
    attributionLabel.setFont (juce::Font (juce::FontOptions ("Courier New", 20.0f, juce::Font::plain)));
    attributionLabel.setColour (juce::Label::textColourId, accentBright);
    attributionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (attributionLabel);

    // --- Voice box captions ---
    for (int v = 0; v < 3; ++v)
    {
        voiceHeaderLbl[v].setText (v == 0 ? "VOICE 1" : (v == 1 ? "VOICE 2" : "VOICE 3"),
                                   juce::dontSendNotification);
        voiceHeaderLbl[v].setFont (juce::Font (juce::FontOptions ("Helvetica", 12.0f, juce::Font::bold)));
        voiceHeaderLbl[v].setColour (juce::Label::textColourId, textColour);
        voiceHeaderLbl[v].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (voiceHeaderLbl[v]);
    }

    // --- Pitch response toggle (STEP default / SMOOTH) ---
    stepButton.setClickingTogglesState (true);
    stepButton.setColour (juce::TextButton::textColourOffId,   textColour);
    stepButton.setColour (juce::TextButton::textColourOnId,    accentBright);
    stepButton.setColour (juce::TextButton::buttonColourId,    accentColour);
    stepButton.setColour (juce::TextButton::buttonOnColourId,  panelBg);
    stepButton.setLookAndFeel (&lnf);
    stepButton.onStateChange = [this] { updateStepButton(); };
    updateStepButton();
    addAndMakeVisible (stepButton);

    // --- Latency display between the two toggles ---
    latencyLabel.setFont (juce::Font (juce::FontOptions ("Helvetica", 15.0f, juce::Font::bold)));
    latencyLabel.setColour (juce::Label::textColourId, muteText);
    latencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (latencyLabel);

    // --- Live mix bar graph ---
    addAndMakeVisible (mixGraph);

    // --- STFT block size toggle ---
    blockButton.setClickingTogglesState (true);
    blockButton.setColour (juce::TextButton::textColourOffId,   textColour);
    blockButton.setColour (juce::TextButton::textColourOnId,    accentBright);
    blockButton.setColour (juce::TextButton::buttonColourId,    accentColour);
    blockButton.setColour (juce::TextButton::buttonOnColourId,  panelBg);
    blockButton.setLookAndFeel (&lnf);
    blockButton.onStateChange = [this] { updateBlockButton(); };
    updateBlockButton();
    addAndMakeVisible (blockButton);

    addAndMakeVisible (balance1Knob);
    addAndMakeVisible (semitone1Knob);
    addAndMakeVisible (fine1Knob);
    addAndMakeVisible (tone1Knob);
    addAndMakeVisible (balance2Knob);
    addAndMakeVisible (semitone2Knob);
    addAndMakeVisible (fine2Knob);
    addAndMakeVisible (tone2Knob);
    addAndMakeVisible (balance3Knob);
    addAndMakeVisible (semitone3Knob);
    addAndMakeVisible (fine3Knob);
    addAndMakeVisible (tone3Knob);

    // Drive the live mix graph (and keep the latency readout honest across
    // sample-rate changes) at a smooth display rate
    startTimerHz (30);

    setSize (396, 540);
    setResizable (false, false);
}

void PolyShifterAudioProcessorEditor::updateBlockButton()
{
    blockButton.setButtonText (blockButton.getToggleState() ? "4096" : "2048");
    refreshLatencyLabel();
}

void PolyShifterAudioProcessorEditor::refreshLatencyLabel()
{
    const double sr = processorRef.getSampleRate();
    const double ms = sr > 0.0 ? 1000.0 * double (processorRef.getActiveLatencySamples()) / sr : 0.0;
    const juce::String text = juce::String::formatted ("Latency: %.1f ms", ms);
    if (latencyLabel.getText() != text)   // avoid pointless repaints
        latencyLabel.setText (text, juce::dontSendNotification);
}

void PolyShifterAudioProcessorEditor::timerCallback()
{
    refreshLatencyLabel();

    // Live mix graph: one bar per element (dry + 3 voices), x = its shift
    // (in STEP mode the semitone snaps to a whole number, fine still applies)
    const bool step = stepModeAtomic->load() < 0.5f;
    float shifts[3];
    for (int v = 0; v < 3; ++v)
    {
        const float semi = semiAtomic[v]->load();
        const float fine = fineAtomic[v]->load() / 100.0f;
        shifts[v] = (step ? std::round (semi) : semi) + fine;
    }

    MixGraph::Bar bars[4];
    bars[0] = { 0.0f, processorRef.getDisplayLevel (0), dryBarColour };
    for (int v = 0; v < 3; ++v)
        bars[v + 1] = { shifts[v], processorRef.getDisplayLevel (v + 1), panelStrokes[v] };
    mixGraph.setBars (bars, 4);
}

void PolyShifterAudioProcessorEditor::updateStepButton()
{
    stepButton.setButtonText (stepButton.getToggleState() ? "SMOOTH" : "STEP");
    // The semitone readouts switch between whole steps and 2-d.p. values
    semitone1Knob.refreshText();
    semitone2Knob.refreshText();
    semitone3Knob.refreshText();
}

PolyShifterAudioProcessorEditor::~PolyShifterAudioProcessorEditor()
{
    stopTimer();
    balance1Knob.resetLookAndFeel();
    semitone1Knob.resetLookAndFeel();
    fine1Knob.resetLookAndFeel();
    tone1Knob.resetLookAndFeel();
    balance2Knob.resetLookAndFeel();
    semitone2Knob.resetLookAndFeel();
    fine2Knob.resetLookAndFeel();
    tone2Knob.resetLookAndFeel();
    balance3Knob.resetLookAndFeel();
    semitone3Knob.resetLookAndFeel();
    fine3Knob.resetLookAndFeel();
    tone3Knob.resetLookAndFeel();
    stepButton.setLookAndFeel (nullptr);
    blockButton.setLookAndFeel (nullptr);
}

//==============================================================================
void PolyShifterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (darkBg);

    // Voice boxes — vertical panels side by side (outline only, same
    // geometry as the band columns in Dynamic Resonator). Voices 1 and 2
    // share the bright stroke; voice 3 (the sub octave) uses the deeper one.
    const int colW = (getWidth() - 44) / 3;   // 24 side margins + 2x10 gaps
    const float margin  = 7.0f;
    const float radius  = 10.0f;
    const float borderW = 2.5f;

    for (int v = 0; v < 3; ++v)
    {
        auto box = juce::Rectangle<float> (12.0f + float (v * (colW + 10)), 36.0f,
                                           float (colW), 364.0f);
        g.setColour (panelStrokes[v]);
        g.drawRoundedRectangle (box.reduced (margin), radius, borderW);
    }
}

void PolyShifterAudioProcessorEditor::resized()
{
    // === Header ===
    nameLabel.setBounds (getLocalBounds().removeFromTop (36).withY (4));

    // === Voice boxes (vertical panels, left to right) ===
    const int colW = (getWidth() - 44) / 3;   // 24 side margins + 2x10 gaps
    juce::Rectangle<int> box[3];
    //(content Xpos, content Ypos, width, size?)
    for (int v = 0; v < 3; ++v)
        box[v] = juce::Rectangle<int> (12 + v * (colW + 10), 40, colW, 352);
    //
    for (int v = 0; v < 3; ++v)
        voiceHeaderLbl[v].setBounds (box[v].reduced (3).removeFromTop (24));

    auto layoutColumn = [this] (KnobControl& a, KnobControl& b, KnobControl& c, KnobControl& d,
                                juce::Rectangle<int> area)
    {
        juce::FlexBox flex;
        flex.flexDirection  = juce::FlexBox::Direction::column;
        flex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;
        flex.alignItems     = juce::FlexBox::AlignItems::center;

        flex.items.addArray ({ juce::FlexItem (a).withFlex (1).withMinWidth (88).withMinHeight (79),
                               juce::FlexItem (b).withFlex (1).withMinWidth (88).withMinHeight (79),
                               juce::FlexItem (c).withFlex (1).withMinWidth (88).withMinHeight (79),
                               juce::FlexItem (d).withFlex (1).withMinWidth (88).withMinHeight (79) });

        flex.performLayout (area);
    };

    layoutColumn (balance1Knob, semitone1Knob, fine1Knob, tone1Knob, box[0].reduced (3).withTrimmedTop (20));
    layoutColumn (balance2Knob, semitone2Knob, fine2Knob, tone2Knob, box[1].reduced (3).withTrimmedTop (20));
    layoutColumn (balance3Knob, semitone3Knob, fine3Knob, tone3Knob, box[2].reduced (3).withTrimmedTop (20));

    // === Mode toggles: 2048/4096 centred on Voice 1's panel, STEP/SMOOTH on
    // Voice 3's panel, latency readout spanning between them ===
    const int v1cx = 12 + colW / 2;
    const int v3cx = 12 + 2 * (colW + 10) + colW / 2;
    blockButton.setBounds (juce::Rectangle<int> (v1cx - 45, 404, 90, 30));
    stepButton.setBounds  (juce::Rectangle<int> (v3cx - 45, 404, 90, 30));
    latencyLabel.setBounds (juce::Rectangle<int> (blockButton.getRight(), 404,
                                                  stepButton.getX() - blockButton.getRight(), 30));

    // === Live mix bar graph (side margins align with the voice panels) ===
    mixGraph.setBounds (17, 442, getWidth() - 35, 62);

    // === Attribution (always at bottom) ===
    attributionLabel.setBounds (getLocalBounds().withY (getHeight() - 26).withHeight (18));
}
