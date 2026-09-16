#pragma once

// The compiler floor, the attribute vocabulary and the invariant macros for
// the foundation layer.  This header names nothing above foundation:: and
// std::, so every foundation header can include it.
//
// The macro prefix stays CRUCIBLE_ on purpose.  Macros have no namespace, the
// repository is crucible, and the layer rule (scripts/check-layer-boundary.sh)
// is stated over namespace roots and include roots, not over macro names.  A
// crucible/ consumer that flips to this header keeps every spelling it has.

#include <contracts>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>
#include <version>

// GCC reports 202400L for `-std=c++26`, the draft value. The floor sits there
// rather than at the published 202600L.
static_assert(__cplusplus >= 202400L, "foundation requires C++26 (-std=c++26)");

#if !defined(__clang__)
static_assert(__GNUC__ >= 16, "foundation requires GCC 16 for -fcontracts and -freflection");
#endif

#define CRUCIBLE_INLINE [[gnu::always_inline]] inline
#define CRUCIBLE_HOT [[gnu::hot, gnu::always_inline]] inline
#define CRUCIBLE_COLD [[gnu::cold, gnu::noinline]]
#define CRUCIBLE_FLATTEN [[gnu::flatten]]
#define CRUCIBLE_NOINLINE [[gnu::noinline]]

// CONST is the stronger of the two. A PURE function may read memory. A CONST
// function must derive its result from its arguments alone.
#define CRUCIBLE_PURE [[gnu::pure, nodiscard]]
#define CRUCIBLE_CONST [[gnu::const, nodiscard]]

#define CRUCIBLE_NONNULL [[gnu::nonnull]]
#define CRUCIBLE_RETURNS_NONNULL [[gnu::returns_nonnull]]
#define CRUCIBLE_MALLOC [[gnu::malloc]]
#define CRUCIBLE_ALLOC_SIZE(n) [[gnu::alloc_size(n)]]
#define CRUCIBLE_ASSUME_ALIGNED(n) [[gnu::assume_aligned(n)]]

// This is a requirement, not a hint. A call the compiler cannot turn into a
// tail call is a compile error.
#define CRUCIBLE_MUSTTAIL [[gnu::musttail]]

#define CRUCIBLE_API __attribute__((visibility("default")))

// The pause hint tells the core that the loop it is in is a spin. It changes
// power draw and the pipeline-flush penalty on loop exit, not the wait itself,
// so an architecture with no such hint spins correctly without one.

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define CRUCIBLE_SPIN_PAUSE _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CRUCIBLE_SPIN_PAUSE __asm__ volatile("yield")
#else
#define CRUCIBLE_SPIN_PAUSE ((void)0)
#endif

// The guard is mandatory, not cosmetic. GCC does not merely ignore an unknown
// [[clang::...]] or [[gsl::...]] attribute, it warns on it.

#if defined(__clang__)
#define CRUCIBLE_LIFETIMEBOUND [[clang::lifetimebound]]
#define CRUCIBLE_OWNER [[gsl::Owner]]
#define CRUCIBLE_POINTER [[gsl::Pointer]]
#define CRUCIBLE_UNSAFE_BUFFER_USAGE [[clang::unsafe_buffer_usage]]
#else
#define CRUCIBLE_LIFETIMEBOUND
#define CRUCIBLE_OWNER
#define CRUCIBLE_POINTER
#define CRUCIBLE_UNSAFE_BUFFER_USAGE
#endif

// The name says relocatable but the check is trivially-copyable. Trivial
// relocatability is a strictly larger set and is not portable across the
// supported compilers, and bulk memcpy needs only trivial copyability.
#define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(T) \
    static_assert(std::is_trivially_copyable_v<T>, #T " must be trivially copyable for Arena memcpy safety")

// The strict variant is for a type whose fields are addressed by computed
// offset. A hierarchy carrying data members in both base and derived cannot
// satisfy it and takes the plain macro.
#define CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(T)                                                       \
    static_assert(std::is_trivially_copyable_v<T>, #T " must be trivially copyable for Arena memcpy safety"); \
    static_assert(std::is_standard_layout_v<T>, #T " must be standard-layout so offsetof() and "              \
                                                   "serialize/deserialize via per-field offsets is "          \
                                                   "well-defined")

// The standard library declares breakpoint, breakpoint_if_debugging and
// is_debugger_present in <debugging> but ships no definitions for them, so
// the assertion macros below would fail to link against the standard names.
// These are stand-ins with the same behaviour.

namespace foundation::detail {

[[nodiscard]] inline bool is_debugger_present() noexcept {
    int fd = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[4096];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    const char* p = buf;
    while ((p = std::strstr(p, "TracerPid:")) != nullptr) {
        p += sizeof("TracerPid:") - 1;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p != '0' && *p >= '0' && *p <= '9') return true;
        break;
    }
    return false;
}

[[gnu::always_inline]] inline void breakpoint() noexcept { __builtin_trap(); }

[[gnu::always_inline]] inline void breakpoint_if_debugging() noexcept {
    if (is_debugger_present()) [[unlikely]]
        __builtin_trap();
}

// The failure arm shared by the two assertion macros below.
//
// Outlined behind a cold call, a use site keeps the compare and a
// five-instruction call stub, and the rest moves to .text.unlikely.  noreturn
// is what lets the caller drop everything after the call.  The tracer is read
// once: written out, the check ran twice, once for the message and once inside
// breakpoint_if_debugging.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void fail_invariant(const char* what, const char* predicate,
                                                                     const char* file, int line) noexcept {
    const bool traced = is_debugger_present();
    if (!traced) {
        std::fprintf(stderr, "foundation: %s failed: %s (%s:%d)\n", what, predicate, file, line);
    } else {
        // Stop in the debugger at the failure, not inside abort.
        __builtin_trap();
    }
    std::abort();
}

}  // namespace foundation::detail

// CRUCIBLE_INVARIANT states a fact the optimizer may rely on, so under NDEBUG
// it carries no check at all. The debug path traps only when a debugger is
// attached: an unconditional trap would kill an unattended run with SIGTRAP
// before the diagnostic above it could be read.

#define CRUCIBLE_ASSERT(cond) contract_assert(cond)

#ifdef NDEBUG
#define CRUCIBLE_DEBUG_ASSERT(cond) ((void)0)
#else
#define CRUCIBLE_DEBUG_ASSERT(cond) contract_assert(cond)
#endif

#ifdef NDEBUG
#define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
#define CRUCIBLE_INVARIANT(cond)                                                          \
    do {                                                                                  \
        if (!(cond)) [[unlikely]] {                                                       \
            ::foundation::detail::fail_invariant("invariant", #cond, __FILE__, __LINE__); \
        }                                                                                 \
    } while (0)
#endif

// This one checks in every build mode. Reach for it where the body cannot
// continue safely if the predicate fails, such as a noexcept constructor
// handed an unusable argument.
#define CRUCIBLE_FATAL_INVARIANT(cond)                                                          \
    do {                                                                                        \
        if (!(cond)) [[unlikely]] {                                                             \
            ::foundation::detail::fail_invariant("fatal invariant", #cond, __FILE__, __LINE__); \
        }                                                                                       \
    } while (0)
