#pragma once

#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>

#include <cstddef>

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

    [[nodiscard]] constexpr std::size_t attached_count() const noexcept {
        return (sense_hub_attached ? 1u : 0u) + (sched_switch_attached ? 1u : 0u) + (pmu_sample_attached ? 1u : 0u)
             + (lock_contention_attached ? 1u : 0u) + (syscall_latency_attached ? 1u : 0u)
             + (sched_tp_btf_attached ? 1u : 0u) + (syscall_tp_btf_attached ? 1u : 0u);
    }
};

class Senses {
public:
    // Each load takes the startup load context.  Its row carries Block,
    // because each program load waits in the kernel while the verifier
    // examines the program.
    [[nodiscard]] static Senses load_all(::fixy::InitLoadCtx const&) noexcept;

    [[nodiscard]] static Senses load_subset(::fixy::InitLoadCtx const&, SensesMask which) noexcept;

    // Each accessor gives a borrow of its facade, or none when the facade
    // did not load, when the Senses is moved from, or in a build with no
    // BPF substrate.  The borrow lives as long as this Senses.
    [[nodiscard]] ::fixy::Option<::fixy::Ref<SenseHub const>> sense_hub() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<SchedSwitch const>> sched_switch() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<PmuSample const>> pmu_sample() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<LockContention const>> lock_contention() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<SyscallLatency const>> syscall_latency() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<SchedTpBtf const>> sched_tp_btf() const noexcept;
    [[nodiscard]] ::fixy::Option<::fixy::Ref<SyscallTpBtf const>> syscall_tp_btf() const noexcept;

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
    //
    // Only a build with the BPF substrate defines the facades.  In a build
    // with no BPF substrate, State holds no facade.  Each load then gives a
    // Senses that attaches nothing, as a host that refuses each load does.
    struct State {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        ::fixy::Option<SenseHub> sense_hub{};
        ::fixy::Option<SchedSwitch> sched_switch{};
        ::fixy::Option<PmuSample> pmu_sample{};
        ::fixy::Option<LockContention> lock_contention{};
        ::fixy::Option<SyscallLatency> syscall_latency{};
        ::fixy::Option<SchedTpBtf> sched_tp_btf{};
        ::fixy::Option<SyscallTpBtf> syscall_tp_btf{};
#endif
    };
    // A State that holds a facade only moves, so the move of its Option
    // empties the source, and a moved-from Senses owns no facade.
    ::fixy::Option<State> state_{};

    explicit Senses(::fixy::Option<State> state) noexcept;
};

}  // namespace crucible::perf
