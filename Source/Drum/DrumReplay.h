#pragma once
#include "DrumAnalysisSnapshot.h"
#include <algorithm>
#include <map>
#include <optional>
#include <tuple>

namespace drum
{
// H: to calibrate on a real corpus (document 02, sections 7.2 and 8).
struct ReplayThresholds
{
    static constexpr double gainDb = 1.0;            // a gain moving by this much is worth a listen
    static constexpr double frequencyRatio = 0.05;   // a frequency moving by 5 %
    static constexpr double otherRatio = 0.10;       // any other unit moving by 10 %
    static constexpr double refusalPoints = 5.0;     // refusal rate difference that raises an alert
    static constexpr double nearThreshold = 0.05;    // "close to a code threshold" band
    static constexpr double sameTolerance = 1.0e-6;  // relative, identical runs
    static constexpr int listenCount = 10;
};

struct ReplayEntry
{
    juce::String file, style, summary;
    int role = 0, hits = 0;
    float intensity = 0;
    bool valid = false, deterministic = true;
    Settings settings = defaultSettings();
    double crest = 0, bassRatio = 0, contrast = 0;
    double activeCrest = std::numeric_limits<double>::quiet_NaN();
};

struct ReplayReport
{
    juce::String label;
    int analysisVersion = drum::analysisVersion, profileVersion = drum::profileVersion;
    std::vector<ReplayEntry> entries;
    std::vector<std::pair<juce::String, juce::String>> skipped;   // file, reason
};

struct ReplayResult
{
    ReplayReport recorded, current;
};

struct RateRow
{
    juce::String name;
    int totalA = 0, refusedA = 0, totalB = 0, refusedB = 0;
    double percentA() const { return totalA > 0 ? 100.0 * refusedA / totalA : 0.0; }
    double percentB() const { return totalB > 0 ? 100.0 * refusedB / totalB : 0.0; }
};

struct Comparison
{
    struct Flip { juce::String file, role, style; bool validA = false; };
    struct Modified { juce::String role, style, control, maxFile; double mean = 0, maxAbs = 0; int count = 0; };
    struct Proximity { juce::String name; int analysed = 0, closeCount = 0; };
    struct Listen { juce::String file, role, style, reason; double score = 0; };

    juce::String labelA, labelB;
    int countA = 0, countB = 0, matched = 0, onlyA = 0, onlyB = 0, nondeterministic = 0;
    RateRow overall;
    std::vector<RateRow> byRole, byStyle;
    std::vector<Flip> flips;
    std::vector<Modified> modified;
    std::vector<Proximity> proximity;
    std::vector<Listen> listen;
    bool refusalAlert = false;
};

struct DrumReplay
{
    static juce::String roleLabel(int role)
    {
        return instrumentName(static_cast<DrumInstrument>(juce::jlimit(0, 9, role)));
    }

    static ReplayEntry makeEntry(const juce::String& file, const SnapshotContext& context, const DrumProposal& proposal,
        const DrumAnalysis& analysis, bool deterministic)
    {
        ReplayEntry entry;
        entry.file = file;
        entry.role = static_cast<int>(instrumentFromValue(context.original[instrument]));
        entry.style = DrumAnalysisSnapshot::styleId(context.style);
        entry.intensity = context.intensity;
        entry.valid = proposal.valid;
        entry.summary = proposal.summary;
        entry.hits = proposal.hits;
        entry.settings = proposal.settings;
        entry.deterministic = deterministic;
        entry.crest = analysis.crestDb();
        entry.activeCrest = analysis.activeCrestDb();
        const auto total = analysis.bandEnergy(20, 16000);
        entry.bassRatio = total > 0 ? analysis.bandEnergy(35, 150) / total : 0.0;
        const auto detector = entry.role == static_cast<int>(DrumInstrument::kick)
            ? 3 + static_cast<size_t>(context.style) : size_t { 3 };
        entry.contrast = analysis.envelopeContrastDb(detector);
        return entry;
    }

    // Replays every snapshot of a folder (recursively) through the current profiles, with the proposal policy
    // recorded in each snapshot unless an override is given.
    static ReplayResult replayFolder(const juce::File& folder, std::optional<bool> compressorUsesActiveCrest = std::nullopt)
    {
        ReplayResult result;
        result.recorded.label = "enregistre (" + folder.getFileName() + ")";
        result.current.label = "version courante";
        auto files = folder.findChildFiles(juce::File::findFiles, true, "*.json");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
            { return a.getFullPathName() < b.getFullPathName(); });
        auto analyser = std::make_unique<DrumAnalyser>();
        for (const auto& file : files)
        {
            const auto key = file.getRelativePathFrom(folder).replace("\\", "/");
            juce::var parsed;
            RestoredSnapshot restored;
            auto status = DrumAnalysisSnapshot::readFile(file, parsed);
            if (status.wasOk()) status = DrumAnalysisSnapshot::restoreSnapshot(analyser->editableAnalysis(), parsed, restored);
            if (status.failed())
            {
                result.recorded.skipped.emplace_back(key, status.getErrorMessage());
                result.current.skipped.emplace_back(key, status.getErrorMessage());
                continue;
            }
            const auto& context = restored.context;
            auto policy = context.policy;
            if (compressorUsesActiveCrest.has_value()) policy.compressorUsesActiveCrest = *compressorUsesActiveCrest;
            analyser->setPolicy(policy);
            const auto proposal = analyser->propose(context.original, context.style, context.intensity);
            const auto again = analyser->propose(context.original, context.style, context.intensity);
            const auto deterministic = proposal.valid == again.valid && proposal.settings == again.settings;
            result.recorded.entries.push_back(makeEntry(key, context, restored.recorded, analyser->analysis(), true));
            result.current.entries.push_back(makeEntry(key, context, proposal, analyser->analysis(), deterministic));
        }
        return result;
    }

    static juce::var reportToVar(const ReplayReport& report)
    {
        auto* meta = new juce::DynamicObject();
        meta->setProperty("label", report.label);
        meta->setProperty("analysisVersion", report.analysisVersion);
        meta->setProperty("profileVersion", report.profileVersion);
        juce::Array<juce::var> entries;
        for (const auto& entry : report.entries)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("file", entry.file);
            object->setProperty("role", entry.role);
            object->setProperty("roleName", roleLabel(entry.role));
            object->setProperty("style", entry.style);
            object->setProperty("intensity", static_cast<double>(entry.intensity));
            object->setProperty("valid", entry.valid);
            object->setProperty("deterministic", entry.deterministic);
            object->setProperty("summary", entry.summary);
            object->setProperty("hits", entry.hits);
            object->setProperty("crest", entry.crest);
            object->setProperty("activeCrest", DrumAnalysisSnapshot::numberOrNull(entry.activeCrest));
            object->setProperty("bassRatio", entry.bassRatio);
            object->setProperty("contrast", entry.contrast);
            object->setProperty("settings", DrumAnalysisSnapshot::settingsToVar(entry.settings));
            entries.add(juce::var(object));
        }
        juce::Array<juce::var> skipped;
        for (const auto& item : report.skipped)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("file", item.first);
            object->setProperty("reason", item.second);
            skipped.add(juce::var(object));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty("meta", juce::var(meta));
        root->setProperty("entries", entries);
        root->setProperty("skipped", skipped);
        return juce::var(root);
    }

    static juce::Result reportFromVar(const juce::var& value, ReplayReport& report)
    {
        auto* root = value.getDynamicObject();
        auto* meta = root != nullptr ? root->getProperty("meta").getDynamicObject() : nullptr;
        const auto* entries = root != nullptr ? root->getProperty("entries").getArray() : nullptr;
        if (meta == nullptr || entries == nullptr) return juce::Result::fail("not a replay report");
        report = {};
        report.label = meta->getProperty("label").toString();
        report.analysisVersion = static_cast<int>(meta->getProperty("analysisVersion"));
        report.profileVersion = static_cast<int>(meta->getProperty("profileVersion"));
        for (const auto& item : *entries)
        {
            auto* object = item.getDynamicObject();
            if (object == nullptr) return juce::Result::fail("invalid entry");
            ReplayEntry entry;
            entry.file = object->getProperty("file").toString();
            entry.role = juce::jlimit(0, 9, static_cast<int>(object->getProperty("role")));
            entry.style = object->getProperty("style").toString();
            entry.intensity = static_cast<float>(static_cast<double>(object->getProperty("intensity")));
            entry.valid = static_cast<bool>(object->getProperty("valid"));
            entry.deterministic = static_cast<bool>(object->getProperty("deterministic"));
            entry.summary = object->getProperty("summary").toString();
            entry.hits = static_cast<int>(object->getProperty("hits"));
            entry.crest = static_cast<double>(object->getProperty("crest"));
            const auto& activeCrest = object->getProperty("activeCrest");
            if (!activeCrest.isVoid()) entry.activeCrest = static_cast<double>(activeCrest);
            entry.bassRatio = static_cast<double>(object->getProperty("bassRatio"));
            entry.contrast = static_cast<double>(object->getProperty("contrast"));
            if (entry.file.isEmpty()) return juce::Result::fail("entry without file");
            if (const auto result = DrumAnalysisSnapshot::settingsFromVar(object->getProperty("settings"), entry.settings); result.failed())
                return result;
            report.entries.push_back(std::move(entry));
        }
        return juce::Result::ok();
    }

    static bool changed(float a, float b)
    {
        return std::abs(static_cast<double>(a) - b) > ReplayThresholds::sameTolerance * juce::jmax(1.0, std::abs(static_cast<double>(a)), std::abs(static_cast<double>(b)));
    }

    // How far a setting moved, in units of "worth a listen" (1 = at the threshold).
    static double listenScore(size_t control, float a, float b)
    {
        if (!changed(a, b)) return 0;
        const auto& spec = controlSpecs[control];
        if (spec.toggle) return 1.0;
        const auto delta = std::abs(static_cast<double>(b) - a);
        const auto ratio = delta / juce::jmax(1.0e-9, std::abs(static_cast<double>(a)), std::abs(static_cast<double>(b)));
        const juce::String unit(spec.unit);
        if (unit == "dB") return delta / ReplayThresholds::gainDb;
        if (unit == "Hz") return ratio / ReplayThresholds::frequencyRatio;
        return ratio / ReplayThresholds::otherRatio;
    }

    static Comparison compare(const ReplayReport& a, const ReplayReport& b)
    {
        Comparison result;
        result.labelA = a.label;
        result.labelB = b.label;
        result.countA = static_cast<int>(a.entries.size());
        result.countB = static_cast<int>(b.entries.size());
        std::map<juce::String, const ReplayEntry*> lookup;
        for (const auto& entry : b.entries) lookup[entry.file] = &entry;

        std::map<int, RateRow> roles;
        std::map<juce::String, RateRow> styles;
        const auto count = [&](bool sideA, const ReplayEntry& entry)
        {
            for (auto* row : { &result.overall, &roles[entry.role], &styles[entry.style] })
            {
                (sideA ? row->totalA : row->totalB) += 1;
                (sideA ? row->refusedA : row->refusedB) += entry.valid ? 0 : 1;
            }
        };
        for (const auto& entry : a.entries) count(true, entry);
        for (const auto& entry : b.entries) { count(false, entry); result.nondeterministic += entry.deterministic ? 0 : 1; }
        result.overall.name = "Total";
        for (auto& [role, row] : roles) { row.name = roleLabel(role); result.byRole.push_back(row); }
        for (auto& [style, row] : styles) { row.name = style; result.byStyle.push_back(row); }
        result.refusalAlert = std::abs(result.overall.percentB() - result.overall.percentA()) > ReplayThresholds::refusalPoints;

        struct Accumulator { double sum = 0, maxAbs = 0; int count = 0; juce::String file; };
        std::map<std::tuple<int, juce::String, size_t>, Accumulator> accumulators;
        for (const auto& before : a.entries)
        {
            const auto found = lookup.find(before.file);
            if (found == lookup.end()) { ++result.onlyA; continue; }
            const auto& after = *found->second;
            ++result.matched;
            if (before.valid != after.valid)
            {
                result.flips.push_back({ before.file, roleLabel(before.role), before.style, before.valid });
                result.listen.push_back({ before.file, roleLabel(before.role), before.style,
                    before.valid ? "valide puis refuse" : "refuse puis valide", 2.0 });
                continue;
            }
            if (!before.valid) continue;
            double best = 0;
            juce::String reason;
            for (size_t control = 0; control < before.settings.size(); ++control)
            {
                if (!changed(before.settings[control], after.settings[control])) continue;
                const auto delta = static_cast<double>(after.settings[control]) - before.settings[control];
                auto& accumulator = accumulators[std::make_tuple(before.role, before.style, control)];
                accumulator.sum += delta;
                ++accumulator.count;
                if (std::abs(delta) > accumulator.maxAbs) { accumulator.maxAbs = std::abs(delta); accumulator.file = before.file; }
                const auto score = listenScore(control, before.settings[control], after.settings[control]);
                if (score > best)
                {
                    best = score;
                    reason = juce::String(controlSpecs[control].id) + " " + (delta >= 0 ? "+" : "") + juce::String(delta, 2)
                        + " " + controlSpecs[control].unit;
                }
            }
            if (best >= 1.0) result.listen.push_back({ before.file, roleLabel(before.role), before.style, reason, best });
        }
        result.onlyB = result.countB - result.matched;
        for (const auto& [key, accumulator] : accumulators)
            result.modified.push_back({ roleLabel(std::get<0>(key)), std::get<1>(key), controlSpecs[std::get<2>(key)].id,
                accumulator.file, accumulator.sum / accumulator.count, accumulator.maxAbs, accumulator.count });
        std::stable_sort(result.listen.begin(), result.listen.end(),
            [](const Comparison::Listen& x, const Comparison::Listen& y) { return x.score > y.score; });
        if (static_cast<int>(result.listen.size()) > ReplayThresholds::listenCount)
            result.listen.resize(static_cast<size_t>(ReplayThresholds::listenCount));

        struct Rule { const char* name; int firstRole, lastRole; double threshold; double ReplayEntry::* field; };
        const std::array<Rule, 5> rules {{
            { "crest = 6 dB (refus / compresseur Snare, Toms)", 0, 5, 6.0, &ReplayEntry::crest },
            { "crest = 10 dB (compresseur Kick)", 0, 0, 10.0, &ReplayEntry::crest },
            { "bass/total = 0.15 (refus Kick)", 0, 0, 0.15, &ReplayEntry::bassRatio },
            { "contraste enveloppe = 12 dB (expander Kick)", 0, 0, 12.0, &ReplayEntry::contrast },
            { "contraste enveloppe = 18 dB (expander Snare, Toms)", 1, 5, 18.0, &ReplayEntry::contrast } }};
        for (const auto& rule : rules)
        {
            Comparison::Proximity row { rule.name, 0, 0 };
            for (const auto& entry : b.entries)
            {
                if (entry.role < rule.firstRole || entry.role > rule.lastRole) continue;
                ++row.analysed;
                if (std::abs(entry.*rule.field - rule.threshold) <= ReplayThresholds::nearThreshold * rule.threshold) ++row.closeCount;
            }
            result.proximity.push_back(row);
        }
        return result;
    }

    static juce::var comparisonToVar(const Comparison& comparison)
    {
        const auto rate = [](const RateRow& row)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("name", row.name);
            object->setProperty("totalA", row.totalA);
            object->setProperty("refusedA", row.refusedA);
            object->setProperty("totalB", row.totalB);
            object->setProperty("refusedB", row.refusedB);
            return juce::var(object);
        };
        auto* root = new juce::DynamicObject();
        root->setProperty("labelA", comparison.labelA);
        root->setProperty("labelB", comparison.labelB);
        root->setProperty("countA", comparison.countA);
        root->setProperty("countB", comparison.countB);
        root->setProperty("matched", comparison.matched);
        root->setProperty("onlyA", comparison.onlyA);
        root->setProperty("onlyB", comparison.onlyB);
        root->setProperty("nondeterministic", comparison.nondeterministic);
        root->setProperty("refusalAlert", comparison.refusalAlert);
        root->setProperty("overall", rate(comparison.overall));
        juce::Array<juce::var> roles, styles, flips, modified, proximity, listen;
        for (const auto& row : comparison.byRole) roles.add(rate(row));
        for (const auto& row : comparison.byStyle) styles.add(rate(row));
        for (const auto& flip : comparison.flips)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("file", flip.file);
            object->setProperty("role", flip.role);
            object->setProperty("style", flip.style);
            object->setProperty("validA", flip.validA);
            flips.add(juce::var(object));
        }
        for (const auto& item : comparison.modified)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("role", item.role);
            object->setProperty("style", item.style);
            object->setProperty("control", item.control);
            object->setProperty("mean", item.mean);
            object->setProperty("maxAbs", item.maxAbs);
            object->setProperty("maxFile", item.maxFile);
            object->setProperty("count", item.count);
            modified.add(juce::var(object));
        }
        for (const auto& item : comparison.proximity)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("name", item.name);
            object->setProperty("analysed", item.analysed);
            object->setProperty("closeCount", item.closeCount);
            proximity.add(juce::var(object));
        }
        for (const auto& item : comparison.listen)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("file", item.file);
            object->setProperty("role", item.role);
            object->setProperty("style", item.style);
            object->setProperty("reason", item.reason);
            object->setProperty("score", item.score);
            listen.add(juce::var(object));
        }
        root->setProperty("byRole", roles);
        root->setProperty("byStyle", styles);
        root->setProperty("flips", flips);
        root->setProperty("modified", modified);
        root->setProperty("proximity", proximity);
        root->setProperty("listen", listen);
        return juce::var(root);
    }

    static juce::String toMarkdown(const Comparison& c)
    {
        juce::String text;
        const auto line = [&text](const juce::String& value) { text << value << "\n"; };
        const auto rateLine = [&](const RateRow& row, bool alert)
        {
            const auto difference = row.percentB() - row.percentA();
            line("| " + row.name + " | " + juce::String(row.refusedA) + "/" + juce::String(row.totalA) + " ("
                + juce::String(row.percentA(), 0) + " %) | " + juce::String(row.refusedB) + "/" + juce::String(row.totalB) + " ("
                + juce::String(row.percentB(), 0) + " %) | " + (difference >= 0 ? "+" : "") + juce::String(difference, 0)
                + " points" + (alert ? " ALERTE" : "") + " |");
        };
        line("# Comparaison : " + c.labelA + " (A) contre " + c.labelB + " (B)");
        line("");
        line("Analyses : A " + juce::String(c.countA) + ", B " + juce::String(c.countB) + ", communes " + juce::String(c.matched)
            + ", seulement A " + juce::String(c.onlyA) + ", seulement B " + juce::String(c.onlyB));
        line("");
        line("## Refus");
        line("");
        line("| | A | B | difference |");
        line("|---|---|---|---|");
        rateLine(c.overall, c.refusalAlert);
        for (const auto& row : c.byRole) rateLine(row, false);
        for (const auto& row : c.byStyle) rateLine(row, false);
        line("");
        line("## Proprietes");
        line("");
        line(c.nondeterministic == 0 ? "Determinisme (E08) : OK" : "Determinisme (E08) : ALERTE, " + juce::String(c.nondeterministic) + " analyses non deterministes");
        line("");
        line("## Analyses qui changent d'etat (valide / refuse)");
        line("");
        if (c.flips.empty()) line("Aucune.");
        for (const auto& flip : c.flips)
            line("- " + flip.file + " (" + flip.role + ", " + flip.style + ") : " + (flip.validA ? "valide puis refuse" : "refuse puis valide"));
        line("");
        line("## Reglages modifies");
        line("");
        if (c.modified.empty()) line("Aucun.");
        juce::String group;
        for (const auto& item : c.modified)
        {
            const auto title = item.role + ", " + item.style;
            if (title != group) { group = title; line(""); line("**" + title + "**"); line(""); }
            line("- " + item.control + " : moyenne " + (item.mean >= 0 ? "+" : "") + juce::String(item.mean, 2) + ", max "
                + juce::String(item.maxAbs, 2) + " (" + item.maxFile + "), " + juce::String(item.count) + " analyses");
        }
        line("");
        line("## Analyses proches d'un seuil (+-" + juce::String(ReplayThresholds::nearThreshold * 100, 0) + " %)");
        line("");
        for (const auto& item : c.proximity)
            line("- " + item.name + " : " + juce::String(item.closeCount) + " sur " + juce::String(item.analysed));
        line("");
        line("## A ecouter");
        line("");
        if (c.listen.empty()) line("Rien a ecouter : aucun reglage ne bouge au-dela des seuils.");
        for (const auto& item : c.listen)
            line("- " + item.file + " (" + item.role + ", " + item.style + ") : " + item.reason);
        return text;
    }
};
}
