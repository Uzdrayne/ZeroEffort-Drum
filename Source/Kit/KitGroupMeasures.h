#pragma once

#include "KitTypes.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace kit
{
// Read-only measures of the microphones of one sub-bus (document 08, SB01 to SB03, SB06 to SB08). They look at what the bus receives:
// the microphones after the correction of their own instance (delay and polarity), added with unit gain. Nothing here changes a
// setting; the numbers are H starting points to calibrate on the corpus (alert thresholds in GroupConstants).
struct GroupConstants
{
    static constexpr double lowBandLowHz = 40, lowBandHighHz = 250;   // the band where microphones cancel or add
    static constexpr int frame = 4096;
    static constexpr float activeBelowPeakDb = 30.0f;               // frames quieter than the loudest minus this are not "active"
    static constexpr float polarityAlert = -0.25f;                  // low-band correlation with the other mics under this: suspect
    static constexpr float delayAlertMs = 1.0f;                     // residual delay above this: alignment insufficient
    static constexpr double searchMs = 10.0;
    static constexpr double minimumSeconds = 1.0;
    static constexpr float unavailableValue = std::numeric_limits<float>::quiet_NaN();
};

struct GroupMic
{
    juce::String name;
    std::vector<float> samples;      // what the instance received: the raw microphone, before its own correction
    float delayMs = 0;               // correction applied by its instance
    bool inverted = false;
    Role role = Role::none;
    juce::uint32 id = 0;             // registry member, to address a balance trim to its instance
};

struct GroupMicMeasure
{
    juce::String name;
    float levelDb = GroupConstants::unavailableValue;           // SB01: active level relative to the sum
    float lowCorrelation = GroupConstants::unavailableValue;    // SB06: low band, against the sum of the other mics
    float residualDelayMs = GroupConstants::unavailableValue;   // SB07: against the loudest mic, after the corrections
};

struct GroupReport
{
    bool valid = false;
    juce::String reason;
    double rate = 0, seconds = 0;
    std::vector<GroupMicMeasure> mics;
    float sumGainDb = GroupConstants::unavailableValue;         // SB03: energy of the sum over the sum of the energies, whole signal
    float sumGainLowDb = GroupConstants::unavailableValue;      // SB03 in the low band: below 0 dB, the microphones cancel each other
    float coherence = GroupConstants::unavailableValue;         // SB02: mean zero-lag correlation of the mic pairs, low band
    float clipMarginDb = GroupConstants::unavailableValue;      // SB08: headroom of the sum
};

class GroupMeasures
{
public:
    static GroupReport analyse(const std::vector<GroupMic>& mics, double rate)
    {
        GroupReport report;
        report.rate = rate;
        for (const auto& mic : mics) report.mics.push_back({ mic.name, {}, {}, {} });
        if (!(rate >= 8000 && rate <= 192000)) { report.reason = "sample rate out of range"; return report; }
        if (mics.size() < 2) { report.reason = "a group report needs at least two microphones of the same instrument in the same Kit group"; return report; }
        size_t length = std::numeric_limits<size_t>::max();
        for (const auto& mic : mics) length = std::min(length, mic.samples.size());
        // The correction of an instance delays its microphone: the signal the bus receives starts `delay` samples later.
        std::vector<size_t> delays;
        size_t longest = 0;
        for (const auto& mic : mics)
        {
            delays.push_back(static_cast<size_t>(std::max(0.0, std::round(static_cast<double>(mic.delayMs) * rate / 1000.0))));
            longest = std::max(longest, delays.back());
        }
        if (longest + static_cast<size_t>(rate * GroupConstants::minimumSeconds) >= length) { report.reason = "the correction delays are longer than the audio read"; return report; }
        std::vector<std::vector<float>> aligned;
        for (size_t mic = 0; mic < mics.size(); ++mic)
        {
            // Samples before `longest` are dropped for every mic: all of them have started by then.
            std::vector<float> shifted(length - longest);
            const auto sign = mics[mic].inverted ? -1.0f : 1.0f;
            for (size_t index = 0; index < shifted.size(); ++index)
            {
                const auto position = index + longest;
                shifted[index] = position >= delays[mic] ? sign * mics[mic].samples[position - delays[mic]] : 0.0f;
            }
            aligned.push_back(std::move(shifted));
        }
        const auto common = length - longest;
        report.seconds = static_cast<double>(common) / rate;
        if (report.seconds < GroupConstants::minimumSeconds) { report.reason = "less than 1 s of common audio"; return report; }

        std::vector<float> sum(common, 0.0f);
        for (const auto& line : aligned)
            for (size_t index = 0; index < common; ++index) sum[index] += line[index];
        float peak = 0;
        for (const auto value : sum) peak = std::max(peak, std::abs(value));
        if (!(peak > 1.0e-6f)) { report.reason = "silent audio"; return report; }
        report.clipMarginDb = -20.0f * std::log10(peak);

        double sumEnergy = 0, partsEnergy = 0;
        for (const auto value : sum) sumEnergy += static_cast<double>(value) * value;
        for (const auto& line : aligned) for (const auto value : line) partsEnergy += static_cast<double>(value) * value;
        if (partsEnergy > 0 && sumEnergy > 0) report.sumGainDb = static_cast<float>(10 * std::log10(sumEnergy / partsEnergy));

        // Active frames of the sum: the level of every mic is read where the group plays, not over the silences.
        const auto frames = common / GroupConstants::frame;
        std::vector<double> frameEnergy(frames, 0.0);
        double loudest = 0;
        for (size_t frame = 0; frame < frames; ++frame)
        {
            for (size_t index = 0; index < static_cast<size_t>(GroupConstants::frame); ++index)
                frameEnergy[frame] += static_cast<double>(sum[frame * GroupConstants::frame + index]) * sum[frame * GroupConstants::frame + index];
            loudest = std::max(loudest, frameEnergy[frame]);
        }
        const auto threshold = loudest * std::pow(10.0, -GroupConstants::activeBelowPeakDb / 10.0);
        std::vector<size_t> active;
        for (size_t frame = 0; frame < frames; ++frame) if (frameEnergy[frame] >= threshold && frameEnergy[frame] > 0) active.push_back(frame);
        if (!active.empty())
        {
            double activeSum = 0;
            for (const auto frame : active) activeSum += frameEnergy[frame];
            for (size_t mic = 0; mic < aligned.size(); ++mic)
            {
                double energy = 0;
                for (const auto frame : active)
                    for (size_t index = 0; index < static_cast<size_t>(GroupConstants::frame); ++index)
                    {
                        const auto value = aligned[mic][frame * GroupConstants::frame + index];
                        energy += static_cast<double>(value) * value;
                    }
                if (energy > 0 && activeSum > 0) report.mics[mic].levelDb = static_cast<float>(10 * std::log10(energy / activeSum));
            }
        }

        // Low band: where polarity and delay faults show.
        std::vector<std::vector<float>> low;
        for (const auto& line : aligned) low.push_back(lowBand(line, rate));
        const auto lowSum = [&] { std::vector<float> total(common, 0.0f); for (const auto& line : low) for (size_t i = 0; i < common; ++i) total[i] += line[i]; return total; }();
        double lowSumEnergy = 0, lowPartsEnergy = 0;
        for (const auto value : lowSum) lowSumEnergy += static_cast<double>(value) * value;
        for (const auto& line : low) for (const auto value : line) lowPartsEnergy += static_cast<double>(value) * value;
        if (lowSumEnergy > 0 && lowPartsEnergy > 0) report.sumGainLowDb = static_cast<float>(10 * std::log10(lowSumEnergy / lowPartsEnergy));
        for (size_t mic = 0; mic < low.size(); ++mic)
        {
            std::vector<float> others(common);
            for (size_t i = 0; i < common; ++i) others[i] = lowSum[i] - low[mic][i];
            report.mics[mic].lowCorrelation = correlation(low[mic], others);
        }
        double pairs = 0, pairSum = 0;
        for (size_t a = 0; a < low.size(); ++a)
            for (size_t b = a + 1; b < low.size(); ++b)
            {
                const auto value = correlation(low[a], low[b]);
                if (std::isfinite(value)) { pairSum += value; pairs += 1; }
            }
        if (pairs > 0) report.coherence = static_cast<float>(pairSum / pairs);

        // Residual delay: the lag of the best low-band match with the loudest mic, searched on a decimated copy.
        size_t reference = 0;
        for (size_t mic = 1; mic < report.mics.size(); ++mic)
            if (!std::isfinite(report.mics[reference].levelDb) || (std::isfinite(report.mics[mic].levelDb) && report.mics[mic].levelDb > report.mics[reference].levelDb)) reference = mic;
        constexpr size_t decimation = 8;
        const auto decimate = [&](const std::vector<float>& line) {
            std::vector<float> small(line.size() / decimation);
            for (size_t i = 0; i < small.size(); ++i) small[i] = line[i * decimation];
            return small;
        };
        const auto smallRate = rate / static_cast<double>(decimation);
        const auto reach = static_cast<int>(std::round(GroupConstants::searchMs * smallRate / 1000.0));
        const auto referenceSmall = decimate(low[reference]);
        report.mics[reference].residualDelayMs = 0.0f;
        for (size_t mic = 0; mic < low.size(); ++mic)
        {
            if (mic == reference) continue;
            const auto small = decimate(low[mic]);
            float best = 0;
            int bestLag = 0;
            for (int lag = -reach; lag <= reach; ++lag)
            {
                const auto value = lagCorrelation(referenceSmall, small, lag);
                if (std::isfinite(value) && std::abs(value) > std::abs(best)) { best = value; bestLag = lag; }
            }
            if (best != 0) report.mics[mic].residualDelayMs = static_cast<float>(1000.0 * bestLag / smallRate);
        }
        report.valid = true;
        return report;
    }

    static juce::String toText(const GroupReport& report, const juce::String& title)
    {
        juce::String text = "GROUP REPORT / " + title + "\n";
        if (!report.valid) return text + report.reason + "\n";
        const auto number = [](float value, int decimals = 1) { return std::isfinite(value) ? juce::String(value, decimals) : juce::String("n/a"); };
        text << juce::String(report.mics.size()) << " microphones, " << number(static_cast<float>(report.seconds)) << " s of playback read at "
             << juce::String(report.rate / 1000.0, 1) << " kHz, after the correction of each microphone's own instance.\n\n";
        text << "Sum: " << number(report.sumGainDb) << " dB against the energy of the microphones taken alone ("
             << number(report.sumGainLowDb) << " dB below 250 Hz), mean low-band correlation " << number(report.coherence, 2)
             << ", headroom " << number(report.clipMarginDb) << " dB.\n\n";
        bool alert = false;
        for (const auto& mic : report.mics)
        {
            text << "- " << mic.name << " : level " << number(mic.levelDb) << " dB against the sum, low-band correlation with the others "
                 << number(mic.lowCorrelation, 2) << ", residual delay " << number(mic.residualDelayMs, 2) << " ms";
            if (std::isfinite(mic.lowCorrelation) && mic.lowCorrelation < GroupConstants::polarityAlert)
            {
                text << "  <- polarity suspect: the low end cancels with the other microphones";
                alert = true;
            }
            if (std::isfinite(mic.residualDelayMs) && std::abs(mic.residualDelayMs) > GroupConstants::delayAlertMs)
            {
                text << "  <- alignment insufficient: use Kit / Phase on the microphone tracks";
                alert = true;
            }
            text << "\n";
        }
        if (std::isfinite(report.sumGainLowDb) && report.sumGainLowDb < -1.5f)
        {
            text << "\nThe low end of the sum is weaker than the microphones alone: check polarity and delay before any group EQ.\n";
            alert = true;
        }
        if (!alert) text << "\nNo polarity or alignment problem seen in this passage.\n";
        text << "Measures only: nothing was changed.\n";
        return text;
    }

private:
    static std::vector<float> lowBand(const std::vector<float>& line, double rate)
    {
        juce::dsp::IIR::Filter<float> high(juce::dsp::IIR::Coefficients<float>::makeHighPass(rate, GroupConstants::lowBandLowHz));
        juce::dsp::IIR::Filter<float> low1(juce::dsp::IIR::Coefficients<float>::makeLowPass(rate, GroupConstants::lowBandHighHz));
        juce::dsp::IIR::Filter<float> low2(juce::dsp::IIR::Coefficients<float>::makeLowPass(rate, GroupConstants::lowBandHighHz));
        std::vector<float> result(line.size());
        for (size_t index = 0; index < line.size(); ++index) result[index] = low2.processSample(low1.processSample(high.processSample(line[index])));
        return result;
    }

    static float correlation(const std::vector<float>& a, const std::vector<float>& b)
    {
        double ab = 0, aa = 0, bb = 0;
        const auto length = std::min(a.size(), b.size());
        for (size_t index = 0; index < length; ++index)
        {
            ab += static_cast<double>(a[index]) * b[index];
            aa += static_cast<double>(a[index]) * a[index];
            bb += static_cast<double>(b[index]) * b[index];
        }
        return aa > 0 && bb > 0 ? static_cast<float>(ab / std::sqrt(aa * bb)) : GroupConstants::unavailableValue;
    }

    // Correlation of a[n] with b[n + lag] (b later than a for a positive lag).
    static float lagCorrelation(const std::vector<float>& a, const std::vector<float>& b, int lag)
    {
        double ab = 0, aa = 0, bb = 0;
        const auto length = static_cast<int>(std::min(a.size(), b.size()));
        for (int index = std::max(0, -lag); index < length && index + lag < length; ++index)
        {
            const double x = a[static_cast<size_t>(index)], y = b[static_cast<size_t>(index + lag)];
            ab += x * y; aa += x * x; bb += y * y;
        }
        return aa > 0 && bb > 0 ? static_cast<float>(ab / std::sqrt(aa * bb)) : GroupConstants::unavailableValue;
    }
};

// SB4, document 08 section 7.1: balance trims, proposed and never applied silently. The sub-bus measures, the user validates, each microphone
// instance then moves its own input gain. Starting points (H): where two engineers left each microphone against the sum of its group
// (Track 2 by A.H., Track 3 by another engineer, docs/archives/ANALYSE_CORPUS_19OCT.md), widened by 1.5 dB on both sides. They did not agree on
// one value (Snare Bottom: -7.0 and -21.1 dB), so a trim is only proposed for a microphone clearly outside what either of them did.
struct BalanceConstants
{
    static constexpr float gain = 0.5f;            // k of the document: share of the distance to the band that is proposed
    static constexpr float maximumTrimDb = 6.0f;
    static constexpr float minimumTrimDb = 0.5f;   // below this, nothing is proposed
    static constexpr float margin = 1.5f;
};

struct BalanceBand { Role role; float lowDb, highDb; };   // plausible level of a microphone against the sum, in dB
inline constexpr std::array<BalanceBand, 4> balanceBands {{
    { Role::kickIn, -3.7f - BalanceConstants::margin, -1.0f + BalanceConstants::margin },
    { Role::kickOut, -6.7f - BalanceConstants::margin, -2.4f + BalanceConstants::margin },
    { Role::snareTop, -1.0f - BalanceConstants::margin, 0.0f + BalanceConstants::margin },
    { Role::snareBottom, -21.1f - BalanceConstants::margin, -7.0f + BalanceConstants::margin },
}};

struct BalanceLine
{
    juce::uint32 id = 0;
    juce::String name;
    float levelDb = GroupConstants::unavailableValue;
    float trimDb = 0;
    juce::String note;
};

struct BalanceProposal
{
    bool valid = false;
    juce::String note;                 // why nothing is proposed, when it is the case
    std::vector<BalanceLine> lines;
    bool any() const
    {
        return valid && std::any_of(lines.begin(), lines.end(), [](const BalanceLine& line) { return line.trimDb != 0; });
    }
};

class GroupBalance
{
public:
    static BalanceProposal propose(const std::vector<GroupMic>& mics, const GroupReport& report, float intensity)
    {
        BalanceProposal proposal;
        if (!report.valid || report.mics.size() != mics.size()) { proposal.note = "no valid group measure"; return proposal; }
        proposal.valid = true;
        for (size_t index = 0; index < mics.size(); ++index)
            proposal.lines.push_back({ mics[index].id, mics[index].name, report.mics[index].levelDb, 0.0f, {} });
        // Refusal of the document: with a mic late or in doubtful polarity, the levels read are not the ones the bus will hold.
        // When two microphones cancel, both look suspect: the weaker one is named (the louder one is the reference to align on).
        const GroupMicMeasure* culprit = nullptr;
        bool polarityCause = false;
        for (const auto& mic : report.mics)
        {
            const bool polarity = std::isfinite(mic.lowCorrelation) && mic.lowCorrelation < GroupConstants::polarityAlert;
            const bool late = std::isfinite(mic.residualDelayMs) && std::abs(mic.residualDelayMs) > GroupConstants::delayAlertMs;
            if (!polarity && !late) continue;
            const auto weaker = culprit == nullptr || (std::isfinite(mic.levelDb) && (!std::isfinite(culprit->levelDb) || mic.levelDb < culprit->levelDb));
            if (!weaker) continue;
            culprit = &mic;
            polarityCause = polarity;
        }
        if (culprit != nullptr)
        {
            proposal.note = "No trim before the correction of Kit / Phase: " + culprit->name + (polarityCause
                ? " is in doubtful polarity (low-band correlation " + juce::String(culprit->lowCorrelation, 2) + "). Invert it (Kit / Phase page of its track), then measure again."
                : " is late by " + juce::String(std::abs(culprit->residualDelayMs), 1) + " ms. Align it with Kit / Phase, then measure again.");
            return proposal;
        }
        if (std::isfinite(report.sumGainLowDb) && report.sumGainLowDb < -1.5f)
        {
            proposal.note = "No trim before the correction of Kit / Phase: the low end of the sum is weaker than the microphones alone.";
            return proposal;
        }
        if (!(intensity > 0)) { proposal.note = "Intensity is 0: nothing is proposed."; return proposal; }
        for (size_t index = 0; index < mics.size(); ++index)
        {
            auto& line = proposal.lines[index];
            const auto band = std::find_if(balanceBands.begin(), balanceBands.end(), [&](const BalanceBand& b) { return b.role == mics[index].role; });
            if (band == balanceBands.end()) { line.note = "no reference for this microphone"; continue; }
            if (!std::isfinite(line.levelDb)) { line.note = "level not measured"; continue; }
            const auto wanted = line.levelDb < band->lowDb ? band->lowDb - line.levelDb : line.levelDb > band->highDb ? band->highDb - line.levelDb : 0.0f;
            const auto trim = juce::jlimit(-BalanceConstants::maximumTrimDb, BalanceConstants::maximumTrimDb,
                BalanceConstants::gain * wanted * juce::jlimit(0.0f, 1.0f, intensity));
            if (std::abs(trim) < BalanceConstants::minimumTrimDb)
            {
                line.note = wanted == 0 ? "within what two engineers did" : "close enough";
                continue;
            }
            line.trimDb = std::round(trim * 10.0f) / 10.0f;
            line.note = line.levelDb < band->lowDb ? "weaker than what two engineers left" : "stronger than what two engineers left";
        }
        if (!proposal.any()) proposal.note = "Balance within the range of the two reference engineers: no trim proposed.";
        return proposal;
    }

    static juce::String toText(const BalanceProposal& proposal)
    {
        juce::String text = "BALANCE (suggestion, applied only if you ask)\n";
        if (!proposal.valid) return text + proposal.note + "\n";
        for (const auto& line : proposal.lines)
        {
            text << "- " << line.name << " : " << (std::isfinite(line.levelDb) ? juce::String(line.levelDb, 1) : juce::String("n/a")) << " dB against the sum, ";
            text << (line.trimDb != 0 ? "trim " + juce::String(line.trimDb > 0 ? "+" : "") + juce::String(line.trimDb, 1) + " dB" : juce::String("no trim")) << " (" << line.note << ")\n";
        }
        if (proposal.note.isNotEmpty()) text << proposal.note << "\n";
        return text;
    }
};}
