#include <crucible/perf/LockContention.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <utility>

extern "C" {
extern const unsigned char lock_contention_bpf_bytecode[];
extern const unsigned int lock_contention_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct LockContentionRingTag {};
}  // namespace

struct LockContention::State : detail::RingState<LockContentionRingTag, TimelineLockEvent> {};

LockContention::LockContention(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
LockContention::LockContention(LockContention&&) noexcept = default;
LockContention& LockContention::operator=(LockContention&&) noexcept = default;
LockContention::~LockContention() = default;

::fixy::Option<LockContention> load_lock_contention(::fixy::InitLoadCtx const& ctx) noexcept {
    // The attach is all-or-nothing.  With the enter tracepoint attached but
    // the exit one missing, wait-start entries fill the map to its capacity,
    // and contention data is lost silently past that point.
    const detail::RingSpec spec{
        .load = {.facade = CRUCIBLE_PERF_FOREIGN_NAME("lock_contention"),
                 .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_lock_contention"),
                 .bytecode = {.abi = lock_contention_bpf_bytecode,
                              .size = static_cast<std::size_t>(lock_contention_bpf_bytecode_len)},
                 .min_attached = 2,
                 .attach_advice = "expected 2 tracepoint attachments (sys_enter_futex and sys_exit_futex), got "
                                  "fewer (kernel missing futex syscall tracepoints, or partial CAP_BPF rejection)"},
        .timeline_map = CRUCIBLE_PERF_FOREIGN_NAME("lock_timeline"),
        .counter_map = CRUCIBLE_PERF_FOREIGN_NAME("lock_wait_count"),
        .counter_fallback = "wait_count() returns 0",
    };
    return detail::load_ring<LockContention>(ctx, spec);
}

uint64_t LockContention::wait_count() const noexcept { return detail::ring_count(state_); }

::fixy::Borrowed<const TimelineLockEvent, LockContention> LockContention::timeline_view() const noexcept {
    return detail::ring_events<LockContention>(state_);
}

uint64_t LockContention::timeline_write_index() const noexcept { return detail::ring_write_index(state_); }

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> LockContention::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> LockContention::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

LockContention::Snapshot LockContention::snapshot() const noexcept {
    return Snapshot{
        .wait_count = wait_count(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
