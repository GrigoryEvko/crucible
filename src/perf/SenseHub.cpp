// crucible::perf::SenseHub — libbpf binding implementation.
//
// Promoted from bench/bpf_senses.cpp on 2026-05-03 (GAPS-004a).
//
// Design points:
//   • Bytecode is embedded at build time (xxd -i output of the
//     clang-compiled sense_hub.bpf.o), so the linked binary is
//     self-contained.  No runtime path lookup.
//   • .rodata const target_tgid is rewritten to getpid() before load,
//     so every tracepoint's is_target() check filters to our process.
//     (target_tgid is the only variable in that section — offset 0.)
//   • Tracepoints that don't exist on the running kernel have their
//     autoload disabled before bpf_object__load, so one missing
//     tracepoint on a stripped-down kernel can't veto the whole
//     program.
//   • our_tids receives our main TID after load so sched_switch can
//     recognize "this is us" in the PREV task context on switch-in
//     (bpf_get_current_pid_tgid returns PREV at sched_switch time).
//   • The counters map is BPF_F_MMAPABLE — we mmap it read-only, one
//     page (12 cache lines of 64 bytes + slack), and userspace reads
//     are __atomic_load_n(ACQUIRE) pairs with the kernel's
//     __sync_fetch_and_add (full barrier) on the producer side.  Pure
//     in-register loads, no syscalls on the read path.

#include <crucible/perf/SenseHub.h>

// detail::BpfLoader brings the shared anonymous-namespace helpers
// (Tagged provenance typedefs, env-var caches, libbpf log control,
// .rodata + tracepoint discovery, libbpf_errno).  GAPS-004x extracted
// these from the 7 BPF facade .cpp files that had each cloned them.
#include <crucible/perf/detail/BpfLoader.h>

#include <crucible/safety/Mutation.h>  // safety::WriteOnce / WriteOnceNonNull / Monotonic
#include <crucible/safety/Pinned.h>    // safety::NonMovable<T>

#include <sys/mman.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

// 64 slots comfortably covers the 58 tracepoints shipped today with
// headroom.  GCC 16 ships std::inplace_vector unconditionally.
#include <inplace_vector>

extern "C" {
extern const unsigned char sense_hub_bpf_bytecode[];
extern const unsigned int  sense_hub_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// All shared helpers live in ::crucible::perf::detail (BpfLoader.h).
// Pull them into the anonymous namespace so call sites below remain
// syntactically identical to pre-extraction.  The implicit using-
// directive from anon-ns to enclosing crucible::perf namespace
// (per [basic.namespace]/4) propagates these names to the
// SenseHub::State member-declaration scope below the closing }.
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

// State CRTP-inherits NonMovable: it owns three exclusive resources
// (bpf_object*, the mmap'd counter region via WriteOnceNonNull, and
// the per-link bpf_link* handles in `links`).  Copying duplicates
// kernel handles → double-close at dtor; moving leaves a moved-from
// shell that the SenseHub::SenseHub(SenseHub&&) path could resurrect
// via std::move(state_).  The 4 explicit `= delete("...")` lines we
// had previously are now subsumed into the NonMovable<State> base —
// the reasons remain encoded in the deleted-op diagnostic strings on
// NonMovable's own ctors/assigns.  Zero codegen cost (EBO collapses
// the empty base).
struct SenseHub::State : crucible::safety::NonMovable<SenseHub::State> {
    struct bpf_object*                        obj   = nullptr;
    std::inplace_vector<struct bpf_link*, 64> links{};

    // Single-set after Phase 7 of load() succeeds.  WriteOnceNonNull
    // catches accidental re-publish (would double-free the prior mmap
    // on dtor) AND replaces the bare-pointer + sentinel-comparison
    // pattern with a typed has_value() / get() surface — sizeof is
    // unchanged (the nullptr sentinel IS the unset marker).
    safety::WriteOnceNonNull<volatile uint64_t*> counters{};

    // Single-set during Phase 7 alongside `counters`.  WriteOnce
    // contract-fires on re-set under semantic=enforce; under release
    // semantic=ignore it's a plain optional<size_t> with zero
    // observable cost relative to the bare size_t.
    safety::WriteOnce<size_t>                    mmap_len{};

    // Monotonic counter — only ever increments via .bump() in the
    // attach loop, never decrements.  Wrapping in safety::Monotonic
    // catches a future maintainer who tries to assign a smaller
    // value (e.g. "zero out on retry") — the contract on advance()
    // / try_advance() fires on the regression.  bump() also
    // contract-fires on UINT_MAX overflow which is the only way an
    // integral monotonic counter can violate the partial-order
    // invariant.  Total programs is bounded by 64 (links cap) so
    // overflow is structurally impossible, but the contract is
    // still load-bearing for "no accidental decrement" semantics.
    safety::Monotonic<size_t>                    attach_fail_cnt{0};

    State() = default;

    // Copy + move are deleted via the NonMovable<State> CRTP base.
    // Keeping the explicit `= delete` lines here would either
    // duplicate the message (review noise) or shadow the base's
    // diagnostic strings (worse).

    ~State() {
        for (struct bpf_link* l : links) if (l != nullptr) bpf_link__destroy(l);
        // counters + mmap_len are paired single-set slots.  Both
        // .has_value() iff Phase 7 of load() ran to completion.
        // Either both are set or neither is — no half-state can
        // sneak past Phase 7's atomic ordering.
        if (counters.has_value()) {
            ::munmap(const_cast<uint64_t*>(counters.get()), mmap_len.get());
        }
        if (obj != nullptr) bpf_object__close(obj);
    }
};

SenseHub::SenseHub() noexcept = default;
SenseHub::SenseHub(SenseHub&&) noexcept = default;
SenseHub& SenseHub::operator=(SenseHub&&) noexcept = default;
SenseHub::~SenseHub() = default;

std::optional<SenseHub> SenseHub::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] BPF sense hub unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] BPF sense hub unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    //
    // libbpf ≥ 1.0 returns NULL on error; pre-1.0 strict-mode builds
    // return an ERR_PTR with top bit set.  libbpf_get_error() handles
    // both — if it's non-zero we drop the pointer unconditionally,
    // never pass it to bpf_object__close (which would crash on an
    // IS_ERR pointer).
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_senses";
    struct bpf_object* obj = bpf_object__open_mem(
        sense_hub_bpf_bytecode,
        static_cast<size_t>(sense_hub_bpf_bytecode_len),
        &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        // Don't let state->~State() call bpf_object__close(IS_ERR_PTR).
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
            // Small section — bounded to sizeof(struct) of the .rodata
            // layout (a handful of bytes in practice).  std::string
            // is fine here; it's off the hot path and the state
            // allocates maps anyway.
            std::string rewritten(
                static_cast<const char*>(current), vsz);
            const Tgid   tgid = current_tgid();
            const uint32_t tgid_raw = tgid.value();  // unwrap once; pass by addr below
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));  // offset 0
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
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids");
        m != nullptr) {
        const Fd      fd  = map_fd(m);
        const Tid     tid = current_tid();
        const uint8_t one = 1;
        // Unwrap to lvalues so bpf_map_update_elem (C API) can take
        // their addresses.  Tagged<>::value() returns const T& so a
        // direct &tid.value() works, but a named local makes the
        // intent obvious to a reviewer.
        const int      fd_raw  = fd.value();
        const uint32_t tid_raw = tid.value();
        (void)bpf_map_update_elem(fd_raw, &tid_raw, &one, BPF_ANY);
    }

    // ── 6. Attach every autoload-enabled program ───────────────────
    //
    // bpf_program__attach returns either a valid pointer, NULL, or an
    // ERR_PTR (pointer with top bit set, encoding -errno).  Passing
    // an ERR_PTR to bpf_link__destroy is UB — it dereferences the
    // encoded integer as a real pointer.  libbpf_get_error()
    // normalizes both failure shapes into a non-zero return, which
    // we then drop.
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
                    "[crucible::perf] BPF attach failed for %s (%s)\n",
                    sec ? sec : "<anon>",
                    std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }
        // inplace_vector has a fixed capacity; the limit is chosen
        // with headroom (64 for 58 tracepoints) but enforce the
        // invariant so a future expansion that goes overboard fails
        // loudly rather than silently dropping attachments.
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] BPF link capacity exhausted "
                    "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel missing every tracepoint, or lacking CAP_PERFMON/CAP_DAC_READ_SEARCH)");
        return std::nullopt;
    }

    // ── 7. mmap the counters array ─────────────────────────────────
    struct bpf_map* counters_map = bpf_object__find_map_by_name(state->obj, "counters");
    if (counters_map == nullptr) {
        report("counters map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd counters_fd = map_fd(counters_map);

    // Guard sysconf — it can return -1 on hardened sandboxes; a
    // naive cast to size_t would underflow the page-size bitmask
    // below and produce a garbage mmap length.
    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page             = static_cast<size_t>(page_l);
    const size_t bytes            = NUM_COUNTERS * sizeof(uint64_t);
    const size_t mmap_len_bytes   = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED,
                                counters_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of counters map failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }
    // Paired single-set: counters and mmap_len commit together.  An
    // earlier early-return leaves both unset; the dtor's
    // counters.has_value() guard then skips munmap correctly.  Order
    // matters under semantic=enforce — set mmap_len FIRST so a
    // hypothetical contract violation on counters.set() leaves a
    // self-consistent State (set→set sequence; partial set on
    // counters means mmap_len already committed, dtor still
    // munmaps — no leak).
    state->mmap_len.set(mmap_len_bytes);
    state->counters.set(static_cast<volatile uint64_t*>(mmap_address));

    // Surface partial-coverage warnings once per successful load.
    // Most runs attach every program; when the kernel is missing a
    // tracepoint or lacks a capability, the corresponding subsystem
    // reads zero and the user deserves to know which counters are
    // dark without having to diff an expected baseline.
    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] BPF sense hub partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get());
    }

    SenseHub h;
    h.state_ = std::move(state);
    return h;
}

Snapshot SenseHub::read() const noexcept {
    Snapshot snapshot;
    // Defensive triple guard: state_ may be null (moved-from
    // SenseHub) and even on a live SenseHub the counters slot is
    // unset until Phase 7 of load() commits.  WriteOnceNonNull's
    // operator bool collapses to a single nullptr-compare.
    if (state_ == nullptr || !state_->counters) return snapshot;
    // Acquire-ordered loads on every slot.  The BPF producer uses
    // __sync_fetch_and_add on these addresses (a full barrier), so
    // pairing with an acquire on the consumer side guarantees that
    // all kernel-side stores preceding the counter bump are visible
    // to us and that the compiler cannot sink subsequent reads above
    // the load.  __atomic_load_n through a `volatile uint64_t*` is a
    // documented GCC extension: the volatile qualifier is allowed
    // under atomics and the load lowers to a single aligned MOV on
    // x86-64 (acquire is free on TSO).
    const volatile uint64_t* __restrict src = state_->counters.get();
    for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
        snapshot.counters[i] = __atomic_load_n(&src[i], __ATOMIC_ACQUIRE);
    }
    return snapshot;
}

safety::Borrowed<const volatile uint64_t, SenseHub>
SenseHub::counters_view() const noexcept {
    // Empty borrow when no live mmap (moved-from / un-loaded /
    // post-load-failure SenseHub).  span(nullptr, 0) is the
    // documented empty-borrow form per Borrowed<>'s constructor
    // contract — zero-sized span_, no UB at construction.
    if (state_ == nullptr || !state_->counters) {
        return safety::Borrowed<const volatile uint64_t, SenseHub>{};
    }
    // The mmap returns NUM_COUNTERS u64 cells; the wire-compat
    // assertion in the header (`sizeof(Snapshot) == 96 * 8`) is
    // the structural witness that the BPF map allocates exactly
    // this much.  Borrowed::ctor takes (T*, count) — wrap.
    return safety::Borrowed<const volatile uint64_t, SenseHub>{
        state_->counters.get(), NUM_COUNTERS};
}

safety::Refined<safety::bounded_above<64>, std::size_t>
SenseHub::attached_programs() const noexcept {
    // Bound is structurally enforced: state_->links is
    // inplace_vector<bpf_link*, 64> — its .size() is in [0, 64]
    // by inplace_vector's own contract.  Wrapping in Refined
    // re-publishes the bound in the type system; consumers can
    // rely on it without re-checking.
    using R = safety::Refined<safety::bounded_above<64>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<64>, std::size_t>
SenseHub::attach_failures() const noexcept {
    // Same bound as attached_programs(): both counters partition
    // the same program-iteration loop — attach_fail_cnt records
    // the failures, links.size() records the successes, and
    // attach_fail_cnt + links.size() ≤ 64 (the inplace_vector cap).
    using R = safety::Refined<safety::bounded_above<64>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

}  // namespace crucible::perf
