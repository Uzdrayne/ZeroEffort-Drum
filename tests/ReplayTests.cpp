#include "Drum/DrumReplay.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

int runReplayCommand(const juce::StringArray& arguments);

namespace
{
void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

std::vector<float> hits(double rate, double frequency, double decay, double period, double noise, int seed)
{
    juce::Random random(seed);
    std::vector<float> audio(static_cast<size_t>(rate * 4));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, period);
        audio[index] = static_cast<float>(0.6 * std::exp(-age * decay) * std::sin(juce::MathConstants<double>::twoPi * frequency * time)
            + noise * (random.nextFloat() * 2.0f - 1.0f));
    }
    return audio;
}

juce::File makeSnapshot(const juce::File& folder, const juce::String& name, const std::vector<float>& audio, double rate,
    int role, drum::DrumStyle style, float intensity)
{
    drum::DrumAnalyser analyser;
    analyser.prepare(rate);
    analyser.push(audio.data(), static_cast<int>(audio.size()));
    drum::SnapshotContext context;
    context.original[drum::instrument] = static_cast<float>(role);
    context.style = style;
    context.intensity = intensity;
    context.source = name;
    const auto file = folder.getChildFile(name + ".json");
    file.getParentDirectory().createDirectory();
    check(drum::DrumAnalysisSnapshot::writeFile(file,
        drum::DrumAnalysisSnapshot::toSnapshot(analyser.analysis(), context, analyser.propose(context.original, style, intensity))).wasOk(),
        "snapshot written");
    return file;
}

juce::File makeCorpus()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("zed_corpus", "");
    folder.createDirectory();
    makeSnapshot(folder, "kick_a", hits(48000, 62, 28, 0.45, 0.005, 1), 48000, 0, drum::DrumStyle::rock, 0.8f);
    makeSnapshot(folder, "kick_b", hits(96000, 55, 20, 0.5, 0.002, 2), 96000, 0, drum::DrumStyle::metal, 1.0f);
    makeSnapshot(folder, "sub/snare_a", hits(48000, 200, 25, 0.5, 0.05, 3), 48000, 1, drum::DrumStyle::natural, 0.5f);
    makeSnapshot(folder, "sub/tom_a", hits(44100, 110, 12, 0.6, 0.01, 4), 44100, 3, drum::DrumStyle::rock, 1.0f);
    makeSnapshot(folder, "silent", std::vector<float>(48000 * 3), 48000, 0, drum::DrumStyle::natural, 1.0f);
    return folder;
}

void testReplayFolder()
{
    const auto folder = makeCorpus();
    folder.getChildFile("broken.json").replaceWithText("{ not json");
    auto stale = folder.getChildFile("kick_a.json").loadFileAsString();
    stale = stale.replace("\"analysisVersion\": " + juce::String(drum::analysisVersion), "\"analysisVersion\": 0", false);
    folder.getChildFile("stale.json").replaceWithText(stale);

    const auto result = drum::DrumReplay::replayFolder(folder);
    check(result.current.entries.size() == 5 && result.recorded.entries.size() == 5, "Five valid snapshots must be replayed, got "
        + juce::String(static_cast<int>(result.current.entries.size())));
    check(result.current.skipped.size() == 2, "The broken and the stale snapshots must be reported as skipped");
    for (const auto& [file, reason] : result.current.skipped)
        check(reason.isNotEmpty(), "A skipped snapshot must say why: " + file);
    check(result.current.entries[0].file == "kick_a.json" && result.current.entries[3].file == "sub/snare_a.json" && result.current.entries[4].valid,
        "Entries are sorted and keyed by relative path");

    // Same code, same snapshots: nothing changes.
    const auto same = drum::DrumReplay::compare(result.recorded, result.current);
    check(same.matched == 5 && same.onlyA == 0 && same.onlyB == 0, "All entries must match");
    check(same.flips.empty() && same.modified.empty() && same.listen.empty() && !same.refusalAlert && same.nondeterministic == 0,
        "Replaying with the code that wrote the snapshots must change nothing");
    check(same.overall.refusedB == 1 && same.overall.totalB == 5, "The silent snapshot is the only refusal");
    check(drum::DrumReplay::toMarkdown(same).contains("Rien a ecouter"), "Markdown must say nothing needs a listen");
    for (const auto& item : same.proximity) check(item.analysed >= 0 && item.closeCount <= item.analysed, "Proximity counts are consistent");

    // A changed code is simulated by editing the recorded side.
    auto edited = result.recorded;
    edited.entries[0].settings[drum::bodyGain] += 3.0f;              // gain: 3 dB, above the 1 dB listen threshold
    edited.entries[1].settings[drum::highPass] *= 1.02f;             // frequency: +2 %, below the 5 % threshold
    edited.entries[4].valid = false;                                  // refused before, valid now
    const auto diff = drum::DrumReplay::compare(edited, result.current);
    check(diff.flips.size() == 1 && diff.flips[0].file == "sub/tom_a.json" && !diff.flips[0].validA, "A refusal turned valid must be listed");
    bool foundBody = false, foundSmall = false;
    for (const auto& item : diff.modified)
    {
        if (item.control == "bodyDb" && item.maxFile == "kick_a.json") foundBody = std::abs(item.maxAbs - 3.0) < 1.0e-4 && std::abs(item.mean + 3.0) < 1.0e-4;
        if (item.control == "highPassHz" && item.maxFile == "kick_b.json") foundSmall = true;
    }
    check(foundBody && foundSmall, "Modified settings must report mean and maximum with the file");
    check(diff.listen.size() == 2 && diff.listen[0].file == "kick_a.json" && diff.listen[1].file == "sub/tom_a.json",
        "Only the 3 dB change and the flip deserve a listen, biggest first");
    check(diff.refusalAlert, "One refusal over five analyses is 20 points: alert");
    const auto markdown = drum::DrumReplay::toMarkdown(diff);
    check(markdown.contains("ALERTE") && markdown.contains("## A ecouter") && markdown.contains("kick_a.json")
        && markdown.contains("bodyDb"), "Markdown must carry the alert, the setting and the listen list");

    // Entries present on one side only.
    auto trimmed = result.current;
    trimmed.entries.pop_back();
    const auto partial = drum::DrumReplay::compare(result.recorded, trimmed);
    check(partial.onlyA == 1 && partial.onlyB == 0 && partial.matched == 4, "Unmatched entries must be counted");

    // Report round trip through text.
    juce::var parsed;
    check(juce::JSON::parse(juce::JSON::toString(drum::DrumReplay::reportToVar(result.current), false), parsed).wasOk(), "Report is JSON");
    drum::ReplayReport reloaded;
    check(drum::DrumReplay::reportFromVar(parsed, reloaded).wasOk(), "Report must reload");
    const auto again = drum::DrumReplay::compare(result.current, reloaded);
    check(again.matched == 5 && again.modified.empty() && again.flips.empty(), "A reloaded report must compare as identical");
    juce::var badReport;
    juce::JSON::parse("{ \"meta\": {} }", badReport);
    drum::ReplayReport rejected;
    check(drum::DrumReplay::reportFromVar(badReport, rejected).failed(), "A file that is not a report must be rejected");
    juce::var comparisonJson;
    check(juce::JSON::parse(juce::JSON::toString(drum::DrumReplay::comparisonToVar(diff), false), comparisonJson).wasOk()
        && static_cast<int>(comparisonJson["matched"]) == 5 && comparisonJson["listen"].size() == 2, "Comparison JSON");

    folder.deleteRecursively();
    std::cout << "PASS: replay folder, diff, refusal alert, listen list and report round trip" << std::endl;
}

int run(std::initializer_list<juce::String> arguments)
{
    juce::StringArray list;
    for (const auto& item : arguments) list.add(item);
    return runReplayCommand(list);
}

void testReplayCommands()
{
    const auto folder = makeCorpus();
    const auto output = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("zed_replay_out", "");
    output.createDirectory();
    const auto prefix = output.getChildFile("replay").getFullPathName();
    check(run({ "--replay", folder.getFullPathName(), prefix }) == 0, "--replay must succeed");
    check(output.getChildFile("replay.json").existsAsFile() && output.getChildFile("replay.md").existsAsFile(), "--replay writes .json and .md");
    check(output.getChildFile("replay.md").loadFileAsString().contains("# Comparaison"), "Markdown report content");
    const auto prefixCompare = output.getChildFile("compare").getFullPathName();
    check(run({ "--compare", prefix + ".json", prefix + ".json", prefixCompare }) == 0, "--compare must succeed");
    check(output.getChildFile("compare.json").existsAsFile() && output.getChildFile("compare.md").existsAsFile(), "--compare writes .json and .md");
    check(run({ "--replay", folder.getChildFile("missing").getFullPathName() }) == 1, "A missing folder must fail");
    check(run({ "--compare", prefix + ".json" }) == 1, "--compare needs two reports");
    check(run({ "--compare", prefix + ".json", folder.getChildFile("kick_a.json").getFullPathName() }) == 1,
        "--compare must reject a file that is not a report");
    check(run({ "--something-else" }) < 0, "Unknown options are left to the caller");

    // WAV export: a short synthetic file written and exported by role
    const auto wav = output.getChildFile("kick take.wav");
    {
        const auto audio = hits(48000, 60, 28, 0.45, 0.003, 9);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
        check(stream != nullptr, "wav stream");
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000, 1, 24, {}, 0));
        check(writer != nullptr, "wav writer");
        stream.release();
        juce::AudioBuffer<float> buffer(1, static_cast<int>(audio.size()));
        buffer.copyFrom(0, 0, audio.data(), static_cast<int>(audio.size()));
        check(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()), "wav written");
    }
    const auto snapshots = output.getChildFile("snapshots");
    check(run({ "--export-wav", wav.getFullPathName(), snapshots.getFullPathName(), "0", "rock", "0.8" }) == 0, "--export-wav must succeed");
    const auto exported = snapshots.findChildFiles(juce::File::findFiles, false, "*.json");
    check(exported.size() == 1 && exported[0].getFileName().contains("Kick") && exported[0].getFileName().contains("rock"),
        "One snapshot named after the take, the role and the style");
    const auto replayed = drum::DrumReplay::replayFolder(snapshots);
    check(replayed.current.entries.size() == 1 && replayed.current.entries[0].valid && replayed.current.entries[0].style == "rock",
        "The exported snapshot must replay as a valid rock kick");
    check(run({ "--export-wav", wav.getFullPathName(), snapshots.getFullPathName(), "12" }) == 1, "An unknown role must fail");
    check(run({ "--export-wav", wav.getFullPathName(), snapshots.getFullPathName(), "0", "jazz" }) == 1, "An unknown style must fail");

    // J3 is the default: the export must record it, and the opt-out flag must record the former behaviour.
    const auto readExport = [&](const juce::File& file, drum::RestoredSnapshot& restored, juce::var& parsed)
    {
        auto target = std::make_unique<drum::DrumAnalyser>();
        check(drum::DrumAnalysisSnapshot::readFile(file, parsed).wasOk()
            && drum::DrumAnalysisSnapshot::restoreSnapshot(target->editableAnalysis(), parsed, restored).wasOk(), "The exported snapshot must restore");
        return target->analysis().excludesLeadFrames();
    };
    drum::RestoredSnapshot restored;
    juce::var parsed;
    check(readExport(exported[0], restored, parsed) && restored.context.policy.compressorUsesActiveCrest,
        "--export-wav must record the J3 defaults");
    check(parsed["meta"]["channels"].isInt() && static_cast<int>(parsed["meta"]["channels"]) == 1 && parsed["meta"]["position"].toString() == "Kick In",
        "--export-wav must record the channel count and the default position");
    const auto legacy = output.getChildFile("legacy");
    check(run({ "--export-wav", wav.getFullPathName(), legacy.getFullPathName(), "0", "rock", "0.8", "--no-exclude-lead-frames", "--no-active-crest" }) == 0,
        "The opt-out flags must be accepted");
    const auto legacyFiles = legacy.findChildFiles(juce::File::findFiles, false, "*.json");
    check(legacyFiles.size() == 1 && !readExport(legacyFiles[0], restored, parsed) && !restored.context.policy.compressorUsesActiveCrest,
        "The opt-out flags must record the former behaviour");

    // Position of the microphone (kitMic), recorded in the settings and written in clear in meta.position
    const auto positioned = output.getChildFile("positioned");
    check(run({ "--export-wav", wav.getFullPathName(), positioned.getFullPathName(), "1", "--position", "1" }) == 0, "--position must be accepted");
    const auto positionedFiles = positioned.findChildFiles(juce::File::findFiles, false, "*.json");
    check(positionedFiles.size() == 1, "One positioned snapshot");
    readExport(positionedFiles[0], restored, parsed);
    check(restored.context.original[drum::kitMic] == 1.0f && parsed["meta"]["position"].toString() == "Snare Bottom",
        "--position 1 on a Snare must record kitMic 1 and Snare Bottom");
    check(run({ "--export-wav", wav.getFullPathName(), positioned.getFullPathName(), "1", "--position", "4" }) == 1, "A position above 3 must fail");
    check(run({ "--export-wav", wav.getFullPathName(), positioned.getFullPathName(), "1", "--position" }) == 1, "--position needs a value");

    folder.deleteRecursively();
    output.deleteRecursively();
    std::cout << "PASS: replay, compare and export-wav commands" << std::endl;
}
}

int runReplayCommand(const juce::StringArray& commandLine)
{
    // Flags may appear anywhere: --exclude-lead-frames / --no-exclude-lead-frames (analysis, --export-wav; J3 is the default),
    // --active-crest / --no-active-crest (proposal, --export-wav records it, --replay overrides the recorded one) and
    // --position <0-3> (kitMic of the take, --export-wav).
    juce::StringArray arguments;
    std::optional<bool> excludeLeadFrames;
    std::optional<bool> activeCrest;
    std::optional<int> position;
    bool badPosition = false;
    for (int index = 0; index < commandLine.size(); ++index)
    {
        const auto& item = commandLine[index];
        if (item == "--exclude-lead-frames") excludeLeadFrames = true;
        else if (item == "--no-exclude-lead-frames") excludeLeadFrames = false;
        else if (item == "--active-crest") activeCrest = true;
        else if (item == "--no-active-crest") activeCrest = false;
        else if (item == "--position")
        {
            const auto value = index + 1 < commandLine.size() ? commandLine[index + 1] : juce::String();
            if (value.length() == 1 && value.containsOnly("0123")) position = value.getIntValue();
            else badPosition = true;
            ++index;
        }
        else arguments.add(item);
    }
    if (arguments.isEmpty()) return -1;
    const auto fail = [](const juce::String& message) { std::cerr << "ERROR: " << message << std::endl; return 1; };
    if (badPosition) return fail("--position needs a value from 0 to 3");
    const auto writeOutputs = [&](const juce::String& prefix, const juce::var& json, const juce::String& markdown)
    {
        if (prefix.isEmpty()) return true;
        return juce::File(prefix + ".json").replaceWithText(juce::JSON::toString(json, false))
            && juce::File(prefix + ".md").replaceWithText(markdown);
    };
    const auto command = arguments[0];
    if (command == "--replay")
    {
        if (arguments.size() < 2 || arguments.size() > 3) return fail("usage: --replay <snapshot folder> [output prefix] [--active-crest|--no-active-crest]");
        const juce::File folder(arguments[1]);
        if (!folder.isDirectory()) return fail("not a folder: " + folder.getFullPathName());
        const auto result = drum::DrumReplay::replayFolder(folder, activeCrest);
        auto markdown = drum::DrumReplay::toMarkdown(drum::DrumReplay::compare(result.recorded, result.current));
        if (!result.current.skipped.empty())
        {
            markdown << "\n## Snapshots ignores\n\n";
            for (const auto& [file, reason] : result.current.skipped) markdown << "- " << file << " : " << reason << "\n";
        }
        std::cout << markdown << std::endl;
        if (!writeOutputs(arguments.size() == 3 ? arguments[2] : juce::String(), drum::DrumReplay::reportToVar(result.current), markdown))
            return fail("cannot write the report");
        return 0;
    }
    if (command == "--compare")
    {
        if (arguments.size() < 3 || arguments.size() > 4) return fail("usage: --compare <report A.json> <report B.json> [output prefix]");
        drum::ReplayReport reports[2];
        for (int side = 0; side < 2; ++side)
        {
            const juce::File file(arguments[1 + side]);
            juce::var parsed;
            auto status = drum::DrumAnalysisSnapshot::readFile(file, parsed);
            if (status.wasOk()) status = drum::DrumReplay::reportFromVar(parsed, reports[side]);
            if (status.failed()) return fail(file.getFullPathName() + ": " + status.getErrorMessage());
        }
        const auto comparison = drum::DrumReplay::compare(reports[0], reports[1]);
        const auto markdown = drum::DrumReplay::toMarkdown(comparison);
        std::cout << markdown << std::endl;
        if (!writeOutputs(arguments.size() == 4 ? arguments[3] : juce::String(), drum::DrumReplay::comparisonToVar(comparison), markdown))
            return fail("cannot write the comparison");
        return 0;
    }
    if (command == "--export-wav")
    {
        if (arguments.size() < 4 || arguments.size() > 6)
            return fail("usage: --export-wav <wav> <snapshot folder> <role 0-9> [natural|rock|metal] [intensity 0-1] [--position 0-3] [--no-exclude-lead-frames] [--no-active-crest]");
        const juce::File wav(arguments[1]), folder(arguments[2]);
        if (!arguments[3].containsOnly("0123456789") || arguments[3].isEmpty() || arguments[3].getIntValue() > 9)
            return fail("role must be an integer from 0 to 9");
        drum::SnapshotContext context;
        context.original[drum::instrument] = arguments[3].getFloatValue();
        if (position.has_value()) context.original[drum::kitMic] = static_cast<float>(*position);
        if (arguments.size() >= 5 && !drum::DrumAnalysisSnapshot::styleFromId(arguments[4].toLowerCase(), context.style))
            return fail("style must be natural, rock or metal");
        if (arguments.size() == 6)
        {
            const auto intensity = arguments[5].getDoubleValue();
            if (!(intensity >= 0 && intensity <= 1)) return fail("intensity must be between 0 and 1");
            context.intensity = static_cast<float>(intensity);
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
        if (reader == nullptr) return fail("cannot read " + wav.getFullPathName());
        if (reader->numChannels < 1 || reader->numChannels > 2 || reader->sampleRate < 8000 || reader->sampleRate > 192000
            || reader->lengthInSamples <= 0 || reader->lengthInSamples > reader->sampleRate * 3600)
            return fail("supported: mono or stereo, 8-192 kHz, at most one hour");
        drum::DrumAnalyser analyser;
        if (excludeLeadFrames.has_value()) analyser.setExcludeLeadFrames(*excludeLeadFrames);
        analyser.prepare(reader->sampleRate);
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), 32768);
        std::array<float, 32768> mono {};
        for (juce::int64 offset = 0; offset < reader->lengthInSamples;)
        {
            const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, reader->lengthInSamples - offset));
            if (!reader->read(&audio, 0, count, offset, true, true)) return fail("read failure");
            for (int sample = 0; sample < count; ++sample)
            {
                float value = 0;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    if (std::abs(audio.getSample(channel, sample)) > std::abs(value)) value = audio.getSample(channel, sample);
                mono[static_cast<size_t>(sample)] = value;   // same channel fold as the ARA analysis
            }
            analyser.push(mono.data(), count);
            offset += count;
        }
        context.source = wav.getFileName();
        context.channels = static_cast<int>(reader->numChannels);
        if (activeCrest.has_value()) context.policy.compressorUsesActiveCrest = *activeCrest;
        analyser.setPolicy(context.policy);
        const auto proposal = analyser.propose(context.original, context.style, context.intensity);
        if (!folder.createDirectory()) return fail("cannot create " + folder.getFullPathName());
        const auto name = juce::File::createLegalFileName(wav.getFileNameWithoutExtension() + "_"
            + drum::DrumReplay::roleLabel(arguments[3].getIntValue()) + "_" + drum::DrumAnalysisSnapshot::styleId(context.style))
            .replaceCharacter(' ', '_');
        const auto status = drum::DrumAnalysisSnapshot::writeFile(folder.getChildFile(name + ".json"),
            drum::DrumAnalysisSnapshot::toSnapshot(analyser.analysis(), context, proposal));
        if (status.failed()) return fail(status.getErrorMessage());
        std::cout << name << ".json : " << (proposal.valid ? "proposition valide" : "analyse refusee : " + proposal.summary) << std::endl;
        return 0;
    }
    return -1;
}

void testReplay()
{
    testReplayFolder();
    testReplayCommands();
}
