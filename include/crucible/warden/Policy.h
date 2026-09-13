#pragma once

#include "CpuTopology.h"

#include <cstdint>

namespace crucible::warden {

// The two real-time classes need a privilege the process may not have:
// either the capability to raise scheduling priority, or a resource
// limit that permits it.
enum class SchedClass : uint8_t {
    Other,  // the default, time-shared
    Batch,  // time-shared, preempted less often
    Idle,  // runs only when the CPU is otherwise idle
    Fifo,  // real-time, runs until it yields or a stronger task arrives
    RoundRobin,  // real-time, with a time slice shared among equals
    Deadline,  // admitted against a runtime, deadline and period
};

enum class ThreadClass : uint8_t {
    Hot,  // dispatch and compiled replay, one per compute device
    Warm,  // compilation, graph building and planning
    Cold,  // everything periodic
};

// The choice taken when a privileged knob cannot be set, because the
// capability is missing, the kernel file is read-only, or the kernel is
// older than the knob.
enum class OnMissingCap : uint8_t {
    DegradeAndWarn,  // log a warning, continue with whatever worked
    Strict,  // refuse to apply; return an error
};

struct Policy {
    // The master switch. When it is false, applying the policy changes
    // nothing at all.
    bool hot_enabled = true;
    SchedClass hot_sched = SchedClass::Other;
    // Nanoseconds. The kernel admits a deadline thread only while the
    // runtime-to-period ratios of all such threads sum to at most one.
    uint64_t hot_runtime_ns = 500'000;
    uint64_t hot_deadline_ns = 1'000'000;
    uint64_t hot_period_ns = 1'000'000;
    // 1 through 99, where higher is more urgent.
    int hot_rt_priority = 50;
    CoreSelector hot_core = {};

    // Niceness, where a higher value yields a smaller share of the CPU.
    int warm_nice = 0;
    int cold_nice = 10;

    // Whether to lock the registered hot regions into memory at all.
    // Which regions those are is decided where they are registered.
    bool mlock_hot_regions = true;

    // Advise the kernel to back the large, mostly static mappings with
    // huge pages.
    bool thp_hint_pools = true;

    // Opt the process out of the background huge-page collapse daemon.
    // Advising a specific region still works afterwards. Only the
    // daemon is silenced.
    bool disable_thp_global = false;

    // Build huge pages at once rather than waiting for the daemon.
    // Linux 6.1 and later. Older kernels ignore it.
    bool thp_collapse_now = false;

    // Whether the page tables of the hot regions were populated before
    // measurement began. The walk itself happens where each region is
    // initialized, so this records that the phase ran.
    bool prefault_hot_regions = true;

    // Pin the hot core to one frequency by writing its minimum and
    // maximum to the same value, which needs write access to the
    // frequency-scaling files.
    bool lock_frequency = false;
    // Keep the core out of the deeper idle states.
    bool disable_c_states = false;

    // Let the persistence writer poll its submission queue in the
    // kernel rather than issuing a syscall per submission.
    bool io_uring_sqpoll = true;
    // Use remote direct memory access for inter-node traffic where the
    // hardware offers it, falling back to sockets where it does not.
    bool rdma_for_comm = true;

    // The rolling window the deadline watchdog measures over, and the
    // number of misses it tolerates inside one window.
    uint32_t deadline_miss_budget = 10;
    uint32_t watchdog_window_sec = 60;

    OnMissingCap on_missing_capability = OnMissingCap::DegradeAndWarn;

    // A well-provisioned cluster node, where every knob is available.
    // Still degrades rather than refusing, so a node that grants less
    // than expected starts anyway and reports itself diminished.
    [[nodiscard]] static constexpr Policy production() noexcept {
        Policy p;
        p.hot_enabled = true;
        p.hot_sched = SchedClass::Deadline;
        p.mlock_hot_regions = true;
        p.thp_hint_pools = true;
        p.disable_thp_global = true;
        p.thp_collapse_now = true;
        p.lock_frequency = true;
        p.disable_c_states = true;
        p.io_uring_sqpoll = true;
        p.rdma_for_comm = true;
        p.on_missing_capability = OnMissingCap::DegradeAndWarn;
        return p;
    }

    // A developer machine, pinned but otherwise unobtrusive. The
    // deadline class is deliberately absent: a spinning thread under it
    // can wedge a workstation.
    [[nodiscard]] static constexpr Policy dev_quiet() noexcept {
        Policy p;
        p.hot_enabled = true;
        p.hot_sched = SchedClass::Other;
        p.mlock_hot_regions = true;  // usually permitted without privilege
        p.thp_hint_pools = false;  // leave the decision to the kernel
        p.disable_thp_global = false;
        p.thp_collapse_now = false;
        p.lock_frequency = false;  // respect the chosen governor
        p.disable_c_states = false;  // and the battery
        p.io_uring_sqpoll = false;
        p.rdma_for_comm = false;
        p.on_missing_capability = OnMissingCap::DegradeAndWarn;
        return p;
    }

    // A guest on a host somebody else owns. Anything needing privilege
    // over the physical machine is skipped, because it is unavailable:
    // frequency, idle states, interrupt steering, CPU isolation.
    //
    // What still works inside a guest is pinning within the guest, page
    // locking against the guest pager, huge-page advice, and kernel-side
    // submission polling.
    //
    // The first-in-first-out class rather than the deadline class: the
    // kernel would admit the deadline, but the host can preempt the
    // whole virtual CPU whenever it likes, which makes that admission a
    // promise nothing can keep.
    [[nodiscard]] static constexpr Policy cloud_vm() noexcept {
        Policy p;
        p.hot_enabled = true;
        p.hot_sched = SchedClass::Fifo;
        p.hot_rt_priority = 50;
        p.mlock_hot_regions = true;
        p.thp_hint_pools = true;
        p.disable_thp_global = false;  // do not fight the guest policy
        p.thp_collapse_now = false;  // unreliable inside a guest
        p.lock_frequency = false;  // not permitted
        p.disable_c_states = false;  // not permitted
        p.io_uring_sqpoll = true;
        p.rdma_for_comm = true;  // offered by some instance types
        p.on_missing_capability = OnMissingCap::DegradeAndWarn;
        return p;
    }

    // Change nothing, which is what reproducing a bug against an
    // untouched system needs.
    [[nodiscard]] static constexpr Policy none() noexcept {
        Policy p;
        p.hot_enabled = false;
        p.mlock_hot_regions = false;
        p.thp_hint_pools = false;
        p.disable_thp_global = false;
        p.thp_collapse_now = false;
        p.lock_frequency = false;
        p.disable_c_states = false;
        p.prefault_hot_regions = false;
        p.io_uring_sqpoll = false;
        p.rdma_for_comm = false;
        return p;
    }
};

// The whole structure travels by value. A field that pushes it past
// this bound probably belongs in a configuration of its own.
static_assert(sizeof(Policy) < 256);

}  // namespace crucible::warden
