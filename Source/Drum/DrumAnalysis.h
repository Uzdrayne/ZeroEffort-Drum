#pragma once
#include "DrumProposal.h"

namespace drum
{
inline constexpr std::array<float, 3> kickGateReleases { 160, 100, 80 };

// Bump when a constant that shapes the measurements changes: 10 ms window and hop, FFT 4096 / 8192
// (switch above 48 kHz), onset 0-70 ms, tail 70-230 ms, 120 ms refractory period, 25-250 Hz hit
// detector band, the six envelope attack/release times and kickGateReleases. Snapshots recorded
// with another version are not comparable.
inline constexpr int analysisVersion = 1;

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
        onsetFrames = tailFrames = bleedFrames = 0;
        lowFast = lowSlow = previousBass = bassEnergy = 0;
        fastCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * 250 / rate);
        slowCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * 25 / rate);
        lastHitSample = -1000000000;
        lastHit = -1000;
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
            windowPeak = juce::jmax(windowPeak, magnitude);
            if (++windowSamples >= juce::jmax(1, static_cast<int>(rate * 0.01)))
            {
                const auto decibels = juce::Decibels::gainToDecibels(windowPeak, -100.0f);
                ++histogram[static_cast<size_t>(juce::jlimit(0, 100, static_cast<int>(decibels + 100)))];
                for (size_t detector = 0; detector < envelopes.size(); ++detector)
                {
                    const auto level = juce::Decibels::gainToDecibels(envelopePeaks[detector], -100.0f);
                    ++envelopeHistograms[detector][static_cast<size_t>(juce::jlimit(0, 100, static_cast<int>(level + 100)))];
                    envelopePeaks[detector] = 0;
                }
                const auto bassLevel = std::sqrt(bassEnergy / windowSamples);
                if (bassLevel > 0.0003 && bassLevel > previousBass * 1.8
                    && windowPeak > previousPeak * 1.3f && windows - lastHit >= 12)
                {
                    ++hits;
                    lastHit = windows;
                    lastHitSample = samples - windowSamples;
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
                auto* phase = age >= 0 && age < 0.07 ? &onsetSpectrum
                    : age >= 0.07 && age < 0.23 ? &tailSpectrum : &bleedSpectrum;
                if (phase == &onsetSpectrum) ++onsetFrames;
                else if (phase == &tailSpectrum) ++tailFrames;
                else ++bleedFrames;
                for (size_t bin = 1; bin < static_cast<size_t>(fftSize / 2); ++bin)
                {
                    const auto power = static_cast<double>(work[bin]) * work[bin];
                    spectrum[bin] += power;
                    (*phase)[bin] += power;
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
    int onsetFrames = 0, tailFrames = 0, bleedFrames = 0;
    juce::int64 lastHitSample = -1000000000;
    double lowFast = 0, lowSlow = 0, previousBass = 0, bassEnergy = 0;
    double fastCoefficient = 0, slowCoefficient = 0;
};
}