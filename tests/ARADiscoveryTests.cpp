#include <juce_audio_utils/juce_audio_utils.h>
#define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
#include <juce_audio_processors/format_types/juce_VST3Headers.h>
#include <juce_audio_processors/utilities/juce_FlagCache.h>
#include <juce_audio_processors/format_types/juce_VST3Common.h>
#include "HostOutput.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class OutputObserver final : public drum::HostOutputObserver
{
public:
    void recordHostOutput(float peakIn, int channelsIn, bool activeIn, bool fallbackIn,
                          bool previouslySilentIn) noexcept override
    {
        peak = peakIn;
        channels = channelsIn;
        active = activeIn;
        fallback = fallbackIn;
        previouslySilent = previouslySilentIn;
        ++calls;
    }
    float peak = 0;
    int channels = 0, calls = 0;
    bool active = false, fallback = false, previouslySilent = false;
};

template <typename Sample>
void checkInactiveHostOutput(int channels)
{
    constexpr int samples = 16;
    const auto layout = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    std::vector<juce::DynamicChannelMapping> inputMap;
    std::vector<juce::DynamicChannelMapping> outputMap { juce::DynamicChannelMapping { layout } };
    outputMap.front().setClientActive(true);
    outputMap.front().setHostActive(false);
    juce::ClientBufferMapperData<Sample> mapper;
    mapper.prepare(channels, samples);
    juce::AudioBuffer<Sample> hostBuffer(channels, samples);
    hostBuffer.clear();
    Steinberg::Vst::AudioBusBuffers bus {};
    bus.numChannels = channels;
    Sample* hostChannels[] { hostBuffer.getWritePointer(0),
        channels == 2 ? hostBuffer.getWritePointer(1) : nullptr };
    if constexpr (std::is_same_v<Sample, float>)
        bus.channelBuffers32 = hostChannels;
    else
        bus.channelBuffers64 = hostChannels;
    bus.silenceFlags = 3;
    Steinberg::Vst::ProcessData data {};
    data.numSamples = samples;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.symbolicSampleSize = std::is_same_v<Sample, float> ? Steinberg::Vst::kSample32 : Steinberg::Vst::kSample64;
    OutputObserver observer;
    const auto render = [&](bool inspect)
    {
        drum::HostOutputProbe<Sample, Steinberg::Vst::ProcessData> probe(data, inspect ? &observer : nullptr, true, true);
        juce::ClientRemappedBuffer<Sample> remapped(mapper, &inputMap, &outputMap, data);
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::fill(remapped.buffer.getWritePointer(channel),
                static_cast<Sample>(0.25 * (channel + 1)), samples);
        require(observer.calls == 0, "Output observation must wait for remapped buffer destruction");
    };
    render(false);
    require(hostBuffer.getMagnitude(0, samples) == 0.0f,
        "Inactive host bus must reproduce missing output despite a nonzero client render");
    outputMap.front().setHostActive(true);
    render(true);
    require(observer.calls == 1 && observer.channels == channels && observer.active && observer.fallback
        && observer.previouslySilent && observer.peak == 0.25f * static_cast<float>(channels),
        "Host observer must see copied samples, channel count, flags and activation after remapping");
    require((bus.silenceFlags & (channels == 1 ? 1u : 3u)) == 0,
        "Nonzero output must not retain host silence flags");
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            require(hostBuffer.getSample(channel, sample) == 0.25f * static_cast<float>(channel + 1),
                "Activated host bus must receive every rendered sample on its correct channel");
    hostBuffer.clear();
    {
        drum::HostOutputProbe<Sample, Steinberg::Vst::ProcessData> probe(data, &observer, true, false);
    }
    require(observer.peak == 0 && (bus.silenceFlags & (channels == 1 ? 1u : 3u)) == (channels == 1 ? 1u : 3u),
        "Zero output must be reported as silent");
    if (channels == 2)
    {
        hostBuffer.setSample(0, 0, static_cast<Sample>(0.5));
        {
            drum::HostOutputProbe<Sample, Steinberg::Vst::ProcessData> probe(data, &observer, true, false);
        }
        require((bus.silenceFlags & 3u) == 2u, "Silence flags must be recomputed independently per channel");
    }
}

void checkActivationOrder(const juce::AudioProcessor& processor)
{
    juce::ClientBufferMapper mapper;
    mapper.updateFromProcessor(processor);
    mapper.setInputBusHostActive(0, false);
    mapper.setOutputBusHostActive(0, false);
    drum::HostBusActivation activation;
    require(!activation.synchronise(mapper, false, true) && !mapper.getOutputMap().front().isHostActive(),
        "Negotiation before ARA binding must not activate buses prematurely");
    require(!activation.synchronise(mapper, true, false) && !mapper.getOutputMap().front().isHostActive(),
        "Unaffected hosts must not use the workaround");
    activation.receive(true);
    require(activation.synchronise(mapper, true, true) && mapper.getOutputMap().front().isHostActive()
        && !mapper.getInputMap().front().isHostActive(),
        "Late ARA binding must recover missing output activation even if input activation was received");
    mapper.updateFromProcessor(processor);
    require(activation.synchronise(mapper, true, true), "Repeated processing must retain fallback activation");
    activation.receive(false);
    mapper.setOutputBusHostActive(0, false);
    require(!activation.synchronise(mapper, true, true) && !mapper.getOutputMap().front().isHostActive(),
        "An explicitly disabled host output must never be reactivated by fallback");
}
}

int main(int argumentCount, char** arguments)
{
    try
    {
        std::cout << std::unitbuf;
        juce::ScopedJuceInitialiser_GUI initialiser;
        checkInactiveHostOutput<float>(1);
        checkInactiveHostOutput<float>(2);
        checkInactiveHostOutput<double>(1);
        checkInactiveHostOutput<double>(2);
        std::cout << "PASS: post-copy probe and per-channel silence flags in float/double\n";
        std::cout << "PASS: inactive host bus reproduces missing output; activation restores mono/stereo samples\n";
        require(argumentCount == 2, "Expected path to a VST3 bundle");
        const juce::File bundle(juce::String::fromUTF8(arguments[1]));
        require(bundle.exists(), "VST3 bundle must exist");

        juce::AudioPluginFormatManager manager;
        auto* format = new juce::VST3PluginFormat();
        manager.addFormat(format);
        juce::OwnedArray<juce::PluginDescription> descriptions;
        std::cout << "Stage: scan VST3 bundle\n";
        format->findAllTypesForFile(descriptions, bundle.getFullPathName());
        require(descriptions.size() == 1, "Bundle must expose one audio processor");
        const auto& description = *descriptions[0];
        std::cout << "Plugin: " << description.name << " / " << description.version << '\n';
        require(description.name == "ZeroEffort Drum", "Built VST3 must expose the ZeroEffort Drum name");
        require(description.hasARAExtension, "VST3 processor must advertise ARA through its interface");

        std::cout << "Stage: instantiate VST3 processor\n";
        juce::String instanceError;
        auto instance = manager.createPluginInstance(description, 48000.0, 512, instanceError);
        if (instanceError.isNotEmpty())
            std::cerr << "Instance creation: " << instanceError << '\n';
        require(instance != nullptr, "VST3 instance must load before requesting its ARA factory");
        checkActivationOrder(*instance);
        std::cout << "PASS: late ARA binding, independent input/output activation and explicit output disable\n";
        require(instance->getTotalNumInputChannels() == 1 && instance->getTotalNumOutputChannels() == 1,
            "The real VST3 bundle must start with a native mono bus");
        auto layout = instance->getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
        require(instance->setBusesLayout(layout) && instance->getTotalNumOutputChannels() == 2,
            "The VST3 wrapper must negotiate stereo on host request");
        layout.inputBuses.set(0, juce::AudioChannelSet::mono());
        layout.outputBuses.set(0, juce::AudioChannelSet::mono());
        require(instance->setBusesLayout(layout) && instance->getTotalNumOutputChannels() == 1,
            "The VST3 wrapper must negotiate back to mono on host request");
        std::cout << "PASS: native mono default and VST3 mono/stereo bus negotiation\n";
        bool completed = false;
        juce::ARAFactoryResult discovery;
        std::cout << "Stage: obtain ARA factory\n";
        juce::createARAFactoryAsync(*instance, [&](juce::ARAFactoryWrapper result)
        {
            std::cout << "Stage: ARA factory callback\n";
            discovery.araFactory = std::move(result);
            completed = true;
        });
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
        while (!completed && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        require(completed, "ARA factory discovery timed out");
        if (discovery.errorMessage.isNotEmpty())
            std::cerr << "Host discovery: " << discovery.errorMessage << '\n';
        const auto* factory = discovery.araFactory.get();
        require(factory != nullptr, "Host must obtain and initialise the factory from the built VST3");
        require(juce::String(factory->plugInName) == description.name,
                "ARA factory and VST3 processor names must match");
        require(factory->lowestSupportedApiGeneration <= ARA::kARAAPIGeneration_2_0_Final
                    && factory->highestSupportedApiGeneration >= ARA::kARAAPIGeneration_2_0_Final,
                "ARA 2.0 generation must be supported");
        require(factory->informationURL != nullptr && juce::String(factory->informationURL).isNotEmpty(),
                "Factory metadata must contain its information URL");
        require(factory->factoryID != nullptr && juce::String(factory->factoryID).isNotEmpty(),
            "Factory ID must be present");
        require(factory->documentArchiveID != nullptr && juce::String(factory->documentArchiveID).isNotEmpty(),
            "Document archive ID must be present");
        require(factory->compatibleDocumentArchiveIDsCount == 0 || factory->compatibleDocumentArchiveIDs != nullptr,
            "Compatible archive IDs must match their declared count");
        require(factory->analyzeableContentTypesCount == 0 || factory->analyzeableContentTypes != nullptr,
            "Analyzable content types must match their declared count");
        require(factory->createDocumentControllerWithDocument != nullptr,
            "Factory must expose document creation");
        std::cout << "Capabilities: content types=" << factory->analyzeableContentTypesCount
              << ", compatible archives=" << factory->compatibleDocumentArchiveIDsCount
              << ", transformations=" << factory->supportedPlaybackTransformationFlags << '\n';
        std::cout << "Factory: " << factory->factoryID << '\n';
        std::cout << "PASS: VST3 scanning, ARA interface, factory loading, initialisation, name, ARA 2.0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}