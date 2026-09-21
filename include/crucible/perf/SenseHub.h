#pragma once

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/safety/_Borrowed.h>
#include <crucible/safety/_Refined.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

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

static_assert(sizeof(Snapshot) == NUM_COUNTERS * sizeof(uint64_t),
              "Snapshot must be a tight 96*u64 = 768B = 12 cache lines; "
              "mmap contract with BPF_F_MMAPABLE array map depends on it");

class SenseHub {
public:
    // Returns nullopt when the kernel refuses the load: no CAP_BPF, a
    // tracepoint this kernel does not carry, or a verifier rejection.
    // A diagnostic line goes to stderr unless CRUCIBLE_PERF_QUIET=1 is
    // set in the environment.
    [[nodiscard]] static std::optional<SenseHub> load(::crucible::effects::Init) noexcept;

    [[nodiscard]] Snapshot read() const noexcept;

    [[nodiscard]] safety::Borrowed<const volatile uint64_t, SenseHub> counters_view() const noexcept;

    // An unattachable tracepoint drops one program silently, so these
    // two counts report how much of the map is live.
    [[nodiscard]] safety::Refined<safety::bounded_above<64>, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to name the
    // subsystems that stayed dark.
    [[nodiscard]] safety::Refined<safety::bounded_above<64>, std::size_t> attach_failures() const noexcept;

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
// An Init-capability context cannot reach this surface.  The permitted
// row of that capability is Row<Init, Alloc, IO> and carries no Block,
// so a startup context widens no further than IO and the gate is
// unsatisfiable there.  A background context permits all three atoms
// and widens to them.

using sense_hub_required_row =
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO,
                             ::crucible::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSenseHubMint = ::crucible::effects::IsExecCtx<Ctx>
                           && ::crucible::effects::Subrow<sense_hub_required_row, typename Ctx::row_type>;

// These grants classify the privileged syscalls the load path issues.
// They do not tighten the effect row.  The row gate above does that.
using mint_sense_hub_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::bpf>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::perf_event_open>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mmap>>;

namespace detail::v179_sense_hub_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::bpf>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::perf_event_open>>
              == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mmap>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(std::tuple_size_v<mint_sense_hub_syscall_grants> == 3,
              "mint_sense_hub_syscall_grants must list exactly 3 syscalls.  "
              "A syscall added to SenseHub::load() needs one more "
              "per<SyscallId::X> in the tuple, an append-only SyscallId "
              "enumerator, and a family_tier_v check here.");
}  // namespace detail::v179_sense_hub_grant_check

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSenseHubMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the kernel counter
// array and heap-allocates State.  Compile-time evaluation would lie
// about the runtime cost.
[[nodiscard]] inline std::optional<SenseHub> mint_sense_hub(Ctx const&, ::crucible::effects::Init init) noexcept {
    return SenseHub::load(init);
}

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSenseHubMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsSenseHubMint<::crucible::effects::BgCompileCtx>);
static_assert(!CtxFitsSenseHubMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsSenseHubMint<::crucible::effects::HotFgCtx>);
static_assert(CtxFitsSenseHubMint<::crucible::effects::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(!::crucible::effects::Subrow<sense_hub_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Init>>,
              "The initialization capability must never permit every atom this gate demands.  It omits "
              "Block because a startup scope must not wait, and the BPF program load waits on the "
              "kernel verifier.  No widening rescues an initialization context.");
static_assert(::crucible::effects::Subrow<sense_hub_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
