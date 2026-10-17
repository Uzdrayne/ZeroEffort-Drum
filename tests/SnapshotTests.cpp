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

enum class Voice { kick, snare, tom };

std::vector<float> synthesise(Voice voice, double rate)
{
    std::vector<float> audio(static_cast<size_t>(rate * 4));
    juce::Random random(1234);
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, 0.5);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        double value = 0;
        if (voice == Voice::kick)
            value = 0.6 * std::exp(-age * 30) * std::sin(juce::MathConstants<double>::twoPi * 65 * time);
        else if (voice == Voice::tom)
            value = 0.5 * std::exp(-age * 12) * std::sin(juce::MathConstants<double>::twoPi * 110 * time);
        else
            value = std::exp(-age * 25) * (0.35 * std::sin(juce::MathConstants<double>::twoPi * 200 * time) + 0.25 * noise);
        audio[index] = static_cast<float>(value + 0.002 * noise);
    }
    return audio;
}

bool close(float a, float b)
{
    return std::abs(a - b) <= 1.0e-6f * juce::jmax(1.0f, std::abs(a), std::abs(b));
}

void sameProposal(const drum::DrumProposal& expected, const drum::DrumProposal& actual, const juce::String& context)
{
    check(expected.valid == actual.valid, context + " / valid");
    check(expected.hits == actual.hits, context + " / hits");
    check(expected.summary == actual.summary, context + " / summary");
    check(close(expected.targetReduction, actual.targetReduction), context + " / targetReduction");
    check(close(expected.maximumReduction, actual.maximumReduction), context + " / maximumReduction");
    for (size_t band = 0; band < 3; ++band)
        check(close(expected.bandTargets[band], actual.bandTargets[band])
            && close(expected.bandCeilings[band], actual.bandCeilings[band]), context + " / bands");
    for (size_t control = 0; control < expected.settings.size(); ++control)
        check(close(expected.settings[control], actual.settings[control]),
            context + " / " + drum::controlSpecs[control].id);
}

void testRoundTrip()
{
    int comparisons = 0, validProposals = 0;
    for (const auto rate : { 48000.0, 96000.0 })
        for (const auto voice : { Voice::kick, Voice::snare, Voice::tom })
        {
            const auto audio = synthesise(voice, rate);
            auto analyser = std::make_unique<drum::DrumAnalyser>();
            analyser->prepare(rate);
            analyser->push(audio.data(), static_cast<int>(audio.size()));

            drum::SnapshotContext context;
            context.style = drum::DrumStyle::rock;
            context.intensity = 0.8f;
            context.original[drum::instrument] = voice == Voice::kick ? 0.0f : voice == Voice::snare ? 1.0f : 2.0f;
            const auto proposal = analyser->propose(context.original, context.style, context.intensity);
            const auto snapshot = drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), context, proposal);

            // Go through text, as a file on disk would.
            const auto text = juce::JSON::toString(snapshot, false);
            check(text.getNumBytesAsUTF8() < 300 * 1024, "Snapshot must stay under 300 KB");
            juce::var parsed;
            check(juce::JSON::parse(text, parsed).wasOk(), "Snapshot text must parse");

            auto restoredAnalyser = std::make_unique<drum::DrumAnalyser>();
            drum::RestoredSnapshot restored;
            const auto result = drum::DrumAnalysisSnapshot::restoreSnapshot(restoredAnalyser->editableAnalysis(), parsed, restored);
            check(result.wasOk(), "Snapshot must restore: " + result.getErrorMessage());
            check(restored.context.style == context.style && close(restored.context.intensity, context.intensity),
                "Style and intensity must survive");
            sameProposal(proposal, restored.recorded, "Recorded proposal");

            for (int element = 0; element < 10; ++element)
                for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
                    for (const auto intensity : { 0.0f, 0.5f, 1.0f })
                    {
                        auto original = restored.context.original;
                        original[drum::instrument] = static_cast<float>(element);
                        const auto expected = analyser->propose(original, style, intensity);
                        const auto replayed = restoredAnalyser->propose(original, style, intensity);
                        sameProposal(expected, replayed, "Replay rate " + juce::String(rate) + " voice "
                            + juce::String(static_cast<int>(voice)) + " role " + juce::String(element)
                            + " style " + juce::String(static_cast<int>(style)) + " intensity " + juce::String(intensity));
                        ++comparisons;
                        validProposals += expected.valid ? 1 : 0;
                    }
        }
    check(validProposals > comparisons / 3, "The round trip must cover many valid proposals");
    std::cout << "PASS: snapshot round trip, " << comparisons << " replays (" << validProposals << " valid)" << std::endl;
}

void testMeasuresAndFiles()
{
    const auto audio = synthesise(Voice::kick, 48000.0);
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(48000.0);
    analyser->push(audio.data(), static_cast<int>(audio.size()));
    drum::SnapshotContext context;
    const auto proposal = analyser->propose(context.original);
    const auto snapshot = drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), context, proposal);

    auto* meta = snapshot["meta"].getDynamicObject();
    check(meta != nullptr, "meta block");
    for (const auto* field : { "schema", "analysisVersion", "profileVersion", "role", "style", "intensity", "source", "licence" })
        check(meta->hasProperty(field), juce::String("meta.") + field + " is mandatory");
    check(static_cast<int>(meta->getProperty("analysisVersion")) == drum::analysisVersion, "analysisVersion recorded");
    check(meta->getProperty("role").toString() == "Kick", "role name recorded");

    // The microphone position and the channel count are written in clear (the position is also in input.settings.kitMic).
    check(meta->getProperty("position").toString() == "Kick In" && !meta->hasProperty("channels"), "Default position, no channel count when unknown");
    const auto labelled = [&](int instrumentValue, int micValue, int channels)
    {
        auto copy = context;
        copy.original[drum::instrument] = static_cast<float>(instrumentValue);
        copy.original[drum::kitMic] = static_cast<float>(micValue);
        copy.channels = channels;
        return drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), copy, proposal)["meta"];
    };
    check(labelled(0, 1, 1)["position"].toString() == "Kick Out" && labelled(1, 0, 1)["position"].toString() == "Snare Top"
        && labelled(1, 1, 1)["position"].toString() == "Snare Bottom", "Kick and Snare positions");
    check(labelled(7, 0, 2)["position"].toString() == "Overheads (stereo)" && labelled(7, 2, 1)["position"].toString() == "Overheads L"
        && labelled(7, 3, 1)["position"].toString() == "Overheads R" && labelled(8, 3, 1)["position"].toString() == "Room R",
        "Overheads and Room positions");
    check(labelled(6, 3, 1)["position"].toString() == "Hi-hat" && labelled(5, 1, 1)["position"].toString() == "Tom basse",
        "The position means nothing for the other instruments");
    check(static_cast<int>(labelled(7, 0, 2)["channels"]) == 2 && static_cast<int>(labelled(0, 0, 1)["channels"]) == 1, "Channel count recorded");
    {
        auto stereo = context;
        stereo.channels = 2;
        juce::var parsedStereo;
        check(juce::JSON::parse(juce::JSON::toString(drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), stereo, proposal), false), parsedStereo).wasOk(),
            "Snapshot with channels parses");
        auto target = std::make_unique<drum::DrumAnalyser>();
        drum::RestoredSnapshot stereoRestored;
        check(drum::DrumAnalysisSnapshot::restoreSnapshot(target->editableAnalysis(), parsedStereo, stereoRestored).wasOk()
            && stereoRestored.context.channels == 2, "The channel count must survive a restore");
    }

    const auto measures = snapshot["measures"];
    check(std::abs(static_cast<double>(measures["lvl_peak_dbfs"]) - analyser->analysis().peakDb()) < 1.0e-4, "peak measure");
    check(std::abs(static_cast<double>(measures["dyn_crest_global_db"]) - analyser->analysis().crestDb()) < 1.0e-4, "crest measure");
    check(static_cast<int>(measures["hit_hits"]) == proposal.hits && proposal.hits >= 3, "hit measure");
    check(static_cast<double>(measures["lvl_env_p10"]) <= static_cast<double>(measures["lvl_env_p95"]), "percentiles ordered");

    const auto settings = snapshot["proposal"]["settings"];
    check(settings.getDynamicObject() != nullptr && settings.getDynamicObject()->getProperties().size() == drum::controlCount,
        "Proposal must be written by parameter identifier");
    check(static_cast<double>(settings["highPassHz"]) == static_cast<double>(proposal.settings[drum::highPass]), "identifier maps to the right control");

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("zed_snapshot", ".json");
    check(drum::DrumAnalysisSnapshot::writeFile(file, snapshot).wasOk(), "Snapshot file must be written");
    juce::var loaded;
    check(drum::DrumAnalysisSnapshot::readFile(file, loaded).wasOk(), "Snapshot file must be read");
    drum::RestoredSnapshot restored;
    auto other = std::make_unique<drum::DrumAnalyser>();
    check(drum::DrumAnalysisSnapshot::restoreSnapshot(other->editableAnalysis(), loaded, restored).wasOk(), "File snapshot must restore");
    sameProposal(proposal, other->propose(restored.context.original, restored.context.style, restored.context.intensity), "File replay");
    file.deleteFile();

    // The export is silent unless the folder variable is set.
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("zed_export", "");
    _putenv_s("ZED_ANALYSIS_EXPORT_DIR", "");
    check(drum::DrumAnalysisSnapshot::exportSnapshot(analyser->analysis(), context, proposal).wasOk()
        && !folder.exists(), "Export must do nothing without ZED_ANALYSIS_EXPORT_DIR");
    _putenv_s("ZED_ANALYSIS_EXPORT_DIR", folder.getFullPathName().toRawUTF8());
    const auto exported = drum::DrumAnalysisSnapshot::exportSnapshot(analyser->analysis(), context, proposal);
    _putenv_s("ZED_ANALYSIS_EXPORT_DIR", "");
    check(exported.wasOk(), "Export must succeed: " + exported.getErrorMessage());
    const auto files = folder.findChildFiles(juce::File::findFiles, false, "snapshot_*_Kick.json");
    check(files.size() == 1, "Export must write one snapshot named after the role");
    juce::var exportedSnapshot;
    check(drum::DrumAnalysisSnapshot::readFile(files[0], exportedSnapshot).wasOk(), "Exported snapshot must be readable");
    folder.deleteRecursively();
    std::cout << "PASS: snapshot measures, identifiers, file and export" << std::endl;
}

void testRejections()
{
    const auto audio = synthesise(Voice::kick, 48000.0);
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(48000.0);
    analyser->push(audio.data(), static_cast<int>(audio.size()));
    drum::SnapshotContext context;
    const auto good = juce::JSON::toString(
        drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), context, analyser->propose(context.original)), false);

    const auto expectRejected = [&](const juce::String& from, const juce::String& to, const char* why)
    {
        check(good.contains(from), juce::String("Edit pattern must exist in the snapshot text: ") + why);
        juce::var parsed;
        check(juce::JSON::parse(good.replace(from, to, false), parsed).wasOk(), juce::String("Edited text must still be JSON: ") + why);
        auto target = std::make_unique<drum::DrumAnalyser>();
        drum::RestoredSnapshot restored;
        check(drum::DrumAnalysisSnapshot::restoreSnapshot(target->editableAnalysis(), parsed, restored).failed(),
            juce::String("Must be rejected: ") + why);
    };
    expectRejected("\"analysisVersion\": " + juce::String(drum::analysisVersion), "\"analysisVersion\": 999", "other analysisVersion");
    expectRejected("\"schema\": " + juce::String(drum::snapshotSchema), "\"schema\": 99", "other schema");
    expectRejected("\"style\": \"natural\"", "\"style\": \"jazz\"", "unknown style");
    expectRejected("\"rate\": 48000", "\"rate\": 5", "invalid rate");
    expectRejected("\"invalid\": false", "\"invalid\": 3", "invalid flag type");
    expectRejected("\"spectrum\": \"", "\"spectrum\": \"AAAA", "truncated spectrum");
    expectRejected("\"histogram\": [", "\"histogram\": [ -1,", "histogram shape");
    check(!good.contains("jazz"), "Sanity check of the edit helper");

    // A failed restore must not leave a half-loaded analysis that looks valid.
    juce::var parsed;
    juce::JSON::parse(good.replace("\"spectrum\": \"", "\"spectrum\": \"AAAA", false), parsed);
    auto target = std::make_unique<drum::DrumAnalyser>();
    drum::RestoredSnapshot restored;
    check(drum::DrumAnalysisSnapshot::restoreSnapshot(target->editableAnalysis(), parsed, restored).failed(), "Corrupt spectrum rejected");
    check(!target->propose(drum::defaultSettings()).valid, "A rejected snapshot must leave an empty analysis");
    std::cout << "PASS: snapshot rejections" << std::endl;
}
}

void testSnapshot()
{
    testRoundTrip();
    testMeasuresAndFiles();
    testRejections();
}
