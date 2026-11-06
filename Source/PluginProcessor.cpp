#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdlib>

//==============================================================================
// Signalsmith's process() indexes its inputs/outputs as io[channel][sample].
// This tiny proxy maps that interface onto a single channel of a JUCE buffer.
struct ChannelIO
{
    float* data;

    struct Channel
    {
        float* data;
        float& operator[] (int index) noexcept { return data[index]; }
    };

    Channel operator[] (int) noexcept { return { data }; }
};

//==============================================================================
// Balance knobs get snap detents at 1/3 and 1/2 so the three voices can be
// dialed to equal thirds, or any two to equal halves. Baked into the range's
// snapToLegalValueFunction because RangedAudioParameter applies that function
// on EVERY value path (slider drags, host automation, state restore) - a
// slider-side hack would leave automation unsnapped. The capture zones
// [0.27..0.39] and [0.44..0.56] don't overlap, so the projection is monotonic.
static juce::NormalisableRange<float> makeBalanceRange()
{
    const auto snap = [] (float, float, float v) -> float
    {
        constexpr float snapPoints[] = { 0.33f, 0.50f };
        constexpr float threshold    = 0.01f;
        for (float p : snapPoints)
            if (std::fabs (v - p) <= threshold)
                return p;
        return v;
    };

    // proportion -> value: linear, then snap (so the slider's own drag value
    // and the text box land on the detent immediately)
    const auto from01 = [snap] (float start, float end, float p) -> float
    {
        return snap (start, end, start + (end - start) * p);
    };
    // value -> proportion: plain linear (knob rotation stays smooth)
    const auto to01 = [] (float start, float end, float v) -> float
    {
        return (v - start) / (end - start);
    };

    return juce::NormalisableRange<float> (0.0f, 1.0f, from01, to01, snap);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PolyShifterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Voice 1 (default: one octave up at half mix)
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V1BALANCE", "Balance", makeBalanceRange(), 0.50f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V1SEMITONE", "Semitone", -12.0f, 12.0f, 12.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V1FINE", "Fine", -50.0f, 50.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V1TONE", "Tone", 0.0f, 1.0f, 1.0f));
    // Voice 2 (off by default, so a fresh session behaves like the single-voice plugin)
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V2BALANCE", "Balance", makeBalanceRange(), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V2SEMITONE", "Semitone", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V2FINE", "Fine", -50.0f, 50.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V2TONE", "Tone", 0.0f, 1.0f, 1.0f));
    // Voice 3 (sub octave: -24..0 st, default -12; off by default)
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V3BALANCE", "Balance", makeBalanceRange(), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V3SEMITONE", "Semitone", -24.0f, 0.0f, -12.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V3FINE", "Fine", -50.0f, 50.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("V3TONE", "Tone", 0.0f, 1.0f, 1.0f));
    // Pitch response: false = STEP (semitone knobs snap to whole semitones,
    // fine bypassed - the harmonizer sound), true = SMOOTH (continuous,
    // the whammy-style glide)
    layout.add (std::make_unique<juce::AudioParameterBool> ("STEPMODE", "Step Mode", false));
    // STFT block size: false = 2048 (default, ~46 ms, the sweet spot),
    // true = 4096 (finer frequency resolution, steadier held notes, ~104 ms)
    layout.add (std::make_unique<juce::AudioParameterBool> ("BLOCK", "Block", false));

    return layout;
}

PolyShifterAudioProcessor::PolyShifterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    bal1Param  = apvts.getRawParameterValue ("V1BALANCE");
    semi1Param = apvts.getRawParameterValue ("V1SEMITONE");
    fine1Param = apvts.getRawParameterValue ("V1FINE");
    bal2Param  = apvts.getRawParameterValue ("V2BALANCE");
    semi2Param = apvts.getRawParameterValue ("V2SEMITONE");
    fine2Param = apvts.getRawParameterValue ("V2FINE");
    bal3Param  = apvts.getRawParameterValue ("V3BALANCE");
    semi3Param = apvts.getRawParameterValue ("V3SEMITONE");
    fine3Param = apvts.getRawParameterValue ("V3FINE");
    toneParam[0] = apvts.getRawParameterValue ("V1TONE");
    toneParam[1] = apvts.getRawParameterValue ("V2TONE");
    toneParam[2] = apvts.getRawParameterValue ("V3TONE");
    stepParam  = apvts.getRawParameterValue ("STEPMODE");
    blockParam = apvts.getRawParameterValue ("BLOCK");

    debugEnabled = (std::getenv ("EXTREME_SHIFTER_DEBUG") != nullptr);
}

void PolyShifterAudioProcessor::updateLatency()
{
    const auto& active = blockParam->load() > 0.5f ? voices[0].poly4096 : voices[0].poly2048;
    setLatencySamples (active.empty() ? 0
        : active[0].inputLatency() + active[0].outputLatency());
}

bool PolyShifterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Without this, FL Studio 64-bit offers a disabled-bus layout, the default
    // implementation rejects it, and FL silently runs the plugin with
    // 0 in / 0 out channels (complete silence). Accept anything up to stereo
    // on either bus - including layouts with one bus disabled.
    const auto inputSet  = layouts.getMainInputChannelSet();
    const auto outputSet = layouts.getMainOutputChannelSet();

    if (inputSet.isDisabled() && outputSet.isDisabled())
        return false;

    return inputSet.size() <= 2 && outputSet.size() <= 2;
}

PolyShifterAudioProcessor::~PolyShifterAudioProcessor() = default;

// LinearSmoothedValue advances its ramp by one step PER CALL - advance it once
// per sample so a kPitchRampSeconds ramp really takes that long of audio time
// (calling getNextValue() once per block would stretch it by the block size).
static float advanceRamp (juce::LinearSmoothedValue<float>& ramp, int steps)
{
    float v = 0.0f;
    while (steps-- > 0)
        v = ramp.getNextValue();
    return v;
}

//==============================================================================
void PolyShifterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numInputChannels = jmax (1, getTotalNumInputChannels());
    currentSampleRate = sampleRate;

    // Each voice gets its own isolated spectral shifter set, prepared at
    // BOTH STFT block sizes, so switching blocks is just a branch - no
    // allocation on the audio thread.
    for (auto& voice : voices)
    {
        voice.poly2048.resize (numInputChannels);
        for (auto& shifter : voice.poly2048)
        {
            // Balanced low-latency setup for live use: 2048-sample STFT block
            // (~46 ms at 44.1 kHz), quarter-block hop, split computation OFF
            // (it adds a full hop of latency and is unnecessary at this block
            // size). Frequency resolution is sr/block (~21.5 Hz at 44.1 kHz).
            shifter.configure (1, 2048, 512, false);
            shifter.reset();
        }

        voice.poly4096.resize (numInputChannels);
        for (auto& shifter : voice.poly4096)
        {
            // 4096-sample STFT block (~104 ms at 44.1 kHz): halves the
            // frequency resolution to ~10.8 Hz - steadier held notes, at
            // roughly double the latency. Same 512-sample hop.
            shifter.configure (1, 4096, 512, false);
            shifter.reset();
        }

        voice.lp.resize (numInputChannels);
        for (auto& filter : voice.lp)
            filter.reset();
    }

    // Report the TOTAL shifter delay (analysis + synthesis + hop) so the host
    // can compensate for it. Seed the block tracker from the ACTUAL parameter
    // (not a hardcoded false): if a restored state has BLOCK=4096, the first
    // processBlock must not re-report a latency change mid-stream - a host
    // (FL in particular) restarting its audio engine on a spurious latency
    // notification at block 1 clicks and buzzes forever until a full rescan.
    lastBlockLarge = blockParam->load() > 0.5f;
    updateLatency();

    // Generous fixed capacity for the dry buffer - some hosts (FL Studio included)
    // can deliver blocks larger than the prepared size; never reallocate on the
    // audio thread, and never write out of bounds.
    dryBuffer.setSize (1, 65536);

    if (debugEnabled)
    {
        debugLogFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("extreme_shifter_debug.log");
        debugLogFile.appendText (juce::String::formatted (
            "prepareToPlay: sr=%.0f block=%d in=%d out=%d latency=%d (%.1f ms) [2048 variant]\n",
            sampleRate, samplesPerBlock, numInputChannels, getTotalNumOutputChannels(),
            voices[0].poly2048[0].inputLatency() + voices[0].poly2048[0].outputLatency(),
            1000.0 * (voices[0].poly2048[0].inputLatency() + voices[0].poly2048[0].outputLatency()) / sampleRate));
        debugBlockCounter = 0;
    }

    seedPitchRamps();

    // Generous fixed capacity for the wet buffers too
    wetBuffer1.setSize (1, 65536);
    wetBuffer2.setSize (1, 65536);
    wetBuffer3.setSize (1, 65536);
}

void PolyShifterAudioProcessor::releaseResources()
{
    // IMPORTANT: only CLEAR, never shrink. setSize(1, 0) frees the allocation
    // down to a stub, and hosts (FL in particular) can resume the audio
    // callback after a delete/re-add WITHOUT a fresh prepareToPlay - the next
    // processBlock would then write a full block into a ~32-byte allocation:
    // silent heap corruption that buzzes forever until the DLL is reloaded
    // (rescan). Keeping the 65536-sample allocations alive makes the resumed
    // processing safe no matter what the host does.
    dryBuffer.clear();
    wetBuffer1.clear();
    wetBuffer2.clear();
    wetBuffer3.clear();
}

// Full DSP state clear on stream stop/restart. The VST3 wrapper calls this
// from setProcessing(false) - i.e. whenever the host stops and restarts the
// processing stream (FL's delete-from-mixer path does exactly this). Without
// it the shifters resume with stale analysis state from the previous stream.
void PolyShifterAudioProcessor::reset()
{
    for (auto& voice : voices)
    {
        for (auto& shifter : voice.poly2048)
            shifter.reset();
        for (auto& shifter : voice.poly4096)
            shifter.reset();
        for (auto& filter : voice.lp)
            filter.reset();
    }

    seedPitchRamps();

    // Re-sync the tracker with the current block parameter so a resumed
    // stream never fires a spurious latency change on its first block.
    lastBlockLarge = blockParam->load() > 0.5f;
}

void PolyShifterAudioProcessor::seedPitchRamps()
{
    // Pitch ramp time: how quickly a knob turn reaches its new pitch.
    // Smaller = snappier (watch for zipper noise), larger = smoother.
    // NOTE: must be advanced per SAMPLE (see advanceRamp in processBlock),
    // otherwise the ramp stretches to tens of seconds. The current value is
    // seeded with the SAME STEP/SMOOTH logic the block loop uses, so a
    // re-prepare or stream restart can never glide the pitch to a different
    // starting point.
    constexpr double kPitchRampSeconds = 0.05;
    const bool step = stepParam->load() < 0.5f;
    auto seedRamp = [&] (juce::LinearSmoothedValue<float>& ramp,
                         std::atomic<float>* semi, std::atomic<float>* fine)
    {
        ramp.reset (currentSampleRate, kPitchRampSeconds);
        ramp.setCurrentAndTargetValue (step
            ? makeTransposeFactor (std::round (semi->load()), fine->load())
            : makeTransposeFactor (semi->load(), fine->load()));
    };
    seedRamp (smoothedPitchFactor[0], semi1Param, fine1Param);
    seedRamp (smoothedPitchFactor[1], semi2Param, fine2Param);
    seedRamp (smoothedPitchFactor[2], semi3Param, fine3Param);
}

//==============================================================================
void PolyShifterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();
    const auto numSamples        = buffer.getNumSamples();

    // Some hosts (FL Studio included) can report 0 active input channels even when
    // audio is present in the buffer. Process whatever channels actually exist -
    // never clear audio that the host delivered.
    const auto processChannels = jmin (numOutputChannels, jmax (1, numInputChannels));

    // Clear only channels that really have no signal
    for (auto channel = processChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    const float bal1 = bal1Param->load();
    const float bal2 = bal2Param->load();
    const float bal3 = bal3Param->load();

    // STEP vs SMOOTH: snap the semitone knobs to whole steps (fine bypassed)
    // for the stepped harmonizer sound, or keep the continuous whammy glide
    const bool stepMode = stepParam->load() < 0.5f;

    // STFT block size: 2048 (default) vs 4096 - the two variants have
    // different latencies, so tell the host when we switch
    const bool blockLarge = blockParam->load() > 0.5f;
    if (blockLarge != lastBlockLarge)
    {
        lastBlockLarge = blockLarge;
        updateLatency();
    }

    // Semitone + fine cents -> one frequency multiplier per voice, passed
    // through a 50 ms ramp (advanced per sample) so knob turns glide smoothly.
    // In STEP mode the semitone snaps to a whole number but the fine cents
    // still apply exactly as in SMOOTH.
    auto voiceFactor = [&] (std::atomic<float>* semi, std::atomic<float>* fine)
    {
        if (stepMode)
            return makeTransposeFactor (std::round (semi->load()), fine->load());
        return makeTransposeFactor (semi->load(), fine->load());
    };

    smoothedPitchFactor[0].setTargetValue (voiceFactor (semi1Param, fine1Param));
    smoothedPitchFactor[1].setTargetValue (voiceFactor (semi2Param, fine2Param));
    smoothedPitchFactor[2].setTargetValue (voiceFactor (semi3Param, fine3Param));

    const float transposeFactor1 = advanceRamp (smoothedPitchFactor[0], numSamples);
    const float transposeFactor2 = advanceRamp (smoothedPitchFactor[1], numSamples);
    const float transposeFactor3 = advanceRamp (smoothedPitchFactor[2], numSamples);

    // Voices with 0 balance are skipped entirely (CPU + keeps a fresh session
    // identical to the single-voice plugin). The dry signal is shared and
    // scaled by the product of the complements of all three balances.
    const bool voice1On = bal1 > 0.0f;
    const bool voice2On = bal2 > 0.0f;
    const bool voice3On = bal3 > 0.0f;
    const float dryGain = (1.0f - bal1) * (1.0f - bal2) * (1.0f - bal3);

    double debugInRmsSum = 0.0, debugOutRmsSum = 0.0;
    bool debugNonFinite = false;
    double drySum = 0.0, wetSum[3] = { 0.0, 0.0, 0.0 };

    for (int channel = 0; channel < processChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        if (channel >= static_cast<int> (voices[0].poly2048.size())
            || channel >= static_cast<int> (voices[0].poly4096.size()))
        {
            // Host changed the layout without a prepareToPlay - leave the channel as-is
            continue;
        }

        // IMPORTANT: both shifter types must run out-of-place - SignalsmithStretch
        // in particular copies its input progressively while writing output, so an
        // in-place call silently corrupts the analysis history and goes silent.
        // Each voice processes the same dry copy into its own wet buffer.
        dryBuffer.copyFrom (0, 0, channelData, numSamples);
        const auto* dryData = dryBuffer.getReadPointer (0);
        auto* wet1 = wetBuffer1.getWritePointer (0);
        auto* wet2 = wetBuffer2.getWritePointer (0);
        auto* wet3 = wetBuffer3.getWritePointer (0);

        auto runVoice = [&] (int voiceIndex, float* wetOut, float smoothedFactor)
        {
            auto& voice = voices[voiceIndex];
            auto& set = blockLarge ? voice.poly4096 : voice.poly2048;
            set[channel].setTransposeFactor (smoothedFactor);   // external 50 ms ramp

            ChannelIO dryIO { dryBuffer.getWritePointer (0) };
            ChannelIO wetIO { wetOut };
            set[channel].process (dryIO, numSamples, wetIO, numSamples);

            // Tone LP on the shifted signal only - the dry input stays
            // untouched. Always runs (at tone = 100% the cutoff sits just
            // under Nyquist, inaudible) so the filter state never jumps.
            voice.lp[channel].setTone (toneParam[voiceIndex]->load(), currentSampleRate);
            voice.lp[channel].process (wetOut, numSamples);
        };

        bool ok1 = true, ok2 = true, ok3 = true;
        try
        {
            if (voice1On)
                runVoice (0, wet1, transposeFactor1);
        }
        catch (...)
        {
            ok1 = false;   // never let the DSP take down the audio
            if (debugEnabled)
                debugLogFile.appendText ("voice 1 shifter threw\n");
        }
        try
        {
            if (voice2On)
                runVoice (1, wet2, transposeFactor2);
        }
        catch (...)
        {
            ok2 = false;
            if (debugEnabled)
                debugLogFile.appendText ("voice 2 shifter threw\n");
        }
        try
        {
            if (voice3On)
                runVoice (2, wet3, transposeFactor3);
        }
        catch (...)
        {
            ok3 = false;
            if (debugEnabled)
                debugLogFile.appendText ("voice 3 shifter threw\n");
        }

        // out = dry*(1-b1)*(1-b2)*(1-b3) + wet1*b1 + wet2*b2 + wet3*b3;
        // non-finite falls back to dry
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float v = dryData[sample] * dryGain;
            if (ok1) v += wet1[sample] * bal1;
            if (ok2) v += wet2[sample] * bal2;
            if (ok3) v += wet3[sample] * bal3;
            channelData[sample] = std::isfinite (v) ? v : dryData[sample];
        }

        // Bar-graph metering: RMS of each mix element (post-Tone wet), so the
        // editor can show the actual loudness of dry and each voice live
        for (int sample = 0; sample < numSamples; ++sample)
        {
            drySum += double (dryData[sample]) * dryData[sample];
            if (ok1) wetSum[0] += double (wet1[sample]) * wet1[sample];
            if (ok2) wetSum[1] += double (wet2[sample]) * wet2[sample];
            if (ok3) wetSum[2] += double (wet3[sample]) * wet3[sample];
        }

        if (debugEnabled)
        {
            for (int sample = 0; sample < numSamples; ++sample)
            {
                debugInRmsSum += double (dryData[sample]) * dryData[sample];
                const float v = channelData[sample];
                debugOutRmsSum += double (v) * v;
                if (! std::isfinite (v))
                    debugNonFinite = true;
            }
        }
    }

    // Smooth the mix contributions (fast attack, slow release) and publish
    // them for the editor's bar graph
    const long meterSamples = long (processChannels) * long (numSamples);
    if (meterSamples > 0)
    {
        const double inv = 1.0 / double (meterSamples);
        // x6 display gain: instrument-level RMS is small, so this makes the
        // bars obvious; the editor clamps at 1.0 so loud signals cap at the
        // same full height
        constexpr float meterGain = 6.0f;
        const float contrib[4] = {
            float (std::sqrt (drySum * inv)) * dryGain * meterGain,
            float (std::sqrt (wetSum[0] * inv)) * bal1 * meterGain,
            float (std::sqrt (wetSum[1] * inv)) * bal2 * meterGain,
            float (std::sqrt (wetSum[2] * inv)) * bal3 * meterGain
        };
        const double blockSeconds = double (numSamples) / currentSampleRate;
        const float attack  = 1.0f - float (std::exp (-blockSeconds / 0.03));
        const float release = 1.0f - float (std::exp (-blockSeconds / 0.25));
        for (int k = 0; k < 4; ++k)
        {
            const float inst = contrib[k];
            smoothLevels[k] += (inst - smoothLevels[k]) * (inst > smoothLevels[k] ? attack : release);
            displayLevels[k].store (smoothLevels[k], std::memory_order_relaxed);
        }
    }

    if (debugEnabled && (++debugBlockCounter & 127) == 1)
    {
        const double n = double (processChannels) * double (numSamples);
        debugLogFile.appendText (juce::String::formatted (
            "block n=%d in=%d out=%d | inRMS=%.4f outRMS=%.4f b1=%.2f b2=%.2f b3=%.2f st1=%.1f st2=%.1f st3=%.1f t1=%.2f t2=%.2f t3=%.2f step=%s stft=%d nonfinite=%d\n",
            numSamples, numInputChannels, numOutputChannels,
            std::sqrt (debugInRmsSum / n), std::sqrt (debugOutRmsSum / n),
            bal1, bal2, bal3, semi1Param->load(), semi2Param->load(), semi3Param->load(),
            toneParam[0]->load(), toneParam[1]->load(), toneParam[2]->load(),
            stepMode ? "SMOOTH" : "STEP", blockLarge ? 4096 : 2048, int (debugNonFinite)));
    }
}

//==============================================================================
void PolyShifterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void PolyShifterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // A host that restores state AFTER prepareToPlay (the order is not
    // standardised) would otherwise have prepareToPlay report the default
    // latency, then fire a spurious latency change on the first block.
    lastBlockLarge = blockParam->load() > 0.5f;
}

//==============================================================================
juce::AudioProcessorEditor* PolyShifterAudioProcessor::createEditor()
{
    return new PolyShifterAudioProcessorEditor (*this);
}

//==============================================================================
// Plugin entry point (JUCE 8 requires this to be defined by the plugin itself)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolyShifterAudioProcessor();
}
