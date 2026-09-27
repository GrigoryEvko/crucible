#include <crucible/perf/SyscallTpBtf.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char syscall_tp_btf_bpf_bytecode[];
extern const unsigned int syscall_tp_btf_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SyscallTpBtfRingTag {};
}  // namespace

struct SyscallTpBtf::State : detail::RingState<SyscallTpBtfRingTag, TimelineSyscallEvent> {};

SyscallTpBtf::SyscallTpBtf() noexcept = default;
SyscallTpBtf::SyscallTpBtf(SyscallTpBtf&&) noexcept = default;
SyscallTpBtf& SyscallTpBtf::operator=(SyscallTpBtf&&) noexcept = default;
SyscallTpBtf::~SyscallTpBtf() = default;

std::optional<SyscallTpBtf> SyscallTpBtf::load(::fixy::InitLoadCtx const& ctx) noexcept {
    // The attach is all-or-nothing.  With sys_enter attached but sys_exit
    // missing, every recorded start entry stays unconsumed until the LRU
    // hash map evicts it.
    const detail::RingSpec spec{
        .load = {.facade = "syscall_tp_btf",
                 .object_name = "crucible_syscall_tp_btf",
                 .bytecode = std::span{syscall_tp_btf_bpf_bytecode,
                                       static_cast<std::size_t>(syscall_tp_btf_bpf_bytecode_len)},
                 .probe_tracepoints = false,
                 .load_advice = "(apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel < 5.5, "
                                "CONFIG_DEBUG_INFO_BTF=n, or verifier rejected)",
                 .min_attached = 2,
                 .attach_advice = "expected 2 tp_btf attachments (sys_enter and sys_exit), got fewer (kernel "
                                  "missing BTF for sys_enter or sys_exit, or partial CAP_BPF rejection)"},
        .timeline_map = "syscall_timeline",
        .counter_map = "total_syscalls",
        .counter_fallback = "total_syscalls() returns 0",
    };
    auto state = detail::load_ring<State>(ctx, spec);
    if (state == nullptr) return std::nullopt;
    SyscallTpBtf hub;
    hub.state_ = std::move(state);
    return hub;
}

uint64_t SyscallTpBtf::total_syscalls() const noexcept { return state_ != nullptr ? state_->count() : 0; }

::fixy::Borrowed<const TimelineSyscallEvent, SyscallTpBtf> SyscallTpBtf::timeline_view() const noexcept {
    return state_ != nullptr ? state_->events<SyscallTpBtf>()
                             : ::fixy::Borrowed<const TimelineSyscallEvent, SyscallTpBtf>{};
}

uint64_t SyscallTpBtf::timeline_write_index() const noexcept {
    return state_ != nullptr ? state_->write_index() : 0;
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallTpBtf::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallTpBtf::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

SyscallTpBtf::Snapshot SyscallTpBtf::snapshot() const noexcept {
    return Snapshot{
        .total_syscalls = total_syscalls(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
