#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace drum
{
struct KickPresenceParameters
{
    float focusLow = 35, focusHigh = 180, holdMs = 25, hysteresisDb = 6;
    float thresholdDb = -35, ratio = 2, rangeDb = 8, attackMs = 1, releaseMs = 100;
    float cleanupDb = 0, bodyHz = 60, bodyAmount = 0, decayMs = 180, transientDb = 0, driveDb = 0;
    bool inverted = false;
};

class KickPresenceDSP
{
public:
    void prepare(double sampleRate, int blockSize, int channels)
    {
        configured = false;
        rate = sampleRate;
        const juce::dsp::ProcessSpec spec { rate, static_cast<juce::uint32>(juce::jmax(1, blockSize)),
            static_cast<juce::uint32>(juce::jlimit(1, 2, channels)) };
        detectorHigh.prepare(spec);
        detectorLow.prepare(spec);
        splitLow.prepare(spec);
        bassLow.prepare(spec);
        detectorHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        detectorLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        splitLow.setCutoffFrequency(static_cast<float>(juce::jmin(1600.0, rate * 0.4)));
        bassLow.setCutoffFrequency(static_cast<float>(juce::jmin(250.0, rate * 0.4)));
        const juce::dsp::ProcessSpec mono { rate, spec.maximumBlockSize, 1 };
        fast.prepare(mono);
        slow.prepare(mono);
        fast.setAttackTime(0.3f);
        fast.setReleaseTime(15);
        slow.setAttackTime(8);
        slow.setReleaseTime(100);
        oscillator.prepare(mono);
        configure({});
        reset();
    }

    void configure(const KickPresenceParameters& parameters) noexcept
    {
        const std::array<float, 17> values { parameters.focusLow, parameters.focusHigh, parameters.holdMs,
            parameters.hysteresisDb, parameters.thresholdDb, parameters.ratio, parameters.rangeDb,
            parameters.attackMs, parameters.releaseMs, parameters.cleanupDb, parameters.bodyHz,
            parameters.bodyAmount, parameters.decayMs, parameters.transientDb, parameters.driveDb,
            parameters.inverted ? 1.0f : 0.0f, static_cast<float>(rate) };
        if (configured && configuration == values) return;
        configuration = values;
        configured = true;
        params = parameters;
        const auto upper = static_cast<float>(rate * 0.4);
        detectorHigh.setCutoffFrequency(juce::jlimit(10.0f, upper, params.focusLow));
        detectorLow.setCutoffFrequency(juce::jlimit(10.0f, upper, juce::jmax(params.focusLow + 10, params.focusHigh)));
        oscillator.setFrequency(params.bodyHz, true);
        attackCoefficient = static_cast<float>(std::exp(-1 / (rate * params.attackMs * 0.001)));
        releaseCoefficient = static_cast<float>(std::exp(-1 / (rate * params.releaseMs * 0.001)));
        decayCoefficient = static_cast<float>(std::exp(std::log(0.001) / (rate * params.decayMs * 0.001)));
        transientCoefficient = static_cast<float>(std::exp(-1 / (rate * 0.012)));
        drive = juce::Decibels::decibelsToGain(params.driveDb);
        driveNormalization = 1 / std::tanh(drive);
    }

    void reset() noexcept
    {
        detectorHigh.reset();
        detectorLow.reset();
        splitLow.reset();
        bassLow.reset();
        fast.reset();
        slow.reset();
        oscillator.reset();
        opened = false;
        armed = true;
        holdRemaining = refractory = bodyAge = 0;
        gateGain = 1;
        bodyEnvelope = bodyVelocity = transientEnvelope = detectorLevel = 0;
        weights = { 1, 1 };
        hits = 0;
        lastCleanup = lastBody = 0;
    }

    float detect(const std::array<float, 2>& signal, int channels) noexcept
    {
        std::array<float, 2> focused {};
        float magnitude = 0;
        int lead = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            focused[index] = detectorLow.processSample(channel, detectorHigh.processSample(channel, signal[index]));
            if (std::abs(focused[index]) > magnitude)
            {
                magnitude = std::abs(focused[index]);
                lead = channel;
            }
        }
        const auto quick = fast.processSample(0, magnitude);
        const auto background = slow.processSample(0, magnitude);
        detectorLevel = juce::Decibels::gainToDecibels(quick, -160.0f);
        if (refractory > 0) --refractory;
        if (quick < background * 1.15f) armed = true;
        if (armed && refractory == 0 && quick > background * 1.65f
            && quick > juce::Decibels::decibelsToGain(params.thresholdDb + 3))
        {
            ++hits;
            armed = false;
            refractory = static_cast<int>(rate * 0.09);
            bodyAge = 0;
            bodyVelocity = quick;
            bodyEnvelope = transientEnvelope = 1;
            oscillator.reset();
            polarity = focused[static_cast<size_t>(lead)] >= 0 ? -1.0f : 1.0f;
            for (int channel = 0; channel < channels; ++channel)
                weights[static_cast<size_t>(channel)] = magnitude > 0
                    ? focused[static_cast<size_t>(channel)] / focused[static_cast<size_t>(lead)] : 0;
        }
        if (bodyAge < static_cast<int>(rate * 0.01)) bodyVelocity = juce::jmax(bodyVelocity, quick);
        if (detectorLevel >= params.thresholdDb)
        {
            opened = true;
            holdRemaining = static_cast<int>(rate * params.holdMs * 0.001);
        }
        else if (holdRemaining > 0) --holdRemaining;
        else if (detectorLevel < params.thresholdDb - params.hysteresisDb) opened = false;
        const auto reduction = opened ? 0.0f : juce::jlimit(-params.rangeDb, 0.0f,
            (detectorLevel - (params.thresholdDb - params.hysteresisDb)) * (params.ratio - 1));
        const auto target = juce::Decibels::decibelsToGain(reduction);
        const auto coefficient = target > gateGain ? attackCoefficient : releaseCoefficient;
        gateGain = target + coefficient * (gateGain - target);
        return -juce::Decibels::gainToDecibels(gateGain, -160.0f);
    }

    void process(std::array<float, 2>& signal, int channels, float cleanupAmount, float presenceAmount) noexcept
    {
        const auto closed = params.rangeDb > 0 ? juce::jlimit(0.0f, 1.0f,
            -juce::Decibels::gainToDecibels(gateGain, -160.0f) / params.rangeDb) : 0.0f;
        const auto trebleGain = juce::Decibels::decibelsToGain(-params.cleanupDb * cleanupAmount * closed);
        lastCleanup = params.cleanupDb * cleanupAmount * closed;
        const auto fadeIn = juce::jmin(1.0f, static_cast<float>(bodyAge) / static_cast<float>(rate * 0.002));
        const auto body = oscillator.processSample(0) * bodyEnvelope * bodyVelocity * fadeIn
            * params.bodyAmount * 0.01f * polarity * (params.inverted ? -1.0f : 1.0f);
        lastBody = std::abs(body);
        const auto transientGain = 1 + transientEnvelope * (juce::Decibels::decibelsToGain(params.transientDb) - 1);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& value = signal[index];
            const auto lower = splitLow.processSample(channel, value);
            value += (value - lower) * (trebleGain - 1);
            const auto enriched = value * transientGain + body * weights[index];
            const auto bass = bassLow.processSample(channel, enriched);
            const auto harmonicDifference = params.driveDb > 0.001f && presenceAmount > 0
                ? (std::tanh(drive * bass) * driveNormalization - bass) * 0.25f : 0.0f;
            value += presenceAmount * (enriched + harmonicDifference - value);
        }
        bodyEnvelope *= decayCoefficient;
        transientEnvelope *= transientCoefficient;
        if (bodyEnvelope < 1.0e-8f) bodyEnvelope = 0;
        if (transientEnvelope < 1.0e-8f) transientEnvelope = 0;
        if (bodyAge < static_cast<int>(rate * 10)) ++bodyAge;
    }

    int hitCount() const noexcept { return hits; }
    float levelDb() const noexcept { return detectorLevel; }
    float cleanupReduction() const noexcept { return lastCleanup; }
    float bodyMagnitude() const noexcept { return lastBody; }

private:
    double rate = 48000;
    KickPresenceParameters params;
    juce::dsp::StateVariableTPTFilter<float> detectorHigh, detectorLow, splitLow, bassLow;
    juce::dsp::BallisticsFilter<float> fast, slow;
    juce::dsp::Oscillator<float> oscillator { [](float phase) { return std::sin(phase); } };
    std::array<float, 2> weights { 1, 1 };
    float gateGain = 1, attackCoefficient = 0, releaseCoefficient = 0;
    float decayCoefficient = 0, transientCoefficient = 0, drive = 1;
    float driveNormalization = 1;
    float bodyEnvelope = 0, bodyVelocity = 0, transientEnvelope = 0, polarity = 1, detectorLevel = -160;
    int holdRemaining = 0, refractory = 0, bodyAge = 0, hits = 0;
    bool opened = false, armed = true;
    float lastCleanup = 0, lastBody = 0;
    std::array<float, 17> configuration {};
    bool configured = false;
};
}