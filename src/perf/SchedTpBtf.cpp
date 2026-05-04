// crucible::perf::SchedTpBtf — libbpf binding implementation.
//
// Sixth per-program facade.  Mirrors src/perf/SchedSwitch.cpp's
// 7-step Phase loop nearly exactly — only the bytecode symbol
// (`sched_tp_btf_bpf_bytecode` vs `sched_switch_bpf_bytecode`) and
// the diagnostic facade name differ.  See SchedSwitch.cpp's docblock
// for the rationale on why the duplication is INTENTIONAL
// (Promote-First architecture, Mike Acton: generalize from ≥2 real
// cases — GAPS-004x extracts the shared helper after we have ≥6
// production loaders).
//
// SchedTpBtf-specific notes (vs SchedSwitch):
//   • The BPF program uses SEC("tp_btf/sched_switch") instead of
//     SEC("tracepoint/sched/sched_switch").  libbpf's
//     bpf_program__attach() handles tp_btf identically to
//     tracepoint, so the attach loop here is unchanged.
//   • disable_unavailable_programs() in this TU only checks for
//     "tracepoint/" prefix (legacy tracefs presence).  tp_btf
//     programs go through a different availability gate
//     (CONFIG_DEBUG_INFO_BTF=y + kernel ≥ 5.5), enforced by
//     bpf_object__load() rejecting the verifier when BTF type
//     lookup fails.  No pre-check needed.

#include <crucible/perf/SchedTpBtf.h>

#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Tagged.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>
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

#include <inplace_vector>

extern "C" {
extern const unsigned char sched_tp_btf_bpf_bytecode[];
extern const unsigned int  sched_tp_btf_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// Same TU-private provenance source tags as SchedSwitch.cpp uses;
// GAPS-004x will surface as `crucible::perf::detail::source::*`.
namespace source {
    struct Kernel {};
    struct BpfMap {};
}

using Tgid = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Tid  = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Fd   = ::crucible::safety::Tagged<int,      source::BpfMap>;
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

// Same env-var caching shape as SchedSwitch.cpp.
[[nodiscard]] bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}
[[nodiscard]] bool env_true_either(const char* canonical,
                                   const char* legacy) noexcept {
    return env_true(canonical) || env_true(legacy);
}
[[nodiscard]] bool quiet()   noexcept {
    static const bool kQuiet =
        env_true_either("CRUCIBLE_PERF_QUIET", "CRUCIBLE_BENCH_BPF_QUIET");
    return kQuiet;
}
[[nodiscard]] bool verbose() noexcept {
    static const bool kVerbose =
        env_true_either("CRUCIBLE_PERF_VERBOSE", "CRUCIBLE_BENCH_BPF_VERBOSE");
    return kVerbose;
}

int libbpf_log_cb(enum libbpf_print_level, const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}
void install_libbpf_log_cb_once() noexcept {
    static std::once_flag once;
    std::call_once(once, [] { libbpf_set_print(libbpf_log_cb); });
}

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

[[nodiscard]] int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

}  // namespace

struct SchedTpBtf::State : crucible::safety::NonMovable<SchedTpBtf::State> {
    struct bpf_object*                       obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};
    safety::WriteOnceNonNull<volatile uint8_t*> timeline_base{};
    safety::WriteOnce<size_t>                   timeline_len{};
    Fd                                          cs_count_fd{-1};
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

SchedTpBtf::SchedTpBtf() noexcept = default;
SchedTpBtf::SchedTpBtf(SchedTpBtf&&) noexcept = default;
SchedTpBtf& SchedTpBtf::operator=(SchedTpBtf&&) noexcept = default;
SchedTpBtf::~SchedTpBtf() = default;

std::optional<SchedTpBtf> SchedTpBtf::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] sched_tp_btf unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] sched_tp_btf unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_sched_tp_btf";
    struct bpf_object* obj = bpf_object__open_mem(
        sched_tp_btf_bpf_bytecode,
        static_cast<size_t>(sched_tp_btf_bpf_bytecode_len),
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
            const Tgid     tgid     = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. (No legacy-tracepoint pre-check — tp_btf availability is
    //       gated by bpf_object__load when BTF type lookup fails.)

    // ── 4. Verify, JIT, allocate maps ──────────────────────────────
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel < 5.5, CONFIG_DEBUG_INFO_BTF=n, or verifier rejected)", -err);
        return std::nullopt;
    }

    // ── 5. Register our main TID in our_tids ───────────────────────
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
                    "[crucible::perf] sched_tp_btf attach failed for %s (%s)\n",
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
                    "[crucible::perf] sched_tp_btf link capacity exhausted "
                    "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    if (state->links.empty()) {
        report("no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; "
               "kernel missing BTF for sched_switch tracepoint)");
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
    state->timeline_len.set(mmap_len_bytes);
    state->timeline_base.set(static_cast<volatile uint8_t*>(mmap_address));

    if (struct bpf_map* cs = bpf_object__find_map_by_name(state->obj, "cs_count");
        cs != nullptr) {
        state->cs_count_fd = map_fd(cs);
    } else {
        if (verbose()) {
            std::fprintf(stderr,
                "[crucible::perf] sched_tp_btf cs_count map missing — "
                "context_switches() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] sched_tp_btf partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get());
    }

    SchedTpBtf h;
    h.state_ = std::move(state);
    return h;
}

uint64_t SchedTpBtf::context_switches() const noexcept {
    if (state_ == nullptr || state_->cs_count_fd.value() < 0) return 0;
    const uint32_t key = 0;
    uint64_t       value = 0;
    if (bpf_map_lookup_elem(state_->cs_count_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineSchedEvent, SchedTpBtf>
SchedTpBtf::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) {
        return safety::Borrowed<const TimelineSchedEvent, SchedTpBtf>{};
    }
    auto* base = state_->timeline_base.get();
    auto* events = reinterpret_cast<const TimelineSchedEvent*>(
        const_cast<const uint8_t*>(base + sizeof(TimelineHeader)));
    return safety::Borrowed<const TimelineSchedEvent, SchedTpBtf>{
        events, TIMELINE_CAPACITY};
}

uint64_t SchedTpBtf::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) return 0;
    auto* base = state_->timeline_base.get();
    auto* hdr  = reinterpret_cast<const volatile TimelineHeader*>(base);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SchedTpBtf::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SchedTpBtf::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

}  // namespace crucible::perf
