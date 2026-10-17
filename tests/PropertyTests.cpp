#include "Drum/DrumAnalysisSnapshot.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

constexpr double twoPi = juce::MathConstants<double>::twoPi;

std::unique_ptr<drum::DrumAnalyser> analyse(const std::vector<float>& audio, double rate, int block = 4096)
{
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += static_cast<size_t>(block))
        analyser->push(audio.data() + offset, static_cast<int>(juce::jmin(static_cast<size_t>(block), audio.size() - offset)));
    return analyser;
}

drum::Settings roleSettings(int role)
{
    auto settings = drum::defaultSettings();
    settings[drum::instrument] = static_cast<float>(role);
    return settings;
}

// A01-A04: signals whose measures are known by calculation.
void testKnownSignals()
{
    constexpr double rate = 48000;
    std::vector<float> sine(static_cast<size_t>(rate * 3));
    for (size_t index = 0; index < sine.size(); ++index)
        sine[index] = static_cast<float>(0.5 * std::sin(twoPi * 1000.0 * static_cast<double>(index) / rate));

    // A01
    const auto measured = analyse(sine, rate);
    const auto& analysis = measured->analysis();
    check(std::abs(analysis.peakDb() + 6.02f) < 0.05f, "A01 peak must be -6.02 dBFS, got " + juce::String(analysis.peakDb()));
    check(std::abs(analysis.rmsDb() + 9.03f) < 0.05f, "A01 RMS must be -9.03 dBFS, got " + juce::String(analysis.rmsDb()));
    check(std::abs(analysis.crestDb() - 3.01f) < 0.05f, "A01 crest must be 3.01 dB, got " + juce::String(analysis.crestDb()));

    // A02
    const auto silence = analyse(std::vector<float>(static_cast<size_t>(rate * 3)), rate);
    for (int role = 0; role < 10; ++role)
    {
        const auto result = silence->propose(roleSettings(role));
        check(!result.valid && result.summary.isNotEmpty(), "A02 silence must be refused with a message, role " + juce::String(role));
    }

    // A03: a full-scale sine driven far past the ceiling and clipped
    std::vector<float> clipped(sine.size());
    for (size_t index = 0; index < clipped.size(); ++index)
        clipped[index] = juce::jlimit(-1.0f, 1.0f, static_cast<float>(4.0 * std::sin(twoPi * 220.0 * static_cast<double>(index) / rate)));
    const auto saturated = analyse(clipped, rate);
    const auto snapshot = drum::DrumAnalysisSnapshot::toSnapshot(saturated->analysis(), {}, saturated->propose(roleSettings(1)));
    check(static_cast<double>(snapshot["measures"]["lvl_clipped_ratio"]) > 0.01, "A03 clipped ratio must exceed 1 %");
    for (const auto role : { 1, 2, 6, 9 })
    {
        const auto result = saturated->propose(roleSettings(role));
        check(!result.valid && result.summary.containsIgnoreCase("clipped"), "A03 clipped signal must be refused as saturated, role " + juce::String(role));
    }

    // A04
    auto invalid = sine;
    invalid[invalid.size() / 2] = std::numeric_limits<float>::quiet_NaN();
    const auto broken = analyse(invalid, rate);
    for (int role = 0; role < 10; ++role)
        check(!broken->propose(roleSettings(role)).valid, "A04 a NaN sample must refuse the proposal, role " + juce::String(role));
    std::cout << "PASS: A01-A04 known signals, silence, clipping and NaN" << std::endl;
}

std::vector<float> decayingHits(double rate, double seconds, double frequency, double decay, double period, double amplitude,
    double noiseLevel, juce::Random& random)
{
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, period);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        audio[index] = static_cast<float>(amplitude * std::exp(-age * decay) * std::sin(twoPi * frequency * time)
            + noiseLevel * noise);
    }
    return audio;
}

// A10: the same signal through blocks of 64, 512 and 4096 samples, and an odd size, must give identical analyses.
void testBlockSizeIndependence()
{
    juce::Random random(77);
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto audio = decayingHits(rate, 3.0, 62, 28, 0.45, 0.6, 0.01, random);
        const auto reference = analyse(audio, rate, 4096);
        const auto referenceText = juce::JSON::toString(drum::DrumAnalysisSnapshot::analysisToVar(reference->analysis()), false);
        for (const auto block : { 64, 512, 257 })
        {
            const auto other = analyse(audio, rate, block);
            check(juce::JSON::toString(drum::DrumAnalysisSnapshot::analysisToVar(other->analysis()), false) == referenceText,
                "A10 analysis must not depend on the block size (rate " + juce::String(rate) + ", block " + juce::String(block) + ")");
            for (int role = 0; role < 10; ++role)
            {
                const auto expected = reference->propose(roleSettings(role), drum::DrumStyle::rock);
                const auto actual = other->propose(roleSettings(role), drum::DrumStyle::rock);
                check(expected.valid == actual.valid && expected.settings == actual.settings,
                    "A10 proposals must not depend on the block size, role " + juce::String(role));
            }
        }
    }
    std::cout << "PASS: A10 block size independence (64, 257, 512, 4096 at 44.1, 48 and 96 kHz)" << std::endl;
}

struct Counters
{
    int analyses = 0, proposals = 0, valid = 0, refused = 0;
};

void checkProposal(const drum::Settings& original, const drum::DrumProposal& proposal, const drum::DrumProposal& again,
    const juce::String& context, Counters& counters)
{
    ++counters.proposals;
    // E08 determinism
    check(proposal.valid == again.valid && proposal.settings == again.settings, "E08 must be deterministic / " + context);
    for (size_t control = 0; control < proposal.settings.size(); ++control)
    {
        const auto value = proposal.settings[control];
        // E01, E02
        check(std::isfinite(value), juce::String("E01 non finite ") + drum::controlSpecs[control].id + " / " + context);
        check(value >= drum::controlSpecs[control].minimum && value <= drum::controlSpecs[control].maximum,
            juce::String("E02 out of bounds ") + drum::controlSpecs[control].id + " / " + context);
    }
    if (!proposal.valid)
    {
        ++counters.refused;
        // E09, E10
        check(proposal.summary.isNotEmpty(), "E09 a refusal needs a message / " + context);
        check(proposal.settings == original, "E10 a refusal must leave the original settings / " + context);
        return;
    }
    ++counters.valid;
    check(std::isfinite(proposal.targetReduction) && std::isfinite(proposal.maximumReduction), "E01 reductions / " + context);
    // E06
    check(proposal.targetReduction <= proposal.maximumReduction + 1.0e-5f, "E06 target above ceiling / " + context);
    // E07
    check(proposal.settings[drum::output] <= -3.0f + 1.0e-5f, "E07 output above -3 dB / " + context);
    check(proposal.settings[drum::bypass] == original[drum::bypass], "E07 bypass must be preserved / " + context);
}

// E01, E02, E06, E07 (plus E08-E10) over many generated signals, roles, styles and intensities.
void testProposalProperties()
{
    juce::Random random(20261016);
    Counters counters;
    for (int signal = 0; signal < 48; ++signal)
    {
        const auto rate = signal % 3 == 0 ? 44100.0 : signal % 3 == 1 ? 48000.0 : 96000.0;
        std::vector<float> audio;
        const auto kind = signal % 6;
        const auto amplitude = 0.05 + 0.9 * random.nextDouble();
        if (kind == 0 || kind == 1)
            audio = decayingHits(rate, 2.5 + 2 * random.nextDouble(), 40 + 80 * random.nextDouble(),
                10 + 40 * random.nextDouble(), 0.25 + 0.5 * random.nextDouble(), amplitude, 0.02 * random.nextDouble(), random);
        else if (kind == 2)
            audio = decayingHits(rate, 3, 150 + 100 * random.nextDouble(), 20, 0.3, amplitude, 0.2 * amplitude, random);
        else if (kind == 3)
        {
            audio.assign(static_cast<size_t>(rate * 3), 0.0f);
            for (auto& value : audio) value = static_cast<float>(amplitude * (random.nextDouble() * 2 - 1));
        }
        else if (kind == 4)
        {
            audio = decayingHits(rate, 3, 60, 20, 0.5, 3.0, 0.0, random);
            for (auto& value : audio) value = juce::jlimit(-1.0f, 1.0f, value);
        }
        else
            audio.assign(static_cast<size_t>(rate * 3), static_cast<float>(0.0005 * amplitude));
        const auto analyser = analyse(audio, rate);
        ++counters.analyses;
        for (int role = 0; role < 10; ++role)
            for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
                for (const auto intensity : { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f })
                {
                    auto original = roleSettings(role);
                    original[drum::bypass] = random.nextBool() ? 1.0f : 0.0f;
                    original[drum::output] = static_cast<float>(random.nextInt(31) - 24);
                    original[drum::input] = static_cast<float>(random.nextInt(25) - 12);
                    const auto proposal = analyser->propose(original, style, intensity);
                    const auto again = analyser->propose(original, style, intensity);
                    checkProposal(original, proposal, again, "signal " + juce::String(signal) + " kind " + juce::String(kind)
                        + " rate " + juce::String(rate) + " role " + juce::String(role) + " style "
                        + juce::String(static_cast<int>(style)) + " intensity " + juce::String(intensity), counters);
                }
    }
    check(counters.valid > counters.proposals / 4 && counters.refused > counters.proposals / 10,
        "The generated signals must cover both valid and refused proposals");
    std::cout << "PASS: E01 E02 E06 E07 (+E08 E09 E10) on " << counters.proposals << " proposals from "
              << counters.analyses << " signals (" << counters.valid << " valid, " << counters.refused << " refused)" << std::endl;
}
}

void testAnalysisProperties()
{
    testKnownSignals();
    testBlockSizeIndependence();
    testProposalProperties();
}
