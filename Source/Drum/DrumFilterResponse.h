#pragma once

#include "DrumEngine.h"

namespace drum
{
class DrumDynamicResponse
{
public:
    DrumDynamicResponse(const Settings& settings, double sampleRate, const std::array<float, 2>& reductions)
        : enabled(settings[dynamicOn] > 0.5f && settings[bypass] < 0.5f), rate(sampleRate)
    {
        for (size_t band = 0; band < filters.size(); ++band)
        {
            const auto base = static_cast<size_t>(band == 0 ? dynamicLowHz : dynamicHighHz);
            const auto frequency = juce::jlimit(20.0f, static_cast<float>(rate * 0.4), settings[base]);
            const auto reduction = enabled ? juce::jlimit(0.0f, settings[base + 3], reductions[band]) : 0.0f;
            filters[band] = juce::dsp::IIR::Coefficients<double>::makePeakFilter(rate, frequency,
                settings[base + 1], juce::Decibels::decibelsToGain(-static_cast<double>(reduction)));
        }
    }

    double magnitude(double frequency) const
    {
        double result = 1;
        for (const auto& filter : filters)
            result *= filter->getMagnitudeForFrequency(juce::jlimit(0.0, rate * 0.49, frequency), rate);
        return result;
    }

    const bool enabled;

private:
    double rate;
    std::array<juce::dsp::IIR::Coefficients<double>::Ptr, 2> filters;
};

class DrumFilterResponse
{
public:
    DrumFilterResponse(const Settings& settings, double sampleRate)
        : enabled(settings[filtersOn] > 0.5f),
          highCutoff(juce::jlimit(10.0, sampleRate * 0.45, static_cast<double>(settings[highPass]))),
          lowCutoff(juce::jlimit(10.0, sampleRate * 0.45, static_cast<double>(settings[lowPass]))),
          high(juce::dsp::IIR::Coefficients<double>::makeHighPass(sampleRate, highCutoff)),
          low(juce::dsp::IIR::Coefficients<double>::makeLowPass(sampleRate, lowCutoff)), rate(sampleRate)
    {
    }

    double magnitude(double frequency) const
    {
        return highMagnitude(frequency) * lowMagnitude(frequency);
    }

    double highMagnitude(double frequency) const
    {
        return enabled ? high->getMagnitudeForFrequency(juce::jlimit(0.0, rate * 0.49, frequency), rate) : 1.0;
    }

    double lowMagnitude(double frequency) const
    {
        return enabled ? low->getMagnitudeForFrequency(juce::jlimit(0.0, rate * 0.49, frequency), rate) : 1.0;
    }

    const bool enabled;
    const double highCutoff, lowCutoff;

private:
    juce::dsp::IIR::Coefficients<double>::Ptr high, low;
    double rate;
};
}