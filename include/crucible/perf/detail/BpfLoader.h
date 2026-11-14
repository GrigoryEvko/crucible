#pragma once

// The adapter of the perf layer to libbpf: the names, the objects, the maps,
// the programs and the print callback of libbpf, and the probe of a
// tracepoint.  libbpf gives each of them as a raw pointer of C, and its print
// callback takes a printf format with a va_list, so the body of this header
// stands in one opt-out region of the class ABI.

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/handle/Once.h>
#include <fixy/os/Environment.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Process.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/PathText.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace crucible::perf::detail {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: libbpf gives its objects, maps, programs, links and names as raw pointers of C")

namespace source {
struct BpfMap {};
}  // namespace source

using Fd = ::fixy::Tagged<int, source::BpfMap>;

// A name of a BPF object, map or program.  libbpf reads it up to its zero,
// and a report reads the same characters as a TextView.  The macro
// CRUCIBLE_PERF_FOREIGN_NAME makes one from a string literal, so the pointer
// has static storage.
struct ForeignName {
    const char* abi = "";
    ::fixy::TextView text{};
};

// The bytecode of a BPF object, which the build embeds as an array of C
// linkage, and which bpf_object__open_mem reads.
struct ForeignBytes {
    const unsigned char* abi = nullptr;
    std::size_t size = 0;
};

#define CRUCIBLE_PERF_FOREIGN_NAME(literal) \
    (::crucible::perf::detail::ForeignName{.abi = (literal), .text = ::fixy::TextView{literal}})

// A load reads the tracepoint files of tracefs, the id of the process and
// the id of the calling thread, so its context admits the file calls and
// the identity reads.
template <class Ctx>
concept CtxFitsBpfLoad =
    ::foundation::effects::IsExecCtx<Ctx> && ::fixy::fs::CtxAdmitsFs<Ctx> && ::fixy::process::CtxFitsIdentityRead<Ctx>;

[[nodiscard]] inline Fd map_fd(bpf_map* m) noexcept { return ::fixy::mint_tagged<source::BpfMap>(bpf_map__fd(m)); }

// The characters of a name that libbpf gives, up to its zero.  A null name
// is empty.  The characters live as long as the libbpf object that holds
// the name.  Complexity: one pass over the characters.
[[nodiscard]] inline ::fixy::TextView foreign_name(const char* name) noexcept {
    if (name == nullptr) return ::fixy::TextView{};
    std::size_t count = 0;
    while (name[count] != '\0') {
        ++count;
    }
    return ::fixy::mint_foreign_text(name, count);
}

// A function-local static in an inline function has one instance
// across every translation unit that includes this header, so the
// first of these calls anywhere decides the answer for every caller.
// A setenv after that first call has no effect.

[[nodiscard]] inline bool env_true(::fixy::CText name) noexcept {
    return ::fixy::env::read_variable(name).match(
        [](::fixy::TextView value) noexcept { return ::fixy::starts_with(value, "1"); }, [] noexcept { return false; });
}

[[nodiscard]] inline bool quiet() noexcept {
    static const bool kQuiet = env_true("CRUCIBLE_PERF_QUIET");
    return kQuiet;
}

[[nodiscard]] inline bool verbose() noexcept {
    static const bool kVerbose = env_true("CRUCIBLE_PERF_VERBOSE");
    return kVerbose;
}

// libbpf prints to stderr by default, which is loud on a host without
// CAP_BPF, so every libbpf line passes through this gate instead.
// fixy::handle::Once carries the one-shot flag rather than
// std::call_once, whose pthread backing this path does not need.

[[gnu::format(printf, 2, 0)]] inline int libbpf_log_cb(libbpf_print_level, const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

// libbpf holds one print callback for the process, so the install is one per
// process too, whatever shared library asks for it.
CRUCIBLE_PROCESS_WIDE inline void install_libbpf_log_cb_once() noexcept {
    static ::fixy::handle::Once once;
    once.call([] { libbpf_set_print(libbpf_log_cb); });
}

// libbpf names the map "<object name>.rodata", so the match is on the
// suffix rather than on the whole name.

[[nodiscard]] inline bpf_map* find_rodata(bpf_object* obj) noexcept {
    bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        if (::fixy::ends_with(foreign_name(bpf_map__name(map)), ".rodata")) return map;
    }
    return nullptr;
}

// The probe reads the status of the id file of the tracepoint.  The status
// read judges each directory of the path with the effective ids and the
// capabilities of the process.  /sys/kernel/tracing is mode 0700 root, so a
// process that is not root needs CAP_DAC_READ_SEARCH to traverse it, and a
// setcap-ed process keeps that capability for the read.  The bare access
// syscall answers for the real uid, and the kernel then clears the
// effective capabilities for the length of the check, so every tracepoint
// would probe as missing.  Root reaches the same answer either way, because
// uid 0 bypasses DAC.

// The longest path of a probe: the debugfs root, the event and "/id".
inline constexpr std::size_t kTracepointPathBytes = 512;

template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] bool has_tracepoint_id(Ctx const& ctx, ::fixy::CText root, ::fixy::TextView event) noexcept {
    ::foundation::os::PathText<kTracepointPathBytes> path{};
    bool const is_built =
        path.append(root).is_ok() && path.append(event).is_ok() && path.append(::fixy::CText{"/id"}).is_ok();
    return is_built && ::fixy::fs::has_entry(ctx, path.c_text()).value_or(false);
}

// True when the kernel carries the tracepoint category_slash_event, at the
// tracefs path or at the debugfs path.  A probe that the kernel refuses
// reads as a missing tracepoint.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] bool tracepoint_exists(Ctx const& ctx, ::fixy::TextView category_slash_event) noexcept {
    return has_tracepoint_id(ctx, "/sys/kernel/tracing/events/", category_slash_event)
        || has_tracepoint_id(ctx, "/sys/kernel/debug/tracing/events/", category_slash_event);
}

// A load fails for the whole object when any one program names a
// tracepoint this kernel does not carry, so the missing ones lose
// their autoload flag before the load rather than after it.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
void disable_unavailable_programs(Ctx const& ctx, bpf_object* obj) noexcept {
    constexpr ::fixy::TextView kPrefix = "tracepoint/";
    bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        ::fixy::TextView const section = foreign_name(bpf_program__section_name(prog));
        if (!::fixy::starts_with(section, kPrefix)) continue;
        ::fixy::TextView const event =
            section.window(kPrefix.size(), section.size() - kPrefix.size()).value_or(::fixy::TextView{});
        if (!tracepoint_exists(ctx, event)) {
            (void)bpf_program__set_autoload(prog, false);
        }
    }
}

// The error number of the libbpf call that gave p, or zero for a pointer
// that is no error.  libbpf 1.0 and later give null and set errno, and
// libbpf_get_error reads that errno for a null pointer, so the read is
// right only directly after the call.  An older libbpf encodes the error in
// the pointer, and libbpf_get_error decodes it.  The sign flips to the
// POSIX convention.

[[nodiscard]] inline int libbpf_errno(const void* p) noexcept { return static_cast<int>(-libbpf_get_error(p)); }

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace crucible::perf::detail
