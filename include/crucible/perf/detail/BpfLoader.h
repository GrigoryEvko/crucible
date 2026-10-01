#pragma once

#include <fixy/Tagged.h>
#include <fixy/handle/Once.h>
#include <foundation/Platform.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error
#include <fcntl.h>  // AT_FDCWD, AT_EACCESS
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace crucible::perf::detail {

namespace source {
struct Kernel {};
struct BpfMap {};
}  // namespace source

using Tgid = ::fixy::Tagged<uint32_t, source::Kernel>;
using Tid = ::fixy::Tagged<uint32_t, source::Kernel>;
using Fd = ::fixy::Tagged<int, source::BpfMap>;

[[nodiscard]] inline Tgid current_tgid() noexcept {
    return ::fixy::mint_tagged<source::Kernel>(static_cast<uint32_t>(::getpid()));
}

[[nodiscard]] inline Tid current_tid() noexcept {
    return ::fixy::mint_tagged<source::Kernel>(static_cast<uint32_t>(::syscall(
        SYS_gettid)));  // SYSCALL-CAP-OK: a load under an Init context calls it, and gettid only reads the thread id
}

[[nodiscard]] inline Fd map_fd(bpf_map* m) noexcept { return ::fixy::mint_tagged<source::BpfMap>(bpf_map__fd(m)); }

// A function-local static in an inline function has one instance
// across every translation unit that includes this header, so the
// first of these calls anywhere decides the answer for every caller.
// A setenv after that first call has no effect.

[[nodiscard]] inline bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
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
        const char* n = bpf_map__name(map);
        if (n == nullptr) continue;
        const std::string_view name{n};
        if (name.ends_with(".rodata")) return map;
    }
    return nullptr;
}

// The probe runs through faccessat with AT_EACCESS rather than
// access(F_OK).  /sys/kernel/tracing is mode 0700 root, so a non-root
// process needs CAP_DAC_READ_SEARCH to traverse it.  The bare access
// syscall answers for the real uid, and the kernel therefore clears
// the effective capability set for the length of the check whenever
// the real uid is not 0 and SECURE_NO_SETUID_FIXUP is unset.  A
// setcap-ed process loses CAP_DAC_READ_SEARCH for that one call, every
// tracepoint then probes as missing, and every program is disabled.
// AT_EACCESS checks the effective ids and keeps the capabilities.
// Root reaches the same answer either way, because uid 0 bypasses DAC.

[[nodiscard]] inline bool tracepoint_exists(const char* category_slash_event) noexcept {
    std::string path = "/sys/kernel/tracing/events/";
    path.append(category_slash_event);
    path.append("/id");
    if (::faccessat(AT_FDCWD, path.c_str(), F_OK, AT_EACCESS) == 0)
        return true;  // SYSCALL-CAP-OK: a load under an Init context probes that the file exists, with the effective capabilities
    path.assign("/sys/kernel/debug/tracing/events/");
    path.append(category_slash_event);
    path.append("/id");
    return ::faccessat(AT_FDCWD, path.c_str(), F_OK, AT_EACCESS)
        == 0;  // SYSCALL-CAP-OK: a load under an Init context probes the debugfs path, with the effective capabilities
}

// A load fails for the whole object when any one program names a
// tracepoint this kernel does not carry, so the missing ones lose
// their autoload flag before the load rather than after it.
inline void disable_unavailable_programs(bpf_object* obj) noexcept {
    bpf_program* prog = nullptr;
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

// libbpf reports an error two ways.  A pointer-returning entry point
// encodes the errno into the returned pointer, and libbpf_get_error
// decodes it as a positive value, so the sign flips here to reach the
// POSIX convention.  An int-returning entry point already returns
// -errno and needs no decode.  The fallback covers a null pointer that
// carries no encoded error.

[[nodiscard]] inline int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

}  // namespace crucible::perf::detail
