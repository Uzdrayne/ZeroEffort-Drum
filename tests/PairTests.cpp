#include "Drum/DrumPairAnalysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double rate = 44100;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

// Kicks, snares and a hat with broadband content, so that every 1/3 octave band carries energy.
std::vector<float> take(double seconds, int seed, double period = 0.5)
{
    juce::Random random(seed);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto kickAge = std::fmod(time, period), snareAge = std::fmod(time + period / 2, period), hatAge = std::fmod(time, period / 4);
        const auto noise = random.nextDouble() * 2 - 1;
        audio[index] = static_cast<float>(0.5 * std::exp(-kickAge * 20) * std::sin(twoPi * (55 + 30 * std::exp(-kickAge * 40)) * time)
            + 0.3 * std::exp(-kickAge * 600) * noise
            + 0.35 * std::exp(-snareAge * 18) * (0.5 * std::sin(twoPi * 200 * time) + noise)
            + 0.15 * std::exp(-hatAge * 70) * (random.nextDouble() * 2 - 1)
            + 0.002 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

struct Treatment
{
    double gainDb = -3.0, peakDb = 6.0, peakHz = 3000.0;
    bool invert = false, saturate = false;
    int delay = 0, trim = 0;     // delay: leading zeros added; trim: samples removed from the start
};

juce::dsp::IIR::Coefficients<float>::Ptr peakFilter(const Treatment& t)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(rate, static_cast<float>(t.peakHz), 1.0f, juce::Decibels::decibelsToGain(static_cast<float>(t.peakDb)));
}

std::vector<float> treat(const std::vector<float>& raw, const Treatment& t)
{
    juce::dsp::IIR::Filter<float> filter(peakFilter(t));
    std::vector<float> processed(raw.size());
    const auto gain = juce::Decibels::decibelsToGain(static_cast<float>(t.gainDb));
    for (size_t index = 0; index < raw.size(); ++index)
    {
        auto value = filter.processSample(raw[index]);
        if (t.saturate) value = std::tanh(4.0f * value) / 4.0f;
        processed[index] = (t.invert ? -gain : gain) * value;
    }
    std::vector<float> shifted(static_cast<size_t>(t.delay), 0.0f);
    shifted.insert(shifted.end(), processed.begin() + t.trim, processed.end());
    return shifted;
}

float truthDb(const Treatment& t, double frequency)
{
    return static_cast<float>(t.gainDb + juce::Decibels::gainToDecibels(peakFilter(t)->getMagnitudeForFrequency(frequency, rate)));
}

void testKnownEqualiser()
{
    const auto raw = take(40, 1);
    Treatment t;
    t.delay = 1234;
    const auto result = drum::DrumPairAnalysis::analyse(raw, treat(raw, t), rate);
    check(result.aligned && result.accepted, "A clean equalised pair must be accepted: " + result.reason);
    check(result.lagSamples == 1234 && !result.inverted, "The delay must be found to the sample, got " + juce::String(result.lagSamples));
    check(result.medianCoherence > 0.95f, "A linear treatment must be coherent, got " + juce::String(result.medianCoherence));
    float worst = 0;
    int compared = 0;
    for (int band = 0; band < drum::thirdOctaveCount; ++band)
    {
        const auto centre = drum::thirdOctaveCentre(band);
        if (centre < 100 || centre > 8000 || !std::isfinite(result.shapeDb[static_cast<size_t>(band)])) continue;
        const auto error = (result.shapeDb[static_cast<size_t>(band)] + result.gainDb) - truthDb(t, centre);
        worst = juce::jmax(worst, std::abs(error));
        ++compared;
    }
    check(compared >= 15, "The equivalent curve must cover 100 Hz to 8 kHz, got " + juce::String(compared) + " bands");
    check(worst < 1.0f, "The equivalent curve must match the applied EQ within 1 dB, worst " + juce::String(worst) + " dB");
    // the shape has no level: the mean over 100 Hz - 5 kHz of the reliable bands is removed
    check(result.gainDb < 0 && result.gainDb > -3.0f, "The mean gain must lie between the applied -3 dB and the boost, got " + juce::String(result.gainDb));
    check(std::abs(result.deltaCrestDb) < 3.0f, "A linear treatment barely changes the crest, got " + juce::String(result.deltaCrestDb));
    std::cout << "PASS: pair EQ (delay found, curve within " << worst << " dB, coherence " << result.medianCoherence << ")" << std::endl;
}

void testAlignmentVariants()
{
    const auto raw = take(40, 2);
    Treatment inverted;
    inverted.invert = true;
    inverted.delay = 77;
    const auto a = drum::DrumPairAnalysis::analyse(raw, treat(raw, inverted), rate);
    check(a.aligned && a.inverted && a.lagSamples == 77, "An inverted mix must be found inverted: lag " + juce::String(a.lagSamples));
    check(a.accepted, "An inverted pair is still a valid pair: " + a.reason);

    // The mix is exported from a later start (3 s trimmed): the lag is negative, found over the whole file
    Treatment trimmed;
    trimmed.trim = static_cast<int>(3 * rate);
    const auto b = drum::DrumPairAnalysis::analyse(raw, treat(raw, trimmed), rate);
    check(b.aligned && b.lagSamples == -static_cast<int>(3 * rate), "A mix starting 3 s later must give a negative lag, got " + juce::String(b.lagSamples));
    check(b.accepted && b.usedSeconds > 36.0, "The common part must be used: " + juce::String(b.usedSeconds) + " s, " + b.reason);

    // A long offset (12 s of silence before the mix, as the corpus files suggest)
    Treatment late;
    late.delay = static_cast<int>(12 * rate) + 5;
    const auto c = drum::DrumPairAnalysis::analyse(raw, treat(raw, late), rate);
    check(c.aligned && c.lagSamples == late.delay, "A 12 s offset must be found, got " + juce::String(c.lagSamples));
    std::cout << "PASS: pair alignment (inverted, trimmed start, 12 s offset)" << std::endl;
}

void testRejections()
{
    const auto raw = take(40, 3);
    const auto other = take(40, 4);   // same kind of playing, different noise: not the same signal
    const auto unrelated = drum::DrumPairAnalysis::analyse(raw, other, rate);
    check(!unrelated.accepted, "Two different takes must not make a pair");

    Treatment linear;
    const auto reference = drum::DrumPairAnalysis::analyse(raw, treat(raw, linear), rate);
    Treatment saturated;
    saturated.saturate = true;
    saturated.gainDb = 6.0;
    const auto distorted = drum::DrumPairAnalysis::analyse(raw, treat(raw, saturated), rate);
    check(distorted.aligned && std::isfinite(distorted.nonLinearDb) && std::isfinite(reference.nonLinearDb)
        && distorted.nonLinearDb > reference.nonLinearDb + 6.0f,
        "Saturation must raise the non linear colour: " + juce::String(reference.nonLinearDb) + " -> " + juce::String(distorted.nonLinearDb) + " dB");
    check(distorted.deltaCrestDb < reference.deltaCrestDb - 1.0f, "Saturation must lower the crest: " + juce::String(reference.deltaCrestDb) + " -> " + juce::String(distorted.deltaCrestDb));

    const auto shortTake = take(8, 5);
    const auto brief = drum::DrumPairAnalysis::analyse(shortTake, treat(shortTake, linear), rate);
    check(brief.aligned && !brief.accepted && brief.reason.contains("playing"), "Less than 20 s of playing must be refused: " + brief.reason);

    // Automation: the mix gets 6 dB louder half way
    auto automated = treat(raw, linear);
    for (size_t index = automated.size() / 2; index < automated.size(); ++index) automated[index] *= 2.0f;
    const auto drifting = drum::DrumPairAnalysis::analyse(raw, automated, rate);
    check(drifting.warnings.joinIntoString(" ").contains("drift"), "Level automation must be reported, drift " + juce::String(drifting.gainDriftDb));
    std::cout << "PASS: pair verdicts (unrelated refused, saturation seen, short take refused, automation reported)" << std::endl;
}

// A bus is the sum of its mics: the multiple regression must recover each filter, and a single input model must not explain it.
void testBus()
{
    const auto first = take(40, 11), second = take(40, 12, 0.43);   // independent playing, so that each mic has its own alignment
    Treatment t1;                                   // +6 dB at 3 kHz, -3 dB
    Treatment t2;
    t2.peakDb = -4.0; t2.peakHz = 1000.0; t2.gainDb = -6.0; t2.delay = 40;
    const auto a = treat(first, t1), b = treat(second, t2);
    std::vector<float> mix(juce::jmin(a.size(), b.size()));
    for (size_t index = 0; index < mix.size(); ++index) mix[index] = a[index] + b[index];

    const auto result = drum::DrumPairAnalysis::analyseBus({ first, second }, mix, rate);
    check(result.accepted, "A bus made of its two mics must be explained: " + result.reason);
    check(result.lagSamples.size() == 2 && result.lagSamples[0] == 0 && std::abs(result.lagSamples[1] - 40) <= 1,
        "Each input has its own lag: " + juce::String(result.lagSamples[0]) + " and " + juce::String(result.lagSamples[1]));
    check(result.medianCoherence > 0.95f, "The multiple coherence of a true bus must be high, got " + juce::String(result.medianCoherence));
    float worst = 0;
    int compared = 0;
    for (int band = 0; band < drum::thirdOctaveCount; ++band)
    {
        const auto centre = drum::thirdOctaveCentre(band);
        if (centre < 500 || centre > 8000 || !std::isfinite(result.gainDb[0][static_cast<size_t>(band)])) continue;
        worst = juce::jmax(worst, std::abs(result.gainDb[0][static_cast<size_t>(band)] - truthDb(t1, centre)));
        worst = juce::jmax(worst, std::abs(result.gainDb[1][static_cast<size_t>(band)] - truthDb(t2, centre)));
        ++compared;
    }
    check(compared >= 12 && worst < 1.5f, "Each mic's filter must be recovered within 1.5 dB, worst " + juce::String(worst) + " dB over " + juce::String(compared) + " bands");

    // The single input model sees only part of the bus
    const auto single = drum::DrumPairAnalysis::analyse(first, mix, rate);
    check(single.medianCoherence < result.medianCoherence - 0.15f, "One input alone must explain less than the two: " + juce::String(single.medianCoherence) + " against " + juce::String(result.medianCoherence));

    // A bus with a mic missing is not explained by the others
    const auto incomplete = drum::DrumPairAnalysis::analyseBus({ first }, mix, rate);
    check(!incomplete.accepted && incomplete.reason.contains("multiple coherence"), "A bus with a mic missing must be refused: " + incomplete.reason);
    std::cout << "PASS: bus regression (2 mics recovered within " << worst << " dB, multiple coherence " << result.medianCoherence << " against " << single.medianCoherence << " for one mic)" << std::endl;
}

bool loadMono(const juce::File& file, std::vector<float>& destination, double& fileRate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { error = "cannot read " + file.getFullPathName(); return false; }
    if (reader->numChannels < 1 || reader->numChannels > 2) { error = file.getFileName() + " must be mono or stereo"; return false; }
    fileRate = reader->sampleRate;
    destination.assign(static_cast<size_t>(reader->lengthInSamples), 0.0f);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), 65536);
    for (juce::int64 offset = 0; offset < reader->lengthInSamples; offset += 65536)
    {
        const auto count = static_cast<int>(juce::jmin<juce::int64>(65536, reader->lengthInSamples - offset));
        if (!reader->read(&buffer, 0, count, offset, true, true)) { error = "read failure in " + file.getFileName(); return false; }
        for (int index = 0; index < count; ++index)
        {
            float sum = 0;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) sum += buffer.getSample(channel, index);
            destination[static_cast<size_t>(offset + index)] = sum / static_cast<float>(buffer.getNumChannels());
        }
    }
    return true;
}
}

void testPair()
{
    testKnownEqualiser();
    testAlignmentVariants();
    testRejections();
    testBus();
}

// Developer command: ZeroEffortDrumTests.exe --pair <raw.wav> <mix.wav> [output prefix]
// Stereo files are folded to mono by the mean of their channels. Writes <prefix>.json and <prefix>.md. Analysis only.
int runPairCommand(const juce::StringArray& commandLine)
{
    if (commandLine.isEmpty() || commandLine[0] != "--pair") return -1;
    const auto fail = [](const juce::String& message) { std::cerr << "ERROR: " << message << std::endl; return 1; };
    if (commandLine.size() < 3 || commandLine.size() > 4) return fail("usage: --pair <raw.wav> <mix.wav> [output prefix]");
    std::vector<float> raw, mix;
    double rawRate = 0, mixRate = 0;
    juce::String error;
    const juce::File rawFile(commandLine[1]), mixFile(commandLine[2]);
    if (!loadMono(rawFile, raw, rawRate, error) || !loadMono(mixFile, mix, mixRate, error)) return fail(error);
    if (rawRate != mixRate) return fail("the sample rates differ (" + juce::String(rawRate) + " / " + juce::String(mixRate) + "): convert one file first (document 03, section 4)");
    const auto result = drum::DrumPairAnalysis::analyse(raw, mix, rawRate);
    const auto markdown = drum::DrumPairAnalysis::toMarkdown(result, rawFile.getFileNameWithoutExtension() + " / " + mixFile.getFileNameWithoutExtension());
    std::cout << markdown << std::endl;
    if (commandLine.size() == 4)
    {
        const auto prefix = commandLine[3];
        if (!juce::File(prefix + ".json").replaceWithText(juce::JSON::toString(drum::DrumPairAnalysis::toVar(result), false))
            || !juce::File(prefix + ".md").replaceWithText(markdown))
            return fail("cannot write the outputs");
    }
    return 0;
}

// Developer command: ZeroEffortDrumTests.exe --bus <mix.wav> <output prefix> <name>=<input.wav> ... (1 to 4 inputs)
int runBusCommand(const juce::StringArray& commandLine)
{
    if (commandLine.isEmpty() || commandLine[0] != "--bus") return -1;
    const auto fail = [](const juce::String& message) { std::cerr << "ERROR: " << message << std::endl; return 1; };
    if (commandLine.size() < 4) return fail("usage: --bus <mix.wav> <output prefix> <name>=<input.wav> ... (1 to 4 inputs)");
    std::vector<float> mix;
    double mixRate = 0;
    juce::String error;
    if (!loadMono(juce::File(commandLine[1]), mix, mixRate, error)) return fail(error);
    std::vector<std::vector<float>> inputs;
    juce::StringArray names;
    for (int index = 3; index < commandLine.size(); ++index)
    {
        const auto name = commandLine[index].upToFirstOccurrenceOf("=", false, false);
        std::vector<float> samples;
        double inputRate = 0;
        if (!loadMono(juce::File(commandLine[index].fromFirstOccurrenceOf("=", false, false)), samples, inputRate, error)) return fail(error);
        if (inputRate != mixRate) return fail("the sample rates differ for " + name);
        names.add(name);
        inputs.push_back(std::move(samples));
    }
    const auto title = juce::File(commandLine[1]).getFileNameWithoutExtension();
    const auto result = drum::DrumPairAnalysis::analyseBus(inputs, mix, mixRate);
    const auto markdown = drum::DrumPairAnalysis::toMarkdown(result, names, title);
    std::cout << markdown << std::endl;
    const auto prefix = commandLine[2];
    if (prefix.isNotEmpty() && prefix != "-"
        && (!juce::File(prefix + ".json").replaceWithText(juce::JSON::toString(drum::DrumPairAnalysis::toVar(result, names), false)) || !juce::File(prefix + ".md").replaceWithText(markdown)))
        return fail("cannot write the outputs");
    return 0;
}