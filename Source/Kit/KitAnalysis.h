#pragma once

#include <juce_dsp/juce_dsp.h>
#include "KitTypes.h"
#include <complex>
#include <cstdint>
#include <functional>

// Kit analysis engine: measures arrival time and polarity between drum microphones and derives
// corrections plus coherence scores. Runs on a background thread (or offline in tests): it
// allocates and must never be called from the audio thread.
//
// Method (validated on a real Kick / Snare / OH L / OH R multitrack, see docs/KitPhase10.md):
// 1. Onsets are detected on each close microphone (its own hits dominate there).
// 2. Around every onset, a short window of the close mic and a longer window of the other mic are
//    transformed; band-limited cross-spectra are accumulated over all hits (even/odd hits apart).
// 3. The analytic cross-correlation is computed once. The earliest strong envelope peak inside a
//    physically plausible window gives the first arrival (reflections are ignored), then the
//    nearest carrier extremum gives the lag and its sign gives the polarity.
namespace kit
{
struct TrackInput
{
    juce::uint32 id = 0;
    Role role = Role::none;
    int position = 0;
    int channels = 1;
    juce::String label;
    juce::String key;
};

// Reads `count` samples of one track starting at timeline sample `start`. channel -1 = mono sum.
using Reader = std::function<bool(int track, int channel, juce::int64 start, int count, float* destination)>;

enum class PairKind { closeToOverhead, closeToRoom, secondary, overheadPair };

struct Band { float low, high; };

inline Band bandFor(Role role) noexcept
{
    switch (role)
    {
        case Role::kickIn: case Role::kickOut: return { 30.0f, 250.0f };
        case Role::tom: return { 60.0f, 4000.0f };
        case Role::floorTom: return { 40.0f, 3000.0f };
        case Role::hiHat: return { 1000.0f, 12000.0f };
        default: return { 150.0f, 8000.0f };
    }
}

// Upper bound of the close-mic -> overhead search. The measured Kick -> OH distance of the
// reference recording is about 7 ms, so the +/-5 ms of the first specification is not enough.
inline float overheadWindowMs(Role role) noexcept
{
    return role == Role::kickIn || role == Role::kickOut ? 12.0f : role == Role::hiHat ? 8.0f : 10.0f;
}

struct PairResult
{
    PairKind kind = PairKind::closeToOverhead;
    int xTrack = -1, yTrack = -1, xChannel = -1, yChannel = -1, sourceTrack = -1;
    bool valid = false;
    bool outsideWindow = false;     // the strongest correlation lies outside the search window (only set when the guard is on)
    float outsideRatio = 0;         // outside / inside peak of the whitened envelope
    int events = 0;
    float lagSamples = 0, lagMs = 0;
    float r = 0, r0 = 0, rStar = 0, rLowStar = 0;
    float consistencyMs = -1;
    int centre = 0;
    double rate = 48000;
    std::vector<float> curve, lowCurve;
    std::array<double, 4> xBands {}, yBands {};

    static float sample(const std::vector<float>& values, int centreIndex, float lag) noexcept
    {
        if (values.empty()) return 0;
        const auto position = juce::jlimit(0.0f, static_cast<float>(values.size() - 1), centreIndex + lag);
        const auto index = juce::jmin(static_cast<int>(position), static_cast<int>(values.size()) - 2);
        if (index < 0) return values.front();
        const auto fraction = position - static_cast<float>(index);
        return values[static_cast<size_t>(index)] + fraction * (values[static_cast<size_t>(index) + 1] - values[static_cast<size_t>(index)]);
    }
    float at(float lag) const noexcept { return sample(curve, centre, lag); }
    float lowAt(float lag) const noexcept { return lowCurve.empty() ? at(lag) : sample(lowCurve, centre, lag); }
};

class OnsetDetector
{
public:
    void prepare(double sampleRate) noexcept
    {
        rate = sampleRate;
        hop = juce::jmax(16, static_cast<int>(std::lround(sampleRate / 750.0)));
        refractory = static_cast<juce::int64>(sampleRate * 0.12);
        decayPerFrame = static_cast<float>(hop / sampleRate);
        peakDb = -200;
        expected = std::numeric_limits<juce::int64>::min();
    }
    int frameSize() const noexcept { return hop; }

    template <typename Callback>
    void push(const float* data, int count, juce::int64 start, Callback&& onEvent)
    {
        if (start != expected)
        {
            fill = 0;
            framePeak = 0;
            previousDb = -200;
            frameStart = start;
            lastEvent = std::numeric_limits<juce::int64>::min() / 2;
        }
        for (int index = 0; index < count; ++index)
        {
            const auto value = std::abs(data[index]);
            if (std::isfinite(value)) framePeak = juce::jmax(framePeak, value);
            if (++fill < hop) continue;
            const auto db = 20.0f * std::log10(framePeak + 1.0e-9f);
            peakDb = juce::jmax(peakDb - decayPerFrame, db);
            if (db > -60.0f && db > peakDb - 12.0f && db - previousDb > 6.0f && frameStart - lastEvent >= refractory)
            {
                onEvent(frameStart - hop);
                lastEvent = frameStart;
            }
            previousDb = db;
            frameStart += hop;
            fill = 0;
            framePeak = 0;
        }
        expected = start + count;
    }

private:
    double rate = 48000;
    int hop = 64, fill = 0;
    juce::int64 refractory = 5760, frameStart = 0, lastEvent = 0;
    juce::int64 expected = std::numeric_limits<juce::int64>::min();
    float framePeak = 0, previousDb = -200, peakDb = -200, decayPerFrame = 0;
};

class KitSession
{
public:
    static constexpr int targetEvents = 24;

    bool reset(const std::vector<TrackInput>& inputTracks, double sampleRate)
    {
        tracks = inputTracks;
        rate = sampleRate;
        signals.clear();
        pairs.clear();
        sources.clear();
        detectors.clear();
        sourceEvents.clear();
        if (!(rate >= 8000 && rate <= 384000)) return false;
        window = 1 << juce::jlimit(9, 15, static_cast<int>(std::lround(std::log2(rate * 0.0427))));
        maximumLag = static_cast<int>(std::ceil(rate * 0.025));
        pre = static_cast<int>(std::lround(rate * 0.002));
        auto order = 1;
        while ((1 << order) < 2 * window + 2 * maximumLag) ++order;
        size = 1 << order;
        fft = std::make_unique<juce::dsp::FFT>(order);
        half = size / 2;

        for (int track = 0; track < static_cast<int>(tracks.size()); ++track)
        {
            const auto& input = tracks[static_cast<size_t>(track)];
            if (input.role == Role::none) continue;
            if (isDistant(input.role) && input.channels == 2 && input.position == positionMain)
            {
                signals.push_back({ track, 0, input.role, 1 });
                signals.push_back({ track, 1, input.role, 2 });
            }
            else
                signals.push_back({ track, -1, input.role, input.position == positionLeft ? 1
                    : input.position == positionRight ? 2 : 0 });
        }
        const auto signalOf = [this](int track) {
            for (int index = 0; index < static_cast<int>(signals.size()); ++index)
                if (signals[static_cast<size_t>(index)].track == track) return index;
            return -1;
        };
        const auto firstTrack = [this](Role role) {
            for (int track = 0; track < static_cast<int>(tracks.size()); ++track)
                if (tracks[static_cast<size_t>(track)].role == role) return track;
            return -1;
        };
        for (int x = 0; x < static_cast<int>(signals.size()); ++x)
        {
            const auto& close = signals[static_cast<size_t>(x)];
            if (!isClose(close.role)) continue;
            for (int y = 0; y < static_cast<int>(signals.size()); ++y)
            {
                const auto& other = signals[static_cast<size_t>(y)];
                if (other.role == Role::overhead)
                    addPair(PairKind::closeToOverhead, close.track, x, y, bandFor(close.role), -0.5f, overheadWindowMs(close.role));
                else if (other.role == Role::room && (close.role == Role::kickIn || close.role == Role::snareTop))
                    addPair(PairKind::closeToRoom, close.track, x, y, bandFor(close.role), -0.5f, 25.0f);
            }
            if (isSecondary(close.role))
            {
                const auto primary = firstTrack(primaryOf(close.role));
                if (primary >= 0)
                    addPair(PairKind::secondary, primary, signalOf(primary), x,
                        close.role == Role::kickOut ? Band { 30.0f, 2000.0f } : bandFor(close.role),
                        close.role == Role::kickOut ? -1.0f : -2.0f, close.role == Role::kickOut ? 4.0f : 2.0f);
            }
        }
        int left = -1, right = -1;
        for (int index = 0; index < static_cast<int>(signals.size()); ++index)
        {
            const auto& signal = signals[static_cast<size_t>(index)];
            if (signal.role != Role::overhead) continue;
            if (signal.side == 1 && left < 0) left = index;
            if (signal.side == 2 && right < 0) right = index;
        }
        if (left >= 0 && right >= 0)
        {
            auto source = firstTrack(Role::snareTop);
            if (source < 0) source = firstTrack(Role::kickIn);
            if (source >= 0) addPair(PairKind::overheadPair, source, left, right, { 150.0f, 8000.0f }, -1.5f, 1.5f);
        }
        for (const auto& pair : pairs)
            if (std::find(sources.begin(), sources.end(), pair.source) == sources.end())
                sources.push_back(pair.source);
        detectors.resize(sources.size());
        sourceEvents.assign(sources.size(), 0);
        for (auto& detector : detectors) detector.prepare(rate);
        hitRows.clear();
        hitWindow = juce::jmax(16, static_cast<int>(std::lround(rate * KitConstants::hitWindowMs / 1000.0)));
        levelBuffer.assign(static_cast<size_t>(hitWindow), 0.0f);
        timeBuffer.assign(static_cast<size_t>(size) * 2, 0.0f);
        complexBuffer.assign(static_cast<size_t>(size), {});
        analytic.assign(static_cast<size_t>(size), {});
        whitened.assign(static_cast<size_t>(size), {});
        scratch.assign(4096, 0.0f);
        return !pairs.empty();
    }

    int lookahead() const noexcept { return window + maximumLag; }
    int lookback() const noexcept { return pre + maximumLag + 64; }
    double sampleRate() const noexcept { return rate; }
    const std::vector<TrackInput>& inputs() const noexcept { return tracks; }
    void setPolicy(const KitPolicy& newPolicy) noexcept { policy = newPolicy; }

    void process(juce::int64 from, juce::int64 to, const Reader& read)
    {
        std::vector<juce::int64> events;
        std::vector<juce::int64> hits;
        for (size_t source = 0; source < sources.size(); ++source)
        {
            events.clear();
            for (auto position = from; position < to;)
            {
                const auto count = static_cast<int>(juce::jmin<juce::int64>(static_cast<juce::int64>(scratch.size()), to - position));
                if (!read(sources[source], -1, position, count, scratch.data()))
                {
                    detectors[source].push(scratch.data(), 0, std::numeric_limits<juce::int64>::min() / 4, [](juce::int64) {});
                    position += count;
                    continue;
                }
                detectors[source].push(scratch.data(), count, position, [&events](juce::int64 event) { events.push_back(event); });
                position += count;
            }
            for (const auto event : events)
            {
                hits.push_back(event);
                if (accumulate(sources[source], event, read))
                    ++sourceEvents[source];
            }
        }
        recordHits(hits, read);
    }

    int eventCount() const noexcept
    {
        int total = 0;
        for (const auto count : sourceEvents) total += count;
        return total;
    }

    float progress() const noexcept
    {
        if (sources.empty()) return 0;
        float result = 1;
        for (const auto count : sourceEvents)
            result = juce::jmin(result, static_cast<float>(count) / targetEvents);
        return juce::jlimit(0.0f, 1.0f, result);
    }

    std::vector<PairResult> evaluate() const
    {
        std::vector<PairResult> results;
        for (const auto& pair : pairs) results.push_back(evaluatePair(pair));
        return results;
    }

    Report report() const { return solve(evaluate()); }

    // K04: mean power of a close mic during the hits of the other instruments minus its mean power during its own hits,
    // in dB. A hit belongs to the instrument whose mic is the loudest relative to its own loudest hit. NaN until
    // enough own and foreign hits were seen.
    float crosstalkDb(size_t track) const
    {
        if (track >= tracks.size() || !isClose(tracks[track].role) || hitRows.empty()) return std::numeric_limits<float>::quiet_NaN();
        const auto groupOf = [this](size_t index) {
            return tracks[index].role == Role::tom ? 1000 + static_cast<int>(index) : static_cast<int>(primaryOf(tracks[index].role));
        };
        std::vector<float> reference(tracks.size(), 0.0f);
        for (const auto& row : hitRows)
            for (size_t index = 0; index < row.size(); ++index) reference[index] = juce::jmax(reference[index], row[index]);
        double own = 0, foreign = 0;
        int ownCount = 0, foreignCount = 0;
        for (const auto& row : hitRows)
        {
            if (row[track] < 0) continue;
            int owner = -1;
            float best = 0;
            for (size_t index = 0; index < row.size(); ++index)
                if (row[index] >= 0 && reference[index] > 0 && row[index] / reference[index] > best)
                {
                    best = row[index] / reference[index];
                    owner = groupOf(index);
                }
            if (owner < 0) continue;
            if (owner == groupOf(track)) { own += row[track]; ++ownCount; }
            else { foreign += row[track]; ++foreignCount; }
        }
        if (ownCount < KitConstants::minimumCrosstalkHits || foreignCount < KitConstants::minimumCrosstalkHits || !(own > 0))
            return std::numeric_limits<float>::quiet_NaN();
        return static_cast<float>(10.0 * std::log10(juce::jmax(1.0e-12, (foreign / foreignCount) / (own / ownCount))));
    }

    Report solve(const std::vector<PairResult>& rawResults) const
    {
        Report result;
        result.progress = progress();
        result.events = eventCount();
        const auto trackCount = tracks.size();
        std::vector<float> delays(trackCount, 0.0f), measured(trackCount, 0.0f), confidence(trackCount, 0.0f);
        std::vector<bool> inverted(trackCount, false), known(trackCount, false);
        std::vector<juce::String> notes(trackCount);
        const auto results = policy.reconcileOverheads ? reconcileOverheads(rawResults, notes) : rawResults;
        std::vector<bool> refused(trackCount, false);
        bool hasOverheads = false;
        for (const auto& track : tracks) hasOverheads = hasOverheads || track.role == Role::overhead;

        for (size_t track = 0; track < trackCount; ++track)
        {
            if (!isClose(tracks[track].role)) continue;
            const auto crosstalk = crosstalkDb(track);
            if (policy.refuseCrosstalk && std::isfinite(crosstalk) && crosstalk > KitConstants::crosstalkRefusalDb)
            {
                refused[track] = true;
                notes[track] = "crosstalk " + juce::String(crosstalk, 1) + " dB : measurement refused (another instrument masks this mic)";
                continue;
            }
            double weight = 0, lag = 0, sign = 0;
            float best = 0;
            bool outside = false;
            for (const auto& pair : results)
                if (pair.kind == PairKind::closeToOverhead && pair.xTrack == static_cast<int>(track))
                {
                    outside = outside || pair.outsideWindow;
                    if (!pair.valid) continue;
                    const auto w = std::abs(pair.r);
                    weight += w;
                    lag += w * pair.lagMs;
                    sign += pair.r;
                    best = juce::jmax(best, w);
                }
            if (outside && weight <= 0)
            {
                refused[track] = true;
                notes[track] = "strongest correlation outside the search window : measurement refused";
                continue;
            }
            if (weight > 0 && best >= 0.15f)
            {
                known[track] = true;
                measured[track] = static_cast<float>(lag / weight);
                delays[track] = juce::jlimit(0.0f, maxDelayMs, measured[track]);
                inverted[track] = sign < 0;
                confidence[track] = best;
                if (measured[track] > maxDelayMs) notes[track] = "gap above 20 ms : limited";
            }
        }
        for (size_t track = 0; track < trackCount; ++track)
        {
            if (!isSecondary(tracks[track].role)) continue;
            for (const auto& pair : results)
            {
                if (pair.kind != PairKind::secondary || pair.yTrack != static_cast<int>(track) || !pair.valid) continue;
                const auto primary = static_cast<size_t>(pair.xTrack);
                if (refused[primary])
                {
                    refused[track] = true;
                    notes[track] = juce::String(roleName(tracks[primary].role)) + " refused : no measurement";
                    continue;
                }
                const bool opposite = pair.r < 0;
                if (known[primary])
                {
                    auto delay = delays[primary] - pair.lagMs;
                    if (delay < 0)
                    {
                        notes[track] = "arrives after " + juce::String(roleName(tracks[primary].role))
                            + " : partial alignment, no latency";
                        delay = 0;
                    }
                    delays[track] = delay;
                    inverted[track] = inverted[primary] != opposite;
                }
                else
                {
                    if (pair.lagMs >= 0) { delays[primary] = pair.lagMs; delays[track] = 0; }
                    else { delays[primary] = 0; delays[track] = -pair.lagMs; }
                    inverted[primary] = false;
                    inverted[track] = opposite;
                    known[primary] = true;
                    confidence[primary] = juce::jmax(confidence[primary], std::abs(pair.r));
                    notes[primary] = "aligned to " + juce::String(roleName(tracks[track].role)) + " (no overheads)";
                }
                measured[track] = pair.lagMs;
                confidence[track] = std::abs(pair.r);
                known[track] = true;
                notes[track] = notes[track].isEmpty() ? "measured against " + juce::String(roleName(tracks[primary].role)) : notes[track];
            }
        }
        // Rooms are never advanced (that would need plugin latency); only their polarity is corrected.
        std::vector<float> roomRelative(trackCount, 0.0f);
        for (size_t track = 0; track < trackCount; ++track)
        {
            if (tracks[track].role != Role::room) continue;
            const PairResult* best = nullptr;
            for (const auto& pair : results)
                if (pair.kind == PairKind::closeToRoom && pair.yTrack == static_cast<int>(track) && pair.valid
                    && known[static_cast<size_t>(pair.xTrack)] && (best == nullptr || std::abs(pair.r) > std::abs(best->r)))
                    best = &pair;
            if (best == nullptr)
                for (const auto& pair : results)
                    if (pair.kind == PairKind::closeToRoom && pair.yTrack == static_cast<int>(track) && pair.valid
                        && (best == nullptr || std::abs(pair.r) > std::abs(best->r)))
                        best = &pair;
            if (best == nullptr) continue;
            const auto close = static_cast<size_t>(best->xTrack);
            known[track] = true;
            inverted[track] = (best->r < 0) != inverted[close];
            measured[track] = best->lagMs - (known[close] && hasOverheads ? measured[close] : 0.0f);
            roomRelative[track] = measured[track];
            confidence[track] = std::abs(best->r);
            notes[track] = juce::String(measured[track] >= 0 ? "+" : "") + juce::String(measured[track], 2)
                + (hasOverheads ? " ms after the OH" : " ms after " + juce::String(roleName(tracks[close].role)))
                + ", not advanced (realism)";
        }
        for (size_t track = 0; track < trackCount; ++track)
        {
            Recommendation recommendation;
            recommendation.memberId = tracks[track].id;
            recommendation.key = tracks[track].key;
            recommendation.label = tracks[track].label;
            recommendation.role = tracks[track].role;
            if (tracks[track].role == Role::overhead)
            {
                recommendation.measured = hasOverheads;
                recommendation.note = "kit time reference";
            }
            else
            {
                recommendation.measured = known[track];
                recommendation.invert = known[track] && inverted[track];
                recommendation.delayMs = known[track] ? delays[track] : 0.0f;
                recommendation.measuredMs = measured[track];
                recommendation.confidence = confidence[track];
                recommendation.crosstalkDb = crosstalkDb(track);
                recommendation.note = notes[track];
                if (!known[track] && isClose(tracks[track].role) && notes[track].isEmpty())
                    recommendation.note = hasOverheads ? "not enough correlated hits" : "no reference : add the overheads to the kit";
            }
            result.recommendations.push_back(recommendation);
        }

        const auto stateDelay = [&](int track, bool after) { return after && track >= 0 ? delays[static_cast<size_t>(track)] : 0.0f; };
        const auto stateSign = [&](int track, bool after) { return after && track >= 0 && inverted[static_cast<size_t>(track)] ? -1.0f : 1.0f; };
        const auto pairScore = [&](const PairResult& pair, bool after) {
            const auto lag = static_cast<float>((stateDelay(pair.xTrack, after) - stateDelay(pair.yTrack, after)) * rate / 1000.0);
            const auto sign = stateSign(pair.xTrack, after) * stateSign(pair.yTrack, after);
            const auto quality = juce::jlimit(-1.0f, 1.0f, sign * pair.at(lag) / juce::jmax(pair.rStar, 0.05f));
            const auto low = juce::jlimit(-1.0f, 1.0f, sign * pair.lowAt(lag) / juce::jmax(pair.rLowStar, 0.05f));
            const auto timing = pair.consistencyMs < 0 ? 0.5f : std::exp(-pair.consistencyMs / 0.5f);
            return juce::jlimit(0.0f, 1.0f, 0.55f * (quality + 1.0f) * 0.5f + 0.30f * (low + 1.0f) * 0.5f + 0.15f * timing);
        };
        const auto phase = [&](bool after) {
            float total = 0;
            int count = 0;
            for (const auto& pair : results)
                if (pair.valid && (pair.kind == PairKind::closeToOverhead || pair.kind == PairKind::secondary)
                    && (pair.kind == PairKind::secondary || known[static_cast<size_t>(pair.xTrack)]))
                {
                    total += pairScore(pair, after);
                    ++count;
                }
            return count > 0 ? 100.0f * total / count : -1.0f;
        };
        result.scores.phaseBefore = phase(false);
        result.scores.phaseAfter = phase(true);

        for (const auto& pair : results)
        {
            if (pair.kind != PairKind::overheadPair || pair.events < 4) continue;
            if (pair.xTrack == pair.yTrack && pair.r0 > 0.98f && std::abs(pair.lagMs) < 0.05f)
            {
                result.lines.add("Overheads : both channels of the track are identical (mono) : choose Left (L) or Right (R)");
                continue;
            }
            const auto correlation = juce::jlimit(0.0f, 1.0f, (pair.r0 + 1.0f) * 0.5f);
            float difference = std::abs(pair.lagMs);
            const PairResult* toLeft = nullptr;
            const PairResult* toRight = nullptr;
            for (const auto& other : results)
            {
                if (other.kind != PairKind::closeToOverhead || !other.valid || other.xTrack != pair.sourceTrack) continue;
                if (other.yTrack == pair.xTrack && other.yChannel == pair.xChannel) toLeft = &other;
                if (other.yTrack == pair.yTrack && other.yChannel == pair.yChannel) toRight = &other;
            }
            if (toLeft != nullptr && toRight != nullptr) difference = std::abs(toRight->lagMs - toLeft->lagMs);
            double balance = 0;
            int bands = 0;
            for (size_t band = 0; band < pair.xBands.size(); ++band)
                if (pair.xBands[band] > 1.0e-12 && pair.yBands[band] > 1.0e-12)
                {
                    balance += std::abs(10.0 * std::log10(pair.xBands[band] / pair.yBands[band]));
                    ++bands;
                }
            const auto balanceTerm = bands > 0 ? static_cast<float>(std::exp(-(balance / bands) / 4.0)) : 0.5f;
            result.scores.stereo = 100.0f * (0.4f * correlation + 0.35f * std::exp(-difference / 1.5f) + 0.25f * balanceTerm);
            result.lines.add("Overheads L/R : gap " + juce::String(difference, 2) + " ms, correlation "
                + juce::String(pair.r0, 2));
        }
        const auto room = [&](bool after) {
            float total = 0;
            int count = 0;
            for (size_t track = 0; track < trackCount; ++track)
            {
                if (tracks[track].role != Role::room || !known[track]) continue;
                const PairResult* best = nullptr;
                for (const auto& pair : results)
                    if (pair.kind == PairKind::closeToRoom && pair.yTrack == static_cast<int>(track) && pair.valid
                        && (best == nullptr || std::abs(pair.r) > std::abs(best->r)))
                        best = &pair;
                if (best == nullptr) continue;
                const auto sign = stateSign(best->xTrack, after) * stateSign(static_cast<int>(track), after);
                const auto polarity = sign * best->r > 0 ? 1.0f : 0.0f;
                const auto correlation = juce::jlimit(0.0f, 1.0f, std::abs(best->r) / 0.4f);
                const auto plausible = roomRelative[track] >= 0.3f && roomRelative[track] <= 25.0f ? 1.0f : 0.5f;
                total += 0.35f * polarity + 0.45f * correlation + 0.2f * plausible;
                ++count;
            }
            return count > 0 ? 100.0f * total / count : -1.0f;
        };
        result.scores.roomBefore = room(false);
        result.scores.roomAfter = room(true);

        int corrected = 0;
        for (const auto& recommendation : result.recommendations)
        {
            if (recommendation.role == Role::overhead)
            {
                result.lines.insert(0, recommendation.label + " : reference");
                continue;
            }
            juce::String line = recommendation.label + " : ";
            if (!recommendation.measured)
                line += recommendation.note;
            else
            {
                juce::StringArray actions;
                if (recommendation.invert) actions.add("polarity inverted");
                if (recommendation.delayMs >= 0.01f) actions.add("delay by " + juce::String(recommendation.delayMs, 2) + " ms");
                if (actions.isEmpty()) actions.add("already consistent");
                else ++corrected;
                line += actions.joinIntoString(", ") + " (r " + juce::String(recommendation.confidence, 2) + ")";
                if (recommendation.note.isNotEmpty()) line += " / " + recommendation.note;
            }
            result.lines.add(line);
        }
        result.valid = result.scores.phaseBefore >= 0 || result.scores.roomBefore >= 0 || result.scores.stereo >= 0;
        result.headline = result.valid ? juce::String(corrected) + " correction(s) proposed" : "Not enough measurements";
        return result;
    }

private:
    struct Signal { int track, channel; Role role; int side; };
    struct Pair
    {
        PairKind kind;
        int source, x, y;
        Band band;
        float minimumMs, maximumMs;
        std::vector<std::complex<float>> even, odd, low;
        std::vector<float> xPower, yPower;
        double ex = 0, ey = 0, exLow = 0, eyLow = 0;
        std::array<double, 4> xBands {}, yBands {};
        int events = 0;
    };

    // Finding 5 (kit 4 of the corpus): a low close mic (Kick, whose correlation oscillates at about 125 Hz) can lock one
    // cycle (8 ms) apart on the two overheads; the weighted mean of the two lags then matches neither. The L / R overhead
    // spacing is a property of the room, so every close mic must agree with the most trustworthy one. A mic that does not
    // gets its less confident pair re-located near the expected lag; if that fails, its pairs are refused.
    std::vector<PairResult> reconcileOverheads(const std::vector<PairResult>& results, std::vector<juce::String>& notes) const
    {
        auto adjusted = results;
        const auto sideOf = [this](const PairResult& pair) {
            const auto& output = tracks[static_cast<size_t>(pair.yTrack)];
            if (output.position == positionLeft) return 1;
            if (output.position == positionRight) return 2;
            return pair.yChannel == 0 ? 1 : pair.yChannel == 1 ? 2 : 0;
        };
        struct Mic { size_t left = SIZE_MAX, right = SIZE_MAX; };
        std::vector<Mic> mics(tracks.size());
        for (size_t index = 0; index < adjusted.size(); ++index)
        {
            const auto& pair = adjusted[index];
            if (pair.kind != PairKind::closeToOverhead || !pair.valid || pair.xTrack < 0) continue;
            const auto side = sideOf(pair);
            auto& mic = mics[static_cast<size_t>(pair.xTrack)];
            if (side == 1 && (mic.left == SIZE_MAX || std::abs(pair.r) > std::abs(adjusted[mic.left].r))) mic.left = index;
            if (side == 2 && (mic.right == SIZE_MAX || std::abs(pair.r) > std::abs(adjusted[mic.right].r))) mic.right = index;
        }
        const auto confidenceOf = [&](const Mic& mic) { return juce::jmin(std::abs(adjusted[mic.left].r), std::abs(adjusted[mic.right].r)); };
        int reference = -1;
        for (size_t track = 0; track < mics.size(); ++track)
            if (mics[track].left != SIZE_MAX && mics[track].right != SIZE_MAX
                && (reference < 0 || confidenceOf(mics[track]) > confidenceOf(mics[static_cast<size_t>(reference)])))
                reference = static_cast<int>(track);
        if (reference < 0) return adjusted;
        const auto& best = mics[static_cast<size_t>(reference)];
        const auto spacing = adjusted[best.right].lagMs - adjusted[best.left].lagMs;
        for (size_t track = 0; track < mics.size(); ++track)
        {
            const auto& mic = mics[track];
            if (static_cast<int>(track) == reference || mic.left == SIZE_MAX || mic.right == SIZE_MAX) continue;
            auto& left = adjusted[mic.left];
            auto& right = adjusted[mic.right];
            if (std::abs((right.lagMs - left.lagMs) - spacing) <= KitConstants::spacingToleranceMs) continue;
            const bool moveRight = std::abs(right.r) < std::abs(left.r);
            auto& moved = moveRight ? right : left;
            const auto& anchor = moveRight ? left : right;
            const auto expectedMs = anchor.lagMs + (moveRight ? spacing : -spacing);
            const auto centre = static_cast<int>(std::lround(expectedMs * rate / 1000.0));
            const auto reach = static_cast<int>(std::lround(KitConstants::relocateWindowMs * rate / 1000.0));
            int bestLag = centre;
            float bestValue = 0;
            for (int lag = centre - reach; lag <= centre + reach; ++lag)
            {
                const auto value = moved.at(static_cast<float>(lag));
                if (std::abs(value) > std::abs(bestValue)) { bestValue = value; bestLag = lag; }
            }
            if (std::abs(bestValue) >= 0.5f * std::abs(moved.r) && std::abs(bestValue) >= 0.12f)
            {
                notes[track] = "overhead L/R gap inconsistent (cycle slip) : " + juce::String(moved.lagMs, 2) + " ms relocated to "
                    + juce::String(static_cast<float>(bestLag) * 1000.0f / static_cast<float>(rate), 2) + " ms";
                moved.lagSamples = static_cast<float>(bestLag);
                moved.lagMs = static_cast<float>(bestLag * 1000.0 / rate);
                moved.r = juce::jlimit(-1.0f, 1.0f, bestValue);
            }
            else
            {
                notes[track] = "overhead L/R gap inconsistent with the other mics : measurement refused";
                left.valid = right.valid = false;
            }
        }
        return adjusted;
    }

    void addPair(PairKind kind, int source, int x, int y, Band band, float minimumMs, float maximumMs)
    {
        if (source < 0 || x < 0 || y < 0) return;
        Pair pair { kind, source, x, y, band, minimumMs, maximumMs, {}, {}, {}, {}, {}, 0, 0, 0, 0, {}, {}, 0 };
        pair.even.assign(static_cast<size_t>(half) + 1, {});
        pair.odd.assign(static_cast<size_t>(half) + 1, {});
        pair.low.assign(static_cast<size_t>(half) + 1, {});
        pair.xPower.assign(static_cast<size_t>(half) + 1, 0.0f);
        pair.yPower.assign(static_cast<size_t>(half) + 1, 0.0f);
        pairs.push_back(std::move(pair));
    }

    int binOf(float frequency) const noexcept
    {
        return juce::jlimit(0, half, static_cast<int>(std::lround(frequency * size / rate)));
    }

    bool spectrum(int signal, juce::int64 start, int length, std::vector<std::complex<float>>& destination, const Reader& read)
    {
        std::fill(timeBuffer.begin(), timeBuffer.end(), 0.0f);
        const auto& reference = signals[static_cast<size_t>(signal)];
        if (!read(reference.track, reference.channel, start, length, timeBuffer.data())) return false;
        for (int index = 0; index < length; ++index)
            if (!std::isfinite(timeBuffer[static_cast<size_t>(index)])) return false;
        fft->performRealOnlyForwardTransform(timeBuffer.data(), true);
        destination.resize(static_cast<size_t>(half) + 1);
        for (int bin = 0; bin <= half; ++bin)
            destination[static_cast<size_t>(bin)] = { timeBuffer[static_cast<size_t>(bin) * 2], timeBuffer[static_cast<size_t>(bin) * 2 + 1] };
        return true;
    }

    // One row per hit (events of all sources closer than hitClusterMs): the mean power of every close mic over a short
    // window after the first event, -1 for the tracks that are not close mics or could not be read.
    void recordHits(std::vector<juce::int64>& events, const Reader& read)
    {
        if (events.empty()) return;
        std::sort(events.begin(), events.end());
        const auto cluster = static_cast<juce::int64>(rate * KitConstants::hitClusterMs / 1000.0);
        auto clusterStart = events.front() - cluster - 1;
        for (const auto event : events)
        {
            if (event - clusterStart <= cluster) continue;
            clusterStart = event;
            if (static_cast<int>(hitRows.size()) >= KitConstants::maximumHitRows) return;
            std::vector<float> row(tracks.size(), -1.0f);
            int readable = 0;
            for (size_t track = 0; track < tracks.size(); ++track)
            {
                if (!isClose(tracks[track].role)) continue;
                if (!read(static_cast<int>(track), -1, event - pre, hitWindow, levelBuffer.data())) continue;
                double power = 0;
                bool finite = true;
                for (int index = 0; index < hitWindow; ++index)
                {
                    const auto value = levelBuffer[static_cast<size_t>(index)];
                    finite = finite && std::isfinite(value);
                    power += static_cast<double>(value) * value;
                }
                if (!finite) continue;
                row[track] = static_cast<float>(power / hitWindow);
                ++readable;
            }
            if (readable >= 2) hitRows.push_back(std::move(row));
        }
    }

    bool accumulate(int sourceTrack, juce::int64 event, const Reader& read)
    {
        struct Cached { int signal; bool longWindow; bool ok; std::vector<std::complex<float>> data; };
        std::vector<Cached> cache;
        const auto start = event - pre;
        const auto get = [&](int signal, bool longWindow) -> size_t {
            for (size_t index = 0; index < cache.size(); ++index)
                if (cache[index].signal == signal && cache[index].longWindow == longWindow) return index;
            Cached entry { signal, longWindow, false, {} };
            entry.ok = spectrum(signal, longWindow ? start - maximumLag : start,
                longWindow ? window + 2 * maximumLag : window, entry.data, read);
            cache.push_back(std::move(entry));
            return cache.size() - 1;
        };
        bool used = false;
        const std::array<float, 5> edges { 20.0f, 200.0f, 1000.0f, 5000.0f, 16000.0f };
        for (auto& pair : pairs)
        {
            if (pair.source != sourceTrack) continue;
            const auto xIndex = get(pair.x, false);
            const auto yIndex = get(pair.y, true);
            const auto centralIndex = get(pair.y, false);
            const auto& x = cache[xIndex];
            const auto& y = cache[yIndex];
            const auto& central = cache[centralIndex];
            if (!x.ok || !y.ok || !central.ok) continue;
            const bool odd = (pair.events & 1) != 0;
            auto& target = odd ? pair.odd : pair.even;
            const auto low = binOf(pair.band.low), high = binOf(pair.band.high);
            for (int bin = juce::jmax(1, low); bin <= high; ++bin)
            {
                const auto index = static_cast<size_t>(bin);
                const auto& a = x.data[index];
                target[index] += std::conj(a) * y.data[index];
                const auto xPower = std::norm(a), yPower = std::norm(central.data[index]);
                pair.xPower[index] += xPower;
                pair.yPower[index] += yPower;
                pair.ex += 2.0 * xPower;
                pair.ey += 2.0 * yPower;
            }
            for (int bin = juce::jmax(1, binOf(30.0f)); bin <= binOf(200.0f); ++bin)
            {
                const auto index = static_cast<size_t>(bin);
                const auto& a = x.data[index];
                pair.low[index] += std::conj(a) * y.data[index];
                pair.exLow += 2.0 * std::norm(a);
                pair.eyLow += 2.0 * std::norm(central.data[index]);
            }
            if (pair.kind == PairKind::overheadPair)
                for (size_t band = 0; band + 1 < edges.size(); ++band)
                    for (int bin = juce::jmax(1, binOf(edges[band])); bin < binOf(edges[band + 1]); ++bin)
                    {
                        pair.xBands[band] += std::norm(x.data[static_cast<size_t>(bin)]);
                        pair.yBands[band] += std::norm(central.data[static_cast<size_t>(bin)]);
                    }
            ++pair.events;
            used = true;
        }
        return used;
    }

    // Analytic cross-correlation; with powers given, the cross-spectrum is SCOT-whitened so that
    // tonal resonances (long kick/snare decays) cannot hide the first arrival.
    void correlate(std::vector<std::complex<float>>& destination, const std::vector<std::complex<float>>& cross,
                   const std::vector<std::complex<float>>* other, const Pair* whitening = nullptr) const
    {
        std::fill(complexBuffer.begin(), complexBuffer.end(), std::complex<float> {});
        const auto low = whitening != nullptr ? juce::jmax(1, binOf(whitening->band.low)) : 1;
        const auto high = whitening != nullptr ? juce::jmin(half - 1, binOf(whitening->band.high)) : half - 1;
        for (int bin = low; bin <= high; ++bin)
        {
            const auto index = static_cast<size_t>(bin);
            auto value = cross[index] + (other != nullptr ? (*other)[index] : std::complex<float> {});
            if (whitening != nullptr)
            {
                const auto scale = std::sqrt(static_cast<double>(whitening->xPower[index]) * whitening->yPower[index]);
                value = scale > 1.0e-30 ? value / static_cast<float>(scale) : std::complex<float> {};
            }
            complexBuffer[index] = 2.0f * value;
        }
        fft->perform(complexBuffer.data(), destination.data(), true);
    }

    struct Search { bool ok = false; float lag = 0, value = 0; };

    // Earliest prominent peak of the whitened envelope = first arrival; the nearest extremum of
    // the plain correlation gives the exact lag and the polarity.
    Search search(const Pair& pair, double norm, double centroid) const
    {
        Search result;
        if (!(norm > 1.0e-20)) return result;
        const auto first = maximumLag + static_cast<int>(std::lround(pair.minimumMs * rate / 1000.0));
        const auto last = maximumLag + static_cast<int>(std::lround(pair.maximumMs * rate / 1000.0));
        double envelopeMaximum = 0;
        for (int index = first; index <= last; ++index)
            envelopeMaximum = juce::jmax(envelopeMaximum, static_cast<double>(std::abs(whitened[static_cast<size_t>(index)])));
        if (!(envelopeMaximum > 0)) return result;
        const auto prominence = juce::jmax(1, static_cast<int>(std::lround(rate * 0.00025)));
        auto chosen = -1;
        for (int index = first + 1; index < last && chosen < 0; ++index)
        {
            const auto value = std::abs(whitened[static_cast<size_t>(index)]);
            if (value < 0.75 * envelopeMaximum) continue;
            bool peak = true;
            for (int other = juce::jmax(first, index - prominence); other <= juce::jmin(last, index + prominence) && peak; ++other)
                peak = std::abs(whitened[static_cast<size_t>(other)]) <= value;
            if (peak) chosen = index;
        }
        if (chosen < 0)
        {
            chosen = first;
            for (int index = first; index <= last; ++index)
                if (std::abs(whitened[static_cast<size_t>(index)]) > std::abs(whitened[static_cast<size_t>(chosen)])) chosen = index;
        }
        const auto halfWidth = static_cast<int>(std::lround(juce::jlimit(0.00015, 0.0025, 1.0 / (4.0 * juce::jmax(1.0, centroid))) * rate));
        const auto from = juce::jmax(first, chosen - halfWidth), to = juce::jmin(last, chosen + halfWidth);
        auto best = from;
        for (int index = from; index <= to; ++index)
            if (std::abs(analytic[static_cast<size_t>(index)].real()) > std::abs(analytic[static_cast<size_t>(best)].real())) best = index;
        auto offset = 0.0f;
        if (best > first && best < last)
        {
            const auto a = std::abs(analytic[static_cast<size_t>(best - 1)].real());
            const auto b = std::abs(analytic[static_cast<size_t>(best)].real());
            const auto c = std::abs(analytic[static_cast<size_t>(best + 1)].real());
            const auto denominator = a - 2 * b + c;
            if (std::abs(denominator) > 1.0e-12f) offset = juce::jlimit(-0.5f, 0.5f, 0.5f * (a - c) / denominator);
        }
        result.ok = true;
        result.lag = static_cast<float>(best - maximumLag) + offset;
        result.value = static_cast<float>(analytic[static_cast<size_t>(best)].real() / norm);
        return result;
    }

    PairResult evaluatePair(const Pair& pair) const
    {
        PairResult result;
        result.kind = pair.kind;
        result.xTrack = signals[static_cast<size_t>(pair.x)].track;
        result.yTrack = signals[static_cast<size_t>(pair.y)].track;
        result.xChannel = signals[static_cast<size_t>(pair.x)].channel;
        result.yChannel = signals[static_cast<size_t>(pair.y)].channel;
        result.sourceTrack = pair.source;
        result.events = pair.events;
        result.rate = rate;
        result.centre = maximumLag;
        result.xBands = pair.xBands;
        result.yBands = pair.yBands;
        if (pair.events < 2) return result;
        const auto norm = std::sqrt(pair.ex * pair.ey) / size;
        double weighted = 0, magnitude = 0;
        for (int bin = juce::jmax(1, binOf(pair.band.low)); bin <= binOf(pair.band.high); ++bin)
        {
            const auto value = std::abs(pair.even[static_cast<size_t>(bin)] + pair.odd[static_cast<size_t>(bin)]);
            weighted += value * bin * rate / size;
            magnitude += value;
        }
        const auto centroid = magnitude > 0 ? weighted / magnitude : 1000.0;
        correlate(whitened, pair.even, &pair.odd, &pair);
        correlate(analytic, pair.even, &pair.odd);
        const auto main = search(pair, norm, centroid);
        result.curve.resize(static_cast<size_t>(2 * maximumLag + 1));
        for (int index = 0; index <= 2 * maximumLag; ++index)
            result.curve[static_cast<size_t>(index)] = norm > 0 ? static_cast<float>(analytic[static_cast<size_t>(index)].real() / norm) : 0.0f;
        result.r0 = result.curve[static_cast<size_t>(maximumLag)];
        const auto first = maximumLag + static_cast<int>(std::lround(pair.minimumMs * rate / 1000.0));
        const auto last = maximumLag + static_cast<int>(std::lround(pair.maximumMs * rate / 1000.0));
        if (main.ok)
        {
            double inside = 0, outside = 0;
            for (int index = 0; index <= 2 * maximumLag; ++index)
            {
                const auto value = static_cast<double>(std::abs(whitened[static_cast<size_t>(index)]));
                if (index >= first && index <= last) inside = juce::jmax(inside, value);
                else outside = juce::jmax(outside, value);
            }
            result.outsideRatio = inside > 0 ? static_cast<float>(outside / inside) : 0.0f;
            result.outsideWindow = policy.refuseOutsideWindow && result.outsideRatio >= KitConstants::outsideWindowRatio;
        }
        float star = 0;
        for (int index = first; index <= last; ++index) star = juce::jmax(star, std::abs(result.curve[static_cast<size_t>(index)]));
        result.rStar = star;
        if (main.ok)
        {
            result.lagSamples = main.lag;
            result.lagMs = static_cast<float>(main.lag * 1000.0 / rate);
            result.r = juce::jlimit(-1.0f, 1.0f, main.value);
            result.valid = pair.events >= 6 && std::abs(main.value) >= 0.12f && !result.outsideWindow;
        }
        const auto lowNorm = std::sqrt(pair.exLow * pair.eyLow);
        if (lowNorm > 1.0e-20 && pair.band.low < 200.0f && pair.band.high > 30.0f)
        {
            correlate(analytic, pair.low, nullptr);
            result.lowCurve.resize(result.curve.size());
            float lowStar = 0;
            for (int index = 0; index <= 2 * maximumLag; ++index)
            {
                result.lowCurve[static_cast<size_t>(index)] = static_cast<float>(analytic[static_cast<size_t>(index)].real() * size / lowNorm);
                if (index >= first && index <= last) lowStar = juce::jmax(lowStar, std::abs(result.lowCurve[static_cast<size_t>(index)]));
            }
            result.rLowStar = lowStar;
        }
        else
            result.rLowStar = result.rStar;
        if (pair.events >= 8)
        {
            correlate(whitened, pair.even, nullptr, &pair);
            correlate(analytic, pair.even, nullptr);
            const auto even = search(pair, norm, centroid);
            correlate(whitened, pair.odd, nullptr, &pair);
            correlate(analytic, pair.odd, nullptr);
            const auto odd = search(pair, norm, centroid);
            if (even.ok && odd.ok) result.consistencyMs = static_cast<float>(std::abs(even.lag - odd.lag) * 1000.0 / rate);
        }
        return result;
    }

    std::vector<TrackInput> tracks;
    std::vector<Signal> signals;
    std::vector<Pair> pairs;
    std::vector<int> sources;
    std::vector<OnsetDetector> detectors;
    std::vector<int> sourceEvents;
    std::vector<std::vector<float>> hitRows;
    std::vector<float> levelBuffer;
    KitPolicy policy;
    int hitWindow = 1440;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> timeBuffer, scratch;
    mutable std::vector<std::complex<float>> complexBuffer, analytic, whitened;
    double rate = 48000;
    int window = 2048, maximumLag = 1200, pre = 96, size = 8192, half = 4096;
};
}
