#pragma once

// crucible::perf::detail::BpfLoader — shared anonymous-namespace helpers
// extracted from the 7 GAPS-004 BPF facade .cpp files.
//
// ─── WHY THIS HEADER EXISTS (GAPS-004x, #1286) ────────────────────────
//
// SenseHub (GAPS-004a) shipped first as the keystone facade.  The 6
// per-program siblings (SchedSwitch, PmuSample, LockContention,
// SyscallLatency, SchedTpBtf, SyscallTpBtf) each cloned the same
// anonymous-namespace block of helper functions: provenance Tagged
// typedefs (Tgid/Tid/Fd), env-var caches (quiet/verbose), libbpf
// log gate + once-flag installer, .rodata / tracepoint discovery,
// auto-disable for missing tracepoints, and libbpf_get_error stride.
//
// That duplication had a structural cost: every fix touched 7 files,
// and 5 audit rounds (GAPS-004g-AUDIT-{1..5}) found enumeration drift
// across the clones — "5/7" docstring drift, BPF_MAP_TYPE_HASH that
// should have been LRU_HASH, double bpf_ktime_get_ns(), bare `= delete`
// without reasons.  Each audit found one new instance of the same
// class.  Promote-First (CLAUDE.md / Mike Acton: generalize from
// ≥2 real cases) said: ship N skeletons before extracting.  We have
// N=7 — one above the threshold.
//
// This header consolidates the cross-facade discipline:
//   • Provenance source tags: Kernel / BpfMap
//   • Tagged typedefs: Tgid / Tid / Fd (+ sizeof asserts)
//   • Kernel-syscall wrappers: current_tgid / current_tid / map_fd
//   • Env-var caches: env_true / env_true_either / quiet / verbose
//   • libbpf log control: libbpf_log_cb / install_libbpf_log_cb_once
//   • Object discovery: find_rodata / tracepoint_exists
//   • Loader phases: disable_unavailable_programs / libbpf_errno
//
// ─── WHY HEADER-ONLY INLINE (NOT A SHARED .o) ─────────────────────────
//
// All 9 functions are tiny (1-12 lines).  Header-only `inline`:
//   • Avoids cross-TU function-call overhead — each facade inlines
//     them at the call site and `-flto=auto` propagates further.
//   • Function-local-static caches (kQuiet / kVerbose) become single-
//     instance across all TUs that include this header.  A single
//     setenv("CRUCIBLE_PERF_QUIET","1",0) before ANY facade load
//     silences EVERY facade — fixing a latent inconsistency where
//     each .cpp had its own per-TU cache.  Per [basic.def.odr]/12,
//     function-local statics in inline functions have a single
//     shared instance.
//   • `tracepoint_exists` and `disable_unavailable_programs` are
//     unused by the BTF facades (SchedTpBtf, SyscallTpBtf attach via
//     BTF, not by tracepoint name); unused inline functions vanish
//     at link time.  Zero cost to ship them in the shared header.
//
// ─── USAGE PATTERN (per facade .cpp) ──────────────────────────────────
//
//   #include <crucible/perf/detail/BpfLoader.h>
//
//   namespace {
//     namespace source = ::crucible::perf::detail::source;
//     using ::crucible::perf::detail::Tgid;
//     using ::crucible::perf::detail::Tid;
//     using ::crucible::perf::detail::Fd;
//     using ::crucible::perf::detail::current_tgid;
//     using ::crucible::perf::detail::current_tid;
//     using ::crucible::perf::detail::map_fd;
//     using ::crucible::perf::detail::find_rodata;
//     using ::crucible::perf::detail::disable_unavailable_programs;
//     using ::crucible::perf::detail::libbpf_errno;
//     using ::crucible::perf::detail::install_libbpf_log_cb_once;
//     using ::crucible::perf::detail::quiet;
//     using ::crucible::perf::detail::verbose;
//   } // namespace
//
// All call sites then look identical to pre-extraction; only the 13
// using-declarations replace ~110 lines of duplicated bodies.
//
// ─── AXIOM POSTURE ────────────────────────────────────────────────────
//
// • InitSafe: every Tagged typedef wraps a primitive with deterministic
//   default ctor (Tagged{T{}}).  No NSDMI-less aggregates.
// • TypeSafe: source::Kernel ≢ source::BpfMap by phantom tag — a Tgid
//   cannot be silently passed where an Fd was expected.
// • NullSafe: bpf_object*/bpf_map* params validated by [[nodiscard]]
//   returns; nullptr propagation tested by libbpf_errno's IS_ERR walk.
// • MemSafe: no allocations.  Function-local statics own their memory
//   (single global instance per inline-function rule).
// • BorrowSafe: env-var caches written exactly once (call_once /
//   function-local-static); reads thereafter are atomic-load-fast-path.
// • ThreadSafe: install_libbpf_log_cb_once via std::call_once;
//   env-var caches via standard inline-function-local-static
//   (zero-overhead Itanium ABI guard after first init).
// • LeakSafe: no resources owned by this header — facades hold the
//   bpf_object* / mmap pointers; helpers only inspect/configure them.
// • DetSafe: env_true reads are deterministic (same env → same bool).

#include <crucible/safety/Tagged.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error (IS_ERR detection)
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace crucible::perf::detail {

// ── Provenance source tags ────────────────────────────────────────────
//
// Empty types — phantom-only.  TrustLattice<Tag>::element_type is
// empty, so sizeof(Tagged<T, source::*>) == sizeof(T) under EBO.
// See safety/Tagged.h zero-cost guarantee block.
namespace source {
    struct Kernel {};  // value originated from a kernel syscall
                       // (getpid / gettid).
    struct BpfMap {};  // file descriptor returned by libbpf for an
                       // eBPF map handle.
}  // namespace source

using Tgid = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Tid  = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Fd   = ::crucible::safety::Tagged<int,      source::BpfMap>;

// EBO collapse witness — the empty TrustLattice element MUST collapse
// to zero bytes, so Tagged<T, source::*> is the same width as T.  If
// this asserts, Tagged.h's regime-1 path regressed and every facade
// would silently grow a byte per Tgid/Tid/Fd member.
static_assert(sizeof(Tgid) == sizeof(uint32_t),
    "Tagged<uint32_t, source::Kernel> must EBO-collapse the empty "
    "TrustLattice element to sizeof(uint32_t) — see Tagged.h zero-cost "
    "guarantee block");
static_assert(sizeof(Tid)  == sizeof(uint32_t));
static_assert(sizeof(Fd)   == sizeof(int));

// ── Kernel-syscall wrappers ───────────────────────────────────────────

[[nodiscard]] inline Tgid current_tgid() noexcept {
    return Tgid{static_cast<uint32_t>(::getpid())};
}

[[nodiscard]] inline Tid current_tid() noexcept {
    return Tid{static_cast<uint32_t>(::syscall(SYS_gettid))};
}

[[nodiscard]] inline Fd map_fd(struct bpf_map* m) noexcept {
    return Fd{bpf_map__fd(m)};
}

// ── Env-var caches (function-local-static = single global instance) ──
//
// The cache trap: `static const bool kQuiet = env_true_either(...)`
// decides FOREVER at the first call.  setenv() AFTER first call has
// no effect.  Pre-extraction, each .cpp had its own cache, so the
// "first call" was per-TU and behaviour could diverge between
// facades depending on which loaded first.  Extracted to inline
// here, function-local statics get a single instance across all TUs
// (Itanium ABI [basic.def.odr]/12) — every facade sees the same
// quiet/verbose decision after the first call.

[[nodiscard]] inline bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}

[[nodiscard]] inline bool env_true_either(const char* canonical,
                                          const char* legacy) noexcept {
    return env_true(canonical) || env_true(legacy);
}

[[nodiscard]] inline bool quiet() noexcept {
    static const bool kQuiet =
        env_true_either("CRUCIBLE_PERF_QUIET", "CRUCIBLE_BENCH_BPF_QUIET");
    return kQuiet;
}

[[nodiscard]] inline bool verbose() noexcept {
    static const bool kVerbose =
        env_true_either("CRUCIBLE_PERF_VERBOSE", "CRUCIBLE_BENCH_BPF_VERBOSE");
    return kVerbose;
}

// ── libbpf log control ────────────────────────────────────────────────
//
// libbpf prints to stderr by default — verbose loader noise on
// systems without CAP_BPF.  We gate every libbpf log line on the
// CRUCIBLE_PERF_VERBOSE env var.  install_libbpf_log_cb_once() is
// a one-shot install; std::call_once + inline-function-local
// once_flag means a single global flag across all TUs.

inline int libbpf_log_cb(enum libbpf_print_level,
                         const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

inline void install_libbpf_log_cb_once() noexcept {
    static std::once_flag once;
    std::call_once(once, [] { libbpf_set_print(libbpf_log_cb); });
}

// ── Object discovery ──────────────────────────────────────────────────
//
// find_rodata: walks bpf_object__for_each_map looking for the .rodata
// map (libbpf names it "<obj_name>.rodata").  Used by every facade's
// Phase 2 (rewrite .rodata before bpf_object__load).  ends_with()
// match (NOT ==) because libbpf prepends the BPF object name —
// e.g. "sense_hub.rodata", "sched_switch.rodata".

[[nodiscard]] inline struct bpf_map*
find_rodata(struct bpf_object* obj) noexcept {
    struct bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        const char* n = bpf_map__name(map);
        if (n == nullptr) continue;
        const std::string_view name{n};
        if (name.ends_with(".rodata")) return map;
    }
    return nullptr;
}

// ── Tracepoint existence + auto-disable ───────────────────────────────
//
// Probes /sys/kernel/{tracing,debug/tracing}/events/<cat>/<event>/id
// to determine if a tracepoint is supported on the running kernel.
// Used by disable_unavailable_programs() to mark tracepoint/<...>
// SEC programs as autoload=false BEFORE bpf_object__load — otherwise
// load fails for the whole object on missing tracepoints (e.g.
// raw_syscalls/sys_exit isn't always enabled in CI kernels).
//
// BTF facades (SchedTpBtf, SyscallTpBtf) attach via BTF directly and
// don't go through tracepoint_exists / disable_unavailable_programs;
// the unused inline functions vanish at link time when those facades
// don't reference them.

[[nodiscard]] inline bool
tracepoint_exists(const char* category_slash_event) noexcept {
    std::string path = "/sys/kernel/tracing/events/";
    path.append(category_slash_event);
    path.append("/id");
    if (::access(path.c_str(), F_OK) == 0) return true;
    path.assign("/sys/kernel/debug/tracing/events/");
    path.append(category_slash_event);
    path.append("/id");
    return ::access(path.c_str(), F_OK) == 0;
}

inline void disable_unavailable_programs(struct bpf_object* obj) noexcept {
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        const char* sec = bpf_program__section_name(prog);
        if (sec == nullptr) continue;
        static constexpr const char kPrefix[] = "tracepoint/";
        if (std::strncmp(sec, kPrefix, sizeof(kPrefix) - 1) != 0) continue;
        const char* tp = sec + (sizeof(kPrefix) - 1);
        if (!tracepoint_exists(tp)) {
            (void)bpf_program__set_autoload(prog, false);
        }
    }
}

// ── libbpf error decode ───────────────────────────────────────────────
//
// libbpf encodes errors in two distinct ways:
//   • Pointer-returning APIs (bpf_object__open_mem, bpf_program__attach)
//     return IS_ERR-style pointers via libbpf_get_error(p).  The encoded
//     errno is positive when present; negate to match POSIX errno.
//   • Int-returning APIs (bpf_object__load, bpf_map__set_*) return
//     -errno directly.  No encoding needed.
//
// libbpf_errno bridges the two: pass any IS_ERR-style pointer (or NULL)
// and a fallback errno.  Returns negative POSIX errno suitable for
// log lines like "load_failed_errno=-EPERM".

[[nodiscard]] inline int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

}  // namespace crucible::perf::detail
