#include <crucible/perf/LockContention.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char lock_contention_bpf_bytecode[];
extern const unsigned int lock_contention_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct LockContentionRingTag {};
}  // namespace

struct LockContention::State : detail::RingState<LockContentionRingTag, TimelineLockEvent> {};

LockContention::LockContention() noexcept = default;
LockContention::LockContention(LockContention&&) noexcept = default;
LockContention& LockContention::operator=(LockContention&&) noexcept = default;
LockContention::~LockContention() = default;

std::optional<LockContention> LockContention::load(::fixy::InitLoadCtx const& ctx) noexcept {
    // The attach is all-or-nothing.  With the enter tracepoint attached but
    // the exit one missing, wait-start entries fill the map to its capacity,
    // and contention data is lost silently past that point.
    const detail::RingSpec spec{
        .load = {.facade = "lock_contention",
                 .object_name = "crucible_lock_contention",
                 .bytecode = std::span{lock_contention_bpf_bytecode,
                                       static_cast<std::size_t>(lock_contention_bpf_bytecode_len)},
                 .min_attached = 2,
                 .attach_advice = "expected 2 tracepoint attachments (sys_enter_futex and sys_exit_futex), got "
                                  "fewer (kernel missing futex syscall tracepoints, or partial CAP_BPF rejection)"},
        .timeline_map = "lock_timeline",
        .counter_map = "lock_wait_count",
        .counter_fallback = "wait_count() returns 0",
    };
    auto state = detail::load_ring<State>(ctx, spec);
    if (state == nullptr) return std::nullopt;
    LockContention hub;
    hub.state_ = std::move(state);
    return hub;
}

uint64_t LockContention::wait_count() const noexcept { return state_ != nullptr ? state_->count() : 0; }

::fixy::Borrowed<const TimelineLockEvent, LockContention> LockContention::timeline_view() const noexcept {
    return state_ != nullptr ? state_->events<LockContention>()
                             : ::fixy::Borrowed<const TimelineLockEvent, LockContention>{};
}

uint64_t LockContention::timeline_write_index() const noexcept {
    return state_ != nullptr ? state_->write_index() : 0;
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> LockContention::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> LockContention::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

LockContention::Snapshot LockContention::snapshot() const noexcept {
    return Snapshot{
        .wait_count = wait_count(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
