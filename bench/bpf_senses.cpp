// eBPF sense hub loader — libbpf binding for bench::bpf::SenseHub.
//
// Design points:
//   • Bytecode is embedded at build time (xxd -i output of the
//     clang-compiled sense_hub.bpf.o), so the bench binary is
//     self-contained. No runtime path lookup.
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
//     __sync_fetch_and_add (full barrier) on the producer side. Pure
//     in-register loads, no syscalls on the read path.

#include "bpf_senses.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error (IS_ERR detection)
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

// 64 slots comfortably covers the 58 tracepoints shipped today with
// headroom. GCC 16 ships std::inplace_vector unconditionally.
#include <inplace_vector>

extern "C" {
extern const unsigned char sense_hub_bpf_bytecode[];
extern const unsigned int  sense_hub_bpf_bytecode_len;
}

namespace bench::bpf {

namespace {

// ── Tiny TU-private strong types (TypeSafe axiom) ───────────────────
//
// getpid() / gettid() / bpf_map__fd() return raw integers that mean
// different things. Wrapping them as minimal newtypes prevents
// silently passing a TID where a PID was wanted and documents intent
// at the call site. Trivially destructible, zero codegen overhead.
struct Tgid { uint32_t raw; };
struct Tid  { uint32_t raw; };
struct Fd   { int      raw; };
static_assert(sizeof(Tgid) == sizeof(uint32_t));
static_assert(sizeof(Tid)  == sizeof(uint32_t));
static_assert(sizeof(Fd)   == sizeof(int));

[[nodiscard]] Tgid current_tgid() noexcept {
    return Tgid{static_cast<uint32_t>(::getpid())};
}
[[nodiscard]] Tid  current_tid()  noexcept {
    return Tid{static_cast<uint32_t>(::syscall(SYS_gettid))};
}
[[nodiscard]] Fd   map_fd(struct bpf_map* m) noexcept {
    return Fd{bpf_map__fd(m)};
}

// ── Env-var knobs (cached once at first touch) ──────────────────────
//
// getenv() walks environ every call. Load-time decisions and the
// (rare) diagnostic path both want the same two flags; cache them in
// function-local statics so they cost a single atomic load thereafter.
[[nodiscard]] bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}
[[nodiscard]] bool quiet()   noexcept {
    static const bool kQuiet = env_true("CRUCIBLE_BENCH_BPF_QUIET");
    return kQuiet;
}
[[nodiscard]] bool verbose() noexcept {
    static const bool kVerbose = env_true("CRUCIBLE_BENCH_BPF_VERBOSE");
    return kVerbose;
}

// Silence libbpf's INFO/WARN spew by default — we emit a single clean
// diagnostic from load() on failure. Forward everything when the user
// explicitly asks for verbose output. noexcept because libbpf stores
// and invokes this as a C function pointer; throwing across a C
// callback frame is undefined behavior.
int libbpf_log_cb(enum libbpf_print_level, const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

// Register the libbpf print callback exactly once across all SenseHub::
// load() invocations — subsequent calls are no-ops.
void install_libbpf_log_cb_once() noexcept {
    static std::once_flag once;
    std::call_once(once, [] { libbpf_set_print(libbpf_log_cb); });
}

// .rodata maps are named "<progname>.rodata" with progname truncated
// to fit 15 chars total in the map name. We want the main .rodata
// (where target_tgid lives at offset 0), not .rodata.str1.1 or any
// other compiler-emitted companion section — match the exact suffix.
[[nodiscard]] struct bpf_map* find_rodata(struct bpf_object* obj) noexcept {
    struct bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        const char* n = bpf_map__name(map);
        if (n == nullptr) continue;
        const std::string_view name{n};
        if (name.ends_with(".rodata")) return map;
    }
    return nullptr;
}

// Returns true iff /sys/kernel/tracing/events/<category>/<event>/id exists.
// Called for every SEC("tracepoint/…") program before load — missing
// tracepoints get autoload disabled so the object can still load.
[[nodiscard]] bool tracepoint_exists(const char* category_slash_event) noexcept {
    std::string path = "/sys/kernel/tracing/events/";
    path.append(category_slash_event);
    path.append("/id");
    if (::access(path.c_str(), F_OK) == 0) return true;
    // Older kernels expose it via debugfs instead.
    path.assign("/sys/kernel/debug/tracing/events/");
    path.append(category_slash_event);
    path.append("/id");
    return ::access(path.c_str(), F_OK) == 0;
}

void disable_unavailable_programs(struct bpf_object* obj) noexcept {
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        const char* sec = bpf_program__section_name(prog);
        if (sec == nullptr) continue;
        // "tracepoint/<cat>/<event>" — strip the prefix, check sysfs.
        static constexpr const char kPrefix[] = "tracepoint/";
        if (std::strncmp(sec, kPrefix, sizeof(kPrefix) - 1) != 0) continue;
        const char* tp = sec + (sizeof(kPrefix) - 1);
        if (!tracepoint_exists(tp)) {
            (void)bpf_program__set_autoload(prog, false);
        }
    }
}

// Translate a libbpf return pointer into an errno-ish int for the
// report helper. Prefers libbpf_get_error() (which returns -errno
// encoded in the pointer) because libbpf doesn't promise to touch the
// thread-local errno on NULL returns.
[[nodiscard]] int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

} // namespace

struct SenseHub::State {
    struct bpf_object*                        obj   = nullptr;
    std::inplace_vector<struct bpf_link*, 64> links{};
    volatile uint64_t*               counters         = nullptr;
    size_t                           mmap_len         = 0;
    size_t                           attach_fail_cnt  = 0;

    State() = default;

    State(const State&) =
        delete("State owns raw bpf_object*/bpf_link*/mmap — copying would double-free");
    State& operator=(const State&) =
        delete("State owns raw bpf_object*/bpf_link*/mmap — copying would double-free");
    State(State&&) =
        delete("State is pinned inside unique_ptr; move the unique_ptr instead");
    State& operator=(State&&) =
        delete("State is pinned inside unique_ptr; move the unique_ptr instead");

    ~State() {
        for (struct bpf_link* l : links) if (l != nullptr) bpf_link__destroy(l);
        if (counters != nullptr) ::munmap(const_cast<uint64_t*>(counters), mmap_len);
        if (obj      != nullptr) bpf_object__close(obj);
    }
};

SenseHub::SenseHub() noexcept = default;
SenseHub::SenseHub(SenseHub&&) noexcept = default;
SenseHub& SenseHub::operator=(SenseHub&&) noexcept = default;
SenseHub::~SenseHub() = default;

std::optional<SenseHub> SenseHub::load() noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[bench] BPF sense hub unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr, "[bench] BPF sense hub unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    //
    // libbpf ≥ 1.0 returns NULL on error; pre-1.0 strict-mode builds
    // return an ERR_PTR with top bit set. libbpf_get_error() handles
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
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild bench)", e);
        return std::nullopt;
    }
    state->obj = obj;

    // ── 2. Rewrite target_tgid in .rodata to our PID ───────────────
    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            // Small section — bounded to sizeof(struct) of the .rodata
            // layout (a handful of bytes in practice). std::string is
            // fine here; it's off the hot path and the state allocates
            // maps anyway.
            std::string rewritten(
                static_cast<const char*>(current), vsz);
            const Tgid   tgid = current_tgid();
            std::memcpy(rewritten.data(), &tgid.raw, sizeof(tgid.raw));  // offset 0
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. Skip programs whose tracepoints aren't on this kernel ──
    disable_unavailable_programs(state->obj);

    // ── 4. Verify, JIT, allocate maps ──────────────────────────────
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (try `cmake --build --preset bench --target bench-caps`; "
               "verifier rejected, missing CAP_BPF, or kernel too old)", -err);
        return std::nullopt;
    }

    // ── 5. Register our main TID in our_tids ───────────────────────
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids");
        m != nullptr) {
        const Fd      fd  = map_fd(m);
        const Tid     tid = current_tid();
        const uint8_t one = 1;
        (void)bpf_map_update_elem(fd.raw, &tid.raw, &one, BPF_ANY);
    }

    // ── 6. Attach every autoload-enabled program ───────────────────
    //
    // bpf_program__attach returns either a valid pointer, NULL, or an
    // ERR_PTR (pointer with top bit set, encoding -errno). Passing an
    // ERR_PTR to bpf_link__destroy is UB — it dereferences the encoded
    // integer as a real pointer. libbpf_get_error() normalizes both
    // failure shapes into a non-zero return, which we then drop.
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        struct bpf_link* link = bpf_program__attach(prog);
        const long       lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            state->attach_fail_cnt++;
            if (verbose()) {
                const char* sec = bpf_program__section_name(prog);
                std::fprintf(stderr,
                    "[bench] BPF attach failed for %s (%s)\n",
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
            state->attach_fail_cnt++;
            if (verbose()) {
                std::fprintf(stderr,
                    "[bench] BPF link capacity exhausted (bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (try `cmake --build --preset bench --target bench-caps`; "
               "kernel missing every tracepoint, or lacking CAP_PERFMON/CAP_DAC_READ_SEARCH)");
        return std::nullopt;
    }

    // ── 7. mmap the counters array ─────────────────────────────────
    struct bpf_map* counters_map = bpf_object__find_map_by_name(state->obj, "counters");
    if (counters_map == nullptr) {
        report("counters map not found in object (bytecode/header out of sync — rebuild bench)");
        return std::nullopt;
    }
    const Fd counters_fd = map_fd(counters_map);

    // Guard sysconf — it can return -1 on hardened sandboxes; a naive
    // cast to size_t would underflow the page-size bitmask below and
    // produce a garbage mmap length.
    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page  = static_cast<size_t>(page_l);
    const size_t bytes = NUM_COUNTERS * sizeof(uint64_t);
    state->mmap_len    = (bytes + page - 1) & ~(page - 1);
    void* p = ::mmap(nullptr, state->mmap_len, PROT_READ, MAP_SHARED, counters_fd.raw, 0);
    if (p == MAP_FAILED) {
        report("mmap of counters map failed (try `cmake --build --preset bench --target bench-caps`; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }
    state->counters = static_cast<volatile uint64_t*>(p);

    // Surface partial-coverage warnings once per successful load. Most
    // runs attach every program; when the kernel is missing a tracepoint
    // or lacks a capability, the corresponding subsystem reads zero and
    // the user deserves to know which counters are dark without having
    // to diff an expected baseline.
    if (!quiet() && state->attach_fail_cnt != 0) {
        std::fprintf(stderr,
            "[bench] BPF sense hub partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_BENCH_BPF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt);
    }

    SenseHub h;
    h.state_ = std::move(state);
    return h;
}

Snapshot SenseHub::read() const noexcept {
    Snapshot s;
    if (state_ == nullptr || state_->counters == nullptr) return s;
    // Acquire-ordered loads on every slot. The BPF producer uses
    // __sync_fetch_and_add on these addresses (a full barrier), so
    // pairing with an acquire on the consumer side guarantees that
    // all kernel-side stores preceding the counter bump are visible
    // to us and that the compiler cannot sink subsequent reads above
    // the load. __atomic_load_n through a `volatile uint64_t*` is a
    // documented GCC extension: the volatile qualifier is allowed
    // under atomics and the load lowers to a single aligned MOV on
    // x86-64 (acquire is free on TSO).
    const volatile uint64_t* __restrict src = state_->counters;
    for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
        s.counters[i] = __atomic_load_n(&src[i], __ATOMIC_ACQUIRE);
    }
    return s;
}

const volatile uint64_t* SenseHub::counters_ptr() const noexcept {
    return (state_ != nullptr) ? state_->counters : nullptr;
}

size_t SenseHub::attached_programs() const noexcept {
    return (state_ != nullptr) ? state_->links.size() : 0;
}

size_t SenseHub::attach_failures() const noexcept {
    return (state_ != nullptr) ? state_->attach_fail_cnt : 0;
}

} // namespace bench::bpf
