#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <memory>

namespace drum
{
inline constexpr size_t finishMeterCount = 15;

struct FinishParameters
{
    float crushOn = 0, crushAmount = 0, crushThreshold = -30, crushRatio = 8;
    float crushAttack = 3, crushRelease = 100, crushHP = 80, crushLP = 8000;
    float colourOn = 0, colourDrive = 6, colourAmount = 20;
    float clipOn = 0, clipCeiling = -1, clipAmount = 20;
    float shapeOn = 0, shapeAttack = 0, shapeSustain = 0;
    float dynamicOn = 0;
    std::array<float, 2> frequency { 350, 7000 }, quality { 2, 1 };
    std::array<float, 2> threshold { -24, -24 }, range { 4, 4 };
    float levelMatch = 0;
};

class DrumFinishDSP
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        rate = sampleRate;
        const juce::dsp::ProcessSpec mono { rate, static_cast<juce::uint32>(juce::jmax(1, blockSize)), 1 };
        const juce::dsp::ProcessSpec stereo { rate, mono.maximumBlockSize, 2 };
        compressor.prepare(mono);
        fast.prepare(mono);
        slow.prepare(mono);
        fast.setAttackTime(0.3f);
        fast.setReleaseTime(10);
        slow.setAttackTime(20);
        slow.setReleaseTime(180);
        crushHigh.prepare(stereo);
        crushLow.prepare(stereo);
        crushHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        crushLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        for (size_t band = 0; band < 2; ++band)
        {
            detectors[band].prepare(stereo);
            detectors[band].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
            envelopes[band].prepare(mono);
            envelopes[band].setAttackTime(5);
            envelopes[band].setReleaseTime(100);
            for (auto& channel : equalizers)
            {
                *channel[band].coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(rate, 350, 1, 1);
                channel[band].prepare(mono);
            }
        }
        for (auto& oversampler : oversamplers)
        {
            oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, 2,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampler->initProcessing(1);
        }
        oversampleInput.setSize(2, 1);
        energyCoefficient = std::exp(-1.0 / (rate * 0.5));
        matchCoefficient = static_cast<float>(1 - std::exp(-1.0 / (rate * 0.1)));
        configured = false;
        reset();
    }

    void reset() noexcept
    {
        compressor.reset(); fast.reset(); slow.reset(); crushHigh.reset(); crushLow.reset();
        for (auto& filter : detectors) filter.reset();
        for (auto& envelope : envelopes) envelope.reset();
        for (auto& channel : equalizers) for (auto& filter : channel) filter.reset();
        for (auto& oversampler : oversamplers) if (oversampler) oversampler->reset();
        dryEnergy = wetEnergy = leftEnergy = rightEnergy = crossEnergy = 0;
        matchGain = 1;
        heldPeak = 0;
        previousNonlinear = previousDynamic = false;
        counter = 0;
        reductions.fill(0);
        crushReduction = harmonicPeak = clipReduction = 0;
        shapeChange = colourPeak = 0;
    }

    void configure(const FinishParameters& next) noexcept
    {
        if (!configured || next.crushAttack != parameters.crushAttack) compressor.setAttackTime(next.crushAttack);
        if (!configured || next.crushRelease != parameters.crushRelease) compressor.setReleaseTime(next.crushRelease);
        if (!configured || next.crushHP != parameters.crushHP) crushHigh.setCutoffFrequency(limitFrequency(next.crushHP));
        if (!configured || next.crushLP != parameters.crushLP) crushLow.setCutoffFrequency(limitFrequency(next.crushLP));
        for (size_t band = 0; band < 2; ++band)
        {
            if (!configured || next.frequency[band] != parameters.frequency[band])
                detectors[band].setCutoffFrequency(limitFrequency(next.frequency[band]));
            if (!configured || next.quality[band] != parameters.quality[band]) detectors[band].setResonance(next.quality[band]);
        }
        parameters = next;
        configured = true;
    }

    void beginBlock() noexcept
    {
        reductions.fill(0);
        crushReduction = harmonicPeak = clipReduction = 0;
        shapeChange = colourPeak = 0;
    }

    void process(std::array<float, 2>& signal, const std::array<float, 2>& original, int channels) noexcept
    {
        float peak = 0;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(peak, std::abs(signal[static_cast<size_t>(channel)]));
        const auto fastLevel = fast.processSample(0, peak);
        const auto slowLevel = slow.processSample(0, peak);
        if (parameters.shapeOn > 0 && peak > 0.000001f)
        {
            const auto transient = juce::jlimit(0.0f, 1.0f, (fastLevel - slowLevel) / juce::jmax(0.000001f, fastLevel));
            const auto shapeDb = parameters.shapeOn * (parameters.shapeAttack * transient
                + parameters.shapeSustain * (1 - transient));
            if (std::abs(shapeDb) > std::abs(shapeChange)) shapeChange = shapeDb;
            const auto gain = juce::Decibels::decibelsToGain(shapeDb);
            for (int channel = 0; channel < channels; ++channel) signal[static_cast<size_t>(channel)] *= gain;
        }
        const bool dynamic = parameters.dynamicOn > 0.0001f;
        if (!dynamic && previousDynamic)
        {
            for (auto& envelope : envelopes) envelope.reset();
            for (auto& channel : equalizers) for (auto& filter : channel) filter.reset();
        }
        previousDynamic = dynamic;
        if (dynamic)
            for (size_t band = 0; band < 2; ++band)
            {
                float detector = 0;
                for (int channel = 0; channel < channels; ++channel)
                    detector = juce::jmax(detector, std::abs(detectors[band].processSample(channel, signal[static_cast<size_t>(channel)])));
                const auto levelDb = juce::Decibels::gainToDecibels(envelopes[band].processSample(0, detector), -160.0f);
                const auto reduction = juce::jlimit(0.0f, parameters.range[band],
                    (levelDb - parameters.threshold[band]) * 0.5f) * parameters.dynamicOn;
                reductions[band] = juce::jmax(reductions[band], reduction);
                if ((counter & 15) == 0)
                {
                    const auto coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(rate,
                        limitFrequency(parameters.frequency[band]), parameters.quality[band],
                        juce::Decibels::decibelsToGain(-reduction));
                    for (auto& channel : equalizers) *channel[band].coefficients = coefficients;
                }
                for (int channel = 0; channel < channels; ++channel)
                    signal[static_cast<size_t>(channel)] = equalizers[static_cast<size_t>(channel)][band].processSample(signal[static_cast<size_t>(channel)]);
            }
        ++counter;
        if (parameters.crushOn * parameters.crushAmount > 0.0001f)
        {
            std::array<float, 2> wet {};
            float detector = 0;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                wet[index] = crushLow.processSample(channel, crushHigh.processSample(channel, signal[index]));
                detector = juce::jmax(detector, std::abs(wet[index]));
            }
            const auto levelDb = juce::Decibels::gainToDecibels(compressor.processSample(0, detector), -160.0f);
            const auto reduction = juce::jlimit(0.0f, 30.0f, (levelDb - parameters.crushThreshold) * (1 - 1 / parameters.crushRatio));
            crushReduction = juce::jmax(crushReduction, reduction);
            const auto gain = juce::Decibels::decibelsToGain(juce::jmin(18.0f, -parameters.crushThreshold * 0.4f) - reduction);
            const auto amount = parameters.crushOn * parameters.crushAmount * 0.01f;
            for (int channel = 0; channel < channels; ++channel)
                signal[static_cast<size_t>(channel)] += amount * wet[static_cast<size_t>(channel)] * gain;
        }
        const bool nonlinear = parameters.colourOn * parameters.colourAmount + parameters.clipOn * parameters.clipAmount > 0.0001f;
        if (!nonlinear && previousNonlinear)
            for (auto& oversampler : oversamplers) oversampler->reset();
        previousNonlinear = nonlinear;
        if (nonlinear)
        {
            for (int channel = 0; channel < 2; ++channel)
                oversampleInput.setSample(channel, 0, channel < channels ? signal[static_cast<size_t>(channel)] : 0);
            juce::dsp::AudioBlock<float> inputBlock(oversampleInput);
            auto shaped = oversamplers[0]->processSamplesUp(inputBlock);
            auto reference = oversamplers[1]->processSamplesUp(inputBlock);
            const auto drive = juce::Decibels::decibelsToGain(parameters.colourDrive);
            const auto ceiling = juce::Decibels::decibelsToGain(parameters.clipCeiling);
            for (size_t channel = 0; channel < 2; ++channel)
                for (size_t sample = 0; sample < shaped.getNumSamples(); ++sample)
                {
                    auto& value = shaped.getChannelPointer(channel)[sample];
                    const auto saturated = parameters.colourDrive > 0.0001f ? std::tanh(value * drive) / drive : value;
                    const auto colourChange = parameters.colourOn * parameters.colourAmount * 0.01f * (saturated - value);
                    colourPeak = juce::jmax(colourPeak, std::abs(colourChange));
                    value += colourChange;
                    const auto clipped = ceiling * std::tanh(value / ceiling);
                    const auto clipChange = parameters.clipOn * parameters.clipAmount * 0.01f * (clipped - value);
                    clipReduction = juce::jmax(clipReduction, std::abs(clipChange));
                    value += clipChange;
                }
            juce::ignoreUnused(reference);
            juce::dsp::AudioBlock<float> shapedBlock(oversampledResult.data(), 2, 1);
            juce::dsp::AudioBlock<float> referenceBlock(oversampledReference.data(), 2, 1);
            oversamplers[0]->processSamplesDown(shapedBlock);
            oversamplers[1]->processSamplesDown(referenceBlock);
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                const auto residual = oversampledStorage[index] - referenceStorage[index];
                harmonicPeak = juce::jmax(harmonicPeak, std::abs(residual));
                signal[index] += residual;
            }
        }
        double dryPower = 0, wetPower = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dryPower += original[index] * original[index];
            wetPower += signal[index] * signal[index];
        }
        dryEnergy = energyCoefficient * dryEnergy + (1 - energyCoefficient) * dryPower;
        wetEnergy = energyCoefficient * wetEnergy + (1 - energyCoefficient) * wetPower;
        auto target = parameters.levelMatch > 0.5f && dryEnergy > 1.0e-9 && wetEnergy > 1.0e-9
            ? juce::jlimit(0.5f, 2.0f, static_cast<float>(std::sqrt(dryEnergy / wetEnergy))) : 1.0f;
        float wetPeak = 0;
        for (int channel = 0; channel < channels; ++channel)
            wetPeak = juce::jmax(wetPeak, std::abs(signal[static_cast<size_t>(channel)]));
        heldPeak = juce::jmax(wetPeak, heldPeak * static_cast<float>(energyCoefficient));
        if (target > 1 && heldPeak > 0) target = juce::jmin(target, juce::jmax(1.0f, 0.89125094f / heldPeak));
        matchGain += matchCoefficient * (target - matchGain);
        if (parameters.levelMatch > 0.0001f)
            for (int channel = 0; channel < channels; ++channel)
                signal[static_cast<size_t>(channel)] *= 1 + parameters.levelMatch * (matchGain - 1);
        if (channels == 2)
        {
            leftEnergy = energyCoefficient * leftEnergy + (1 - energyCoefficient) * signal[0] * signal[0];
            rightEnergy = energyCoefficient * rightEnergy + (1 - energyCoefficient) * signal[1] * signal[1];
            crossEnergy = energyCoefficient * crossEnergy + (1 - energyCoefficient) * signal[0] * signal[1];
        }
    }

    std::array<float, 2> reductions {};
    float crushReduction = 0, harmonicPeak = 0, clipReduction = 0, shapeChange = 0, colourPeak = 0;
    float matchDb() const noexcept { return juce::Decibels::gainToDecibels(matchGain); }
    float correlation() const noexcept
    {
        const auto denominator = std::sqrt(leftEnergy * rightEnergy);
        return denominator > 1.0e-9 ? juce::jlimit(-1.0f, 1.0f, static_cast<float>(crossEnergy / denominator)) : 0;
    }

private:
    float limitFrequency(float value) const noexcept { return juce::jlimit(20.0f, static_cast<float>(rate * 0.4), value); }
    double rate = 48000, dryEnergy = 0, wetEnergy = 0, energyCoefficient = 0;
    double leftEnergy = 0, rightEnergy = 0, crossEnergy = 0;
    float matchGain = 1, heldPeak = 0, matchCoefficient = 0;
    unsigned int counter = 0;
    bool previousNonlinear = false, previousDynamic = false, configured = false;
    FinishParameters parameters;
    juce::dsp::BallisticsFilter<float> compressor, fast, slow;
    juce::dsp::StateVariableTPTFilter<float> crushHigh, crushLow;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> detectors;
    std::array<juce::dsp::BallisticsFilter<float>, 2> envelopes;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 2> equalizers;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers;
    juce::AudioBuffer<float> oversampleInput;
    std::array<float, 2> oversampledStorage {}, referenceStorage {};
    std::array<float*, 2> oversampledResult { &oversampledStorage[0], &oversampledStorage[1] };
    std::array<float*, 2> oversampledReference { &referenceStorage[0], &referenceStorage[1] };
};
}