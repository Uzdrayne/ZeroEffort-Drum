#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace drum
{
struct MultibandParameters
{
    float lowHz = 140, highHz = 2000;
    std::array<std::array<float, 7>, 3> bands {{
        { -24, 3, 15, 140, 0, 12, 0 },
        { -24, 2, 12, 110, 0, 8, 0 },
        { -24, 1.5f, 10, 80, 0, 6, 0 }
    }};
};

class DrumMultiband
{
public:
    void prepare(double sampleRate, int blockSize, int channels)
    {
        rate = sampleRate;
        const juce::dsp::ProcessSpec spec { rate, static_cast<juce::uint32>(juce::jmax(1, blockSize)),
            static_cast<juce::uint32>(juce::jlimit(1, 2, channels)) };
        lower.prepare(spec);
        upper.prepare(spec);
        lowPhase.prepare(spec);
        lowPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
        for (auto& envelope : envelopes) envelope.prepare({ rate, spec.maximumBlockSize, 1 });
        configured = false;
        configure({});
        reset();
    }

    void configure(const MultibandParameters& requested) noexcept
    {
        if (configured && requested.lowHz == parameters.lowHz && requested.highHz == parameters.highHz
            && requested.bands == parameters.bands) return;
        parameters = requested;
        configured = true;
        const auto low = juce::jlimit(40.0f, static_cast<float>(rate * 0.2), parameters.lowHz);
        const auto high = juce::jlimit(low * 2, static_cast<float>(rate * 0.45), parameters.highHz);
        lower.setCutoffFrequency(low);
        upper.setCutoffFrequency(high);
        lowPhase.setCutoffFrequency(high);
        for (size_t band = 0; band < envelopes.size(); ++band)
        {
            envelopes[band].setAttackTime(parameters.bands[band][2]);
            envelopes[band].setReleaseTime(parameters.bands[band][3]);
        }
    }

    void reset() noexcept
    {
        lower.reset();
        upper.reset();
        lowPhase.reset();
        for (auto& envelope : envelopes) envelope.reset();
        beginBlock();
    }

    void beginBlock() noexcept { peaks.fill(0); reductions.fill(0); envelopePeaks.fill(-160); }

    void process(std::array<float, 2>& signal, int channels, float mix, float masterMakeup) noexcept
    {
        std::array<std::array<float, 2>, 3> separated {};
        std::array<float, 3> detector {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            float low = 0, remaining = 0, middle = 0, high = 0;
            lower.processSample(channel, signal[index], low, remaining);
            upper.processSample(channel, remaining, middle, high);
            separated[0][index] = lowPhase.processSample(channel, low);
            separated[1][index] = middle;
            separated[2][index] = high;
            for (size_t band = 0; band < separated.size(); ++band)
                detector[band] = juce::jmax(detector[band], std::abs(separated[band][index]));
        }
        const auto anySolo = parameters.bands[0][6] > 0.5f || parameters.bands[1][6] > 0.5f || parameters.bands[2][6] > 0.5f;
        signal.fill(0);
        for (size_t band = 0; band < separated.size(); ++band)
        {
            const auto& values = parameters.bands[band];
            peaks[band] = juce::jmax(peaks[band], detector[band]);
            const auto level = juce::Decibels::gainToDecibels(envelopes[band].processSample(0, detector[band]), -160.0f);
            envelopePeaks[band] = juce::jmax(envelopePeaks[band], level);
            const auto reduction = juce::jlimit(0.0f, values[5], juce::jmax(0.0f, level - values[0]) * (1 - 1 / values[1]));
            reductions[band] = juce::jmax(reductions[band], reduction);
            const auto gain = 1 + mix * (juce::Decibels::decibelsToGain(values[4] + masterMakeup - reduction) - 1);
            if (!anySolo || values[6] > 0.5f)
                for (int channel = 0; channel < channels; ++channel)
                    signal[static_cast<size_t>(channel)] += separated[band][static_cast<size_t>(channel)] * gain;
        }
    }

    std::array<float, 3> peaks {}, reductions {};
    std::array<float, 3> envelopePeaks {};

private:
    double rate = 48000;
    bool configured = false;
    MultibandParameters parameters;
    juce::dsp::LinkwitzRileyFilter<float> lower, upper, lowPhase;
    std::array<juce::dsp::BallisticsFilter<float>, 3> envelopes;
};
}