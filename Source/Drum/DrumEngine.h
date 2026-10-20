#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "KickPresenceDSP.h"
#include "DrumMultiband.h"
#include "DrumInstrument.h"
#include "DrumBusDSP.h"
#include "DrumFinishDSP.h"
#include "../Kit/KitAligner.h"

namespace drum
{
enum Control
{
    bypass, input, filtersOn, highPass, lowPass,
    gateOn, gateThreshold, gateRatio, gateRange, gateAttack, gateRelease,
    eqOn, bodyFrequency, bodyGain, boxFrequency, boxGain,
    attackFrequency, attackGain, airFrequency, airGain, eqQ,
    compressorOn, threshold, ratio, attack, release, makeup, mix, output,
    focusOn, focusLow, focusHigh, gateHold, gateHysteresis, cleanupOn, cleanupDepth,
    presenceOn, reinforceFrequency, reinforceAmount, reinforceDecay, transientAmount,
    saturationDrive, reinforceInvert, bodyQ, boxQ, attackQ, airQ,
    multibandOn, crossoverLow, crossoverHigh,
    lowThreshold, lowRatio, lowAttack, lowRelease, lowMakeup, lowRange, lowSolo,
    midThreshold, midRatio, midAttack, midRelease, midMakeup, midRange, midSolo,
    highThreshold, highRatio, highAttack, highRelease, highMakeup, highRange, highSolo,
    instrument, glueOn, glueThreshold, glueRatio, glueAttack, glueRelease, glueMakeup, glueMix,
    glueSidechain, glueKnee, glueRange, ambienceOn, ambienceAmount, ambienceSize, ambiencePredelay,
    ambienceHighPass, ambienceLowPass,
    levelMatchOn, crushOn, crushAmount, crushThreshold, crushRatio, crushAttack, crushRelease, crushHP, crushLP,
    colourOn, colourDrive, colourMix, clipOn, clipCeiling, clipMix,
    shapeOn, shapeAttack, shapeSustain, dynamicOn,
    dynamicLowHz, dynamicLowQ, dynamicLowThreshold, dynamicLowRange,
    dynamicHighHz, dynamicHighQ, dynamicHighThreshold, dynamicHighRange,
    ambienceCharacter, ambienceCompression, autoMeasuredOn, autoStyleOption, autoIntensityOption, monitorMonoOn,
    kitGroup, kitMic, kitMode, kitStrength, kitPolarity, kitDelay, controlCount
};

struct ControlSpec
{
    const char* id;
    const char* name;
    const char* unit;
    float minimum, maximum, initial, skew;
    bool toggle = false;
};

inline const std::array<ControlSpec, controlCount> controlSpecs {{
    { "drumBypass", "Bypass", "", 0, 1, 0, 1, true },
    { "inputDb", "Input", "dB", -24, 24, 0, 1 },
    { "filtersOn", "Filters", "", 0, 1, 0, 1, true },
    { "highPassHz", "High-pass", "Hz", 20, 250, 30, 0.45f },
    { "lowPassHz", "Low-pass", "Hz", 1000, 20000, 18000, 0.45f },
    { "gateOn", "Expander", "", 0, 1, 0, 1, true },
    { "gateThresholdDb", "Threshold", "dB", -80, 0, -45, 1 },
    { "gateRatio", "Ratio", ":1", 1, 8, 2, 0.6f },
    { "gateRangeDb", "Range", "dB", 0, 60, 18, 1 },
    { "gateAttackMs", "Attack", "ms", 0.1f, 30, 1, 0.4f },
    { "gateReleaseMs", "Release", "ms", 10, 1000, 180, 0.4f },
    { "eqOn", "Equalizer", "", 0, 1, 0, 1, true },
    { "bodyHz", "Body", "Hz", 30, 200, 65, 0.5f },
    { "bodyDb", "Body gain", "dB", -12, 12, 0, 1 },
    { "boxHz", "Box", "Hz", 150, 1200, 350, 0.5f },
    { "boxDb", "Box gain", "dB", -12, 12, 0, 1 },
    { "attackHz", "Attack", "Hz", 1000, 8000, 3500, 0.5f },
    { "attackDb", "Attack gain", "dB", -12, 12, 0, 1 },
    { "airHz", "Air", "Hz", 4000, 16000, 9000, 0.5f },
    { "airDb", "Air gain", "dB", -12, 12, 0, 1 },
    { "eqQ", "Q global", "", 0.3f, 4, 0.707f, 0.6f },
    { "compressorOn", "Compressor", "", 0, 1, 0, 1, true },
    { "compressorThresholdDb", "Threshold", "dB", -60, 0, -18, 1 },
    { "compressorRatio", "Ratio", ":1", 1, 12, 3, 0.6f },
    { "compressorAttackMs", "Attack", "ms", 0.1f, 100, 20, 0.4f },
    { "compressorReleaseMs", "Release", "ms", 10, 1500, 150, 0.4f },
    { "compressorMakeupDb", "Makeup", "dB", -12, 18, 0, 1 },
    { "compressorMix", "Mix", "%", 0, 100, 100, 1 },
    { "prototypeGainDb", "Output", "dB", -60, 12, 0, 1 },
    { "kickFocusOn", "Kick Focus", "", 0, 1, 0, 1, true },
    { "kickFocusLowHz", "Detect HP", "Hz", 20, 100, 35, 0.5f },
    { "kickFocusHighHz", "Detect LP", "Hz", 120, 500, 180, 0.5f },
    { "kickHoldMs", "Hold", "ms", 0, 100, 25, 0.6f },
    { "kickHysteresisDb", "Hysteresis", "dB", 0, 12, 6, 1 },
    { "kickCleanupOn", "Bleed Clean", "", 0, 1, 0, 1, true },
    { "kickCleanupDb", "Treble range", "dB", 0, 30, 12, 1 },
    { "kickPresenceOn", "Body / Presence", "", 0, 1, 0, 1, true },
    { "kickReinforceHz", "Body tune", "Hz", 30, 120, 60, 0.5f },
    { "kickReinforcePercent", "Body amount", "%", 0, 100, 30, 1 },
    { "kickReinforceDecayMs", "Body decay", "ms", 60, 400, 180, 0.5f },
    { "kickTransientDb", "Punch", "dB", 0, 6, 2, 1 },
    { "kickSaturationDb", "Drive", "dB", 0, 18, 4, 1 },
    { "kickBodyInvert", "Body polarity", "", 0, 1, 0, 1, true },
    { "bodyQ", "Body Q", "", 0, 6, 0, 0.6f },
    { "boxQ", "Box Q", "", 0, 6, 0, 0.6f },
    { "attackQ", "Attack Q", "", 0, 6, 0, 0.6f },
    { "airQ", "Air Q", "", 0, 6, 0, 0.6f },
    { "multibandOn", "Multiband", "", 0, 1, 0, 1, true },
    { "crossoverLowHz", "Low / Mid", "Hz", 60, 350, 140, 0.5f },
    { "crossoverHighHz", "Mid / High", "Hz", 800, 6000, 2000, 0.5f },
    { "lowThresholdDb", "Low threshold", "dB", -80, 0, -24, 1 },
    { "lowRatio", "Low ratio", ":1", 1, 12, 3, 0.6f },
    { "lowAttackMs", "Low attack", "ms", 0.1f, 100, 15, 0.4f },
    { "lowReleaseMs", "Low release", "ms", 10, 1500, 140, 0.4f },
    { "lowMakeupDb", "Low makeup", "dB", -12, 12, 0, 1 },
    { "lowRangeDb", "Low max GR", "dB", 0, 24, 12, 1 },
    { "lowSolo", "Solo Low", "", 0, 1, 0, 1, true },
    { "midThresholdDb", "Mid threshold", "dB", -80, 0, -24, 1 },
    { "midRatio", "Mid ratio", ":1", 1, 12, 2, 0.6f },
    { "midAttackMs", "Mid attack", "ms", 0.1f, 100, 12, 0.4f },
    { "midReleaseMs", "Mid release", "ms", 10, 1500, 110, 0.4f },
    { "midMakeupDb", "Mid makeup", "dB", -12, 12, 0, 1 },
    { "midRangeDb", "Mid max GR", "dB", 0, 24, 8, 1 },
    { "midSolo", "Solo Mid", "", 0, 1, 0, 1, true },
    { "highThresholdDb", "High threshold", "dB", -80, 0, -24, 1 },
    { "highRatio", "High ratio", ":1", 1, 12, 1.5f, 0.6f },
    { "highAttackMs", "High attack", "ms", 0.1f, 100, 10, 0.4f },
    { "highReleaseMs", "High release", "ms", 10, 1500, 80, 0.4f },
    { "highMakeupDb", "High makeup", "dB", -12, 12, 0, 1 },
    { "highRangeDb", "High max GR", "dB", 0, 24, 6, 1 },
    { "highSolo", "Solo High", "", 0, 1, 0, 1, true },
    { "drumInstrument", "Instrument", "", 0, 12, 0, 1 },
    { "busGlueOn", "Bus Glue", "", 0, 1, 0, 1, true },
    { "busThresholdDb", "Glue threshold", "dB", -60, 0, -18, 1 },
    { "busRatio", "Glue ratio", ":1", 1, 10, 2, 0.6f },
    { "busAttackMs", "Glue attack", "ms", 0.1f, 100, 30, 0.4f },
    { "busReleaseMs", "Glue release", "ms", 10, 1500, 100, 0.4f },
    { "busMakeupDb", "Glue makeup", "dB", -12, 18, 0, 1 },
    { "busMix", "Glue mix", "%", 0, 100, 100, 1 },
    { "busSidechainHz", "Sidechain HP", "Hz", 0, 250, 80, 0.5f },
    { "busKneeDb", "Knee", "dB", 0, 12, 6, 1 },
    { "busRangeDb", "Glue max GR", "dB", 0, 12, 6, 1 },
    { "busAmbienceOn", "Ambience", "", 0, 1, 0, 1, true },
    { "busAmbiencePercent", "Room amount", "%", 0, 100, 2, 1 },
    { "busAmbienceSize", "Room size", "%", 0, 100, 25, 1 },
    { "busPredelayMs", "Pre-delay", "ms", 0, 40, 10, 1 },
    { "busWetHighPassHz", "Room HP", "Hz", 50, 1000, 150, 0.5f },
    { "busWetLowPassHz", "Room LP", "Hz", 2000, 16000, 7000, 0.5f },
    { "finishLevelMatchOn", "Level A/B", "", 0, 1, 0, 1, true },
    { "finishCrushOn", "Crush", "", 0, 1, 0, 1, true },
    { "finishCrushPercent", "Crush amount", "%", 0, 100, 20, 1 },
    { "finishCrushThresholdDb", "Crush threshold", "dB", -60, 0, -30, 1 },
    { "finishCrushRatio", "Crush ratio", ":1", 1, 20, 8, 0.6f },
    { "finishCrushAttackMs", "Crush attack", "ms", 0.1f, 50, 3, 0.4f },
    { "finishCrushReleaseMs", "Crush release", "ms", 20, 1000, 100, 0.4f },
    { "finishCrushHighPassHz", "Crush HP", "Hz", 20, 1000, 80, 0.5f },
    { "finishCrushLowPassHz", "Crush LP", "Hz", 2000, 18000, 8000, 0.5f },
    { "finishColourOn", "Color", "", 0, 1, 0, 1, true },
    { "finishColourDriveDb", "Color drive", "dB", 0, 24, 6, 1 },
    { "finishColourPercent", "Color amount", "%", 0, 100, 20, 1 },
    { "finishClipOn", "Soft clip", "", 0, 1, 0, 1, true },
    { "finishClipCeilingDb", "Clip ceiling", "dB", -18, 0, -1, 1 },
    { "finishClipPercent", "Clip amount", "%", 0, 100, 20, 1 },
    { "finishShapeOn", "Attack / Sustain", "", 0, 1, 0, 1, true },
    { "finishAttackDb", "Attack", "dB", -12, 12, 0, 1 },
    { "finishSustainDb", "Sustain", "dB", -12, 12, 0, 1 },
    { "finishDynamicEqOn", "Dynamic EQ", "", 0, 1, 0, 1, true },
    { "finishDynamicLowHz", "Resonance Hz", "Hz", 80, 2000, 350, 0.5f },
    { "finishDynamicLowQ", "Resonance Q", "", 0.3f, 6, 2, 1 },
    { "finishDynamicLowThresholdDb", "Resonance threshold", "dB", -60, 0, -24, 1 },
    { "finishDynamicLowRangeDb", "Resonance max", "dB", 0, 12, 4, 1 },
    { "finishDynamicHighHz", "Cymbals Hz", "Hz", 2000, 16000, 7000, 0.5f },
    { "finishDynamicHighQ", "Cymbals Q", "", 0.3f, 6, 1, 1 },
    { "finishDynamicHighThresholdDb", "Cymbals threshold", "dB", -60, 0, -24, 1 },
    { "finishDynamicHighRangeDb", "Cymbals max", "dB", 0, 12, 4, 1 },
    { "busAmbienceCharacter", "Room character", "%", 0, 100, 0, 1 },
    { "busAmbienceCompression", "Room density", "%", 0, 100, 0, 1 },
    { "autoMeasuredBodyOn", "Measured body", "", 0, 1, 0, 1, true },
    { "autoStyleOption", "Auto style", "", 0, 2, 1, 1 },
    { "autoIntensityOption", "Auto intensity", "%", 0, 100, 75, 1 },
    { "monitorMonoOn", "Mono", "", 0, 1, 0, 1, true },
    { "kitGroup", "Kit group", "", 0, 7, 0, 1 },
    { "kitMic", "Kit mic", "", 0, 3, 0, 1 },
    { "kitMode", "Kit mode", "", 0, 2, 1, 1 },
    { "kitStrength", "Kit strength", "%", 0, 100, 100, 1 },
    { "kitPolarity", "Kit polarity", "", 0, 1, 0, 1, true },
    { "kitDelayMs", "Kit delay", "ms", 0, 20, 0, 1 }
}};

// Kit settings travel with the instrument (per ARA audio modification, or insert parameters)
// but are never part of an Auto preset proposal.
inline void keepKitSettings(std::array<float, controlCount>& target, const std::array<float, controlCount>& source) noexcept
{
    for (auto control : { kitGroup, kitMic, kitMode, kitStrength, kitPolarity, kitDelay })
        target[static_cast<size_t>(control)] = source[static_cast<size_t>(control)];
}

using Settings = std::array<float, controlCount>;

inline float bandQ(const Settings& settings, size_t band) noexcept
{
    const std::array<int, 4> controls { bodyQ, boxQ, attackQ, airQ };
    const auto value = settings[static_cast<size_t>(controls[band])];
    return value > 0 ? juce::jmax(0.3f, value) : settings[eqQ];
}

inline Settings defaultSettings() noexcept
{
    Settings result {};
    for (size_t index = 0; index < result.size(); ++index)
        result[index] = controlSpecs[index].initial;
    return result;
}

struct AtomicSettings
{
    AtomicSettings() noexcept { store(defaultSettings()); }
    Settings load() const noexcept
    {
        Settings result {};
        for (size_t index = 0; index < result.size(); ++index)
            result[index] = values[index].load(std::memory_order_relaxed);
        return result;
    }
    void store(const Settings& settings) noexcept
    {
        for (size_t index = 0; index < settings.size(); ++index)
            values[index].store(settings[index], std::memory_order_relaxed);
    }
    std::array<std::atomic<float>, controlCount> values;
};

class DrumEngine
{
public:
    void prepare(double sampleRate, int blockSize, int channels)
    {
        rate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(juce::jmax(1, blockSize)),
                                    static_cast<juce::uint32>(juce::jlimit(1, 2, channels)) };
        high.prepare(spec);
        low.prepare(spec);
        high.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        low.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        juce::dsp::ProcessSpec monoSpec { sampleRate, spec.maximumBlockSize, 1 };
        gateEnvelope.prepare(monoSpec);
        compressionEnvelope.prepare(monoSpec);
        presence.prepare(sampleRate, blockSize, channels);
        multiband.prepare(sampleRate, blockSize, channels);
        bus.prepare(sampleRate, blockSize, channels);
        finish.prepare(sampleRate, blockSize);
        aligner.prepare(sampleRate, blockSize, static_cast<int>(spec.numChannels));
        for (auto& smooth : smoothed)
            smooth.reset(rate, 0.02);
        for (auto& quality : smoothedQuality)
            quality.reset(rate, 0.02);
        firstBlock = true;
        updateFilters(defaultSettings());
        reset();
    }

    void reset() noexcept
    {
        firstBlock = true;
        high.reset();
        low.reset();
        gateEnvelope.reset();
        compressionEnvelope.reset();
        presence.reset();
        multiband.reset();
        bus.reset();
        finish.reset();
        aligner.reset();
        for (auto& meter : finishMeters) meter.store(0, std::memory_order_relaxed);
        glueEnvelopeMeter.store(-160);
        glueReductionMeter.store(0);
        multibandRunning = false;
        for (auto& meter : bandReductionMeters) meter.store(0);
        for (auto& meter : bandPeakMeters) meter.store(0);
        for (auto& meter : bandEnvelopeMeters) meter.store(-160);
        presenceRunning = false;
        detectedHits.store(0, std::memory_order_relaxed);
        displayedCompression = 0;
        compressionDisplayMeter.store(0, std::memory_order_relaxed);
        displayedExpansion = 0;
        gateDisplayMeter.store(0, std::memory_order_relaxed);
        for (auto& channel : bands)
            for (auto& filter : channel)
                filter.reset();
    }

    void process(juce::AudioBuffer<float>& buffer, const Settings& requested) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        // Kit alignment first (raw requested values: the aligner crossfades its own changes).
        // Bypass also removes it so A/B compares with the untouched recording.
        const bool bypassed = std::isfinite(requested[bypass]) && requested[bypass] > 0.5f;
        aligner.process(buffer, bypassed ? 0.0f : requested[kitDelay], !bypassed && requested[kitPolarity] > 0.5f);
        Settings settings {};
        for (size_t index = 0; index < settings.size(); ++index)
        {
            const auto& spec = controlSpecs[index];
            const auto value = std::isfinite(requested[index]) ? requested[index] : spec.initial;
            settings[index] = juce::jlimit(spec.minimum, spec.maximum, value);
            if (firstBlock)
                smoothed[index].setCurrentAndTargetValue(settings[index]);
            else
                smoothed[index].setTargetValue(settings[index]);
        }
        for (size_t band = 0; band < smoothedQuality.size(); ++band)
        {
            if (firstBlock) smoothedQuality[band].setCurrentAndTargetValue(bandQ(settings, band));
            else smoothedQuality[band].setTargetValue(bandQ(settings, band));
        }
        firstBlock = false;
        gateEnvelope.setAttackTime(settings[gateAttack]);
        gateEnvelope.setReleaseTime(settings[gateRelease]);
        compressionEnvelope.setAttackTime(settings[attack]);
        compressionEnvelope.setReleaseTime(settings[release]);
        float inputPeak = 0, outputPeak = 0, gateReduction = 0, compressionReduction = 0;
        float maximumCleanup = 0, maximumBody = 0;
        float maximumGlue = 0, maximumGlueEnvelope = -160;
        multiband.beginBlock();
        finish.beginBlock();
        bus.beginBlock();
        const auto channels = juce::jmin(2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (size_t index = 0; index < settings.size(); ++index)
                settings[index] = smoothed[index].getNextValue();
            for (size_t band = 0; band < smoothedQuality.size(); ++band)
                settings[static_cast<size_t>(bodyQ) + band] = smoothedQuality[band].getNextValue();
            if ((sample & 15) == 0)
                updateFilters(settings);
            std::array<float, 2> dry {}, signal {};
            float detector = 0;
            for (int channel = 0; channel < channels; ++channel)
            {
                dry[static_cast<size_t>(channel)] = buffer.getSample(channel, sample);
                inputPeak = juce::jmax(inputPeak, std::abs(dry[static_cast<size_t>(channel)]));
                const auto gained = dry[static_cast<size_t>(channel)] * juce::Decibels::decibelsToGain(settings[input]);
                const auto filtered = low.processSample(channel, high.processSample(channel, gained));
                signal[static_cast<size_t>(channel)] = gained + settings[filtersOn] * (filtered - gained);
                detector = juce::jmax(detector, std::abs(signal[static_cast<size_t>(channel)]));
            }
            const auto gateLevel = juce::Decibels::gainToDecibels(gateEnvelope.processSample(0, detector), -160.0f);
            auto attenuation = juce::jlimit(-settings[gateRange], 0.0f,
                (gateLevel - settings[gateThreshold]) * (settings[gateRatio] - 1.0f));
            const auto runPresence = settings[focusOn] + settings[cleanupOn] + settings[presenceOn] > 0.0001f;
            if (runPresence)
            {
                presenceRunning = true;
                const auto focusedReduction = presence.detect(signal, channels);
                attenuation += settings[focusOn] * (-focusedReduction - attenuation);
            }
            else if (presenceRunning)
            {
                presence.reset();
                presenceRunning = false;
            }
            attenuation *= settings[gateOn];
            const auto gateGain = juce::Decibels::decibelsToGain(attenuation);
            gateReduction = juce::jmax(gateReduction, -attenuation);
            for (int channel = 0; channel < channels; ++channel)
                signal[static_cast<size_t>(channel)] *= gateGain;
            if (runPresence)
            {
                presence.process(signal, channels, settings[cleanupOn], settings[presenceOn]);
                maximumCleanup = juce::jmax(maximumCleanup, presence.cleanupReduction());
                maximumBody = juce::jmax(maximumBody, presence.bodyMagnitude() * settings[presenceOn]);
            }
            detector = 0;
            for (int channel = 0; channel < channels; ++channel)
            {
                auto& value = signal[static_cast<size_t>(channel)];
                auto equalized = value;
                for (auto& filter : bands[static_cast<size_t>(channel)])
                    equalized = filter.processSample(equalized);
                value += settings[eqOn] * (equalized - value);
                detector = juce::jmax(detector, std::abs(value));
            }
            const auto compressionLevel = juce::Decibels::gainToDecibels(compressionEnvelope.processSample(0, detector), -160.0f);
            const auto reduction = juce::jmax(0.0f, compressionLevel - settings[threshold])
                * (1.0f - 1.0f / settings[ratio]);
            const auto compressedGain = juce::Decibels::decibelsToGain(settings[makeup] - reduction);
            const auto wet = settings[compressorOn] * settings[mix] * 0.01f;
            auto multibandSignal = signal;
            const auto multibandAmount = settings[multibandOn] * settings[compressorOn];
            if (multibandAmount > 0.0001f)
            {
                multibandRunning = true;
                multiband.process(multibandSignal, channels, settings[mix] * 0.01f, settings[makeup]);
            }
            else if (multibandRunning)
            {
                multiband.reset();
                multibandRunning = false;
            }
            compressionReduction = juce::jmax(compressionReduction, reduction * settings[compressorOn] * (1 - multibandAmount)
                + *std::max_element(multiband.reductions.begin(), multiband.reductions.end()) * multibandAmount);
            for (int channel = 0; channel < channels; ++channel)
            {
                auto value = signal[static_cast<size_t>(channel)] * (1.0f + wet * (compressedGain - 1.0f));
                value += multibandAmount * (multibandSignal[static_cast<size_t>(channel)] - value);
                signal[static_cast<size_t>(channel)] = value;
            }
            auto busSignal = signal;
            const auto glueReduction = bus.process(busSignal, channels);
            maximumGlue = juce::jmax(maximumGlue, glueReduction);
            maximumGlueEnvelope = juce::jmax(maximumGlueEnvelope, bus.envelopeDb());
            compressionReduction = juce::jmax(compressionReduction, glueReduction * settings[glueOn]);
            finish.process(busSignal, dry, channels);
            for (int channel = 0; channel < channels; ++channel)
                busSignal[static_cast<size_t>(channel)] *= juce::Decibels::decibelsToGain(settings[output]);
            if (channels == 2)
            {
                const auto mono = (busSignal[0] + busSignal[1]) * 0.5f;
                for (auto& channel : busSignal) channel += settings[monitorMonoOn] * (mono - channel);
            }
            for (int channel = 0; channel < channels; ++channel)
            {
                auto value = busSignal[static_cast<size_t>(channel)];
                value += settings[bypass] * (dry[static_cast<size_t>(channel)] - value);
                buffer.setSample(channel, sample, value);
                outputPeak = juce::jmax(outputPeak, std::abs(value));
            }
        }
        inputMeter.store(inputPeak, std::memory_order_relaxed);
        outputMeter.store(outputPeak, std::memory_order_relaxed);
        gateMeter.store(gateReduction, std::memory_order_relaxed);
        cleanupMeter.store(maximumCleanup, std::memory_order_relaxed);
        reinforcementMeter.store(maximumBody, std::memory_order_relaxed);
        detectedHits.store(presence.hitCount(), std::memory_order_relaxed);
        compressionMeter.store(compressionReduction, std::memory_order_relaxed);
        glueReductionMeter.store(maximumGlue, std::memory_order_relaxed);
        glueEnvelopeMeter.store(maximumGlueEnvelope, std::memory_order_relaxed);
        finishMeters[0].store(finish.crushReduction, std::memory_order_relaxed);
        finishMeters[1].store(finish.reductions[0], std::memory_order_relaxed);
        finishMeters[2].store(finish.reductions[1], std::memory_order_relaxed);
        finishMeters[3].store(finish.matchDb(), std::memory_order_relaxed);
        finishMeters[4].store(finish.correlation(), std::memory_order_relaxed);
        finishMeters[5].store(finish.shapeChange, std::memory_order_relaxed);
        finishMeters[6].store(finish.colourPeak, std::memory_order_relaxed);
        finishMeters[7].store(finish.clipReduction, std::memory_order_relaxed);
        finishMeters[8].store(bus.roomMagnitude(), std::memory_order_relaxed);
        finishMeters[9].store(bus.roomCompression(), std::memory_order_relaxed);
        finishMeters[10].store(static_cast<float>((++meterFrame) & 0xffffffu), std::memory_order_relaxed);
        finishMeters[11].store(maximumGlue, std::memory_order_relaxed);
        finishMeters[12].store(maximumCleanup, std::memory_order_relaxed);
        finishMeters[13].store(maximumBody, std::memory_order_relaxed);
        finishMeters[14].store(static_cast<float>(presence.hitCount()), std::memory_order_relaxed);
        for (size_t band = 0; band < 3; ++band)
        {
            bandReductionMeters[band].store(multiband.reductions[band], std::memory_order_relaxed);
            bandPeakMeters[band].store(multiband.peaks[band], std::memory_order_relaxed);
            bandEnvelopeMeters[band].store(multiband.envelopePeaks[band], std::memory_order_relaxed);
        }
        const auto decay = static_cast<float>(std::exp(-buffer.getNumSamples() / (rate * 0.3)));
        displayedCompression = juce::jmax(compressionReduction, displayedCompression * decay);
        compressionDisplayMeter.store(displayedCompression, std::memory_order_relaxed);
        const auto expansionDecay = static_cast<float>(std::exp(-buffer.getNumSamples() / (rate * 0.15)));
        displayedExpansion = juce::jmax(gateReduction, displayedExpansion * expansionDecay);
        gateDisplayMeter.store(displayedExpansion, std::memory_order_relaxed);
    }

    std::atomic<float> inputMeter { 0 }, outputMeter { 0 }, gateMeter { 0 }, compressionMeter { 0 };
    std::atomic<float> compressionDisplayMeter { 0 };
    std::atomic<float> gateDisplayMeter { 0 };
    std::atomic<float> cleanupMeter { 0 }, reinforcementMeter { 0 };
    std::atomic<int> detectedHits { 0 };
    std::array<std::atomic<float>, 3> bandReductionMeters {}, bandPeakMeters {};
    std::array<std::atomic<float>, 3> bandEnvelopeMeters {};
    std::atomic<float> glueReductionMeter { 0 }, glueEnvelopeMeter { -160 };
    std::array<std::atomic<float>, finishMeterCount> finishMeters {};

private:
    kit::KitAligner aligner;
    unsigned int meterFrame = 0;
    void updateFilters(const Settings& settings) noexcept
    {
        FinishParameters finishing;
        finishing.levelMatch = settings[levelMatchOn];
        finishing.crushOn = settings[crushOn]; finishing.crushAmount = settings[crushAmount];
        finishing.crushThreshold = settings[crushThreshold]; finishing.crushRatio = settings[crushRatio];
        finishing.crushAttack = settings[crushAttack]; finishing.crushRelease = settings[crushRelease];
        finishing.crushHP = settings[crushHP]; finishing.crushLP = settings[crushLP];
        finishing.colourOn = settings[colourOn]; finishing.colourDrive = settings[colourDrive]; finishing.colourAmount = settings[colourMix];
        finishing.clipOn = settings[clipOn]; finishing.clipCeiling = settings[clipCeiling]; finishing.clipAmount = settings[clipMix];
        finishing.shapeOn = settings[shapeOn]; finishing.shapeAttack = settings[shapeAttack]; finishing.shapeSustain = settings[shapeSustain];
        finishing.dynamicOn = settings[dynamicOn];
        finishing.frequency = { settings[dynamicLowHz], settings[dynamicHighHz] };
        finishing.quality = { settings[dynamicLowQ], settings[dynamicHighQ] };
        finishing.threshold = { settings[dynamicLowThreshold], settings[dynamicHighThreshold] };
        finishing.range = { settings[dynamicLowRange], settings[dynamicHighRange] };
        finish.configure(finishing);
        DrumBusParameters parameters;
        parameters.compressorEnabled = settings[glueOn] > 0.0001f;
        parameters.thresholdDb = settings[glueThreshold];
        parameters.ratio = settings[glueRatio];
        parameters.attackMs = settings[glueAttack];
        parameters.releaseMs = settings[glueRelease];
        parameters.makeupDb = settings[glueMakeup];
        parameters.mix = settings[glueMix] * 0.01f * settings[glueOn];
        parameters.sidechainHPHz = settings[glueSidechain];
        parameters.kneeDb = settings[glueKnee];
        parameters.rangeDb = settings[glueRange];
        parameters.ambienceEnabled = settings[ambienceOn] > 0.0001f;
        parameters.ambiencePercent = settings[ambienceAmount] * settings[ambienceOn];
        parameters.size = settings[ambienceSize] * 0.01f;
        parameters.predelayMs = settings[ambiencePredelay];
        parameters.wetHPHz = settings[ambienceHighPass];
        parameters.wetLPHz = settings[ambienceLowPass];
        parameters.character = settings[ambienceCharacter];
        parameters.density = settings[ambienceCompression];
        bus.configure(parameters);
        if (settings[multibandOn] * settings[compressorOn] > 0.0001f)
        {
            MultibandParameters multibandParameters;
            multibandParameters.lowHz = settings[crossoverLow];
            multibandParameters.highHz = settings[crossoverHigh];
            for (size_t band = 0; band < 3; ++band)
                for (size_t control = 0; control < 7; ++control)
                        multibandParameters.bands[band][control] = settings[static_cast<size_t>(lowThreshold) + band * 7 + control];
                    multiband.configure(multibandParameters);
        }
        if (settings[focusOn] + settings[cleanupOn] + settings[presenceOn] > 0.0001f)
            presence.configure({ settings[focusLow], settings[focusHigh], settings[gateHold], settings[gateHysteresis],
                settings[gateThreshold], settings[gateRatio], settings[gateRange], settings[gateAttack], settings[gateRelease],
                settings[cleanupDepth], settings[reinforceFrequency], settings[reinforceAmount], settings[reinforceDecay],
                settings[transientAmount], settings[saturationDrive], settings[reinforceInvert] > 0.5f });
        const auto frequency = [&](float value) { return juce::jlimit(10.0f, static_cast<float>(rate * 0.45), value); };
        high.setCutoffFrequency(frequency(settings[highPass]));
        low.setCutoffFrequency(frequency(settings[lowPass]));
        const std::array<int, 4> frequencies { bodyFrequency, boxFrequency, attackFrequency, airFrequency };
        const std::array<int, 4> gains { bodyGain, boxGain, attackGain, airGain };
        for (size_t band = 0; band < frequencies.size(); ++band)
        {
            const auto coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(rate,
                frequency(settings[static_cast<size_t>(frequencies[band])]), bandQ(settings, band),
                juce::Decibels::decibelsToGain(settings[static_cast<size_t>(gains[band])]));
            for (auto& channel : bands)
                *channel[band].coefficients = coefficients;
        }
    }

    double rate = 48000;
    float displayedCompression = 0;
    float displayedExpansion = 0;
    bool firstBlock = true;
    bool presenceRunning = false;
    bool multibandRunning = false;
    KickPresenceDSP presence;
    DrumMultiband multiband;
    DrumBusDSP bus;
    DrumFinishDSP finish;
    juce::dsp::StateVariableTPTFilter<float> high, low;
    juce::dsp::BallisticsFilter<float> gateEnvelope, compressionEnvelope;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 4>, 2> bands;
    std::array<juce::SmoothedValue<float>, controlCount> smoothed;
    std::array<juce::SmoothedValue<float>, 4> smoothedQuality;
};
}