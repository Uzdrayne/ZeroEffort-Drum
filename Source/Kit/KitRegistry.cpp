#include "KitRegistry.h"

namespace kit
{
Registry::Registry()
{
    worker.startThread(juce::Thread::Priority::low);
}

Registry::~Registry()
{
    worker.stopThread(4000);
}

Member* Registry::join()
{
    std::lock_guard<std::mutex> guard(lock);
    for (auto& slot : members)
    {
        if (slot != nullptr && slot->active.load(std::memory_order_acquire)) continue;
        if (slot == nullptr)
        {
            slot = std::make_unique<Member>();
            for (auto& ring : slot->rings)
            {
                ring.reset(new float[static_cast<size_t>(Member::ringSize)]);
                std::fill(ring.get(), ring.get() + Member::ringSize, 0.0f);
            }
        }
        slot->writeEnd.store(std::numeric_limits<juce::int64>::min(), std::memory_order_release);
        slot->contiguousStart.store(std::numeric_limits<juce::int64>::min(), std::memory_order_release);
        slot->epoch.fetch_add(1, std::memory_order_acq_rel);
        slot->expectedNext = std::numeric_limits<juce::int64>::min();
        slot->channels.store(0);
        slot->rate.store(0);
        slot->hasAudio.store(false);
        slot->identity = {};
        slot->memberId.store(nextId++, std::memory_order_release);
        slot->active.store(true, std::memory_order_release);
        return slot.get();
    }
    return nullptr;
}

void Registry::leave(Member* member)
{
    if (member == nullptr) return;
    std::lock_guard<std::mutex> guard(lock);
    member->active.store(false, std::memory_order_release);
    member->writeEnd.store(std::numeric_limits<juce::int64>::min(), std::memory_order_release);
    member->epoch.fetch_add(1, std::memory_order_acq_rel);
    member->identity = {};
}

void Registry::setIdentity(Member& member, const Identity& identity)
{
    std::lock_guard<std::mutex> guard(lock);
    const auto& previous = member.identity;
    if (previous.group != identity.group || previous.instrument != identity.instrument
        || previous.position != identity.position || previous.role != identity.role || previous.key != identity.key)
    {
        auto& state = groups[static_cast<size_t>(previous.group)];
        if (state.report.find(member.id()) != nullptr
            || (state.analysing && std::find(state.sessionIds.begin(), state.sessionIds.end(), member.id()) != state.sessionIds.end()))
        {
            // Hide stale results immediately; only the worker disposes of an in-flight session.
            state.invalidateRequested = true;
            state.report = {};
            state.apply.report = {};
            balances[static_cast<size_t>(previous.group)].trims.clear();
            state.status = "Kit identity changed : analyze again";
            revisions[static_cast<size_t>(previous.group)].fetch_add(1);
        }
    }
    member.identity = identity;
    member.identity.group = juce::jlimit(0, groupCount - 1, identity.group);
}

void Registry::startAnalysis(int group)
{
    if (group < 0 || group >= groupCount) return;
    std::lock_guard<std::mutex> guard(lock);
    groups[static_cast<size_t>(group)].startRequested = true;
    groups[static_cast<size_t>(group)].stopRequested = false;
    groups[static_cast<size_t>(group)].status = "Preparing kit analysis";
    revisions[static_cast<size_t>(group)].fetch_add(1);
}

void Registry::stopAnalysis(int group)
{
    if (group < 0 || group >= groupCount) return;
    std::lock_guard<std::mutex> guard(lock);
    auto& state = groups[static_cast<size_t>(group)];
    state.startRequested = false;
    if (state.analysing) state.stopRequested = true;
}

juce::uint32 Registry::revision(int group) const noexcept
{
    return group >= 0 && group < groupCount ? revisions[static_cast<size_t>(group)].load() : 0;
}

void Registry::requestApply(int group, const std::vector<juce::uint32>& selected, bool reset)
{
    if (group < 0 || group >= groupCount) return;
    std::lock_guard<std::mutex> guard(lock);
    auto& state = groups[static_cast<size_t>(group)];
    state.apply.serial = applySerials[static_cast<size_t>(group)].load() + 1;
    state.apply.reset = reset;
    state.apply.members = selected;
    state.apply.report = state.report;
    applySerials[static_cast<size_t>(group)].store(state.apply.serial);
}

juce::uint32 Registry::applySerial(int group) const noexcept
{
    return group >= 0 && group < groupCount ? applySerials[static_cast<size_t>(group)].load() : 0;
}

Registry::ApplyRequest Registry::applyRequest(int group) const
{
    if (group < 0 || group >= groupCount) return {};
    std::lock_guard<std::mutex> guard(lock);
    return groups[static_cast<size_t>(group)].apply;
}

void Registry::requestBalance(int group, std::vector<std::pair<juce::uint32, float>> trims, bool undo)
{
    if (group < 0 || group >= groupCount) return;
    std::lock_guard<std::mutex> guard(lock);
    auto& request = balances[static_cast<size_t>(group)];
    request.serial = balanceSerials[static_cast<size_t>(group)].load() + 1;
    request.undo = undo;
    request.trims = std::move(trims);
    balanceSerials[static_cast<size_t>(group)].store(request.serial);
}

juce::uint32 Registry::balanceSerial(int group) const noexcept
{
    return group >= 0 && group < groupCount ? balanceSerials[static_cast<size_t>(group)].load() : 0;
}

Registry::BalanceRequest Registry::balanceRequest(int group) const
{
    if (group < 0 || group >= groupCount) return {};
    std::lock_guard<std::mutex> guard(lock);
    return balances[static_cast<size_t>(group)];
}

int Registry::memberCount() const
{
    std::lock_guard<std::mutex> guard(lock);
    int count = 0;
    for (const auto& member : members)
        if (member != nullptr && member->active.load()) ++count;
    return count;
}

GroupView Registry::view(int group) const
{
    GroupView result;
    if (group < 0 || group >= groupCount) return result;
    const auto now = juce::Time::getMillisecondCounter();
    std::lock_guard<std::mutex> guard(lock);
    const auto& state = groups[static_cast<size_t>(group)];    result.group = group;
    for (const auto& member : members)
        if (member != nullptr && member->active.load() && member->identity.group == group)
            result.members.push_back({ member->id(), member->identity, member->isLive(now), member->everPlayed(),
                member->channels.load(), member->rate.load() });
    result.analysing = !state.invalidateRequested && (state.analysing || state.startRequested);
    result.status = state.status;
    result.report = state.report;
    result.reportRevision = revisions[static_cast<size_t>(group)].load();
    return result;
}

std::vector<GroupMic> Registry::recentAudio(int group, const std::vector<Role>& roles, int maximumSamples, double& rate) const
{
    std::vector<GroupMic> result;
    rate = 0;
    if (group < 0 || group >= groupCount || maximumSamples < 1) return result;
    const auto now = juce::Time::getMillisecondCounter();
    std::lock_guard<std::mutex> guard(lock);
    std::vector<const Member*> chosen;
    for (const auto& member : members)
    {
        if (member == nullptr || !member->active.load() || member->identity.group != group || !member->isLive(now)) continue;
        if (std::find(roles.begin(), roles.end(), member->identity.role) == roles.end()) continue;
        const auto memberRate = member->rate.load(std::memory_order_relaxed);
        if (rate == 0) rate = memberRate;
        if (memberRate != rate) continue;
        chosen.push_back(member.get());
    }
    if (chosen.size() < 2) { rate = 0; return result; }
    auto end = std::numeric_limits<juce::int64>::max();
    auto first = std::numeric_limits<juce::int64>::min();
    for (const auto* member : chosen)
    {
        end = juce::jmin(end, member->writeEnd.load(std::memory_order_acquire));
        first = juce::jmax(first, member->contiguousStart.load(std::memory_order_acquire));
    }
    // Stay clear of the newest samples (the owners keep writing) and of the oldest (overwritten by the ring).
    end -= Member::writeMargin;
    auto start = juce::jmax(first, end - juce::jmin<juce::int64>(maximumSamples, Member::ringSize - 2 * Member::writeMargin));
    if (end <= start) { rate = 0; return result; }
    const auto count = static_cast<int>(end - start);
    for (const auto* member : chosen)
    {
        GroupMic mic;
        mic.name = member->identity.name;
        mic.delayMs = member->identity.delayMs;
        mic.inverted = member->identity.inverted;
        mic.role = member->identity.role;
        mic.id = member->id();
        mic.samples.assign(static_cast<size_t>(count), 0.0f);
        if (!member->read(2, start, count, mic.samples.data())) { result.clear(); rate = 0; return result; }
        result.push_back(std::move(mic));
    }
    return result;
}

void Registry::runOnce()
{
    std::lock_guard<std::mutex> work(workLock);
    const auto now = juce::Time::getMillisecondCounter();
    for (int group = 0; group < groupCount; ++group)
        tick(group, now);
}

void Registry::publish(int group, Report report, const juce::String& status, bool finished)
{
    std::lock_guard<std::mutex> guard(lock);
    auto& state = groups[static_cast<size_t>(group)];
    if (state.invalidateRequested) return;
    report.final = finished;
    state.report = std::move(report);
    state.status = status;
    if (finished)
    {
        state.analysing = false;
        state.session.reset();
        state.sessionMembers.clear();
    }
    revisions[static_cast<size_t>(group)].fetch_add(1);
}

void Registry::tick(int group, juce::uint32 now)
{
    auto& state = groups[static_cast<size_t>(group)];
    bool finishing = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (state.invalidateRequested)
        {
            state.invalidateRequested = false;
            state.analysing = false;
            state.status = "Kit identity changed : analyze again";
            state.session.reset();
            state.sessionMembers.clear();
            state.sessionIds.clear();
            state.inputs.clear();
        }
        if (state.stopRequested)
        {
            state.stopRequested = false;
            finishing = state.analysing;
        }
        if (state.startRequested)
        {
            state.startRequested = false;
            state.session.reset();
            state.sessionMembers.clear();
            state.sessionIds.clear();
            state.inputs.clear();
            for (auto& member : members)
                if (member != nullptr && member->active.load() && member->identity.group == group
                    && member->identity.role != Role::none)
                {
                    state.sessionMembers.push_back(member.get());
                    state.sessionIds.push_back(member->id());
                    TrackInput input;
                    input.id = member->id();
                    input.role = member->identity.role;
                    input.position = member->identity.position;
                    input.label = member->identity.name.isNotEmpty() ? member->identity.name : juce::String(roleName(input.role));
                    input.key = member->identity.key;
                    state.inputs.push_back(input);
                }
            state.frontier = std::numeric_limits<juce::int64>::min();
            state.startedMs = state.lastReportMs = state.lastTickMs = now;
            state.playedMs = state.liveSinceMs = 0;
            state.ignored = 0;
            state.report = {};
            state.analysing = state.sessionMembers.size() >= 2;
            state.status = state.analysing ? "Start project playback"
                : "Incomplete kit : at least two tracks in the same group";
            revisions[static_cast<size_t>(group)].fetch_add(1);
        }
        if (!state.analysing) return;
        for (size_t index = 0; index < state.sessionMembers.size(); ++index)
        {
            const auto* member = state.sessionMembers[index];
            if (!member->active.load() || member->id() != state.sessionIds[index]
                || member->identity.role != state.inputs[index].role || member->identity.group != group)
            {
                state.analysing = false;
                state.session.reset();
                state.sessionMembers.clear();
                state.status = "Kit changed during analysis : restart it";
                revisions[static_cast<size_t>(group)].fetch_add(1);
                return;
            }
        }
    }

    const auto elapsed = now - state.lastTickMs;
    state.lastTickMs = now;
    const auto& sessionMembers = state.sessionMembers;
    if (state.session == nullptr)
    {
        // Only instances that actually play take part. Instances that never received timeline
        // audio (host editor instances, tracks of another project) are ignored immediately;
        // instances that played earlier but are silent now get a short grace period.
        int live = 0, waiting = 0;
        double rate = 0;
        bool mismatch = false;
        for (const auto* member : sessionMembers)
        {
            if (member->isLive(now))
            {
                ++live;
                const auto memberRate = member->rate.load();
                if (rate == 0) rate = memberRate;
                else if (std::abs(rate - memberRate) > 0.5) mismatch = true;
            }
            else if (member->everPlayed())
                ++waiting;
        }
        if (mismatch)
        {
            publish(group, {}, "Different sample rates in the kit", true);
            return;
        }
        if (live < 2)
        {
            state.liveSinceMs = 0;
            if (finishing) publish(group, {}, "Analysis cancelled", true);
            else
            {
                std::lock_guard<std::mutex> guard(lock);
                state.status = "Start project playback (" + juce::String(live) + " kit track(s) playing)";
            }
            return;
        }
        if (state.liveSinceMs == 0) state.liveSinceMs = now;
        if (waiting > 0 && now - state.liveSinceMs < 1500 && !finishing)
        {
            std::lock_guard<std::mutex> guard(lock);
            state.status = "Playback received on " + juce::String(live) + " tracks, waiting for the others...";
            return;
        }
        if (!(rate >= 8000.0 && rate <= 384000.0))
        {
            publish(group, {}, "Unknown sample rate : restart playback", true);
            return;
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            std::vector<Member*> playing;
            std::vector<juce::uint32> ids;
            std::vector<TrackInput> inputs;
            for (size_t index = 0; index < state.sessionMembers.size(); ++index)
                if (state.sessionMembers[index]->isLive(now))
                {
                    playing.push_back(state.sessionMembers[index]);
                    ids.push_back(state.sessionIds[index]);
                    inputs.push_back(state.inputs[index]);
                    inputs.back().channels = juce::jmax(1, state.sessionMembers[index]->channels.load());
                }
            state.ignored = static_cast<int>(state.sessionMembers.size() - playing.size());
            state.sessionMembers = std::move(playing);
            state.sessionIds = std::move(ids);
            state.inputs = std::move(inputs);
        }
        auto session = std::make_unique<KitSession>();
        if (state.sessionMembers.size() < 2 || !session->reset(state.inputs, rate))
        {
            publish(group, {}, "No analyzable pair : overheads (or Kick In + Kick Out, "
                "Snare Top + Bottom) are needed among the playing tracks", true);
            return;
        }
        state.session = std::move(session);
        state.epochs.assign(sessionMembers.size(), 0);
        for (size_t index = 0; index < sessionMembers.size(); ++index)
            state.epochs[index] = sessionMembers[index]->epoch.load();
        state.frontier = std::numeric_limits<juce::int64>::min();
    }

    auto& session = *state.session;
    auto end = std::numeric_limits<juce::int64>::max();
    auto first = std::numeric_limits<juce::int64>::min();
    int live = 0;
    bool restarted = false;
    for (size_t index = 0; index < sessionMembers.size(); ++index)
    {
        const auto* member = sessionMembers[index];
        if (!member->isLive(now)) continue;
        const auto memberEnd = member->writeEnd.load(std::memory_order_acquire);
        const auto memberStart = member->contiguousStart.load(std::memory_order_acquire);
        if (memberEnd == std::numeric_limits<juce::int64>::min() || memberEnd < memberStart) continue;
        ++live;
        end = juce::jmin(end, memberEnd);
        first = juce::jmax(first, memberStart);
        const auto epoch = member->epoch.load();
        if (epoch != state.epochs[index])
        {
            state.epochs[index] = epoch;
            restarted = true;
        }
    }
    if (live >= 2)
    {
        state.playedMs += elapsed;
        if (restarted || state.frontier == std::numeric_limits<juce::int64>::min() || state.frontier < first)
            state.frontier = first + session.lookback();
        const auto to = end - session.lookahead();
        const auto capacity = Member::ringSize - Member::writeMargin - session.lookahead() - session.lookback();
        if (to - state.frontier > capacity) state.frontier = to - capacity / 2;
        if (to > state.frontier)
        {
            const auto reader = [&sessionMembers](int track, int channel, juce::int64 start, int count, float* destination) {
                return track >= 0 && track < static_cast<int>(sessionMembers.size())
                    && sessionMembers[static_cast<size_t>(track)]->read(channel, start, count, destination);
            };
            session.process(state.frontier, to, reader);
            state.frontier = to;
        }
    }
    const auto progress = session.progress();
    const auto timeout = state.playedMs > 45000;
    const auto finished = finishing || progress >= 1.0f || timeout;
    if (finished || now - state.lastReportMs > 700)
    {
        state.lastReportMs = now;
        auto report = session.report();
        report.progress = progress;
        juce::String status;
        if (finished)
            status = report.valid ? "Kit analysis finished (" + juce::String(report.events) + " hits)"
                : "Analysis finished without a reliable measurement : play a passage with clear hits";
        else if (live < 2)
            status = "Playback stopped : resume playback to continue";
        else
            status = "Listening to the kit " + juce::String(juce::roundToInt(progress * 100)) + " % ("
                + juce::String(report.events) + " hits)";
        if (state.ignored > 0)
            status += " / " + juce::String(state.ignored) + " instance(s) not playing, ignored";
        publish(group, std::move(report), status, finished);
    }
}
}
