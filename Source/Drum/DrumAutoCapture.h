#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace drum
{
class DrumAutoCapture
{
public:
    unsigned int begin() noexcept
    {
        const auto next = ++sequence;
        request.store(next, std::memory_order_release);
        return next;
    }
    void cancel() noexcept { request.store(0, std::memory_order_release); }
    bool ready(unsigned int generation) const noexcept
    {
        return generation != 0 && completed.load(std::memory_order_acquire) == generation;
    }
    float progress() const noexcept { return amount.load(std::memory_order_relaxed); }
    const float* data() const noexcept { return audio.get(); }
    int size() const noexcept { return length; }
    double sampleRate() const noexcept { return rate; }

    void push(const juce::AudioBuffer<float>& buffer, double sampleRate) noexcept
    {
        const auto generation = request.load(std::memory_order_acquire);
        if (generation == 0 || completed.load(std::memory_order_relaxed) == generation)
            return;
        if (active != generation)
        {
            active = generation;
            length = 0;
            rate = sampleRate;
            amount.store(0, std::memory_order_relaxed);
        }
        if (!std::isfinite(rate) || sampleRate != rate || rate < 8000 || rate > 192000)
        {
            length = 0;
            completed.store(generation, std::memory_order_release);
            return;
        }
        const auto target = juce::jmin(capacity, static_cast<int>(rate * 8));
        const auto count = juce::jmin(buffer.getNumSamples(), target - length);
        for (int sample = 0; sample < count; ++sample)
        {
            float value = 0;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto candidate = buffer.getSample(channel, sample);
                if (!std::isfinite(candidate) || std::abs(candidate) > std::abs(value)) value = candidate;
            }
            audio[static_cast<size_t>(length++)] = value;
        }
        amount.store(static_cast<float>(length) / target, std::memory_order_relaxed);
        if (length == target)
            completed.store(generation, std::memory_order_release);
    }

private:
    static constexpr int capacity = 192000 * 8;
    std::unique_ptr<float[]> audio { new float[capacity] };
    std::atomic<unsigned int> request { 0 }, completed { 0 };
    std::atomic<float> amount { 0 };
    unsigned int sequence = 0, active = 0;
    int length = 0;
    double rate = 0;
};
}