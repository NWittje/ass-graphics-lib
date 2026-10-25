#pragma once
#include <atomic>
#include <cstdint>
namespace ag {
enum class ChoiceDecisionKind : std::uint32_t { none, accepted, cancelled };
struct ChoiceDecision { ChoiceDecisionKind kind{}; std::int32_t index{-1}; };
// Draft, allocation-free state. Native notification/input ordering must be
// qualified against a real CBS_DROPDOWNLIST before this can be shipped.
class ChoiceDecisionState {
    static inline std::atomic<std::uint64_t> next_ticket_{1};
    std::uint64_t ticket_{}, revision_{};
    std::int32_t opening_{-1};
    bool active_{}, closed_{}, resolved_{};
    void finish() noexcept { if (closed_ && resolved_) active_ = false; }
public:
    struct Input { std::uint64_t ticket{}, revision{}; std::int32_t index{-1}; bool eligible{}; };
    bool active() const noexcept { return active_; }
    std::uint64_t ticket() const noexcept { return ticket_; }
    void invalidate() noexcept {
        ticket_ = next_ticket_.fetch_add(1); ++revision_;
        active_ = closed_ = resolved_ = false;
    }
    void open(std::int32_t current) noexcept { invalidate(); opening_ = current; active_ = true; }
    ChoiceDecision accept(std::int32_t index) noexcept {
        if (!active_ || resolved_ || index < 0) return {};
        resolved_ = true; ++revision_; finish();
        return {ChoiceDecisionKind::accepted, index};
    }
    ChoiceDecision cancel() noexcept {
        if (!active_ || resolved_) return {};
        resolved_ = true; ++revision_; finish();
        return {ChoiceDecisionKind::cancelled, opening_};
    }
    // CLOSEUP never implies acceptance. Resolve missing native endings only
    // after native processing returns; no polling/timer/worker is introduced.
    void close() noexcept { if (active_) { closed_ = true; finish(); } }
    ChoiceDecision settle(std::uint64_t token, bool native_closed) noexcept {
        if (token != ticket_ || !active_ || !closed_ || !native_closed) return {};
        return cancel();
    }
    Input input(std::int32_t current, bool native_open) const noexcept {
        return {ticket_, revision_, current, !active_ && !native_open};
    }
    ChoiceDecision finish_input(const Input& before, std::int32_t current, bool native_open) noexcept {
        if (!before.eligible || native_open || active_ || before.ticket != ticket_ ||
            before.revision != revision_ || current < 0 || before.index == current) return {};
        ++revision_; return {ChoiceDecisionKind::accepted, current};
    }
};
} // namespace ag