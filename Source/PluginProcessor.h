#pragma once

#include <cmath>
#include <JuceHeader.h>
#include "signalsmith-stretch.h"

//==============================================================================
// Simple 2-pole Butterworth low-pass for the per-voice Tone control - tames
// the metallic high-frequency artifacts of the spectral shift. Applied to
// each voice's WET signal only (the dry input always stays untouched).
//
// The filter NEVER hard-bypasses: at tone = 1.0 the cutoff is clamped to just
// under Nyquist (inaudible), keeping the state running continuously. A hard
// bypass would produce a click every time tone crosses 100% - the wet signal
// would jump between filtered and unfiltered mid-stream.
struct ToneLpFilter
{
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    // tone: 0..1 -> cutoff 800 Hz .. just under Nyquist (1.0 = fully open)
    void setTone (float tone, double sampleRate)
    {
        sr = float (sampleRate);
        const float fc = std::min (800.0f * std::pow (25.0f, tone), 0.95f * sr * 0.5f);

        const float w0    = 2.0f * 3.14159265f * fc / sr;
        const float alpha = std::sin (w0) * 0.7071f;   // Q = 1/(2*0.7071) ~ Butterworth
        const float a0    = 1.0f + alpha;
        const float c     = 1.0f - std::cos (w0);
        b0 = 0.5f * c / a0;
        b1 = c / a0;
        b2 = b0;
        a1 = (-2.0f * std::cos (w0)) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void process (float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            data[i] = y;
        }
    }

    float sr = 44100.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
/**
    Extreme Shifter - a real-time, polyphonic pitch shifter for live guitar.

    Uses one SignalsmithStretch instance per channel (the library's spectral
    peak-tracking handles polyphonic input), with a smoothed transpose factor
    so knob movements never click.
*/
class PolyShifterAudioProcessor  : public juce::AudioProcessor
{
public:
    PolyShifterAudioProcessor();
    ~PolyShifterAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    // Called by the host when the processing stream stops/restarts (the VST3
    // wrapper invokes this from setProcessing(false)) - a full DSP state clear
    // so a resumed stream never starts from stale filter/shifter state.
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Extreme Shifter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    // FL Studio probes with layouts that include disabled buses; rejecting them
    // makes the host silently run the plugin with 0 in / 0 out channels.
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Latency of the ACTIVE block-size variant (samples) - used by the
    // editor's latency display
    int getActiveLatencySamples() const
    {
        const auto& active = (blockParam->load() > 0.5f) ? voices[0].poly4096 : voices[0].poly2048;
        return active.empty() ? 0 : active[0].inputLatency() + active[0].outputLatency();
    }

    // Smoothed mix-contribution level (0..~1) of dry/voice1/voice2/voice3
    // for the editor's bar graph: measured RMS x the element's mix gain
    float getDisplayLevel (int index) const
    {
        return displayLevels[index].load (std::memory_order_relaxed);
    }

private:
    // Three isolated voices; each is a full spectral shifter set, prepared at
    // both STFT block sizes so switching blocks is just a branch - no
    // allocation on the audio thread.
    struct VoiceShifterSet
    {
        std::vector<signalsmith::stretch::SignalsmithStretch<float>> poly2048;
        std::vector<signalsmith::stretch::SignalsmithStretch<float>> poly4096;
        std::vector<ToneLpFilter> lp;   // per-channel Tone filter (wet only)
    };
    VoiceShifterSet voices[3];

    // Smooths out knob/expression parameter changes to prevent digital clicks
    juce::LinearSmoothedValue<float> smoothedPitchFactor[3];

    // Cached lock-free parameter pointers (avoid APVTS lookups on the audio thread)
    std::atomic<float>* bal1Param   { nullptr };
    std::atomic<float>* semi1Param  { nullptr };
    std::atomic<float>* fine1Param  { nullptr };
    std::atomic<float>* bal2Param   { nullptr };
    std::atomic<float>* semi2Param  { nullptr };
    std::atomic<float>* fine2Param  { nullptr };
    std::atomic<float>* bal3Param   { nullptr };
    std::atomic<float>* semi3Param  { nullptr };
    std::atomic<float>* fine3Param  { nullptr };
    std::atomic<float>* toneParam[3] { nullptr, nullptr, nullptr };
    std::atomic<float>* blockParam  { nullptr };
    std::atomic<float>* stepParam   { nullptr };
    bool lastBlockLarge = false;
    double currentSampleRate = 44100.0;

    // Bar-graph metering: [dry, v1, v2, v3] mix contributions. Written on the
    // audio thread (smoothed there too), read lock-free by the editor timer.
    std::atomic<float> displayLevels[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float smoothLevels[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Report the active shifter's total delay (analysis + synthesis)
    void updateLatency();

    // Seed all three pitch ramps at the current parameter values using the
    // same STEP/SMOOTH logic the block loop uses (shared by prepareToPlay
    // and reset, so a re-prepare or stream restart never glides the pitch)
    void seedPitchRamps();

    // Re-usable dry/wet buffers (no allocation on the audio thread)
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer1, wetBuffer2, wetBuffer3;

    // Debug diagnostics: enabled with EXTREME_SHIFTER_DEBUG=1 (see processBlock)
    bool debugEnabled = false;
    juce::File debugLogFile;
    int debugBlockCounter = 0;

    // 2^((semitones + cents/100) / 12) - the frequency multiplier the shifter expects
    static float makeTransposeFactor (float semitones, float cents)
    {
        return std::pow (2.0f, (semitones + cents / 100.0f) / 12.0f);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyShifterAudioProcessor)
};
