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
//     are pure volatile loads. No syscalls on the read path.

#include "bpf_senses.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
extern const unsigned char sense_hub_bpf_bytecode[];
extern const unsigned int  sense_hub_bpf_bytecode_len;
}

namespace bench::bpf {

namespace {

[[nodiscard]] bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}

[[nodiscard]] bool quiet()   noexcept { return env_true("CRUCIBLE_BENCH_BPF_QUIET"); }
[[nodiscard]] bool verbose() noexcept { return env_true("CRUCIBLE_BENCH_BPF_VERBOSE"); }

// Silence libbpf's INFO/WARN spew by default — we emit a single clean
// diagnostic from load() on failure. Forward everything when the user
// explicitly asks for verbose output.
int libbpf_log_cb(enum libbpf_print_level, const char* fmt, va_list args) {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

// .rodata maps are named "<progname>.rodata" with progname truncated to
// fit 15 chars total in the map name. Substring match is the robust path.
[[nodiscard]] struct bpf_map* find_rodata(struct bpf_object* obj) noexcept {
    struct bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        const char* n = bpf_map__name(map);
        if (n != nullptr && std::strstr(n, ".rodata") != nullptr) return map;
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

} // namespace

struct SenseHub::State {
    struct bpf_object*               obj      = nullptr;
    std::vector<struct bpf_link*>    links{};
    volatile uint64_t*               counters = nullptr;
    size_t                           mmap_len = 0;

    State() = default;

    State(const State&)            = delete;
    State& operator=(const State&) = delete;
    State(State&&)                 = delete;
    State& operator=(State&&)      = delete;

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
    libbpf_set_print(libbpf_log_cb);

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
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_senses";
    state->obj = bpf_object__open_mem(
        sense_hub_bpf_bytecode,
        static_cast<size_t>(sense_hub_bpf_bytecode_len),
        &opts);
    if (state->obj == nullptr) {
        report("bpf_object__open_mem failed", errno);
        return std::nullopt;
    }

    // ── 2. Rewrite target_tgid in .rodata to our PID ───────────────
    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            std::vector<uint8_t> rewritten(
                static_cast<const uint8_t*>(current),
                static_cast<const uint8_t*>(current) + vsz);
            const uint32_t tgid = static_cast<uint32_t>(::getpid());
            std::memcpy(rewritten.data(), &tgid, sizeof(tgid));  // target_tgid offset 0
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. Skip programs whose tracepoints aren't on this kernel ──
    disable_unavailable_programs(state->obj);

    // ── 4. Verify, JIT, allocate maps ──────────────────────────────
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed", -err);
        return std::nullopt;
    }

    // ── 5. Register our main TID in our_tids ───────────────────────
    if (struct bpf_map* m = bpf_object__find_map_by_name(state->obj, "our_tids");
        m != nullptr) {
        const int fd = bpf_map__fd(m);
        const uint32_t tid = static_cast<uint32_t>(::syscall(SYS_gettid));
        const uint8_t  one = 1;
        (void)bpf_map_update_elem(fd, &tid, &one, BPF_ANY);
    }

    // ── 6. Attach every autoload-enabled program ───────────────────
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, state->obj) {
        if (!bpf_program__autoload(prog)) continue;
        if (struct bpf_link* link = bpf_program__attach(prog); link != nullptr) {
            state->links.push_back(link);
        }
    }
    if (state->links.empty()) {
        report("no programs attached (kernel missing every tracepoint?)");
        return std::nullopt;
    }

    // ── 7. mmap the counters array ─────────────────────────────────
    struct bpf_map* counters_map = bpf_object__find_map_by_name(state->obj, "counters");
    if (counters_map == nullptr) {
        report("counters map not found in object");
        return std::nullopt;
    }
    const int counters_fd = bpf_map__fd(counters_map);

    const size_t bytes = NUM_COUNTERS * sizeof(uint64_t);
    const size_t page  = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    state->mmap_len    = (bytes + page - 1) & ~(page - 1);
    void* p = ::mmap(nullptr, state->mmap_len, PROT_READ, MAP_SHARED, counters_fd, 0);
    if (p == MAP_FAILED) {
        report("mmap of counters map failed", errno);
        return std::nullopt;
    }
    state->counters = static_cast<volatile uint64_t*>(p);

    SenseHub h;
    h.state_ = std::move(state);
    return h;
}

Snapshot SenseHub::read() const noexcept {
    Snapshot s;
    if (state_ == nullptr || state_->counters == nullptr) return s;
    const volatile uint64_t* __restrict src = state_->counters;
    for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
        s.counters[i] = src[i];  // volatile qualification forces one load per iteration
    }
    return s;
}

const volatile uint64_t* SenseHub::counters_ptr() const noexcept {
    return (state_ != nullptr) ? state_->counters : nullptr;
}

size_t SenseHub::attached_programs() const noexcept {
    return (state_ != nullptr) ? state_->links.size() : 0;
}

} // namespace bench::bpf
