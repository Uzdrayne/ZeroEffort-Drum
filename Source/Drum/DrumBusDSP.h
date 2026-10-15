#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace drum
{
struct DrumBusParameters
{
    bool compressorEnabled = false;
    float attackMs = 10, releaseMs = 100, thresholdDb = -18, ratio = 2;
    float makeupDb = 0, mix = 1, sidechainHPHz = 80, kneeDb = 6, rangeDb = 6;
    bool ambienceEnabled = false;
    float ambiencePercent = 0, size = 0.25f, predelayMs = 10;
    float wetHPHz = 150, wetLPHz = 7000;
    float character = 0, density = 0;
};

class DrumBusDSP
{
public:
    void prepare(double sampleRate, int blockSize, int channels)
    {
        prepared = false;
        rate = std::isfinite(sampleRate) ? juce::jlimit(8000.0, 192000.0, sampleRate) : 48000.0;
        preparedChannels = juce::jlimit(1, 2, channels);
        const juce::dsp::ProcessSpec spec { rate, static_cast<juce::uint32>(juce::jmax(1, blockSize)),
            static_cast<juce::uint32>(preparedChannels) };
        detectorHigh.prepare(spec);
        wetHigh.prepare(spec);
        wetLow.prepare(spec);
        detectorHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        wetHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        wetLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        envelope.prepare({ rate, spec.maximumBlockSize, 1 });
        roomEnvelope.prepare({ rate, spec.maximumBlockSize, 1 });
        roomEnvelope.setAttackTime(5);
        roomEnvelope.setReleaseTime(150);
        envelope.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
        delay.assign(static_cast<size_t>(std::ceil(rate * 0.04)) + 1, {});
        juce::Reverb::Parameters room;
        room.dryLevel = 0;
        room.wetLevel = 1.0f / 3.0f;
        room.width = 1;
        room.damping = 0.7f;
        room.freezeMode = 0;
        reverb.setParameters(room);
        reverb.setSampleRate(rate);
        configured = false;
        compressorActive = ambienceActive = false;
        prepared = true;
        configure(parameters);
        reset();
    }

    void reset() noexcept
    {
        detectedLevelDb = -160;
        beginBlock();
        detectorHigh.reset();
        envelope.reset();
        resetAmbience();
    }

    void configure(const DrumBusParameters& requested) noexcept
    {
        if (!prepared) return;
        DrumBusParameters next = requested;
        next.attackMs = bounded(next.attackMs, 0.1f, 100.0f, 10);
        next.releaseMs = bounded(next.releaseMs, 10.0f, 2000.0f, 100);
        next.thresholdDb = bounded(next.thresholdDb, -80.0f, 0.0f, -18);
        next.ratio = bounded(next.ratio, 1.0f, 20.0f, 2);
        next.makeupDb = bounded(next.makeupDb, -24.0f, 24.0f, 0);
        next.mix = bounded(next.mix, 0.0f, 1.0f, 1);
        next.sidechainHPHz = bounded(next.sidechainHPHz, 0.0f, static_cast<float>(rate * 0.45), 80);
        next.kneeDb = bounded(next.kneeDb, 0.0f, 24.0f, 6);
        next.rangeDb = bounded(next.rangeDb, 0.0f, 60.0f, 6);
        next.ambiencePercent = bounded(next.ambiencePercent, 0.0f, 100.0f, 0);
        next.size = bounded(next.size, 0.0f, 1.0f, 0.25f);
        next.predelayMs = bounded(next.predelayMs, 0.0f, 40.0f, 10);
        next.wetHPHz = bounded(next.wetHPHz, 10.0f, static_cast<float>(rate * 0.4), 150);
        next.wetLPHz = bounded(next.wetLPHz, next.wetHPHz, static_cast<float>(rate * 0.45), 7000);
        next.character = bounded(next.character, 0, 100, 0);
        next.density = bounded(next.density, 0, 100, 0);
        const std::array<float, 18> values { next.attackMs, next.releaseMs, next.thresholdDb, next.ratio,
            next.makeupDb, next.mix, next.sidechainHPHz, next.kneeDb, next.rangeDb,
            next.ambiencePercent, next.size, next.predelayMs, next.wetHPHz, next.wetLPHz,
            next.compressorEnabled ? 1.0f : 0.0f, next.ambienceEnabled ? 1.0f : 0.0f,
            next.character, next.density };
        if (configured && values == configuration) return;
        if (!configured || next.attackMs != parameters.attackMs) envelope.setAttackTime(next.attackMs);
        if (!configured || next.releaseMs != parameters.releaseMs) envelope.setReleaseTime(next.releaseMs);
        if (!configured || next.sidechainHPHz != parameters.sidechainHPHz)
        {
            detectorHigh.setCutoffFrequency(juce::jmax(10.0f, next.sidechainHPHz));
            if (configured && (next.sidechainHPHz == 0 || parameters.sidechainHPHz == 0))
                detectorHigh.reset();
        }
        if (!configured || next.wetHPHz != parameters.wetHPHz) wetHigh.setCutoffFrequency(next.wetHPHz);
        if (!configured || next.wetLPHz != parameters.wetLPHz) wetLow.setCutoffFrequency(next.wetLPHz);
        if (!configured || next.size != parameters.size || next.character != parameters.character)
        {
            auto room = reverb.getParameters();
            room.roomSize = 0.05f + 0.65f * next.size + 0.15f * next.character * 0.01f;
            room.damping = 0.7f - 0.45f * next.character * 0.01f;
            reverb.setParameters(room);
        }
        if (!configured || next.predelayMs != parameters.predelayMs)
            delaySamples = static_cast<size_t>(std::round(rate * next.predelayMs * 0.001));
        if (!configured || next.ratio != parameters.ratio) slope = 1 - 1 / next.ratio;
        if (!configured || next.makeupDb != parameters.makeupDb)
            makeupGain = juce::Decibels::decibelsToGain(next.makeupDb);
        if (!configured || next.kneeDb != parameters.kneeDb)
            kneeFactor = next.kneeDb > 0 ? 1 / (2 * next.kneeDb) : 0;
        const bool runCompressor = next.compressorEnabled && next.mix > 0;
        const bool runAmbience = next.ambienceEnabled && next.ambiencePercent > 0;
        if (compressorActive && !runCompressor)
        {
            detectorHigh.reset();
            envelope.reset();
        }
        if (ambienceActive && !runAmbience) resetAmbience();
        compressorActive = runCompressor;
        ambienceActive = runAmbience;
        ambienceGain = next.ambiencePercent * 0.01f;
        parameters = next;
        configuration = values;
        configured = true;
    }

    float envelopeDb() const noexcept { return detectedLevelDb; }

    void beginBlock() noexcept { roomPeak = roomReductionMeter = 0; }
    float roomMagnitude() const noexcept { return roomPeak; }
    float roomCompression() const noexcept { return roomReductionMeter; }

    float process(std::array<float, 2>& signal, int channels) noexcept
    {
        if (!prepared || channels <= 0) return 0;
        const auto count = juce::jmin(preparedChannels, juce::jmin(2, channels));
        float reduction = 0;
        if (compressorActive)
        {
            float detector = 0;
            for (int channel = 0; channel < count; ++channel)
            {
                const auto value = signal[static_cast<size_t>(channel)];
                const auto filtered = parameters.sidechainHPHz > 0 ? detectorHigh.processSample(channel, value) : value;
                detector = juce::jmax(detector, std::abs(filtered));
            }
            const auto level = juce::Decibels::gainToDecibels(envelope.processSample(0, detector), -160.0f);
            detectedLevelDb = level;
            const auto above = level - parameters.thresholdDb;
            if (slope > 0)
            {
                if (parameters.kneeDb > 0 && above > -0.5f * parameters.kneeDb
                    && above < 0.5f * parameters.kneeDb)
                {
                    const auto kneePosition = above + 0.5f * parameters.kneeDb;
                    reduction = slope * kneePosition * kneePosition * kneeFactor;
                }
                else if (above > 0)
                    reduction = slope * above;
                reduction = juce::jlimit(0.0f, parameters.rangeDb, reduction);
            }
            const auto compressedGain = reduction > 0
                ? makeupGain * juce::Decibels::decibelsToGain(-reduction) : makeupGain;
            const auto gain = 1 + parameters.mix * (compressedGain - 1);
            for (int channel = 0; channel < count; ++channel)
                signal[static_cast<size_t>(channel)] *= gain;
        }
        else detectedLevelDb = -160;
        if (ambienceActive)
        {
            const auto readPosition = (delayPosition + delay.size() - delaySamples) % delay.size();
            std::array<float, 2> wet {};
            for (int channel = 0; channel < count; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                wet[index] = delaySamples == 0 ? signal[index] : delay[readPosition][index];
                delay[delayPosition][index] = signal[index];
            }
            delayPosition = (delayPosition + 1) % delay.size();
            if (count == 2) reverb.processStereo(&wet[0], &wet[1], 1);
            else reverb.processMono(&wet[0], 1);
            const auto wetLevel = juce::Decibels::gainToDecibels(roomEnvelope.processSample(0,
                juce::jmax(std::abs(wet[0]), std::abs(wet[1]))), -160.0f);
            const auto roomReduction = juce::jlimit(0.0f, 12.0f, (wetLevel + 30) * 0.75f);
            roomReductionMeter = juce::jmax(roomReductionMeter, roomReduction * parameters.density * 0.01f);
            const auto roomGain = juce::Decibels::decibelsToGain((6 - roomReduction) * parameters.density * 0.01f);
            for (int channel = 0; channel < count; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                const auto filtered = wetLow.processSample(channel, wetHigh.processSample(channel, wet[index]));
                const auto returned = ambienceGain * filtered * roomGain;
                roomPeak = juce::jmax(roomPeak, std::abs(returned));
                signal[index] += returned;
            }
        }
        return reduction;
    }

private:
    static float bounded(float value, float minimum, float maximum, float fallback) noexcept
    {
        return juce::jlimit(minimum, maximum, std::isfinite(value) ? value : fallback);
    }

    void resetAmbience() noexcept
    {
        reverb.reset();
        roomEnvelope.reset();
        wetHigh.reset();
        wetLow.reset();
        for (auto& frame : delay) frame.fill(0);
        delayPosition = 0;
    }

    double rate = 48000;
    int preparedChannels = 2;
    bool prepared = false, configured = false, compressorActive = false, ambienceActive = false;
    DrumBusParameters parameters;
    std::array<float, 18> configuration {};
    juce::dsp::StateVariableTPTFilter<float> detectorHigh, wetHigh, wetLow;
    juce::dsp::BallisticsFilter<float> envelope;
    juce::Reverb reverb;
    juce::dsp::BallisticsFilter<float> roomEnvelope;
    std::vector<std::array<float, 2>> delay;
    size_t delayPosition = 0, delaySamples = 0;
    float slope = 0, makeupGain = 1, kneeFactor = 0, ambienceGain = 0;
    float detectedLevelDb = -160;
    float roomPeak = 0, roomReductionMeter = 0;
};
}