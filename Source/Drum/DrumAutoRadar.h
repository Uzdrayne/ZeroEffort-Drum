#pragma once
#include "DrumPresetPreview.h"
#include "DrumFilterResponse.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace drum
{
// "Auto radar": twelve axes that describe a take, drawn as a polar chart while Auto runs (analysis, calculation,
// verification). The three curves use the same axes and the same arithmetic: what was measured on the source, what the
// proposal aims at, and what was measured on the processed signal. Pure data, no drawing, never used by a profile.
inline constexpr size_t radarAxisCount = 12;

struct RadarAxis
{
    const char* id;
    const char* label;
    const char* unit;
    float low, high;          // H: raw values mapped to 0..1 (outside: clamped). Chosen on the corpus of 17/10/2026.
    const char* meaning;
};

inline constexpr std::array<RadarAxis, radarAxisCount> radarAxes {{
    { "sub", "Sub", "dB", -45.0f, -3.0f, "Energy 20-60 Hz, relative to the total" },
    { "grave", "Low", "dB", -45.0f, -3.0f, "Energy 60-200 Hz, relative to the total" },
    { "basMedium", "Low-mid", "dB", -45.0f, -3.0f, "Energy 200-500 Hz, relative to the total" },
    { "medium", "Mid", "dB", -45.0f, -3.0f, "Energy 500 Hz-2 kHz, relative to the total" },
    { "presence", "Presence", "dB", -45.0f, -3.0f, "Energy 2-5 kHz, relative to the total" },
    { "brillance", "Brilliance", "dB", -45.0f, -3.0f, "Energy 5-10 kHz, relative to the total" },
    { "air", "Air", "dB", -45.0f, -3.0f, "Energy 10-20 kHz, relative to the total" },
    { "crest", "Crest", "dB", 4.0f, 30.0f, "Peak minus RMS level of the active passages: gap between loud hits and body" },
    { "range", "Range", "dB", 10.0f, 80.0f, "Peak minus noise floor: room available between the hits and silence" },
    { "attack", "Attack", "dB", -5.0f, 30.0f, "Attack energy (0-70 ms) minus tail energy (70-230 ms)" },
    { "density", "Density", "/s", 0.0f, 12.0f, "Hits detected per second" },
    { "decay", "Decay", "ms", 0.0f, 1200.0f, "Estimated decay time (T60) of the hits" }
}};

struct RadarCurve
{
    std::array<float, radarAxisCount> raw;   // NaN: not available
    RadarCurve() { raw.fill(std::numeric_limits<float>::quiet_NaN()); }

    // 0..1 on the axis scale; NaN when the value is not available
    float unit(size_t axis) const noexcept
    {
        const auto value = raw[axis];
        if (!std::isfinite(value)) return std::numeric_limits<float>::quiet_NaN();
        const auto& spec = radarAxes[axis];
        return juce::jlimit(0.0f, 1.0f, (value - spec.low) / (spec.high - spec.low));
    }
    bool any() const noexcept
    {
        return std::any_of(raw.begin(), raw.end(), [](float value) { return std::isfinite(value); });
    }
};

// What the editor reads while Auto runs.
struct AutoRadarView
{
    enum Phase { analysis, calculation, verification, finished };
    Phase phase = analysis;
    float progress = 0;                      // 0..1 inside the current phase
    bool active = false, busy = false, ready = false, refused = false, hasTarget = false, hasResult = false;
    RadarCurve source, target, result;
    int role = 0;                            // DrumInstrument index, to name the controls
    juce::String title;                      // role and profile, for the panel header
    juce::String message;                    // why the analysis was refused (refused == true)
};

struct Change
{
    size_t control = 0;
    float before = 0, after = 0;
    float significance = 0;                  // |after - before| as a share of the control range; a switch counts 1
};

class DrumAutoRadar
{
public:
    static float bandOf(const DrumAnalysis& analysis, size_t band)
    {
        return analysis.bandRelativeDb(namedBands[band].lower, namedBands[band].upper);
    }

    static float attackDb(const DrumAnalysis& analysis)
    {
        const auto onset = analysis.phasePower(DrumAnalysis::Phase::onset, 20, 16000);
        const auto tail = analysis.phasePower(DrumAnalysis::Phase::tail, 20, 16000);
        if (!std::isfinite(onset) || !std::isfinite(tail) || onset <= 0 || tail <= 0) return DrumAnalysis::unavailable();
        return static_cast<float>(10 * std::log10(onset / tail));
    }

    static float crestOf(const DrumAnalysis& analysis)
    {
        const auto active = analysis.activeCrestDb();
        return std::isfinite(active) ? active : analysis.crestDb();
    }

    static float rangeOf(const DrumAnalysis& analysis)
    {
        const auto floor = analysis.noiseFloorDb();
        return std::isfinite(floor) ? analysis.peakDb() - floor : DrumAnalysis::unavailable();
    }

    // The same twelve measures for any analysis: the source, or the processed signal.
    static RadarCurve measure(const DrumAnalysis& analysis)
    {
        RadarCurve curve;
        if (analysis.analysedWindows() < 1) return curve;
        for (size_t band = 0; band < namedBands.size(); ++band) curve.raw[band] = bandOf(analysis, band);
        curve.raw[7] = crestOf(analysis);
        curve.raw[8] = rangeOf(analysis);
        curve.raw[9] = attackDb(analysis);
        curve.raw[10] = analysis.hitsPerSecond();
        curve.raw[11] = analysis.decayT60Ms();
        return curve;
    }

    // What the proposal aims at, built from the source measures and the proposal itself. Bands: the source spectrum through
    // the proposed filters and EQ (a calculation). Crest, range and attack: the effect the proposal declares (compression
    // target, expander range, Attack / Sustain shaping). Density and decay are not changed by the proposal. Compression,
    // saturation and the like are not modelled: the third curve, measured, shows what really happened.
    static RadarCurve intention(const DrumAnalysis& analysis, const DrumProposal& proposal)
    {
        auto curve = measure(analysis);
        if (!curve.any() || !proposal.valid) return curve;
        const auto& settings = proposal.settings;
        const auto rate = analysis.sampleRate();
        const DrumFilterResponse filters(settings, rate);
        std::array<juce::dsp::IIR::Coefficients<double>::Ptr, 4> peaks;
        const std::array<std::pair<int, int>, 4> bands {{ { bodyFrequency, bodyGain }, { boxFrequency, boxGain },
            { attackFrequency, attackGain }, { airFrequency, airGain } }};
        const bool equalised = settings[eqOn] > 0.5f;
        for (size_t index = 0; index < peaks.size(); ++index)
            peaks[index] = juce::dsp::IIR::Coefficients<double>::makePeakFilter(rate,
                juce::jlimit(20.0, rate * 0.45, static_cast<double>(settings[static_cast<size_t>(bands[index].first)])),
                static_cast<double>(bandQ(settings, index)),
                juce::Decibels::decibelsToGain(static_cast<double>(settings[static_cast<size_t>(bands[index].second)])));
        const auto powerGain = [&](double frequency) {
            auto magnitude = filters.magnitude(frequency);
            if (equalised)
                for (const auto& peak : peaks)
                    magnitude *= peak->getMagnitudeForFrequency(juce::jlimit(0.0, rate * 0.49, frequency), rate);
            return magnitude * magnitude;
        };
        for (size_t band = 0; band < namedBands.size(); ++band)
            curve.raw[band] = analysis.shapedBandRelativeDb(namedBands[band].lower, namedBands[band].upper, powerGain);
        const bool compressing = settings[compressorOn] > 0.5f || settings[multibandOn] > 0.5f || settings[glueOn] > 0.5f;
        if (compressing && std::isfinite(curve.raw[7])) curve.raw[7] -= proposal.targetReduction;
        if (settings[gateOn] > 0.5f && std::isfinite(curve.raw[8])) curve.raw[8] += settings[gateRange];
        if (settings[shapeOn] > 0.5f && std::isfinite(curve.raw[9])) curve.raw[9] += settings[shapeAttack] - settings[shapeSustain];
        return curve;
    }

    // The settings a user can hear and reason about; Q factors, detector times and the like are left to the full report.
    static bool isPrimary(size_t control) noexcept
    {
        switch (control)
        {
            case input: case highPass: case lowPass: case gateThreshold: case gateRange:
            case bodyFrequency: case bodyGain: case boxFrequency: case boxGain:
            case attackFrequency: case attackGain: case airFrequency: case airGain:
            case threshold: case ratio: case makeup: case output:
            case reinforceAmount: case transientAmount: case saturationDrive: case cleanupDepth:
            case lowRange: case midRange: case highRange: case glueThreshold: case glueRatio: case glueRange:
            case ambienceAmount: case crushAmount: case colourMix: case clipMix: case shapeAttack: case shapeSustain:
            case dynamicLowRange: case dynamicHighRange:
                return true;
            default: return false;
        }
    }

    // The controls an Auto proposal moves the most, for the "why" list. Module switches always count; among the values only
    // the primary ones do. Bookkeeping controls are left out.
    static std::vector<Change> significantChanges(const Settings& before, const Settings& after, size_t maximum = 24)
    {
        std::vector<Change> changes;
        for (size_t control = 0; control < controlCount; ++control)
        {
            if (control == bypass || control == instrument || control >= autoMeasuredOn) continue;
            const auto& spec = controlSpecs[control];
            if (!spec.toggle && !isPrimary(control)) continue;
            const auto span = spec.maximum - spec.minimum;
            if (span <= 0) continue;
            const auto difference = std::abs(after[control] - before[control]);
            const auto significance = spec.toggle ? (difference > 0.5f ? 1.0f : 0.0f) : difference / span;
            if (significance >= 0.02f) changes.push_back({ control, before[control], after[control], significance });
        }
        std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.significance > b.significance; });
        if (changes.size() > maximum) changes.resize(maximum);
        return changes;
    }
};
}
