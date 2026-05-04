// crucible::perf::SyscallTpBtf — libbpf binding implementation.
//
// Seventh per-program facade.  Mirrors src/perf/SyscallLatency.cpp's
// loader nearly exactly — only the bytecode symbol
// (`syscall_tp_btf_bpf_bytecode` vs `syscall_latency_bpf_bytecode`)
// and the diagnostic facade name differ.  See SchedSwitch.cpp's
// docblock for the rationale on why the duplication is INTENTIONAL
// (Promote-First; GAPS-004x extracts the shared helper).
//
// SyscallTpBtf-specific notes (vs SyscallLatency):
//   • SEC("tp_btf/sys_enter") + SEC("tp_btf/sys_exit") instead of
//     the legacy raw_syscalls tracepoints.  ~30% lower per-event
//     cost via BTF + CO-RE; needs CONFIG_DEBUG_INFO_BTF=y + kernel
//     ≥ 5.5.
//   • Same all-or-nothing attach policy: BOTH programs must attach
//     or load() returns nullopt — a half-attach (only sys_enter, no
//     sys_exit) would record syscall_start entries that never get
//     consumed on exit, leaking until the LRU_HASH evicts them.

#include <crucible/perf/SyscallTpBtf.h>

#include <crucible/perf/detail/BpfLoader.h>  // GAPS-004x shared loader helpers

#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>

#include <sys/mman.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <inplace_vector>

extern "C" {
extern const unsigned char syscall_tp_btf_bpf_bytecode[];
extern const unsigned int  syscall_tp_btf_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// Shared loader helpers from detail::BpfLoader (GAPS-004x).
// BTF-typed tp_btf programs attach via BTF, not by tracepoint name.
namespace source = ::crucible::perf::detail::source;
using ::crucible::perf::detail::Tgid;
using ::crucible::perf::detail::Tid;
using ::crucible::perf::detail::Fd;
using ::crucible::perf::detail::current_tgid;
using ::crucible::perf::detail::map_fd;
using ::crucible::perf::detail::find_rodata;
using ::crucible::perf::detail::libbpf_errno;
using ::crucible::perf::detail::install_libbpf_log_cb_once;
using ::crucible::perf::detail::quiet;
using ::crucible::perf::detail::verbose;

}  // namespace

struct SyscallTpBtf::State : crucible::safety::NonMovable<SyscallTpBtf::State> {
    struct bpf_object*                       obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};
    safety::WriteOnceNonNull<volatile uint8_t*> timeline_base{};
    safety::WriteOnce<size_t>                   timeline_len{};
    Fd                                          total_syscalls_fd{-1};
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

SyscallTpBtf::SyscallTpBtf() noexcept = default;
SyscallTpBtf::SyscallTpBtf(SyscallTpBtf&&) noexcept = default;
SyscallTpBtf& SyscallTpBtf::operator=(SyscallTpBtf&&) noexcept = default;
SyscallTpBtf::~SyscallTpBtf() = default;

std::optional<SyscallTpBtf> SyscallTpBtf::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] syscall_tp_btf unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] syscall_tp_btf unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_syscall_tp_btf";
    struct bpf_object* obj = bpf_object__open_mem(
        syscall_tp_btf_bpf_bytecode,
        static_cast<size_t>(syscall_tp_btf_bpf_bytecode_len),
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

    // ── 5. (No our_tids registration — filters on target_tgid only.)

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
                    "[crucible::perf] syscall_tp_btf attach failed for %s (%s)\n",
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
                    "[crucible::perf] syscall_tp_btf link capacity exhausted "
                    "(bump inplace_vector size)\n");
            }
            continue;
        }
        state->links.push_back(link);
    }
    // Both tp_btf/sys_enter AND sys_exit required.  Same all-or-
    // nothing policy as SyscallLatency.cpp.
    if (state->links.size() < 2) {
        report("expected 2 tp_btf attachments (sys_enter + sys_exit), got fewer "
               "— kernel missing BTF for sys_enter/sys_exit, or partial CAP_BPF rejection");
        return std::nullopt;
    }

    // ── 7. mmap the syscall_timeline ring buffer ───────────────────
    struct bpf_map* timeline_map =
        bpf_object__find_map_by_name(state->obj, "syscall_timeline");
    if (timeline_map == nullptr) {
        report("syscall_timeline map not found in object (bytecode/header out of sync — rebuild)");
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
                                  TIMELINE_CAPACITY * sizeof(TimelineSyscallEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED,
                                timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of syscall_timeline failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }
    state->timeline_len.set(mmap_len_bytes);
    state->timeline_base.set(static_cast<volatile uint8_t*>(mmap_address));

    if (struct bpf_map* ts =
            bpf_object__find_map_by_name(state->obj, "total_syscalls");
        ts != nullptr) {
        state->total_syscalls_fd = map_fd(ts);
    } else {
        if (verbose()) {
            std::fprintf(stderr,
                "[crucible::perf] syscall_tp_btf total_syscalls map missing — "
                "total_syscalls() will return 0\n");
        }
    }

    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] syscall_tp_btf partial: %zu program(s) failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get());
    }

    SyscallTpBtf h;
    h.state_ = std::move(state);
    return h;
}

uint64_t SyscallTpBtf::total_syscalls() const noexcept {
    if (state_ == nullptr || state_->total_syscalls_fd.value() < 0) return 0;
    const uint32_t key = 0;
    uint64_t       value = 0;
    if (bpf_map_lookup_elem(state_->total_syscalls_fd.value(), &key, &value) != 0) {
        return 0;
    }
    return value;
}

safety::Borrowed<const TimelineSyscallEvent, SyscallTpBtf>
SyscallTpBtf::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) {
        return safety::Borrowed<const TimelineSyscallEvent, SyscallTpBtf>{};
    }
    auto* base = state_->timeline_base.get();
    auto* events = reinterpret_cast<const TimelineSyscallEvent*>(
        const_cast<const uint8_t*>(base + sizeof(TimelineHeader)));
    return safety::Borrowed<const TimelineSyscallEvent, SyscallTpBtf>{
        events, TIMELINE_CAPACITY};
}

uint64_t SyscallTpBtf::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) return 0;
    auto* base = state_->timeline_base.get();
    auto* hdr  = reinterpret_cast<const volatile TimelineHeader*>(base);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SyscallTpBtf::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t>
SyscallTpBtf::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

}  // namespace crucible::perf
