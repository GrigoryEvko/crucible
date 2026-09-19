#pragma once

#include <crucible/effects/_Capabilities.h>
#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace crucible::perf {

// The tp_btf facades attach only on a kernel built with
// CONFIG_DEBUG_INFO_BTF=y.  They hook the same kernel events as
// sched_switch and syscall_latency, so a mask that selects a BTF facade
// together with its counterpart records every event twice.

struct SensesMask {
    bool sense_hub : 1 = false;
    bool sched_switch : 1 = false;
    bool pmu_sample : 1 = false;
    bool lock_contention : 1 = false;
    bool syscall_latency : 1 = false;
    bool sched_tp_btf : 1 = false;
    bool syscall_tp_btf : 1 = false;

    [[nodiscard]] static constexpr SensesMask all() noexcept {
        return SensesMask{
            .sense_hub = true,
            .sched_switch = true,
            .pmu_sample = true,
            .lock_contention = true,
            .syscall_latency = true,
            .sched_tp_btf = true,
            .syscall_tp_btf = true,
        };
    }

    [[nodiscard]] constexpr bool any() const noexcept {
        return sense_hub || sched_switch || pmu_sample || lock_contention || syscall_latency || sched_tp_btf
            || syscall_tp_btf;
    }
};

struct CoverageReport {
    bool sense_hub_attached = false;
    bool sched_switch_attached = false;
    bool pmu_sample_attached = false;
    bool lock_contention_attached = false;
    bool syscall_latency_attached = false;
    bool sched_tp_btf_attached = false;
    bool syscall_tp_btf_attached = false;

    [[nodiscard]] std::size_t attached_count() const noexcept {
        return (sense_hub_attached ? 1u : 0u) + (sched_switch_attached ? 1u : 0u) + (pmu_sample_attached ? 1u : 0u)
             + (lock_contention_attached ? 1u : 0u) + (syscall_latency_attached ? 1u : 0u)
             + (sched_tp_btf_attached ? 1u : 0u) + (syscall_tp_btf_attached ? 1u : 0u);
    }
};

class Senses {
public:
    [[nodiscard]] static Senses load_all(::crucible::effects::Init) noexcept;

    [[nodiscard]] static Senses load_subset(::crucible::effects::Init, SensesMask which) noexcept;

    [[nodiscard]] const SenseHub* sense_hub() const noexcept;
    [[nodiscard]] const SchedSwitch* sched_switch() const noexcept;
    [[nodiscard]] const PmuSample* pmu_sample() const noexcept;
    [[nodiscard]] const LockContention* lock_contention() const noexcept;
    [[nodiscard]] const SyscallLatency* syscall_latency() const noexcept;
    [[nodiscard]] const SchedTpBtf* sched_tp_btf() const noexcept;
    [[nodiscard]] const SyscallTpBtf* syscall_tp_btf() const noexcept;

    [[nodiscard]] CoverageReport coverage() const noexcept;

    Senses(const Senses&) = delete("Senses owns 7 BPF objects + mmaps; copying would double-close");
    Senses& operator=(const Senses&) = delete("Senses owns 7 BPF objects + mmaps; copying would double-close");
    Senses(Senses&&) noexcept;
    Senses& operator=(Senses&&) noexcept;
    ~Senses() noexcept;

private:
    // State is inlined rather than held behind a pointer to heap
    // storage.  The load path is noexcept and cannot allocate.  This
    // header already declares all seven facade types, so a heap
    // indirection would hide no compile-time dependency either.
    struct State {
        std::optional<SenseHub> sense_hub;
        std::optional<SchedSwitch> sched_switch;
        std::optional<PmuSample> pmu_sample;
        std::optional<LockContention> lock_contention;
        std::optional<SyscallLatency> syscall_latency;
        std::optional<SchedTpBtf> sched_tp_btf;
        std::optional<SyscallTpBtf> syscall_tp_btf;
    };
    std::optional<State> state_;

    explicit Senses(std::optional<State>) noexcept;
};

}  // namespace crucible::perf
