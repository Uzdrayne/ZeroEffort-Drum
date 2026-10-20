#pragma once
#include "DrumProposal.h"
#include <limits>

namespace drum
{
inline constexpr std::array<float, 3> kickGateReleases { 160, 100, 80 };

// Bump when a constant that shapes the measurements changes: 10 ms window and hop, FFT 4096 / 8192
// (switch above 48 kHz), onset 0-70 ms, tail 70-230 ms, 120 ms refractory period, 25-250 Hz hit
// detector band, the six envelope attack/release times, kickGateReleases and MeasureConstants.
// Also bump when the snapshot gains analysis fields. Snapshots recorded with another version are not
// comparable. Version 2: active-window energy and hit decay added (J2). Version 3: lead frame policy (J3).
// Version 4: lead frames excluded from the bleed by default (J3 activated).
// Version 5: active window threshold P95 - 55 dB instead of P95 - 40 dB (01-Q1 calibrated).
// Version 6: double hit detection on by default (J4a activated).
inline constexpr int analysisVersion = 6;

// H: starting points to calibrate on a corpus (document 05, 01-Q1 and 01-Q5).
struct MeasureConstants
{
    // 01-Q1 calibrated on 17 October 2026 (40 -> 55, see docs/archives/SUIVI_IMPLEMENTATION.md): plateau of the active crest.
    static constexpr float activeBelowP95Db = 55;     // a window is active above P95 minus this...
    static constexpr float activeAboveFloorDb = 12;   // ...and above the noise floor plus this (H: kept at 12, see 01-Q1 notes)
    static constexpr int decayReferenceWindows = 3;   // windows after a hit used to find its peak level
    static constexpr float decayDropDb = 20;          // the decay time is the time to fall this far
    static constexpr int decayMaxWindows = 100;       // give up after 1 s
    static constexpr int minimumDecayHits = 3;        // hits needed before a median decay is reported
    static constexpr float t60Factor = 3;             // T60 is extrapolated from the 20 dB drop
    static constexpr double bandLimitRatio = 0.45;    // bands above this fraction of the rate are ignored (01-Q4)
    // J3, problem 1. Activated on 17 October 2026 (validated by the user): it changes the bleed spectrum, hence the Kick air correction.
    static constexpr bool excludeLeadFramesByDefault = true;
    // J4, problem 3 (double kicks). Activated on 17 October 2026 (validated by the user): it changes the hit count, hence the
    // onset / tail / bleed split of the spectrum. A hit may re-arm after doubleHitMinimumWindows (10 ms each) instead of 12
    // when the window peak rises by doubleHitRise against the previous window. H: to calibrate on a corpus of fast playing.
    static constexpr bool doubleHitsByDefault = true;
    static constexpr int doubleHitMinimumWindows = 6;
    static constexpr float doubleHitRise = 4.0f;
};

inline constexpr int thirdOctaveCount = 31;           // 20 Hz to 20 kHz, nominal centres 1000 * 10^(n/10), n = -17..13
inline double thirdOctaveCentre(int index) noexcept { return 1000.0 * std::pow(10.0, (index - 17) / 10.0); }

struct NamedBand
{
    const char* id;
    double lower, upper;
};

inline constexpr std::array<NamedBand, 7> namedBands {{
    { "sub", 20, 60 }, { "grave", 60, 200 }, { "basMedium", 200, 500 }, { "medium", 500, 2000 },
    { "presence", 2000, 5000 }, { "brillance", 5000, 10000 }, { "air", 10000, 20000 } }};

// Retains the original Kick temporal detectors alongside shared signal measurements.
// Profiles read this object by const reference; FFT buffers and histograms are never copied.
class DrumAnalysis
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        fftSize = rate > 48000 ? 8192 : 4096;
        samples = clipped = 0;
        energy = 0;
        peak = windowPeak = previousPeak = 0;
        invalid = false;
        spectrum.fill(0);
        onsetSpectrum.fill(0);
        tailSpectrum.fill(0);
        bleedSpectrum.fill(0);
        histogram.fill(0);
        for (auto& values : envelopeHistograms) values.fill(0);
        const std::array<float, 6> attacks { 25, 8, 4, 1, 1, 1 };
        const std::array<float, 6> releases { 140, 100, 80,
            kickGateReleases[0], kickGateReleases[1], kickGateReleases[2] };
        for (size_t index = 0; index < envelopes.size(); ++index)
        {
            envelopes[index].prepare({ sampleRate, 32768, 1 });
            envelopes[index].setAttackTime(attacks[index]);
            envelopes[index].setReleaseTime(releases[index]);
            envelopes[index].reset();
            envelopePeaks[index] = 0;
        }
        frame.fill(0);
        position = windowSamples = hits = windows = 0;
        onsetFrames = tailFrames = bleedFrames = leadFrames = 0;
        lowFast = lowSlow = previousBass = bassEnergy = 0;
        fastCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * 250 / rate);
        slowCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * 25 / rate);
        lastHitSample = -1000000000;
        lastHit = -1000;
        windowEnergy = 0;
        windowEnergyByPeak.fill(0);
        decayHistogram.fill(0);
        decayCensored = trackAge = 0;
        trackActive = false;
        trackReference = -100;
    }

    void push(const float* audio, int length)
    {
        for (int index = 0; index < length; ++index)
        {
            const auto value = audio[index];
            if (!std::isfinite(value)) { invalid = true; continue; }
            const auto magnitude = std::abs(value);
            peak = juce::jmax(peak, magnitude);
            clipped += magnitude >= 0.999f ? 1 : 0;
            energy += static_cast<double>(value) * value;
            ++samples;
            for (size_t detector = 0; detector < envelopes.size(); ++detector)
                envelopePeaks[detector] = juce::jmax(envelopePeaks[detector], envelopes[detector].processSample(0, magnitude));
            lowFast += fastCoefficient * (value - lowFast);
            lowSlow += slowCoefficient * (value - lowSlow);
            const auto bassValue = lowFast - lowSlow;
            bassEnergy += bassValue * bassValue;
            windowEnergy += static_cast<double>(value) * value;
            windowPeak = juce::jmax(windowPeak, magnitude);
            if (++windowSamples >= juce::jmax(1, static_cast<int>(rate * 0.01)))
            {
                const auto decibels = juce::Decibels::gainToDecibels(windowPeak, -100.0f);
                const auto peakBucket = static_cast<size_t>(juce::jlimit(0, 100, static_cast<int>(decibels + 100)));
                ++histogram[peakBucket];
                windowEnergyByPeak[peakBucket] += windowEnergy;
                windowEnergy = 0;
                for (size_t detector = 0; detector < envelopes.size(); ++detector)
                {
                    const auto level = juce::Decibels::gainToDecibels(envelopePeaks[detector], -100.0f);
                    ++envelopeHistograms[detector][static_cast<size_t>(juce::jlimit(0, 100, static_cast<int>(level + 100)))];
                    envelopePeaks[detector] = 0;
                }
                if (trackActive)
                {
                    ++trackAge;
                    if (trackAge <= MeasureConstants::decayReferenceWindows)
                        trackReference = juce::jmax(trackReference, decibels);
                    else if (decibels <= trackReference - MeasureConstants::decayDropDb)
                    {
                        ++decayHistogram[static_cast<size_t>(juce::jmin(trackAge, MeasureConstants::decayMaxWindows))];
                        trackActive = false;
                    }
                    else if (trackAge >= MeasureConstants::decayMaxWindows)
                    {
                        ++decayCensored;
                        trackActive = false;
                    }
                }
                const auto bassLevel = std::sqrt(bassEnergy / windowSamples);
                const auto gap = windows - lastHit;
                const auto rearmed = gap >= 12 || (detectDoubleHits && gap >= MeasureConstants::doubleHitMinimumWindows
                    && windowPeak > previousPeak * MeasureConstants::doubleHitRise);
                if (bassLevel > 0.0003 && bassLevel > previousBass * 1.8
                    && windowPeak > previousPeak * 1.3f && rearmed)
                {
                    ++hits;
                    lastHit = windows;
                    lastHitSample = samples - windowSamples;
                    if (trackActive) ++decayCensored;
                    trackActive = true;
                    trackAge = 0;
                    trackReference = decibels;
                }
                previousBass = bassLevel;
                bassEnergy = 0;
                previousPeak = windowPeak;
                windowSamples = 0;
                windowPeak = 0;
                ++windows;
            }
            frame[static_cast<size_t>(position)] = value;
            position = (position + 1) % fftSize;
            if (samples >= fftSize && samples % juce::jmax(1, static_cast<int>(rate * 0.01)) == 0)
            {
                work.fill(0);
                for (int frameIndex = 0; frameIndex < fftSize; ++frameIndex)
                    work[static_cast<size_t>(frameIndex)] = frame[static_cast<size_t>((position + frameIndex) % fftSize)];
                if (fftSize == 4096)
                {
                    window.multiplyWithWindowingTable(work.data(), fftSize);
                    fft.performFrequencyOnlyForwardTransform(work.data());
                }
                else
                {
                    largeWindow.multiplyWithWindowingTable(work.data(), fftSize);
                    largeFFT.performFrequencyOnlyForwardTransform(work.data());
                }
                const auto age = (samples - fftSize / 2 - lastHitSample) / rate;
                // Problem 1: a frame whose centre precedes the hit by less than half its length already holds the attack.
                const auto lead = excludeLeadFrames && age < 0 && age >= -(fftSize / 2) / rate;
                auto* phase = lead ? nullptr
                    : age >= 0 && age < 0.07 ? &onsetSpectrum
                    : age >= 0.07 && age < 0.23 ? &tailSpectrum : &bleedSpectrum;
                if (phase == nullptr) ++leadFrames;
                else if (phase == &onsetSpectrum) ++onsetFrames;
                else if (phase == &tailSpectrum) ++tailFrames;
                else ++bleedFrames;
                for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
                {
                    const auto power = static_cast<double>(work[bin]) * work[bin];
                    spectrum[bin] += power;
                    if (phase != nullptr) (*phase)[bin] += power;
                }
            }
        }
    }

    float spectralPeak(double lower, double upper) const
    {
        double maximum = -1;
        float frequency = static_cast<float>(lower);
        for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
        {
            const auto hz = static_cast<double>(bin) * rate / fftSize;
            if (hz >= lower && hz <= upper && spectrum[bin] > maximum)
            {
                maximum = spectrum[bin];
                frequency = static_cast<float>(hz);
            }
        }
        return frequency;
    }

    double phaseEnergy(const std::array<double, 4096>& values, double lower, double upper) const
    {
        double result = 0;
        for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
        {
            const auto frequency = static_cast<double>(bin) * rate / fftSize;
            if (frequency >= lower && frequency <= upper) result += values[bin];
        }
        return result;
    }

    // Kick corrections compare onset, tail and bleed spectra, not the overall spectral peak.
    std::pair<float, float> kickTemporalResonance() const
    {
        std::pair<float, float> result { 350.0f, 0.0f };
        if (onsetFrames < 3 || tailFrames < 3) return result;
        const auto body = phaseEnergy(onsetSpectrum, 35, 150) / onsetFrames;
        if (body <= 0) return result;
        double best = 0;
        for (double frequency = 180; frequency <= 1000; frequency *= std::pow(2.0, 1.0 / 6))
        {
            const auto lower = frequency / 1.12, upper = frequency * 1.12;
            const auto tail = phaseEnergy(tailSpectrum, lower, upper) / tailFrames;
            const auto onset = phaseEnergy(onsetSpectrum, lower, upper) / onsetFrames;
            const auto background = phaseEnergy(bleedSpectrum, lower, upper) / juce::jmax(1, bleedFrames);
            const auto neighbours = (phaseEnergy(tailSpectrum, frequency / 1.5, frequency / 1.2)
                + phaseEnergy(tailSpectrum, frequency * 1.2, frequency * 1.5)) / tailFrames;
            if (tail > body * 0.015 && tail > onset * 0.12 && tail > background * 2
                && tail > neighbours * 2)
            {
                const auto strength = juce::jmin(tail / (body * 0.015), tail / juce::jmax(1.0e-20, neighbours * 2));
                if (strength > best)
                {
                    best = strength;
                    result = { static_cast<float>(frequency), -juce::jlimit(0.5f, 3.0f, static_cast<float>(1.5 * std::log2(strength))) };
                }
            }
        }
        return result;
    }

    double bandEnergy(double lower, double upper) const
    {
        double result = 0;
        for (size_t bin = 1; bin < spectrum.size(); ++bin)
        {
            const auto frequency = static_cast<double>(bin) * rate / fftSize;
            if (frequency >= lower && frequency <= upper) result += spectrum[bin];
        }
        return result;
    }

    float kickDominantBody() const
    {
        size_t best = 1;
        double maximum = -1;
        for (size_t bin = 1; bin < spectrum.size(); ++bin)
        {
            const auto frequency = static_cast<double>(bin) * rate / fftSize;
            const auto power = onsetFrames > 0 ? onsetSpectrum[bin] : spectrum[bin];
            if (frequency >= 35 && frequency <= 140 && power > maximum)
            { best = bin; maximum = power; }
        }
        return juce::jlimit(35.0f, 140.0f, static_cast<float>(best * rate / fftSize));
    }

    float percentile(float fraction) const
    {
        return percentileOf(histogram, fraction);
    }

    float peakDb() const { return juce::Decibels::gainToDecibels(peak, -100.0f); }
    float rmsDb() const
    {
        return juce::Decibels::gainToDecibels(samples > 0 ? static_cast<float>(std::sqrt(energy / samples)) : 0.0f, -100.0f);
    }
    float crestDb() const { return peakDb() - rmsDb(); }

    // J2 measures: additive, read by nothing in the profiles yet. Unavailable values are NaN.
    static float unavailable() noexcept { return std::numeric_limits<float>::quiet_NaN(); }

    int hitCount() const noexcept { return hits; }
    int analysedWindows() const noexcept { return windows; }
    double durationSeconds() const noexcept { return rate > 0 ? static_cast<double>(samples) / rate : 0.0; }

    // L05: P10 of the window peak distribution.
    float noiseFloorDb() const { return windows > 0 ? percentile(0.10f) : unavailable(); }

    // Active windows: peak above max(P95 - 55 dB, noise floor + 12 dB) (decision 01-Q1), never above P95 itself so that
    // a signal without dynamics (steady tone, constant noise) keeps its loudest windows instead of none.
    float activeThresholdDb() const
    {
        const auto p95 = percentile(0.95f);
        return juce::jmin(p95, juce::jmax(p95 - MeasureConstants::activeBelowP95Db,
            noiseFloorDb() + MeasureConstants::activeAboveFloorDb));
    }

    int activeWindowCount() const
    {
        if (windows < 1) return 0;
        const auto threshold = activeThresholdDb();
        int count = 0;
        // bucket 0 holds the windows at or below -100 dBFS (digital silence): never active
        for (size_t index = 1; index < histogram.size(); ++index)
            if (static_cast<float>(index) - 100 >= threshold) count += histogram[index];
        return count;
    }

    // L03: RMS over the active windows only.
    float activeRmsDb() const
    {
        const auto count = activeWindowCount();
        if (count < 1) return unavailable();
        const auto threshold = activeThresholdDb();
        double active = 0;
        for (size_t index = 1; index < histogram.size(); ++index)
            if (static_cast<float>(index) - 100 >= threshold) active += windowEnergyByPeak[index];
        const auto length = static_cast<double>(juce::jmax(1, static_cast<int>(rate * 0.01)));
        return juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(active / (count * length))), -100.0f);
    }

    // D02: global peak minus the active RMS.
    float activeCrestDb() const
    {
        const auto level = activeRmsDb();
        return std::isfinite(level) ? peakDb() - level : unavailable();
    }

    // H02
    float hitsPerSecond() const
    {
        const auto duration = durationSeconds();
        return duration > 0 ? static_cast<float>(hits / duration) : unavailable();
    }

    // H06: time from the hit detection to a 20 dB fall of the 10 ms peak envelope (median over the hits that got
    // there before the next hit, 10 ms resolution). T60 is that time extrapolated, not measured.
    int decayObservedHits() const noexcept
    {
        int count = 0;
        for (const auto value : decayHistogram) count += value;
        return count;
    }
    int decayCensoredHits() const noexcept { return decayCensored; }

    float decayT20Ms() const
    {
        const auto total = decayObservedHits();
        if (total < MeasureConstants::minimumDecayHits) return unavailable();
        int count = 0;
        for (size_t index = 0; index < decayHistogram.size(); ++index)
        {
            count += decayHistogram[index];
            if (count * 2 >= total) return static_cast<float>(index) * 10.0f;
        }
        return unavailable();
    }
    float decayT60Ms() const
    {
        const auto t20 = decayT20Ms();
        return std::isfinite(t20) ? t20 * MeasureConstants::t60Factor : unavailable();
    }

    int spectrumFrames() const noexcept { return onsetFrames + tailFrames + bleedFrames + leadFrames; }

    // J3, problem 1: leave the frames that already hold an attack out of the bleed spectrum (they are still in
    // the overall spectrum). Set before prepare(); the default is MeasureConstants::excludeLeadFramesByDefault.
    void setExcludeLeadFrames(bool exclude) noexcept { excludeLeadFrames = exclude; }
    bool excludesLeadFrames() const noexcept { return excludeLeadFrames; }
    // J4: allow a hit to follow the previous one after 60 ms when its attack is much louder than the decayed tail.
    void setDoubleHitDetection(bool enable) noexcept { detectDoubleHits = enable; }
    bool detectsDoubleHits() const noexcept { return detectDoubleHits; }
    int leadFrameCount() const noexcept { return leadFrames; }

    // Average FFT power per frame of one phase in a band (onset, tail or bleed spectra); NaN without frames.
    enum class Phase { onset, tail, bleed };
    double phasePower(Phase phase, double lower, double upper) const
    {
        const auto frames = phase == Phase::onset ? onsetFrames : phase == Phase::tail ? tailFrames : bleedFrames;
        if (frames < 1) return std::numeric_limits<double>::quiet_NaN();
        return phaseEnergy(phase == Phase::onset ? onsetSpectrum : phase == Phase::tail ? tailSpectrum : bleedSpectrum,
            lower, upper) / frames;
    }

    // S01: power of one third-octave band per analysed frame, in dB of the unscaled FFT power.
    float thirdOctaveDb(int index) const
    {
        const auto frames = spectrumFrames();
        if (index < 0 || index >= thirdOctaveCount || frames < 1) return unavailable();
        const auto centre = thirdOctaveCentre(index);
        const auto lower = centre * std::pow(2.0, -1.0 / 6), upper = centre * std::pow(2.0, 1.0 / 6);
        if (upper > rate * MeasureConstants::bandLimitRatio) return unavailable();
        double sum = 0;
        for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
        {
            const auto hz = static_cast<double>(bin) * rate / fftSize;
            if (hz >= lower && hz < upper) sum += spectrum[bin];
        }
        return sum > 0 ? static_cast<float>(10 * std::log10(sum / frames)) : unavailable();
    }

    // S04: slope of the power density per octave (regression of the third octave density in dB against log2 of the
    // centre frequency, 100 Hz to 10 kHz). Pink noise reads -3 dB/octave, white noise 0. NaN when fewer than 8 bands fit.
    float spectralSlopeDbPerOctave() const
    {
        const auto frames = spectrumFrames();
        if (frames < 1) return unavailable();
        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        int count = 0;
        for (int index = 0; index < thirdOctaveCount; ++index)
        {
            const auto centre = thirdOctaveCentre(index);
            const auto lower = centre * std::pow(2.0, -1.0 / 6), upper = centre * std::pow(2.0, 1.0 / 6);
            if (centre < 100 || centre > 10000 || upper > rate * MeasureConstants::bandLimitRatio) continue;
            double sum = 0;
            int bins = 0;
            for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
            {
                const auto hz = static_cast<double>(bin) * rate / fftSize;
                if (hz >= lower && hz < upper) { sum += spectrum[bin]; ++bins; }
            }
            if (bins < 1 || sum <= 0) continue;
            const auto x = std::log2(centre), y = 10 * std::log10(sum / bins / frames);
            sumX += x; sumY += y; sumXX += x * x; sumXY += x * y;
            ++count;
        }
        if (count < 8) return unavailable();
        const auto denominator = count * sumXX - sumX * sumX;
        return denominator > 0 ? static_cast<float>((count * sumXY - sumX * sumY) / denominator) : unavailable();
    }

    // S02: energy of a band relative to the whole 20 Hz - 20 kHz range, in dB.
    float bandRelativeDb(double lower, double upper) const
    {
        const auto limit = juce::jmin(20000.0, rate * MeasureConstants::bandLimitRatio);
        const auto total = bandEnergy(20, limit);
        if (lower >= limit || total <= 0) return unavailable();
        const auto band = bandEnergy(lower, juce::jmin(upper, limit));
        return band > 0 ? static_cast<float>(10 * std::log10(band / total)) : unavailable();
    }

    double sampleRate() const noexcept { return rate; }

    // S02 after a frequency response: the same band level, every bin weighted by `powerGain(frequency)` (power, not amplitude).
    // With a gain of 1 it equals bandRelativeDb. Used to show what a proposal aims at (Auto radar), never by a profile.
    template <typename PowerGain>
    float shapedBandRelativeDb(double lower, double upper, PowerGain&& powerGain) const
    {
        const auto limit = juce::jmin(20000.0, rate * MeasureConstants::bandLimitRatio);
        double total = 0, band = 0;
        for (size_t bin = 1; bin < spectrum.size(); ++bin)
        {
            const auto frequency = static_cast<double>(bin) * rate / fftSize;
            if (frequency < 20 || frequency > limit) continue;
            const auto power = spectrum[bin] * powerGain(frequency);
            total += power;
            if (frequency >= lower && frequency <= juce::jmin(upper, limit)) band += power;
        }
        if (lower >= limit || total <= 0 || !(band > 0)) return unavailable();
        return static_cast<float>(10 * std::log10(band / total));
    }

    // P02: onset energy in the click band minus onset energy in the body band, in dB.
    float attackBodyGapDb(double bodyLower, double bodyUpper, double clickLower, double clickUpper) const
    {
        if (onsetFrames < 3 || clickUpper > rate * MeasureConstants::bandLimitRatio) return unavailable();
        const auto body = phaseEnergy(onsetSpectrum, bodyLower, bodyUpper);
        const auto click = phaseEnergy(onsetSpectrum, clickLower, clickUpper);
        return body > 0 && click > 0 ? static_cast<float>(10 * std::log10(click / body)) : unavailable();
    }

    // P03: bleed treble (6-12 kHz) relative to 2 % of the onset bass (35-150 Hz), per frame. The Kick air cut starts
    // above 1 and grows with its log2.
    float bleedTrebleRatio() const
    {
        if (bleedFrames < 1 || onsetFrames < 1) return unavailable();
        const auto treble = phasePower(Phase::bleed, 6000, 12000);
        const auto bass = phasePower(Phase::onset, 35, 150);
        return bass > 0 && treble > 0 ? static_cast<float>(treble / (bass * 0.02)) : unavailable();
    }

    // Reference bands of document 01, section 5.5; other roles have none.
    float attackBodyGapDb(DrumInstrument role) const
    {
        switch (role)
        {
            case DrumInstrument::kick: return attackBodyGapDb(35, 150, 2000, 5000);
            case DrumInstrument::snare: return attackBodyGapDb(120, 250, 3000, 6000);
            case DrumInstrument::tom1: case DrumInstrument::tom2: case DrumInstrument::tom3: case DrumInstrument::floorTom:
                return attackBodyGapDb(60, 250, 2000, 5000);
            default: return unavailable();
        }
    }

    // Spread between the loud (P90) and quiet (P20) levels of one envelope detector; the expander gate looks at it.
    float envelopeContrastDb(size_t detector) const
    {
        return percentileOf(envelopeHistograms[detector], 0.9f) - percentileOf(envelopeHistograms[detector], 0.2f);
    }

    float percentileOf(const std::array<int, 101>& values, float fraction) const
    {
        const auto target = juce::jmax(1, static_cast<int>(windows * fraction));
        int count = 0;
        for (size_t index = 0; index < histogram.size(); ++index)
        {
            count += values[index];
            if (count >= target) return static_cast<float>(index) - 100;
        }
        return 0;
    }

private:
    friend struct KickProfile;
    friend class ProfileAssembly;
    friend struct MasterBusProfile;
    friend class LegacyProposalReference;
    friend struct DrumAnalysisSnapshot;

    int fftSize = 4096;
    juce::dsp::FFT fft { 12 };
    juce::dsp::FFT largeFFT { 13 };
    juce::dsp::WindowingFunction<float> window { 4096, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> largeWindow { 8192, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 8192> frame {};
    std::array<float, 16384> work {};
    std::array<double, 4096> spectrum {};
    std::array<double, 4096> onsetSpectrum {}, tailSpectrum {}, bleedSpectrum {};
    std::array<int, 101> histogram {};
    std::array<std::array<int, 101>, 6> envelopeHistograms {};
    std::array<juce::dsp::BallisticsFilter<float>, 6> envelopes;
    std::array<float, 6> envelopePeaks {};
    double rate = 48000, energy = 0;
    juce::int64 samples = 0, clipped = 0;
    int position = 0, windowSamples = 0, windows = 0, hits = 0, lastHit = -1000;
    float peak = 0, windowPeak = 0, previousPeak = 0;
    bool invalid = false;
    int onsetFrames = 0, tailFrames = 0, bleedFrames = 0, leadFrames = 0;
    bool excludeLeadFrames = MeasureConstants::excludeLeadFramesByDefault;
    bool detectDoubleHits = MeasureConstants::doubleHitsByDefault;
    juce::int64 lastHitSample = -1000000000;
    double lowFast = 0, lowSlow = 0, previousBass = 0, bassEnergy = 0;
    double fastCoefficient = 0, slowCoefficient = 0;
    double windowEnergy = 0;
    std::array<double, 101> windowEnergyByPeak {};   // energy of the windows, bucketed like histogram
    std::array<int, 101> decayHistogram {};           // hits by number of 10 ms windows to fall decayDropDb
    int decayCensored = 0, trackAge = 0;
    float trackReference = -100;
    bool trackActive = false;
};
}