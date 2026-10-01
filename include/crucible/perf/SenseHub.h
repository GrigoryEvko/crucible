#pragma once

#include <fixy/Borrowed.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Counter indices must match the index enum of the kernel-side BPF
// program that fills the map.  The gaps in the numbering are reserved
// slots that read zero and keep each subsystem on its own cache line.
enum Idx : uint32_t {
    NET_TCP_ESTABLISHED = 0,
    NET_TCP_LISTEN = 1,
    NET_TCP_TIME_WAIT = 2,
    NET_TCP_CLOSE_WAIT = 3,
    NET_TCP_OTHER = 4,
    NET_UDP_ACTIVE = 5,
    NET_UNIX_ACTIVE = 6,
    NET_TX_BYTES = 7,

    NET_RX_BYTES = 8,
    FD_CURRENT = 9,
    FD_OPEN_OPS = 10,
    IO_READ_BYTES = 11,
    IO_WRITE_BYTES = 12,
    IO_READ_OPS = 13,
    IO_WRITE_OPS = 14,
    MEM_MMAP_COUNT = 15,

    MEM_MUNMAP_COUNT = 16,
    MEM_PAGE_FAULTS_MIN = 17,
    MEM_PAGE_FAULTS_MAJ = 18,
    MEM_BRK_CALLS = 19,
    SCHED_CTX_VOL = 20,
    SCHED_CTX_INVOL = 21,
    SCHED_MIGRATIONS = 22,
    SCHED_RUNTIME_NS = 23,

    SCHED_WAIT_NS = 24,
    FUTEX_WAIT_COUNT = 25,
    FUTEX_WAIT_NS = 26,
    THREADS_CREATED = 27,
    SCHED_SLEEP_NS = 28,
    SCHED_IOWAIT_NS = 29,
    SCHED_BLOCKED_NS = 30,
    WAKEUPS_RECEIVED = 31,

    KERNEL_LOCK_COUNT = 32,
    KERNEL_LOCK_NS = 33,
    SOFTIRQ_STOLEN_NS = 34,
    THREADS_EXITED = 35,
    CPU_FREQ_CHANGES = 36,
    WAKEUPS_SENT = 37,

    RSS_ANON_BYTES = 40,
    RSS_FILE_BYTES = 41,
    RSS_SWAP_ENTRIES = 42,
    RSS_SHMEM_BYTES = 43,
    DIRECT_RECLAIM_COUNT = 44,
    DIRECT_RECLAIM_NS = 45,
    SWAP_OUT_PAGES = 46,
    THP_COLLAPSE_OK = 47,

    THP_COLLAPSE_FAIL = 48,
    NUMA_MIGRATE_PAGES = 49,
    COMPACTION_STALLS = 50,
    EXTFRAG_EVENTS = 51,
    DISK_READ_BYTES = 52,
    DISK_WRITE_BYTES = 53,
    DISK_IO_LATENCY_NS = 54,
    DISK_IO_COUNT = 55,

    PAGE_CACHE_MISSES = 56,
    READAHEAD_PAGES = 57,
    WRITE_THROTTLE_JIFFIES = 58,
    IO_UNPLUG_COUNT = 59,
    TCP_RETRANSMIT_COUNT = 60,
    TCP_RST_SENT = 61,
    TCP_ERROR_COUNT = 62,
    SKB_DROP_COUNT = 63,

    TCP_MIN_SRTT_US = 64,
    TCP_MAX_SRTT_US = 65,
    TCP_LAST_CWND = 66,
    TCP_CONG_LOSS = 67,
    SIGNAL_FATAL_COUNT = 68,
    SIGNAL_LAST_SIGNO = 69,
    OOM_KILLS_SYSTEM = 70,
    OOM_KILL_US = 71,

    RECLAIM_STALL_LOOPS = 72,
    THERMAL_MAX_TRIP = 73,
    MCE_COUNT = 74,

    NUM_COUNTERS = 96,
};

struct Snapshot {
    std::array<uint64_t, NUM_COUNTERS> counters{};

    [[nodiscard]] uint64_t operator[](Idx i) const noexcept { return counters[static_cast<uint32_t>(i)]; }

    // Most counters accumulate, so b - a is the delta over the run.
    // The slots below are instantaneous gauges: the kernel-side program
    // sets or maxes them on each event instead of adding, so post < pre
    // is physically possible when a connection closes or RSS shrinks.
    //
    //   FD_CURRENT, TCP_{MIN,MAX}_SRTT_US, TCP_LAST_CWND,
    //   THERMAL_MAX_TRIP, SIGNAL_LAST_SIGNO, OOM_KILL_US,
    //   RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE,
    //   NET_UNIX_ACTIVE, RSS_{ANON,FILE,SHMEM}_BYTES, RSS_SWAP_ENTRIES
    //
    // Unsigned subtraction would wrap those to near 2^64, so the
    // difference saturates at zero and a gauge that decreased reads as
    // no delta.  Read the true gauge value from the later snapshot.
    //
    // std::sub_sat states this directly, but libstdc++ does not define
    // __cpp_lib_saturation_arithmetic yet.
    [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
        Snapshot r;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            uint64_t diff = 0;
            if (__builtin_sub_overflow(counters[i], older.counters[i], &diff)) [[unlikely]] {
                diff = 0;
            }
            r.counters[i] = diff;
        }
        return r;
    }
};

class SenseHub {
public:
    // Returns nullopt when the kernel refuses the load: no CAP_BPF, a
    // tracepoint this kernel does not carry, or a verifier rejection.
    // A diagnostic line goes to stderr unless CRUCIBLE_PERF_QUIET=1 is
    // set in the environment.
    //
    // The load takes the startup load context.  Its row carries Block,
    // because the load waits in the kernel while the verifier examines
    // the program.
    [[nodiscard]] static std::optional<SenseHub> load(::fixy::InitLoadCtx const&) noexcept;

    [[nodiscard]] Snapshot read() const noexcept;

    [[nodiscard]] ::fixy::Borrowed<const volatile uint64_t, SenseHub> counters_view() const noexcept;

    // An unattachable tracepoint drops one program silently, so these
    // two counts report how much of the map is live.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<64>, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to name the
    // subsystems that stayed dark.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<64>, std::size_t> attach_failures() const noexcept;

    SenseHub(const SenseHub&) = delete("SenseHub owns unique BPF object + mmap — copying would double-close");
    SenseHub&
    operator=(const SenseHub&) = delete("SenseHub owns unique BPF object + mmap — copying would double-close");
    SenseHub(SenseHub&&) noexcept;
    SenseHub& operator=(SenseHub&&) noexcept;
    ~SenseHub();

private:
    struct State;
    SenseHub() noexcept;

    std::unique_ptr<State> state_;
};

// The load issues bpf() and perf_event_open() and maps the counter
// array.  The bpf(BPF_PROG_LOAD) call enters the kernel and waits while
// the verifier walks the program.  The wait is why the row carries
// Block.  IO covers the bpf, perf_event_open and mmap traffic.  Alloc
// covers the state the load path takes from the heap.
//
// The startup load context and the background load context each claim
// Block on top of Alloc and IO, so each passes this gate.  The cold init
// context and the compile context stop at IO, and the gate refuses
// both.  The second argument is the startup load context that the load
// takes.

using sense_hub_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSenseHubMint = ::foundation::effects::IsExecCtx<Ctx>
                           && ::foundation::effects::Subrow<sense_hub_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSenseHubMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the kernel counter
// array and heap-allocates State.  Compile-time evaluation would lie
// about the runtime cost.
[[nodiscard]] inline std::optional<SenseHub> mint_sense_hub(Ctx const&, ::fixy::InitLoadCtx const& init) noexcept {
    return SenseHub::load(init);
}

}  // namespace crucible::perf
