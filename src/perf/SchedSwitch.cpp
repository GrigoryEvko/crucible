// crucible::perf::SchedSwitch — libbpf binding implementation.
//
// First per-program facade in the GAPS-004 BPF series (sibling to
// the keystone SenseHub aggregator).  Mirrors SenseHub.cpp's loader
// shape as closely as possible:
//   • 7-step Phase loop (parse ELF → rewrite .rodata → disable
//     unavailable programs → bpf_object__load → register our_tids →
//     attach programs → mmap timeline) maps 1:1 to SenseHub's
//     numbered phases, ordered identically.
//   • Same WriteOnce/WriteOnceNonNull/Tagged/Monotonic/NonMovable
//     wrapper toolkit, same anon-namespace source tags, same
//     env-var conventions.
//   • Same `report()` lambda shape, same libbpf_log_cb gate,
//     same install_libbpf_log_cb_once() once-flag.
//
// This duplication is INTENTIONAL — Promote-First architecture
// (CLAUDE.md / Mike Acton: generalize from ≥2 real cases).  When
// GAPS-004c (PmuSample) lands as the SECOND per-program facade,
// the duplication between the two loaders becomes informational
// for the GAPS-004x BpfLoader extraction.  Today we have ONE
// production loader (SenseHub) + ONE not-quite-twin (this one);
// the right number of skeletons before extracting an abstraction
// is two, not one.
//
// Design points specific to SchedSwitch (vs SenseHub):
//   • `cs_count` is a 1-element BPF_MAP_TYPE_ARRAY map, NOT
//     BPF_F_MMAPABLE.  Reading it is one bpf_map_lookup_elem
//     syscall (~1 µs).  The header documents the cost; future
//     work (or a kernel-side bytecode update) could promote it.
//   • `our_tids` is populated with the current TID at load time
//     (same as SenseHub).  A future GAPS-004x can iterate
//     /proc/self/task/* to populate ALL TIDs of our process.
//     Today: main thread only.
//   • `sched_timeline` IS BPF_F_MMAPABLE.  We mmap it read-only,
//     hand the events array out via Borrowed<>.  The 64-byte
//     header sits before events[]; readers grab the header
//     separately via timeline_write_index().

#include <crucible/perf/SchedSwitch.h>

// detail::BpfLoader brings the shared anonymous-namespace helpers
// (Tagged provenance typedefs, env-var caches, libbpf log control,
// .rodata + tracepoint discovery, libbpf_errno).  Pulling them via
// using-declarations below keeps every call site syntactically
// unchanged but eliminates the cross-facade duplication that 5 audit
// rounds repeatedly found drift in.  GAPS-004x context block in the
// header explains why this lives in `detail/` (TU-internal helpers,
// not a public API).
#include <crucible/perf/detail/BpfLoader.h>

#include <crucible/safety/Mutation.h>  // safety::WriteOnce / WriteOnceNonNull / Monotonic
#include <crucible/safety/Pinned.h>    // safety::NonMovable<T>

#include <sys/mman.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

// 8 slots — sched_switch.bpf.c contains exactly one program today,
// but the cap stays at the inplace_vector<...,8> shape used by
// SenseHub for uniformity.  GCC 16 ships std::inplace_vector
// unconditionally.
#include <inplace_vector>

extern "C" {
extern const unsigned char sched_switch_bpf_bytecode[];
extern const unsigned int  sched_switch_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// All shared helpers live in ::crucible::perf::detail (BpfLoader.h).
// Pull them into the anonymous namespace so call sites below remain
// syntactically identical to the pre-extraction code.  Anon-namespace
// using-declarations have an implicit using-directive into the
// enclosing namespace (per [basic.namespace]/4) so members of struct
// State below also see Fd / Tgid / Tid via name lookup.
namespace source = ::crucible::perf::detail::source;
using ::crucible::perf::detail::Tgid;
using ::crucible::perf::detail::Tid;
using ::crucible::perf::detail::Fd;
using ::crucible::perf::detail::current_tgid;
using ::crucible::perf::detail::current_tid;
using ::crucible::perf::detail::map_fd;
using ::crucible::perf::detail::find_rodata;
using ::crucible::perf::detail::disable_unavailable_programs;
using ::crucible::perf::detail::libbpf_errno;
using ::crucible::perf::detail::install_libbpf_log_cb_once;
using ::crucible::perf::detail::quiet;
using ::crucible::perf::detail::verbose;

}  // namespace

// State CRTP-inherits NonMovable for the same reason SenseHub::State
// does — exclusive resources (bpf_object*, bpf_link*, mmap region)
// must not be duplicated.  Subsumes 4 explicit `= delete` lines into
// the empty base class; EBO collapses the base.
struct SchedSwitch::State : crucible::safety::NonMovable<SchedSwitch::State> {
    struct bpf_object*                       obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};

    // Single-set during Phase 7 of load() — the mmap'd
    // sched_timeline base.  WriteOnceNonNull's nullptr sentinel
    // doubles as the "not loaded yet" marker; the dtor's
    // .has_value() guard skips munmap when un-set.
    safety::WriteOnceNonNull<volatile uint8_t*> timeline_base{};

    // mmap length in bytes — the page-rounded size that munmap
    // expects.  Paired single-set with timeline_base.
    safety::WriteOnce<size_t>                   timeline_len{};

    // FD of the cs_count map, captured during load() and kept for
    // the lifetime of the SchedSwitch.  context_switches() uses it
    // for bpf_map_lookup_elem.  Tagged with source::BpfMap to
    // distinguish from arbitrary FDs at the type level.  Sentinel
    // value is -1 (the canonical "unset FD" Linux convention);
    // context_switches() short-circuits on `< 0` rather than issuing
    // a syscall on an invalid FD.
    Fd                                          cs_count_fd{-1};

    // Monotonic counter — only ever .bump()s on attach failures,
    // never resets.  Same discipline as SenseHub::State.
    safety::Monotonic<size_t>                   attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links) if (l != nullptr) bpf_link__destroy(l);
        if (timeline_base.has_value()) {
            ::munmap(const_cast<uint8_t*>(timeline_base.get()),
                     timeline_len.get());
        }
        if (obj != nullptr) bpf_object__close(obj);
    }
};

SchedSwitch::SchedSwitch() noexcept = default;
SchedSwitch::SchedSwitch(SchedSwitch&&) noexcept = default;
SchedSwitch& SchedSwitch::operator=(SchedSwitch&&) noexcept = default;
SchedSwitch::~SchedSwitch() = default;

std::optional<SchedSwitch> SchedSwitch::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] sched_switch unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] sched_switch unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_sched_switch";
    struct bpf_object* obj = bpf_object__open_mem(
        sched_switch_bpf_bytecode,
        static_cast<size_t>(sched_switch_bpf_bytecode_len),
        &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        state->obj = nullptr;
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild)", e);
        return std::nullopt;
    }
    state->obj = obj;

    // ── 2. Rewrite target_tgid in .rodata to our PID ───────────────
    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            std::string rewritten(
                static_cast<const char*>(current), vsz);
            const Tgid     tgid = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. Skip programs whose tracepoints aren't on this kernel ──
    disable_unavailable_programs(state->obj);

    // ── 4. Verify, JIT, allocate maps ──────────────────────────────
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "verifier rejected, missing CAP_BPF, or kernel too old)", -err);
        return std::nullopt;
    }

    // ── 5. Register our main TID in our_tids ───────────────────────
    //
    // The sched_switch tracepoint fires in PREV-task context, so
    // the BPF program looks up next_pid in our_tids to recognise
    // "this is one of our threads switching IN".  A first-ship
    // facade only registers the loader's main TID; multi-thread
    // workloads will miss off-CPU events for their other threads
    // until GAPS-004x adds /proc/self/task/* iteration.
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids");
        m != nullptr) {
        const Fd      fd  = map_fd(m);
        const Tid     tid = current_tid();
        const uint8_t one = 1;
        const int      fd_raw  = fd.value();
        const uint32_t tid_raw = tid.value();
        (void)bpf_map_update_elem(fd_raw, &tid_raw, &one, BPF_ANY);
    }

    // ── 6. Attach every autoload-enabled program ───────────────────
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        struct bpf_link* link = bpf_program__attach(prog);
        const long       lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                const char* sec = bpf_program__section_name(prog);
                std::fprintf(stderr,
                    "[crucible::perf] sched_switch attach failed for %s (%s)\n",
                    sec ? sec : "<anon>",
                    std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] sched_switch link capacity exhausted "
                    "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel missing sched_switch tracepoint)");
        return std::nullopt;
    }

    // ── 7. mmap the sched_timeline ring buffer ─────────────────────
    struct bpf_map* timeline_map =
        bpf_object__find_map_by_name(state->obj, "sched_timeline");
    if (timeline_map == nullptr) {
        report("sched_timeline map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd timeline_fd = map_fd(timeline_map);

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    // sizeof(TimelineHeader) + sizeof(events[TIMELINE_CAPACITY])
    //   = 64 + (4096 * 24)
    //   = 98368 bytes
    // Round up to page granularity (24 pages of 4 KB on x86_64).
    const size_t bytes          = sizeof(TimelineHeader) +
                                  TIMELINE_CAPACITY * sizeof(TimelineSchedEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED,
                                timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of sched_timeline failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }
    // Paired single-set: timeline_base + timeline_len commit
    // together.  Set length first so a hypothetical contract
    // violation on timeline_base.set() leaves the dtor's munmap
    // self-consistent (both unset → no munmap; len set + base
    // set → dtor munmaps; len set alone → impossible because
    // base.set() never threw).
    state->timeline_len.set(mmap_len_bytes);
    state->timeline_base.set(static_cast<volatile uint8_t*>(mmap_address));

    // Capture the cs_count FD for context_switches() lookups.  The
    // map is a 1-element ARRAY map (not mmap-able as currently
    // declared in sched_switch.bpf.c), so reads require a syscall.
    if (struct bpf_map* cs = bpf_object__find_map_by_name(state->obj, "cs_count");
        cs != nullptr) {
        state->cs_count_fd = map_fd(cs);
    } else {
        // Soft failure — the timeline still works without cs_count,
        // and context_switches() returns 0 when fd is -1.  Print
        // the warning if the user asked for verbose output.
        if (verbose()) {
            std::fprintf(stderr,
                "[crucible::perf] sched_switch cs_count map missing — "
                "context_switches() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] sched_switch partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get());
    }

    SchedSwitch h;
    h.state_ = std::move(state);
    return h;
}

uint64_t SchedSwitch::context_switches() const noexcept {
    if (state_ == nullptr || state_->cs_count_fd.value() < 0) return 0;
    // bpf_map_lookup_elem on an ARRAY map with key=0 returns the
    // single u64 counter the BPF program __sync_fetch_and_add's
    // every sched_switch event whose prev_pid is in our process.
    // Cost: one syscall, ~1 µs on most kernels.
    const uint32_t key = 0;
    uint64_t       value = 0;
    if (bpf_map_lookup_elem(state_->cs_count_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineSchedEvent, SchedSwitch>
SchedSwitch::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) {
        return safety::Borrowed<const TimelineSchedEvent, SchedSwitch>{};
    }
    // The mmap'd region starts with the 64-byte header, then
    // TIMELINE_CAPACITY events.  Hand out a span of just the
    // events portion — the header is accessed separately via
    // timeline_write_index().  We bit_cast'd `const_cast`-style
    // to drop volatile because std::span<const volatile T> is
    // unimplementable for non-scalar T in libstdc++ today (the
    // span impl instantiates element copy/move ctors that fail
    // under volatile); consumers are expected to do volatile /
    // atomic loads at the field-access site (see header docblock
    // for the canonical __atomic_load_n(&events[slot].ts_ns,
    // __ATOMIC_ACQUIRE) idiom).
    auto* base = state_->timeline_base.get();
    auto* events = reinterpret_cast<const TimelineSchedEvent*>(
        const_cast<const uint8_t*>(base + sizeof(TimelineHeader)));
    return safety::Borrowed<const TimelineSchedEvent, SchedSwitch>{
        events, TIMELINE_CAPACITY};
}

uint64_t SchedSwitch::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) return 0;
    auto* base = state_->timeline_base.get();
    auto* hdr  = reinterpret_cast<const volatile TimelineHeader*>(base);
    // Volatile load of write_idx — the BPF program updates it via
    // __sync_fetch_and_add (a full memory barrier on x86 / a
    // release on aarch64 via STLXR), so a plain volatile load on
    // x86 reads a value that's at most one event behind real-time.
    // No acquire fence needed: ts_ns is the per-event completion
    // marker; reader checks ts_ns != 0 before trusting other event
    // fields.
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SchedSwitch::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SchedSwitch::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

SchedSwitch::Snapshot SchedSwitch::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches   = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
