#include "ARA/RegionCache.h"
#include "ARA/CacheValidity.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

drum::CachedRegion makeRegion(juce::int64 start, int channels, int length, float value)
{
    drum::CachedRegion region;
    region.startSample = start;
    region.audio.setSize(channels, length);
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < length; ++sample)
            region.audio.setSample(channel, sample, value);
    return region;
}
}

int main()
{
    try
    {
        drum::CacheValidity validity;
        int kickRegion = 0, snareRegion = 0, kickSource = 0, snareSource = 0;
        const auto kickRegionToken = validity.observe(&kickRegion);
        const auto snareRegionToken = validity.observe(&snareRegion);
        const auto kickSourceToken = validity.observe(&kickSource);
        const auto sharedKickSourceToken = validity.observe(&kickSource);
        const auto snareSourceToken = validity.observe(&snareSource);
        require(kickSourceToken == sharedKickSourceToken, "Readers of one source must share its validity token");
        validity.invalidate(&snareSource);
        require(kickRegionToken->load() && kickSourceToken->load() && snareRegionToken->load()
            && !snareSourceToken->load(), "Editing Snare must not invalidate the Kick or unrelated regions");
        const auto rebuiltSnareSource = validity.observe(&snareSource);
        require(rebuiltSnareSource->load() && !snareSourceToken->load(),
            "Rebuilding a source must not revalidate stale readers of its previous contents");
        validity.invalidate(&kickRegion);
        require(!kickRegionToken->load() && kickSourceToken->load() && rebuiltSnareSource->load(),
            "Deleting a region must invalidate only that region and preserve the other track");
        validity.invalidate(&kickSource);
        require(!kickSourceToken->load() && !sharedKickSourceToken->load() && rebuiltSnareSource->load(),
            "Editing a shared source must invalidate all its readers but not a different source");
        require(validity.observe(&kickRegion)->load() && !kickRegionToken->load(),
            "Reused object addresses must receive fresh validity tokens without reviving old caches");
        const drum::RegionRequirements valid { true, 48000.0, 48000.0, 2, 2, 10.0, 0.0, 2.0, 10.0 };
        require(drum::checkRegion(valid) == drum::CacheFailure::none, "Valid region must be accepted");
        auto candidate = valid;
        candidate.sampleAccess = false;
        require(drum::checkRegion(candidate) == drum::CacheFailure::sampleAccess, "Report disabled host access");
        candidate = valid;
        candidate.sourceRate = 44100.0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::sampleRate, "Report rate mismatch");
        candidate = valid;
        candidate.sourceChannels = 1;
        candidate.duration = candidate.modificationDuration = 17.5;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none,
            "Cubase mono source at 48 kHz must support stereo rendering for a 17.5 s region");
        candidate.renderChannels = 1;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none, "Mono to mono must remain supported");
        candidate = valid;
        candidate.renderChannels = 1;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none, "Stereo sources must support mono rendering");
        candidate = valid;
        candidate.sourceChannels = 0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::channels, "Reject missing source channels");
        candidate.sourceChannels = 3;
        require(drum::checkRegion(candidate) == drum::CacheFailure::channels, "Reject unsupported channel conversion");
        candidate.sourceChannels = 2;
        candidate.renderChannels = 0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::channels, "Reject missing output channels");
        candidate.renderChannels = 3;
        require(drum::checkRegion(candidate) == drum::CacheFailure::channels, "Reject unsupported output layout");
        candidate = valid;
        candidate.duration = candidate.modificationDuration = 31.0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none, "Regions beyond thirty seconds must be accepted");
        candidate.duration = candidate.modificationDuration = 30.0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none, "Thirty seconds is within limit");
        candidate.sourceChannels = 1;
        candidate.duration = candidate.modificationDuration = 231.448;
        require(drum::checkRegion(candidate) == drum::CacheFailure::none,
            "The user's 231.448 second mono region must pass ARA validation");
        const auto longLength = static_cast<int>(std::llround(candidate.duration * candidate.renderRate));
        require(drum::cacheHasCapacity(longLength, 1, 0, 0)
            && drum::cacheHasCapacity(longLength, 2, 0, 0),
            "231 seconds at 48 kHz must fit the cache in native mono and stereo");
        require(!drum::cacheHasCapacity(longLength * 4, 2, 0, 0),
            "Long high-rate stereo sources must still respect the RAM budget");
        require(!drum::cacheHasCapacity(longLength, 1, drum::maximumCacheBytes - 4, 0)
            && !drum::cacheHasCapacity(1, 1, 0, drum::maximumCachedRegions),
            "Aggregate memory and region-count limits must remain enforced");
        require(drum::cacheHasCapacity(drum::maximumCacheBytes / 4, 1, 0, 0)
            && !drum::cacheHasCapacity(drum::maximumCacheBytes / 4 + 1, 1, 0, 0)
            && !drum::cacheHasCapacity(std::numeric_limits<juce::int64>::max(), 2, 0, 0),
            "Cache capacity must accept its exact boundary and reject overflow safely");
        candidate.duration = candidate.modificationDuration =
            (static_cast<double>(std::numeric_limits<int>::max()) + 1) / candidate.renderRate;
        require(drum::checkRegion(candidate) == drum::CacheFailure::durationLimit,
            "Sample counts must be validated before narrowing to JUCE buffer lengths");
        {
            std::vector<drum::CachedRegion> longRegions;
            longRegions.push_back(makeRegion(48000, 1, longLength, 0.25f));
            juce::AudioBuffer<float> longOutput(2, 256);
            require(drum::renderPlaybackBlock(longRegions, 48000 + 180 * 48000, true, true, false, longOutput)
                    == drum::PlaybackState::rendered
                && longOutput.getSample(0, 255) == 0.25f && longOutput.getSample(1, 255) == 0.25f,
                "Seeking after three minutes must render the long mono cache to both stereo outputs");
            drum::renderRegions(longRegions, 48000 + longLength - 2, longOutput);
            require(longOutput.getSample(0, 1) == 0.25f && longOutput.getSample(0, 2) == 0,
                "Long regions must retain exact end boundaries");
        }
        candidate = valid;
        candidate.sourceStart = -1.0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::invalidTiming, "Report invalid timing");
        candidate = valid;
        candidate.modificationDuration = 12.0;
        require(drum::checkRegion(candidate) == drum::CacheFailure::timeStretch, "Report time stretch");
        require(juce::String(drum::describeFailure(drum::CacheFailure::readFailure)).contains("read failed"),
            "Read failure must have an explicit diagnostic");
        require(juce::String(drum::describeCacheState(0, 1, false)).contains("Cache empty"),
            "Rejected regions must not report cache ready");
        require(juce::String(drum::describeCacheState(1, 1, false)).contains("Cache partial"),
            "Mixed success must report partial cache");
        require(juce::String(drum::describeCacheState(1, 0, false)) == "Cache ready",
            "Only complete cache may report ready");
        require(juce::String(drum::describeCacheState(0, 0, false)) == "No regions cached",
            "An unprepared empty cache must not report ready");
        require(juce::String(drum::describeCacheState(1, 0, true)) == "Cache invalidated",
            "Invalidated cache must take precedence");
        std::vector<drum::CachedRegion> regions;
        regions.push_back(makeRegion(100, 2, 8, 0.25f));
        regions.push_back(makeRegion(106, 2, 4, 0.5f));
        juce::AudioBuffer<float> output(2, 16);
        require(drum::renderPlaybackBlock(regions, 100, true, true, false, output) == drum::PlaybackState::rendered
                && output.getSample(0, 0) == 0.25f, "Playing with valid position must produce audio");
        require(drum::renderPlaybackBlock(regions, 100, true, false, false, output) == drum::PlaybackState::stopped
                && output.getMagnitude(0, 16) == 0.0f, "Stopped transport must clear previous audio");
        require(drum::renderPlaybackBlock(regions, 100, true, true, true, output) == drum::PlaybackState::stale,
            "Invalidated cache must be identified");
        require(drum::renderPlaybackBlock(regions, 100, false, true, false, output) == drum::PlaybackState::missingPosition,
            "Missing host position must be identified");
        require(drum::renderPlaybackBlock(regions, 0, true, true, false, output) == drum::PlaybackState::outsideRegions,
            "Host time outside cached region must be identified");
        drum::renderRegions(regions, 98, output);
        require(output.getSample(0, 0) == 0.0f, "Before region must be silent");
        require(output.getSample(0, 2) == 0.25f, "Region must start at project position");
        require(output.getSample(0, 8) == 0.75f, "Overlapping regions must sum");
        require(output.getSample(0, 10) == 0.5f, "Second region must continue");
        require(output.getSample(0, 12) == 0.0f, "After regions must be silent");
        for (int sample = 0; sample < 16; ++sample)
            require(output.getSample(0, sample) == output.getSample(1, sample), "Stereo must stay aligned");

        drum::renderRegions(regions, 106, output);
        require(output.getSample(0, 0) == 0.75f, "Seek must not depend on previous playback");
        drum::renderRegions(regions, 98, output);
        require(output.getSample(0, 2) == 0.25f, "Loop must reproduce region");
        drum::renderRegions(regions, -20, output);
        require(output.getMagnitude(0, 16) == 0.0f, "Negative project time must be safe");

        juce::AudioBuffer<float> whole(2, 12), first(2, 5), second(2, 7);
        drum::renderRegions(regions, 100, whole);
        drum::renderRegions(regions, 100, first);
        drum::renderRegions(regions, 105, second);
        for (int sample = 0; sample < 12; ++sample)
            require(whole.getSample(0, sample) == (sample < 5 ? first.getSample(0, sample)
                                                                           : second.getSample(0, sample - 5)),
                    "Rendering must not depend on block size");

        std::vector<drum::CachedRegion> mono;
        mono.push_back(makeRegion(0, 1, 4, 0.2f));
        drum::renderRegions(mono, 0, output);
        for (int sample = 0; sample < 16; ++sample)
        {
            const auto expected = sample < 4 ? 0.2f : 0.0f;
            require(output.getSample(0, sample) == expected && output.getSample(1, sample) == expected,
                "Mono must duplicate at unity to both outputs without extending region boundaries");
        }
        drum::renderRegions(mono, 2, output);
        require(output.getSample(0, 0) == 0.2f && output.getSample(1, 0) == 0.2f
                && output.getSample(1, 2) == 0.0f, "Mono stereo mapping must respect seek offsets");
        mono.push_back(makeRegion(2, 1, 4, 0.3f));
        drum::renderRegions(mono, 0, output);
        require(output.getSample(0, 2) == 0.5f && output.getSample(1, 2) == 0.5f,
            "Overlapping mono regions must sum equally on both outputs");
        juce::AudioBuffer<float> monoOutput(1, 4);
        drum::renderRegions(mono, 0, monoOutput);
        require(monoOutput.getSample(0, 0) == 0.2f && monoOutput.getSample(0, 2) == 0.5f,
            "Native mono rendering must preserve gain");
        std::vector<drum::CachedRegion> distinctStereo;
        distinctStereo.push_back(makeRegion(0, 2, 4, 0.1f));
        distinctStereo[0].audio.setSample(1, 0, -0.4f);
        drum::renderRegions(distinctStereo, 0, output);
        require(output.getSample(0, 0) == 0.1f && output.getSample(1, 0) == -0.4f,
            "Native stereo channels must not be folded or duplicated");
        drum::renderRegions(distinctStereo, 0, monoOutput);
        require(std::abs(monoOutput.getSample(0, 0) + 0.15f) < 0.000001f,
            "Stereo to mono must average both channels, not discard the right channel");
        require(monoOutput.getSample(0, 1) == 0.1f,
            "Identical stereo channels must retain their level when folded to mono");
        distinctStereo[0].audio.setSample(0, 2, 0.5f);
        distinctStereo[0].audio.setSample(1, 2, -0.5f);
        drum::renderRegions(distinctStereo, 2, monoOutput);
        require(monoOutput.getSample(0, 0) == 0.0f && monoOutput.getSample(0, 2) == 0.0f,
            "Downmix must respect phase cancellation, seek offsets and region boundaries");
        distinctStereo.push_back(makeRegion(0, 1, 4, 0.2f));
        drum::renderRegions(distinctStereo, 0, monoOutput);
        require(std::abs(monoOutput.getSample(0, 0) - 0.05f) < 0.000001f,
            "Mixed mono and stereo sources must map independently into the same mono output");
        juce::AudioBuffer<float> empty(1, 0);
        drum::renderRegions(mono, 0, empty);
        regions.clear();
        drum::renderRegions(regions, 0, output);
        require(output.getMagnitude(0, 16) == 0.0f, "No region must clear previous output");
        std::cout << "PASS: region position, boundaries, overlap, stereo, seek, loop, blocks, mono, empty\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}