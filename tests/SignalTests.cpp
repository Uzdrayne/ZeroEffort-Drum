#include "Drum/DrumAnalysisSnapshot.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

std::unique_ptr<drum::DrumAnalyser> analyse(const std::vector<float>& audio, double rate)
{
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
        analyser->push(audio.data() + offset, static_cast<int>(juce::jmin(size_t { 4096 }, audio.size() - offset)));
    return analyser;
}

std::vector<float> whiteNoise(double rate, double seconds, double amplitude, int seed)
{
    juce::Random random(seed);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (auto& value : audio) value = static_cast<float>(amplitude * (random.nextDouble() * 2 - 1));
    return audio;
}

// Paul Kellet's economical pink filter: -3 dB/octave within 0.05 dB from 10 Hz to Nyquist.
std::vector<float> pinkNoise(double rate, double seconds, double amplitude, int seed)
{
    auto audio = whiteNoise(rate, seconds, 1.0, seed);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& sample : audio)
    {
        const double white = sample;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        sample = static_cast<float>(amplitude * pink * 0.11);
    }
    return audio;
}

// White noise limited to [lower, upper] Hz by steep Butterworth filters (order 8 each side).
std::vector<float> bandNoise(double rate, double seconds, double lower, double upper, int seed)
{
    auto audio = whiteNoise(rate, seconds, 0.5, seed);
    juce::dsp::ProcessSpec spec { rate, static_cast<juce::uint32>(audio.size()), 1 };
    using Filter = juce::dsp::IIR::Filter<float>;
    std::vector<Filter> stages;
    for (const auto& coefficients : juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(static_cast<float>(lower), rate, 8))
        stages.emplace_back(coefficients);
    for (const auto& coefficients : juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(static_cast<float>(upper), rate, 8))
        stages.emplace_back(coefficients);
    for (auto& stage : stages) stage.prepare(spec);
    for (auto& sample : audio)
        for (auto& stage : stages) sample = stage.processSample(sample);
    return audio;
}

// A05
void testSpectralSlope()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto pink = analyse(pinkNoise(rate, 10, 0.5, 31), rate);
        const auto slope = pink->analysis().spectralSlopeDbPerOctave();
        check(std::isfinite(slope) && std::abs(slope + 3.0f) < 0.5f, "A05 pink noise must slope -3 dB/octave +-0.5, got " + juce::String(slope) + " at " + juce::String(rate));
        const auto white = analyse(whiteNoise(rate, 10, 0.5, 32), rate)->analysis().spectralSlopeDbPerOctave();
        check(std::isfinite(white) && std::abs(white) < 0.5f, "A05 white noise must be flat, got " + juce::String(white) + " at " + juce::String(rate));
        const auto brown = analyse(pinkNoise(rate, 10, 0.5, 33), rate)->analysis().spectralSlopeDbPerOctave();
        check(brown < white - 2.0f, "A05 pink noise must be steeper than white noise");
    }
    check(std::isnan(analyse(std::vector<float>(48000 * 3), 48000)->analysis().spectralSlopeDbPerOctave()), "A05 silence has no slope");
    std::cout << "PASS: A05 spectral slope: pink -3 dB/oct, white 0, at 44.1, 48 and 96 kHz" << std::endl;
}

// A06: octave-wide noise lands in its band
void testBandNoise()
{
    struct Band { const char* name; double lower, upper; };
    const std::array<Band, 4> bands {{ { "basMedium", 200, 500 }, { "medium", 500, 2000 }, { "presence", 2000, 5000 }, { "brillance", 5000, 10000 } }};
    for (const auto rate : { 48000.0, 96000.0 })
        for (const auto& band : bands)
        {
            const auto analysis = analyse(bandNoise(rate, 8, band.lower * 1.06, band.upper / 1.06, 41), rate);
            const auto inside = analysis->analysis().bandRelativeDb(band.lower, band.upper);
            check(inside > -1.0f, juce::String("A06 noise limited to ") + band.name + " must put its energy there (within 1 dB), got " + juce::String(inside) + " dB at " + juce::String(rate));
            for (const auto& other : drum::namedBands)
                if (juce::String(other.id) != band.name)
                {
                    const auto level = analysis->analysis().bandRelativeDb(other.lower, other.upper);
                    check(std::isnan(level) || level < -12.0f, juce::String("A06 the ") + other.id + " band must stay quiet for " + band.name + ", got " + juce::String(level) + " dB");
                }
            // third octave view: the band's centre is the loudest region, far bands are much lower
            const auto centre = static_cast<int>(std::lround(10 * std::log10(std::sqrt(band.lower * band.upper) / 1000.0))) + 17;
            const auto at = analysis->analysis().thirdOctaveDb(centre);
            for (int index = 0; index < drum::thirdOctaveCount; ++index)
                if (std::abs(index - centre) >= 8 && std::isfinite(analysis->analysis().thirdOctaveDb(index)))
                    check(at - analysis->analysis().thirdOctaveDb(index) > 20.0f, juce::String("A06 third octave ") + juce::String(index) + " must be 20 dB below the band centre for " + band.name);
        }
    std::cout << "PASS: A06 octave band noise: energy in the right band (< 1 dB), others below -12 dB" << std::endl;
}

std::vector<float> kickTrain(double rate, int hits, double period, double frequency)
{
    juce::Random random(51);
    std::vector<float> audio(static_cast<size_t>(rate * period * (hits + 1)));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, period);
        const auto number = static_cast<int>(time / period);
        const auto envelope = number < hits ? std::exp(-age * 30) : 0.0;
        audio[index] = static_cast<float>(0.6 * envelope * std::sin(twoPi * frequency * time) + 0.0002 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

// A07
void testKickCount()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto analyser = analyse(kickTrain(rate, 24, 0.5, 60), rate);
        const auto hits = analyser->analysis().hitCount();
        check(hits == 24, "A07 24 decaying kicks must be counted exactly, got " + juce::String(hits) + " at " + juce::String(rate));
        const auto proposal = analyser->propose(drum::defaultSettings(), drum::DrumStyle::natural, 1.0f);
        check(proposal.valid && proposal.hits == 24, "A07 the proposal must report the 24 hits at " + juce::String(rate));
        check(std::abs(analyser->analysis().kickDominantBody() - 60.0f) <= 6.0f, "A07 the body must be 60 Hz +-6, got " + juce::String(analyser->analysis().kickDominantBody()));
        check(std::abs(proposal.settings[drum::bodyFrequency] - 60.0f) <= 6.0f, "A07 the proposed body frequency must be 60 Hz +-6, got " + juce::String(proposal.settings[drum::bodyFrequency]));
    }
    for (const auto frequency : { 45.0, 80.0, 120.0 })
    {
        const auto analyser = analyse(kickTrain(48000, 24, 0.5, frequency), 48000);
        check(std::abs(analyser->analysis().kickDominantBody() - static_cast<float>(frequency)) <= 12.0f, "A07 body tracks the kick frequency " + juce::String(frequency));
    }
    std::cout << "PASS: A07 24 kicks counted and body at 60 Hz +-6, at 44.1, 48 and 96 kHz" << std::endl;
}

// A11: the same analogue signal at three sample rates gives coherent measures. Raw S01 power is not part of that:
// the FFT doubles above 48 kHz, so only the relative measures (S02, S04) are comparable across rates.
void testSampleRates()
{
    // 100 Hz keeps the body away from the 60 Hz band edge, where the bin assignment legitimately depends on the rate.
    // The hit detector has an absolute threshold (bass level 0.0003, about -70 dBFS): a bed of noise above it is counted
    // as hits (a pink bed of -54 dBFS RMS gave 17 and 30 hits for 16 kicks). Reported in the tracking file, not fixed here.
    const auto a = analyse(kickTrain(44100, 16, 0.5, 100), 44100), b = analyse(kickTrain(48000, 16, 0.5, 100), 48000), c = analyse(kickTrain(96000, 16, 0.5, 100), 96000);
    const std::array<const drum::DrumAnalysis*, 3> all { &a->analysis(), &b->analysis(), &c->analysis() };
    for (const auto* item : all)
    {
        check(item->hitCount() == 16, "A11 the same 16 kicks must be counted at every rate, got " + juce::String(item->hitCount()));
        check(std::abs(item->peakDb() - all[1]->peakDb()) < 0.3f && std::abs(item->rmsDb() - all[1]->rmsDb()) < 0.3f, "A11 peak and RMS must agree across rates");
        check(std::abs(item->crestDb() - all[1]->crestDb()) < 0.3f, "A11 crest must agree across rates");
        check(std::abs(item->activeCrestDb() - all[1]->activeCrestDb()) < 0.5f, "A11 active crest must agree across rates");
        for (const auto& band : drum::namedBands)
        {
            if (juce::String(band.id) == "air") continue;   // 44.1 kHz stops at 19.8 kHz
            const auto level = item->bandRelativeDb(band.lower, band.upper), reference = all[1]->bandRelativeDb(band.lower, band.upper);
            // Bands that only hold the noise bed are skipped: the same noise amplitude per sample has half the density at 96 kHz.
            if (reference < -40.0f) continue;
            check(std::abs(level - reference) < 1.0f, juce::String("A11 band ") + band.id + " relative level must agree across rates, got " + juce::String(level) + " against " + juce::String(reference));
        }
        check(std::abs(item->kickDominantBody() - all[1]->kickDominantBody()) <= 12.0f, "A11 body frequency must agree across rates");
    }
    // Reported, not a failure: the raw third octave power depends on the FFT size.
    const auto gap = c->analysis().thirdOctaveDb(17) - b->analysis().thirdOctaveDb(17);
    check(std::abs(gap) > 1.0f, "A11 raw S01 power was found to depend on the FFT size (96 kHz uses 8192 points): if that changed, update the documentation");
    std::cout << "PASS: A11 sample rates 44.1 / 48 / 96 kHz coherent for relative measures (raw S01 differs by " << gap << " dB at 1 kHz, see doc 01)" << std::endl;
}
}

void testSignals()
{
    testSpectralSlope();
    testBandNoise();
    testKickCount();
    testSampleRates();
}
