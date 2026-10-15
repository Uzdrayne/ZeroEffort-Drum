#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace drum
{
class SpectrumMailbox
{
public:
    static constexpr int size = 4096;
    static_assert(std::atomic<int>::is_always_lock_free);
    std::atomic<int> subscribers { 0 };

    void publish(const std::array<float, size>& source, double sampleRate) noexcept
    {
        int expected = 0;
        if (!state.compare_exchange_strong(expected, 1, std::memory_order_acquire)) return;
        frame = source;
        rate = sampleRate;
        state.store(2, std::memory_order_release);
    }

    bool read(std::array<float, size>& destination, double& sampleRate) noexcept
    {
        int expected = 2;
        if (!state.compare_exchange_strong(expected, 3, std::memory_order_acquire)) return false;
        destination = frame;
        sampleRate = rate;
        state.store(0, std::memory_order_release);
        return true;
    }

private:
    std::atomic<int> state { 0 };
    std::array<float, size> frame {};
    double rate = 48000;
};

class SpectrumCapture
{
public:
    void reset() noexcept { position = 0; }

    void push(const juce::AudioBuffer<float>& audio, SpectrumMailbox& mailbox, double rate) noexcept
    {
        if (mailbox.subscribers.load(std::memory_order_relaxed) == 0 || audio.getNumChannels() == 0)
        {
            reset();
            return;
        }
        const auto channel = audio.getNumChannels() > 1 && audio.getMagnitude(1, 0, audio.getNumSamples())
            > audio.getMagnitude(0, 0, audio.getNumSamples()) ? 1 : 0;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            frame[static_cast<size_t>(position++)] = audio.getSample(channel, sample);
            if (position == SpectrumMailbox::size)
            {
                mailbox.publish(frame, rate);
                position = 0;
            }
        }
    }

private:
    std::array<float, SpectrumMailbox::size> frame {};
    int position = 0;
};

class SpectrumDisplay
{
public:
    SpectrumDisplay() { levels.fill(-100); }

    bool update(SpectrumMailbox& mailbox)
    {
        for (auto& level : levels) level = juce::jmax(-100.0f, level - 2.0f);
        if (!mailbox.read(frame, rate)) return false;
        work.fill(0);
        std::copy(frame.begin(), frame.end(), work.begin());
        window.multiplyWithWindowingTable(work.data(), SpectrumMailbox::size);
        fft.performFrequencyOnlyForwardTransform(work.data(), true);
        for (size_t bin = 0; bin < levels.size(); ++bin)
        {
            const auto level = juce::Decibels::gainToDecibels(work[bin] * (2.0f / SpectrumMailbox::size), -100.0f);
            levels[bin] = juce::jmax(levels[bin], level);
        }
        return true;
    }

    float magnitudeDb(double frequency) const noexcept
    {
        if (frequency > rate * 0.5) return -100;
        const auto bin = juce::jlimit(1, SpectrumMailbox::size / 2,
            static_cast<int>(std::round(frequency * SpectrumMailbox::size / rate)));
        return levels[static_cast<size_t>(bin)];
    }

private:
    juce::dsp::FFT fft { 12 };
    juce::dsp::WindowingFunction<float> window { SpectrumMailbox::size, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, SpectrumMailbox::size> frame {};
    std::array<float, SpectrumMailbox::size * 2> work {};
    std::array<float, SpectrumMailbox::size / 2 + 1> levels {};
    double rate = 48000;
};
}