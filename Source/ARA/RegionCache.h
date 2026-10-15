#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <limits>
#include <vector>

namespace drum
{
enum class CacheFailure
{
    none, sampleAccess, sampleRate, channels, invalidTiming, durationLimit,
    timeStretch, memoryLimit, sourceRange, readFailure
};

struct RegionRequirements
{
    bool sampleAccess;
    double sourceRate;
    double renderRate;
    int sourceChannels;
    int renderChannels;
    double duration;
    double sourceStart;
    double playbackStart;
    double modificationDuration;
};

inline CacheFailure checkRegion(const RegionRequirements& region) noexcept
{
    if (!region.sampleAccess)
        return CacheFailure::sampleAccess;
    if (!std::isfinite(region.sourceRate) || !std::isfinite(region.renderRate)
        || region.renderRate <= 0.0 || region.sourceRate != region.renderRate)
        return CacheFailure::sampleRate;
    if (region.sourceChannels < 1 || region.sourceChannels > 2
        || region.renderChannels < 1 || region.renderChannels > 2)
        return CacheFailure::channels;
    if (!std::isfinite(region.duration) || region.duration <= 0.0
        || !std::isfinite(region.sourceStart) || region.sourceStart < 0.0
        || !std::isfinite(region.playbackStart) || !std::isfinite(region.modificationDuration)
        || std::abs(region.playbackStart) > 7.0 * 24.0 * 3600.0
        || region.sourceStart > 7.0 * 24.0 * 3600.0)
        return CacheFailure::invalidTiming;
    if (region.duration * region.renderRate > std::numeric_limits<int>::max())
        return CacheFailure::durationLimit;
    if (std::abs(region.duration - region.modificationDuration) > 0.5 / region.renderRate)
        return CacheFailure::timeStretch;
    return CacheFailure::none;
}

inline constexpr juce::int64 maximumCacheBytes = 128 * 1024 * 1024;
inline constexpr size_t maximumCachedRegions = 128;

inline bool cacheHasCapacity(juce::int64 samples, int sourceChannels,
                            juce::int64 usedBytes, size_t regionCount) noexcept
{
    if (samples <= 0 || samples > std::numeric_limits<int>::max()
        || sourceChannels < 1 || sourceChannels > 2 || usedBytes < 0
        || usedBytes > maximumCacheBytes || regionCount >= maximumCachedRegions)
        return false;
    const auto bytesPerFrame = sourceChannels * static_cast<juce::int64>(sizeof(float));
    return samples <= (maximumCacheBytes - usedBytes) / bytesPerFrame;
}

inline const char* describeFailure(CacheFailure failure) noexcept
{
    switch (failure)
    {
        case CacheFailure::none: return "None";
        case CacheFailure::sampleAccess: return "Host sample access disabled";
        case CacheFailure::sampleRate: return "Source / render sample rate mismatch";
        case CacheFailure::channels: return "Source / render channel mismatch";
        case CacheFailure::invalidTiming: return "Invalid region timing";
        case CacheFailure::durationLimit: return "Region exceeds supported sample count";
        case CacheFailure::timeStretch: return "Time-stretched region not supported";
        case CacheFailure::memoryLimit: return "Cache exceeds 128 MiB or 128 regions";
        case CacheFailure::sourceRange: return "Region exceeds source sample range";
        case CacheFailure::readFailure: return "Host audio read failed";
    }
    return "Unknown cache failure";
}

inline const char* describeCacheState(int loaded, int failed, bool stale) noexcept
{
    if (stale)
        return "Cache invalidated";
    if (loaded == 0)
        return failed > 0 ? "Cache empty - audio unavailable" : "No regions cached";
    return failed > 0 ? "Cache partial - some regions unavailable" : "Cache ready";
}

struct CachedRegion
{
    juce::int64 startSample = 0;
    juce::AudioBuffer<float> audio;
};

inline bool renderRegions(const std::vector<CachedRegion>& regions,
                          juce::int64 startSample, juce::AudioBuffer<float>& output) noexcept
{
    output.clear();
    bool intersects = false;
    const auto block = juce::Range<juce::int64>::withStartAndLength(startSample, output.getNumSamples());
    for (const auto& region : regions)
    {
        const auto overlap = block.getIntersectionWith(
            juce::Range<juce::int64>::withStartAndLength(region.startSample, region.audio.getNumSamples()));
        if (overlap.isEmpty())
            continue;
        intersects = true;

        const auto destinationOffset = static_cast<int>(overlap.getStart() - startSample);
        const auto sourceOffset = static_cast<int>(overlap.getStart() - region.startSample);
        const auto length = static_cast<int>(overlap.getLength());
        if (region.audio.getNumChannels() == 2 && output.getNumChannels() == 1)
        {
            output.addFrom(0, destinationOffset, region.audio, 0, sourceOffset, length, 0.5f);
            output.addFrom(0, destinationOffset, region.audio, 1, sourceOffset, length, 0.5f);
            continue;
        }
        const auto monoToStereo = region.audio.getNumChannels() == 1 && output.getNumChannels() == 2;
        const auto channels = monoToStereo ? 2 : juce::jmin(output.getNumChannels(), region.audio.getNumChannels());
        for (int channel = 0; channel < channels; ++channel)
            output.addFrom(channel, destinationOffset, region.audio, monoToStereo ? 0 : channel, sourceOffset, length);
    }
    return intersects;
}

enum class PlaybackState { waiting, stopped, stale, missingPosition, outsideRegions, rendered };

inline const char* describePlaybackState(PlaybackState state) noexcept
{
    switch (state)
    {
        case PlaybackState::waiting: return "No playback callback received";
        case PlaybackState::stopped: return "Host transport stopped";
        case PlaybackState::stale: return "Cache invalidated";
        case PlaybackState::missingPosition: return "Host position missing";
        case PlaybackState::outsideRegions: return "Position outside cached regions";
        case PlaybackState::rendered: return "Rendering cached region";
    }
    return "Unknown playback state";
}

inline PlaybackState renderPlaybackBlock(const std::vector<CachedRegion>& regions,
                                         juce::int64 startSample, bool hasPosition,
                                         bool isPlaying, bool stale, juce::AudioBuffer<float>& output) noexcept
{
    output.clear();
    if (stale)
        return PlaybackState::stale;
    if (!isPlaying)
        return PlaybackState::stopped;
    if (!hasPosition)
        return PlaybackState::missingPosition;
    return renderRegions(regions, startSample, output) ? PlaybackState::rendered : PlaybackState::outsideRegions;
}
}