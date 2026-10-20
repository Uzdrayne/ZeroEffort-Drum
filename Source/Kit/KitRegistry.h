#pragma once

#include "KitAnalysis.h"
#include "KitGroupMeasures.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace kit
{
// One plugin instance in the shared kit registry.
// The audio thread of its owner only calls write()/markIdle(); they are wait-free and never
// allocate. Readers (analysis thread) validate every copy with the epoch/writeEnd pair, so a
// torn read is discarded instead of being analysed.
class Member
{
public:
    static constexpr int ringBits = 17;
    static constexpr juce::int64 ringSize = juce::int64 { 1 } << ringBits;
    static constexpr juce::int64 writeMargin = 16384;

    juce::uint32 id() const noexcept { return memberId.load(std::memory_order_acquire); }

    void write(const float* const* channelData, int channelCount, int count, juce::int64 timeline, double sampleRate) noexcept
    {
        if (count <= 0 || channelData == nullptr || channelCount <= 0 || !active.load(std::memory_order_relaxed)) return;
        channelCount = juce::jmin(2, channelCount);
        if (count > writeMargin)
        {
            markIdle();
            return;
        }
        if (timeline != expectedNext || sampleRate != rate.load(std::memory_order_relaxed)
            || channelCount != channels.load(std::memory_order_relaxed))
        {
            // Publish an empty range before the epoch moves: a reader that observes the new epoch
            // can never pair it with the previous writeEnd while the ring is being rewritten.
            writeEnd.store(std::numeric_limits<juce::int64>::min(), std::memory_order_release);
            contiguousStart.store(timeline, std::memory_order_release);
            rate.store(sampleRate, std::memory_order_relaxed);
            channels.store(channelCount, std::memory_order_relaxed);
            epoch.fetch_add(1, std::memory_order_acq_rel);
        }
        auto* left = rings[0].get();
        auto* right = rings[1].get();
        const auto* first = channelData[0];
        const auto* second = channelData[channelCount > 1 ? 1 : 0];
        for (int index = 0; index < count; ++index)
        {
            const auto slot = static_cast<size_t>((timeline + index) & (ringSize - 1));
            left[slot] = first[index];
            right[slot] = second[index];
        }
        writeEnd.store(timeline + count, std::memory_order_release);
        expectedNext = timeline + count;
        lastWrite.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        hasAudio.store(true, std::memory_order_relaxed);
    }

    void markIdle() noexcept
    {
        if (expectedNext != std::numeric_limits<juce::int64>::min())
        {
            writeEnd.store(std::numeric_limits<juce::int64>::min(), std::memory_order_release);
            epoch.fetch_add(1, std::memory_order_acq_rel);
        }
        expectedNext = std::numeric_limits<juce::int64>::min();
    }

    bool read(int channel, juce::int64 start, int count, float* destination) const noexcept
    {
        const auto before = epoch.load(std::memory_order_acquire);
        const auto first = contiguousStart.load(std::memory_order_acquire);
        const auto end = writeEnd.load(std::memory_order_acquire);
        if (count <= 0 || end == std::numeric_limits<juce::int64>::min() || start < first || start + count > end
            || start < end + writeMargin - ringSize) return false;
        const auto* left = rings[0].get();
        const auto* right = rings[1].get();
        const auto stereo = channels.load(std::memory_order_relaxed) > 1;
        for (int index = 0; index < count; ++index)
        {
            const auto slot = static_cast<size_t>((start + index) & (ringSize - 1));
            destination[index] = channel == 0 || (!stereo && channel < 0) ? left[slot]
                : channel == 1 ? right[slot] : 0.5f * (left[slot] + right[slot]);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto after = epoch.load(std::memory_order_acquire);
        const auto latest = writeEnd.load(std::memory_order_acquire);
        return before == after && start >= latest + writeMargin - ringSize;
    }

    bool isLive(juce::uint32 now) const noexcept
    {
        // Signed difference: a write newer than `now` (taken earlier by the caller) is live.
        return writeEnd.load(std::memory_order_acquire) != std::numeric_limits<juce::int64>::min()
            && static_cast<juce::int32>(now - lastWrite.load(std::memory_order_relaxed)) < 600;
    }

    // False for instances that never processed timeline audio (e.g. host-side editor instances).
    bool everPlayed() const noexcept { return hasAudio.load(std::memory_order_relaxed); }

private:
    friend class Registry;
    std::array<std::unique_ptr<float[]>, 2> rings;
    std::atomic<bool> active { false };
    std::atomic<juce::uint32> memberId { 0 };
    std::atomic<juce::int64> writeEnd { std::numeric_limits<juce::int64>::min() };
    std::atomic<juce::int64> contiguousStart { std::numeric_limits<juce::int64>::min() };
    std::atomic<juce::uint32> epoch { 0 };
    std::atomic<int> channels { 0 };
    std::atomic<double> rate { 0 };
    std::atomic<juce::uint32> lastWrite { 0 };
    std::atomic<bool> hasAudio { false };
    juce::int64 expectedNext = std::numeric_limits<juce::int64>::min();
    Identity identity;  // guarded by Registry::lock
};

// Process-wide registry shared by every ZeroEffort Drum instance loaded in the host process
// (owned through juce::SharedResourcePointer). Kits are separated by a user-selected group; members
// that stop processing (other project, disabled track) are excluded by their activity time.
class Registry
{
public:
    Registry();
    ~Registry();

    Member* join();
    void leave(Member* member);
    void setIdentity(Member& member, const Identity& identity);
    void startAnalysis(int group);
    void stopAnalysis(int group);
    GroupView view(int group) const;
    // The most recent audio (at most `maximumSamples`, mono mean of each microphone) of the live members of one group whose role is in `roles`,
    // on the span all of them cover. Used by the sub-buses for their read-only group report; empty when fewer than two microphones are live.
    std::vector<GroupMic> recentAudio(int group, const std::vector<Role>& roles, int maximumSamples, double& rate) const;
    juce::uint32 revision(int group) const noexcept;

    // Kit-wide apply from the preview: every listed member applies its own recommendation
    // (or clears its correction when `reset`) on its message thread.
    struct ApplyRequest
    {
        juce::uint32 serial = 0;
        bool reset = false;
        std::vector<juce::uint32> members;
        Report report;
    };
    void requestApply(int group, const std::vector<juce::uint32>& members, bool reset);
    juce::uint32 applySerial(int group) const noexcept;
    ApplyRequest applyRequest(int group) const;
    // Balance trims (SB4): the sub-bus asks, after the user validated, every listed microphone instance to move its own input gain by
    // its trim, or (undo) to restore the gain it had before. Same hand-over as the kit apply request: each member reads it on its message thread.
    struct BalanceRequest
    {
        juce::uint32 serial = 0;
        bool undo = false;
        std::vector<std::pair<juce::uint32, float>> trims;   // member id, dB
    };
    void requestBalance(int group, std::vector<std::pair<juce::uint32, float>> trims, bool undo);
    juce::uint32 balanceSerial(int group) const noexcept;
    BalanceRequest balanceRequest(int group) const;
    void runOnce();
    int memberCount() const;

private:
    struct GroupState
    {
        bool startRequested = false, stopRequested = false, analysing = false, invalidateRequested = false;
        std::unique_ptr<KitSession> session;
        std::vector<Member*> sessionMembers;
        std::vector<juce::uint32> sessionIds, epochs;
        std::vector<TrackInput> inputs;
        juce::int64 frontier = std::numeric_limits<juce::int64>::min();
        juce::uint32 startedMs = 0, lastReportMs = 0, playedMs = 0, lastTickMs = 0, liveSinceMs = 0;
        int ignored = 0;
        Report report;
        ApplyRequest apply;
        juce::String status { "Ready" };
    };

    class Worker final : public juce::Thread
    {
    public:
        explicit Worker(Registry& registryIn) : juce::Thread("ZeroEffort Kit Analysis"), registry(registryIn) {}
        void run() override
        {
            while (!threadShouldExit())
            {
                registry.runOnce();
                wait(100);
            }
        }
    private:
        Registry& registry;
    };

    void tick(int group, juce::uint32 now);
    void publish(int group, Report report, const juce::String& status, bool finished);

    mutable std::mutex lock;
    std::mutex workLock;
    std::array<std::unique_ptr<Member>, maxMembers> members;
    std::array<GroupState, groupCount> groups;
    std::array<std::atomic<juce::uint32>, groupCount> revisions {};
    std::array<std::atomic<juce::uint32>, groupCount> applySerials {};
    std::array<BalanceRequest, groupCount> balances;
    std::array<std::atomic<juce::uint32>, groupCount> balanceSerials {};
    juce::uint32 nextId = 1;
    Worker worker { *this };
};
}
