#include "Drum/DrumAutoPreset.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "Drum/DrumPresetPreview.h"
#include "Drum/DrumPairAnalysis.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
bool loadBuffer(const juce::File& file, juce::AudioBuffer<float>& destination, double& rate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { error = "cannot read " + file.getFullPathName(); return false; }
    if (reader->numChannels < 1 || reader->numChannels > 2) { error = file.getFileName() + " must be mono or stereo"; return false; }
    rate = reader->sampleRate;
    destination.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    return reader->read(&destination, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
}

std::vector<float> meanOf(const juce::AudioBuffer<float>& buffer)
{
    std::vector<float> mono(static_cast<size_t>(buffer.getNumSamples()), 0.0f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float sum = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) sum += buffer.getSample(channel, sample);
        mono[static_cast<size_t>(sample)] = sum / static_cast<float>(buffer.getNumChannels());
    }
    return mono;
}

// The loudest channel of every sample, as the plug-in analysis folds its input.
std::vector<float> loudestOf(const juce::AudioBuffer<float>& buffer)
{
    std::vector<float> mono(static_cast<size_t>(buffer.getNumSamples()), 0.0f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float value = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs(buffer.getSample(channel, sample)) > std::abs(value)) value = buffer.getSample(channel, sample);
        mono[static_cast<size_t>(sample)] = value;
    }
    return mono;
}

juce::String number(float value, int decimals = 1)
{
    return std::isfinite(value) ? juce::String(value, decimals) : juce::String("n/a");
}
}

// Developer command: ZeroEffortDrumTests.exe --vs-plugin <raw.wav> <role 0-9> <natural|rock|metal> <intensity 0-1> <mix.wav> [output prefix] [--off id,id,...] [--set id=value,...] [--no-reference]
// Runs the raw take through the Auto proposal of the plug-in (nothing is played) and compares the equivalent EQ curve of
// that processing with the one the mixer obtained (document 03). Analysis only: writes <prefix>.json and <prefix>.md.
int runVersusCommand(const juce::StringArray& arguments)
{
    if (arguments.isEmpty() || arguments[0] != "--vs-plugin") return -1;
    // Optional "--off id,id,..." (set to 0) and "--set id=value,..." : controls of the proposal changed before the render, to see
    // which module is responsible for a part of the curve, or what a different value would do.
    auto commandLine = arguments;
    juce::StringArray overrides;
    const bool noReference = commandLine.contains("--no-reference");   // ProposalPolicy::cymbalReference = false
    commandLine.removeString("--no-reference");
    for (const auto* option : { "--off", "--set" })
        if (const auto index = commandLine.indexOf(option); index >= 0 && index + 1 < commandLine.size())
        {
            juce::StringArray tokens;
            tokens.addTokens(commandLine[index + 1], ",", "");
            for (const auto& token : tokens) overrides.add(juce::String(option) == "--off" ? token + "=0" : token);
            commandLine.removeRange(index, 2);
        }
    const auto fail = [](const juce::String& message) { std::cerr << "ERROR: " << message << std::endl; return 1; };
    if (commandLine.size() < 6 || commandLine.size() > 7)
        return fail("usage: --vs-plugin <raw.wav> <role 0-9> <natural|rock|metal> <intensity 0-1> <mix.wav> [output prefix]");
    const auto role = commandLine[2].getIntValue();
    if (role < 0 || role >= drum::instrumentCount || !commandLine[2].containsOnly("0123456789")) return fail("role must be an integer from 0 to 12");
    const auto styleName = commandLine[3].toLowerCase();
    if (styleName != "natural" && styleName != "rock" && styleName != "metal") return fail("style must be natural, rock or metal");
    const auto style = styleName == "rock" ? drum::DrumStyle::rock : styleName == "metal" ? drum::DrumStyle::metal : drum::DrumStyle::natural;
    const auto intensity = commandLine[4].getFloatValue();
    if (!(intensity >= 0.0f && intensity <= 1.0f)) return fail("intensity must be between 0 and 1");

    juce::AudioBuffer<float> raw, mix;
    double rawRate = 0, mixRate = 0;
    juce::String error;
    const juce::File rawFile(commandLine[1]), mixFile(commandLine[5]);
    if (!loadBuffer(rawFile, raw, rawRate, error) || !loadBuffer(mixFile, mix, mixRate, error)) return fail(error);
    if (rawRate != mixRate) return fail("the sample rates differ: convert one file first");
    if (rawRate < 8000 || rawRate > 192000 || raw.getNumSamples() < rawRate * 2) return fail("the raw take must hold at least 2 s at 8-192 kHz");

    drum::DrumAnalyser analyser;
    analyser.prepare(rawRate);
    const auto folded = loudestOf(raw);
    for (size_t offset = 0; offset < folded.size(); offset += 32768)
        analyser.push(folded.data() + offset, static_cast<int>(juce::jmin<size_t>(32768, folded.size() - offset)));
    if (noReference) { auto policy = analyser.getPolicy(); policy.cymbalReference = false; analyser.setPolicy(policy); }
    auto original = drum::defaultSettings();
    original[drum::instrument] = static_cast<float>(role);
    auto proposal = analyser.propose(original, style, intensity);
    if (!proposal.valid) return fail("the plug-in refuses this take: " + proposal.summary.upToFirstOccurrenceOf("\n", false, false));

    drum::DrumPresetPreview preview;
    for (int pass = 0; pass < 2; ++pass)
    {
        preview.prepare(rawRate, raw.getNumChannels(), proposal);
        for (int offset = 0; offset < raw.getNumSamples(); offset += 32768)
        {
            const auto count = juce::jmin(32768, raw.getNumSamples() - offset);
            juce::AudioBuffer<float> slice(raw.getNumChannels(), count);
            for (int channel = 0; channel < raw.getNumChannels(); ++channel) slice.copyFrom(channel, 0, raw, channel, offset, count);
            preview.push(slice, count);
        }
        if (pass == 0) preview.refineThreshold(proposal); else preview.finish(proposal);
    }

    for (const auto& assignment : overrides)
    {
        const auto id = assignment.upToFirstOccurrenceOf("=", false, false).trim();
        bool found = false;
        for (size_t control = 0; control < drum::controlSpecs.size(); ++control)
            if (id == drum::controlSpecs[control].id) { proposal.settings[control] = assignment.fromFirstOccurrenceOf("=", false, false).getFloatValue(); found = true; }
        if (!found) return fail("--off / --set: unknown control id " + id);
    }
    juce::AudioBuffer<float> rendered(raw.getNumChannels(), raw.getNumSamples());
    drum::DrumEngine engine;
    engine.prepare(rawRate, 32768, raw.getNumChannels());
    for (int offset = 0; offset < raw.getNumSamples(); offset += 32768)
    {
        const auto count = juce::jmin(32768, raw.getNumSamples() - offset);
        juce::AudioBuffer<float> block(raw.getNumChannels(), count);
        for (int channel = 0; channel < raw.getNumChannels(); ++channel) block.copyFrom(channel, 0, raw, channel, offset, count);
        engine.process(block, proposal.settings);
        for (int channel = 0; channel < raw.getNumChannels(); ++channel) rendered.copyFrom(channel, offset, block, channel, 0, count);
    }

    const auto rawMono = meanOf(raw);
    const auto plugin = drum::DrumPairAnalysis::analyse(rawMono, meanOf(rendered), rawRate);
    const auto mixer = drum::DrumPairAnalysis::analyse(rawMono, meanOf(mix), rawRate);

    // Agreement on the bands where both curves can be trusted, from 100 Hz to 8 kHz.
    double squared = 0, sumP = 0, sumM = 0, sumPP = 0, sumMM = 0, sumPM = 0;
    int compared = 0, agree = 0, significant = 0;
    for (int band = 0; band < drum::thirdOctaveCount; ++band)
    {
        const auto centre = drum::thirdOctaveCentre(band);
        const auto p = plugin.shapeDb[static_cast<size_t>(band)], m = mixer.shapeDb[static_cast<size_t>(band)];
        if (centre < 100 || centre > 8000 || !std::isfinite(p) || !std::isfinite(m)
            || !mixer.reliable[static_cast<size_t>(band)] || !plugin.reliable[static_cast<size_t>(band)]) continue;
        ++compared;
        squared += (p - m) * (p - m);
        sumP += p; sumM += m; sumPP += p * p; sumMM += m * m; sumPM += p * m;
        if (std::abs(m) >= 1.5f) { ++significant; if ((p > 0) == (m > 0) && std::abs(p) >= 0.5f) ++agree; }
    }
    const auto rms = compared > 0 ? std::sqrt(squared / compared) : -1.0;
    const auto rmsMixerOnly = [&] {
        double total = 0; int n = 0;
        for (int band = 0; band < drum::thirdOctaveCount; ++band)
        {
            const auto centre = drum::thirdOctaveCentre(band);
            const auto m = mixer.shapeDb[static_cast<size_t>(band)];
            if (centre < 100 || centre > 8000 || !std::isfinite(m) || !mixer.reliable[static_cast<size_t>(band)] || !plugin.reliable[static_cast<size_t>(band)]) continue;
            total += m * m; ++n;
        }
        return n > 0 ? std::sqrt(total / n) : -1.0;
    }();
    double correlation = -2;
    if (compared > 3)
    {
        const auto n = static_cast<double>(compared);
        const auto covariance = sumPM - sumP * sumM / n, varianceP = sumPP - sumP * sumP / n, varianceM = sumMM - sumM * sumM / n;
        if (varianceP > 1.0e-9 && varianceM > 1.0e-9) correlation = covariance / std::sqrt(varianceP * varianceM);
    }

    const auto& s = proposal.settings;
    juce::String md;
    md << "# " << rawFile.getFileNameWithoutExtension() << " : plug-in (" << drum::instrumentName(drum::instrumentFromValue(static_cast<float>(role)))
       << ", " << styleName << " " << juce::roundToInt(intensity * 100) << " %) contre ingenieur (" << mixFile.getParentDirectory().getFileName() << "/" << mixFile.getFileNameWithoutExtension() << ")\n\n";
    if (compared < 6)
        md << "**Accord : non evalue, " << compared << " bande(s) fiable(s) seulement entre 100 Hz et 8 kHz (il en faut 6) : la paire de l'ingenieur est trop peu coherente avec le brut**\n\n";
    else
    md << "**Accord sur " << compared << " bandes fiables (100 Hz - 8 kHz) : ecart quadratique moyen " << number(static_cast<float>(rms)) << " dB"
       << " (l'ingenieur seul s'ecarte de " << number(static_cast<float>(rmsMixerOnly)) << " dB d'une courbe plate)"
       << ", correlation des formes " << number(static_cast<float>(correlation), 2)
       << ", meme sens sur " << agree << " des " << significant << " bandes ou l'ingenieur fait plus de 1,5 dB**\n\n";
    md << "- Paire ingenieur : " << (mixer.accepted ? "acceptee" : "ECARTEE (" + mixer.reason + ")") << ", coherence " << number(mixer.medianCoherence, 2)
       << ", decalage " << number(static_cast<float>(mixer.lagSamples * 1000.0 / rawRate), 2) << " ms" << (mixer.inverted ? ", polarite inversee" : "") << "\n";
    md << "- Paire plug-in : " << (plugin.accepted ? "acceptee" : "ECARTEE (" + plugin.reason + ")") << ", coherence " << number(plugin.medianCoherence, 2) << "\n";
    md << "- Niveau moyen 100 Hz - 5 kHz : ingenieur " << number(mixer.gainDb) << " dB, plug-in " << number(plugin.gainDb) << " dB\n";
    md << "- Crest actif brut -> mixe : ingenieur " << number(mixer.deltaActiveCrestDb) << " dB, plug-in " << number(plugin.deltaActiveCrestDb) << " dB\n";
    md << "- Crest brut -> mixe : ingenieur " << number(mixer.deltaCrestDb) << " dB, plug-in " << number(plugin.deltaCrestDb) << " dB\n";
    md << "- Plancher de bruit (gain retire) : ingenieur " << number(mixer.deltaNoiseFloorDb) << " dB, plug-in " << number(plugin.deltaNoiseFloorDb) << " dB\n";
    md << "- Frappes brut -> mixe : ingenieur " << mixer.hitsRaw << " -> " << mixer.hitsMix << ", plug-in " << plugin.hitsRaw << " -> " << plugin.hitsMix << "\n";
    md << "- Couleur non lineaire : ingenieur " << number(mixer.nonLinearDb) << " dB, plug-in " << number(plugin.nonLinearDb) << " dB\n";
    md << "- Reglages proposes : filtres " << number(s[drum::highPass], 0) << " Hz / " << number(s[drum::lowPass], 0) << " Hz ; EQ " << (s[drum::eqOn] > 0.5f ? "oui" : "non")
       << " corps " << number(s[drum::bodyFrequency], 0) << " Hz " << number(s[drum::bodyGain]) << " dB, boite " << number(s[drum::boxFrequency], 0) << " Hz "
       << number(s[drum::boxGain]) << " dB, attaque " << number(s[drum::attackFrequency], 0) << " Hz " << number(s[drum::attackGain]) << " dB, air "
       << number(s[drum::airFrequency], 0) << " Hz " << number(s[drum::airGain]) << " dB ; compresseur " << (s[drum::compressorOn] > 0.5f ? "oui" : "non")
       << " seuil " << number(s[drum::threshold]) << " dB rapport " << number(s[drum::ratio]) << " ; expandeur " << (s[drum::gateOn] > 0.5f ? "oui" : "non") << "\n\n";
    md << "| Bande (Hz) | Ingenieur (dB) | Plug-in (dB) | Plug-in - ingenieur | Fiable |\n|---|---|---|---|---|\n";
    for (int band = 0; band < drum::thirdOctaveCount; ++band)
    {
        const auto p = plugin.shapeDb[static_cast<size_t>(band)], m = mixer.shapeDb[static_cast<size_t>(band)];
        if (!std::isfinite(p) && !std::isfinite(m)) continue;
        const bool both = mixer.reliable[static_cast<size_t>(band)] && plugin.reliable[static_cast<size_t>(band)];
        md << "| " << drum::DrumAnalysisSnapshot::thirdOctaveLabel(band) << " | " << number(m) << " | " << number(p) << " | "
           << (std::isfinite(p) && std::isfinite(m) ? number(p - m) : juce::String("n/a")) << " | " << (both ? "oui" : "non") << " |\n";
    }
    std::cout << md << std::endl;

    if (commandLine.size() == 7)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("raw", rawFile.getFileName());
        object->setProperty("mix", mixFile.getFileName());
        object->setProperty("role", role);
        object->setProperty("style", styleName);
        object->setProperty("intensity", static_cast<double>(intensity));
        object->setProperty("bands_compared", compared);
        object->setProperty("rms_db", compared >= 6 ? juce::var(rms) : juce::var());
        object->setProperty("mixer_rms_db", compared >= 6 ? juce::var(rmsMixerOnly) : juce::var());
        object->setProperty("correlation", compared >= 6 && correlation > -1.5 ? juce::var(correlation) : juce::var());
        object->setProperty("agree", agree);
        object->setProperty("significant", significant);
        object->setProperty("mixer", drum::DrumPairAnalysis::toVar(mixer));
        object->setProperty("plugin", drum::DrumPairAnalysis::toVar(plugin));
        const auto prefix = commandLine[6];
        if (!juce::File(prefix + ".json").replaceWithText(juce::JSON::toString(juce::var(object.release()), false))
            || !juce::File(prefix + ".md").replaceWithText(md))
            return fail("cannot write the outputs");
    }
    return 0;
}

namespace
{
void expect(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

// 40 s of kick-like hits (a decaying 60 Hz tone plus a burst of noise) every 0.4 s, over a very quiet floor.
std::vector<float> syntheticTake(double rate, int seconds)
{
    juce::Random random(11);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, 0.4);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        audio[index] = static_cast<float>(0.5 * std::exp(-age * 18) * std::sin(juce::MathConstants<double>::twoPi * 60 * age)
            + 0.3 * std::exp(-age * 60) * noise + 0.0003 * noise);
    }
    return audio;
}

void writeWav(const juce::File& file, const std::vector<float>& audio, double rate)
{
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    expect(stream->openedOk(), "the test file must be writable");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
    expect(writer != nullptr, "the WAV writer must be created");
    stream.release();
    const float* channel = audio.data();
    expect(writer->writeFromFloatArrays(&channel, 1, static_cast<int>(audio.size())), "the test file must be written");
}

juce::var runVersus(const juce::File& raw, const juce::File& mix, const juce::File& prefix, const char* role = "0")
{
    const auto code = runVersusCommand({ "--vs-plugin", raw.getFullPathName(), role, "rock", "0.75", mix.getFullPathName(), prefix.getFullPathName() });
    expect(code == 0, "--vs-plugin must succeed on a valid pair");
    return juce::JSON::parse(prefix.withFileExtension("json"));
}

float bandOf(const juce::var& curve, const char* label)
{
    const auto* shape = curve["forme_db_1_3_octave"].getArray();
    for (int band = 0; band < drum::thirdOctaveCount; ++band)
        if (drum::DrumAnalysisSnapshot::thirdOctaveLabel(band) == label && shape != nullptr && !(*shape)[band].isVoid())
            return static_cast<float>(static_cast<double>((*shape)[band]));
    return std::numeric_limits<float>::quiet_NaN();
}
}

void testCymbalReference()
{
    constexpr double rate = 48000;
    const auto take = syntheticTake(rate, 12);
    drum::DrumAnalyser plain, referenced;
    plain.prepare(rate);
    referenced.prepare(rate);
    for (size_t offset = 0; offset < take.size(); offset += 4096)
    {
        const auto count = static_cast<int>(juce::jmin<size_t>(4096, take.size() - offset));
        plain.push(take.data() + offset, count);
        referenced.push(take.data() + offset, count);
    }
    expect(referenced.getPolicy().cymbalReference && drum::ProposalDefaults::cymbalReference, "The reference EQ is on by default (profileVersion 3)");
    auto policy = plain.getPolicy();
    policy.cymbalReference = false;
    plain.setPolicy(policy);

    const auto propose = [&](const drum::DrumAnalyser& analyser, drum::DrumInstrument role, drum::DrumStyle style, float intensity) {
        auto original = drum::defaultSettings();
        original[drum::instrument] = static_cast<float>(role);
        return analyser.propose(original, style, intensity);
    };
    const auto same = [](const drum::DrumProposal& a, const drum::DrumProposal& b) { return a.valid == b.valid && a.settings == b.settings; };
    using drum::DrumInstrument;
    using drum::DrumStyle;

    for (const auto role : { DrumInstrument::kick, DrumInstrument::snare, DrumInstrument::tom1, DrumInstrument::room, DrumInstrument::master })
        for (const auto style : { DrumStyle::natural, DrumStyle::rock, DrumStyle::metal })
            expect(same(propose(plain, role, style, 0.75f), propose(referenced, role, style, 0.75f)),
                "The reference EQ must not touch the other instruments");
    for (const auto role : { DrumInstrument::hiHat, DrumInstrument::overheads })
    {
        expect(same(propose(plain, role, DrumStyle::natural, 0.75f), propose(referenced, role, DrumStyle::natural, 0.75f)),
            "Natural must stay without imposed coloring");
        expect(same(propose(plain, role, DrumStyle::rock, 0.0f), propose(referenced, role, DrumStyle::rock, 0.0f)),
            "Zero intensity must change nothing");
    }

    const auto overheadsOff = propose(plain, DrumInstrument::overheads, DrumStyle::rock, 0.75f);
    const auto overheadsOn = propose(referenced, DrumInstrument::overheads, DrumStyle::rock, 0.75f);
    expect(overheadsOff.valid && overheadsOn.valid && overheadsOff.settings[drum::eqOn] == 0 && overheadsOff.settings[drum::highPass] == 100,
        "With the option off the Overheads keep their neutral EQ and 100 Hz high-pass");
    expect(overheadsOn.settings[drum::eqOn] == 1 && overheadsOn.settings[drum::highPass] == 180
        && overheadsOn.settings[drum::airFrequency] == 6000 && overheadsOn.settings[drum::airGain] > 3.0f
        && overheadsOn.settings[drum::airGain] < 6.0f && overheadsOn.summary.contains("reference EQ"),
        "By default the Overheads get a 180 Hz high-pass and a gentle air lift");
    const auto louder = propose(referenced, DrumInstrument::overheads, DrumStyle::rock, 1.0f);
    expect(louder.settings[drum::airGain] > overheadsOn.settings[drum::airGain], "The air lift grows with the intensity");

    const auto hatOff = propose(plain, DrumInstrument::hiHat, DrumStyle::metal, 0.75f);
    const auto hatOn = propose(referenced, DrumInstrument::hiHat, DrumStyle::metal, 0.75f);
    expect(hatOff.settings[drum::eqOn] == 0 && hatOn.settings[drum::eqOn] == 1 && hatOn.settings[drum::highPass] == 250
        && hatOn.settings[drum::bodyFrequency] == 200 && hatOn.settings[drum::bodyGain] < -6.0f && hatOn.settings[drum::bodyGain] >= -12.0f,
        "By default the Hi-hat gets a body cut at 200 Hz, within the control range");
}

void testVersus()
{
    constexpr double rate = 48000;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("zed_versus_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    expect(folder.createDirectory(), "the temporary folder must be created");
    struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup { folder };

    const auto raw = syntheticTake(rate, 40);
    juce::dsp::IIR::Coefficients<float>::Ptr shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf(rate, 2500.0f, 0.7f, juce::Decibels::decibelsToGain(6.0f));
    juce::dsp::IIR::Filter<float> filter(shelf);
    std::vector<float> quieter(raw.size()), brighter(raw.size());
    for (size_t index = 0; index < raw.size(); ++index) { quieter[index] = 0.5f * raw[index]; brighter[index] = filter.processSample(raw[index]); }
    writeWav(folder.getChildFile("raw.wav"), raw, rate);
    writeWav(folder.getChildFile("quieter.wav"), quieter, rate);
    writeWav(folder.getChildFile("brighter.wav"), brighter, rate);

    const auto level = runVersus(folder.getChildFile("raw.wav"), folder.getChildFile("quieter.wav"), folder.getChildFile("a"));
    const auto mixerLevel = level["mixer"];
    expect(static_cast<bool>(mixerLevel["accepted"]) && std::abs(static_cast<double>(mixerLevel["gain_moyen_db"]) + 6.02) < 0.3,
        "A mix that is the raw take at half the level must be read as -6 dB");
    expect(std::abs(bandOf(mixerLevel, "1000")) < 0.5f && std::abs(bandOf(mixerLevel, "4000")) < 0.5f,
        "A pure level change must leave the shape flat");

    const auto bright = runVersus(folder.getChildFile("raw.wav"), folder.getChildFile("brighter.wav"), folder.getChildFile("b"));
    const auto mixerBright = bright["mixer"];
    expect(static_cast<bool>(mixerBright["accepted"]), "A filtered copy of the raw take must be accepted");
    expect(bandOf(mixerBright, "8000") - bandOf(mixerBright, "400") > 3.5f,
        "A +6 dB high shelf must show as a rise of the equivalent curve");
    expect(static_cast<int>(bright["bands_compared"]) >= 6 && std::isfinite(static_cast<double>(bright["rms_db"])),
        "The comparison must cover enough reliable bands");
    expect(!static_cast<juce::var>(bright["plugin"]["forme_db_1_3_octave"]).isVoid(), "The plug-in curve must be written");

    expect(runVersusCommand({ "--vs-plugin", folder.getChildFile("raw.wav").getFullPathName(), "13", "rock", "0.75",
        folder.getChildFile("brighter.wav").getFullPathName() }) == 1, "An invalid role must be refused");
    expect(runVersusCommand({ "--vs-plugin", folder.getChildFile("raw.wav").getFullPathName(), "0", "disco", "0.75",
        folder.getChildFile("brighter.wav").getFullPathName() }) == 1, "An invalid style must be refused");
    expect(runVersusCommand({ "--vs-plugin", folder.getChildFile("missing.wav").getFullPathName(), "0", "rock", "0.75",
        folder.getChildFile("brighter.wav").getFullPathName() }) == 1, "A missing file must be refused");
    expect(runVersusCommand({ "--other" }) == -1, "Other commands must pass through");
    std::cout << "PASS: --vs-plugin (level -6 dB and +6 dB shelf recovered, bad arguments refused)" << std::endl;
    testCymbalReference();
    std::cout << "PASS: cymbal reference EQ (on by default, Hi-hat and Overheads in Rock / Metal only, can be switched off)" << std::endl;
}