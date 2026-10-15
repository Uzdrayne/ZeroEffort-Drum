#pragma once

#include <cmath>
#include <type_traits>

namespace drum
{
class HostOutputObserver
{
public:
    virtual ~HostOutputObserver() = default;
    virtual void recordHostOutput(float peak, int channels, bool active, bool fallback,
                                  bool previouslySilent) noexcept = 0;
};

struct HostBusActivation
{
    bool inputReceived = false;
    bool outputReceived = false;

    void receive(bool input) noexcept
    {
        (input ? inputReceived : outputReceived) = true;
    }

    template <typename Mapper>
    bool synchronise(Mapper& mapper, bool bound, bool affectedHost) const noexcept
    {
        if (!bound || !affectedHost)
            return false;
        if (!inputReceived && mapper.getInputMap().size() == 1)
            mapper.setInputBusHostActive(0, mapper.getInputMap().front().isClientActive());
        if (!outputReceived && mapper.getOutputMap().size() == 1)
        {
            mapper.setOutputBusHostActive(0, mapper.getOutputMap().front().isClientActive());
            return mapper.getOutputMap().front().isClientActive();
        }
        return false;
    }
};

template <typename Sample, typename Data>
class HostOutputProbe
{
public:
    HostOutputProbe(Data& dataIn, HostOutputObserver* observerIn, bool activeIn, bool fallbackIn) noexcept
        : data(dataIn), observer(observerIn), active(activeIn), fallback(fallbackIn) {}

    ~HostOutputProbe() noexcept
    {
        if (observer == nullptr || data.outputs == nullptr || data.numSamples <= 0)
            return;
        float peak = 0.0f;
        int channels = 0;
        bool previouslySilent = false;
        for (int index = 0; index < data.numOutputs; ++index)
        {
            auto& bus = data.outputs[index];
            auto** pointers = [&]
            {
                if constexpr (std::is_same_v<Sample, float>)
                    return bus.channelBuffers32;
                else
                    return bus.channelBuffers64;
            }();
            if (pointers == nullptr)
                continue;
            previouslySilent = previouslySilent || bus.silenceFlags != 0;
            for (int channel = 0; channel < bus.numChannels; ++channel)
            {
                if (pointers[channel] == nullptr)
                    continue;
                ++channels;
                Sample channelPeak = 0;
                for (int sample = 0; sample < data.numSamples; ++sample)
                    channelPeak = std::fmax(channelPeak, std::abs(pointers[channel][sample]));
                peak = std::fmax(peak, static_cast<float>(channelPeak));
                if (channel < 64)
                {
                    const auto mask = decltype(bus.silenceFlags) { 1 } << channel;
                    if (channelPeak > 0)
                        bus.silenceFlags &= ~mask;
                    else
                        bus.silenceFlags |= mask;
                }
            }
        }
        observer->recordHostOutput(peak, channels, active, fallback, previouslySilent);
    }

private:
    Data& data;
    HostOutputObserver* observer;
    bool active;
    bool fallback;
};
}