#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "KitTypes.h"

namespace kit
{
// Polarity and delay correction applied at the very start of the chain.
// Integer-sample delay (no interpolation filtering), changes are crossfaded over 20 ms and the
// polarity is ramped, so automation or Auto corrections never click. No latency is reported:
// only close microphones are delayed onto the overheads.
class KitAligner
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channelCount)
    {
        rate = sampleRate > 0 ? sampleRate : 48000.0;
        const auto needed = static_cast<int>(std::ceil(rate * maxDelayMs / 1000.0)) + juce::jmax(1, maximumBlockSize) + 8;
        capacity = 1;
        while (capacity < needed) capacity <<= 1;
        history.setSize(juce::jlimit(1, 2, channelCount), capacity);
        fadeLength = juce::jmax(1, static_cast<int>(rate * 0.02));
        polarity.reset(rate, 0.02);
        reset();
    }

    void reset() noexcept
    {
        history.clear();
        writeIndex = 0;
        previous = current = 0;
        fade = 0;
        polarity.setCurrentAndTargetValue(1.0f);
        initialised = false;
    }

    int delaySamples() const noexcept { return current; }

    void process(juce::AudioBuffer<float>& buffer, float delayMs, bool invert) noexcept
    {
        if (capacity <= 1 || buffer.getNumSamples() <= 0) return;
        const auto maximum = juce::jmin(capacity - 1 - buffer.getNumSamples(), static_cast<int>(std::ceil(rate * maxDelayMs / 1000.0)));
        const auto requested = std::isfinite(delayMs)
            ? juce::jlimit(0, juce::jmax(0, maximum), static_cast<int>(std::lround(delayMs * rate / 1000.0))) : 0;
        if (!initialised)
        {
            current = previous = requested;
            polarity.setCurrentAndTargetValue(invert ? -1.0f : 1.0f);
            initialised = true;
        }
        else if (requested != current && fade == 0)
        {
            previous = current;
            current = requested;
            fade = fadeLength;
        }
        polarity.setTargetValue(invert ? -1.0f : 1.0f);
        const auto channels = juce::jmin(buffer.getNumChannels(), history.getNumChannels());
        const auto mask = capacity - 1;
        const bool transparent = current == 0 && fade == 0 && !polarity.isSmoothing() && polarity.getTargetValue() > 0;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto gain = polarity.getNextValue();
            const auto mix = fade > 0 ? 1.0f - static_cast<float>(fade) / static_cast<float>(fadeLength) : 1.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* line = history.getWritePointer(channel);
                line[writeIndex] = buffer.getSample(channel, sample);
                if (transparent) continue;
                auto value = line[(writeIndex - current) & mask];
                if (fade > 0) value = mix * value + (1.0f - mix) * line[(writeIndex - previous) & mask];
                buffer.setSample(channel, sample, gain * value);
            }
            if (fade > 0) --fade;
            writeIndex = (writeIndex + 1) & mask;
        }
    }

private:
    juce::AudioBuffer<float> history;
    juce::LinearSmoothedValue<float> polarity { 1.0f };
    double rate = 48000;
    int capacity = 0, writeIndex = 0, previous = 0, current = 0, fade = 0, fadeLength = 960;
    bool initialised = false;
};
}
