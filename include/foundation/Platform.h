#pragma once

// The compiler floor, the platform floor, the attribute vocabulary and the
// invariant macros for the foundation layer.  This header names nothing above
// foundation:: and std::, so every foundation header can include it.
//
// The macro prefix stays CRUCIBLE_ on purpose.  Macros have no namespace, the
// repository is crucible, and the layer rule (utils/scripts/check-layer-boundary.py)
// is stated over namespace roots and include roots, not over macro names.  A
// crucible/ consumer that flips to this header keeps every spelling it has.

// The platform floor.  The tree assumes a 64-bit, little-endian x86_64 or
// aarch64 target with 64-byte cache lines.  These checks refuse a target that
// breaks one of these assumptions.  Each message states the rule and the code
// to audit before the rule changes.  The two #error checks come before the
// first include.  On a target outside the floor, a system header can fail
// first, with a message that names no rule.

#if !defined(__x86_64__) && !defined(__aarch64__)
#error \
    "foundation supports x86_64 and aarch64 only. Before you add an architecture, audit each alignas(64), CRUCIBLE_SPIN_PAUSE, each counter read in fixy/os/Time.h, and each SIMD path and intrinsic."
#endif

// Apple aarch64 cores have 128-byte cache lines.  No macro identifies an
// Apple core under Linux.  This check refuses only a target that defines
// __APPLE__.
#if defined(__APPLE__) && defined(__aarch64__)
#error \
    "Apple aarch64 cores have 128-byte cache lines, and the tree assumes 64-byte lines. Before you add this target, audit each alignas(64) and each struct whose size is one cache line."
#endif

#include <bit>
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

static_assert(sizeof(void*) == 8,
              "foundation supports 64-bit targets only. Before you add a 32-bit target, audit each size and offset "
              "calculation, each struct with a fixed size, and each tag in the bits of a pointer.");

static_assert(std::endian::native == std::endian::little,
              "foundation supports little-endian targets only. Before you add a big-endian target, audit each file "
              "format and wire format that the tree reads or writes as raw bytes, and each hash of raw bytes.");

// GCC gives two interference sizes as predefined macros.  For each x86
// target, GCC sets the two sizes to 64.  For an aarch64 core whose GCC tuning
// gives an L1 line, GCC sets the two sizes to that line.  For each other
// aarch64 target, GCC sets the constructive size to 64 and the destructive
// size to 256, which covers the range of aarch64 lines and names no line.
// Only a tuned core with a longer line gives a constructive size other than
// 64.  The checks read the macros and not
// std::hardware_destructive_interference_size, because -Winterference-size
// refuses a use of that constant in a header.
static_assert(__GCC_CONSTRUCTIVE_SIZE == 64,
              "the tree assumes 64-byte cache lines, and GCC gives a longer line for the target core. Before you "
              "add this target, audit each alignas(64) and each struct whose size is one cache line.");

#if defined(__x86_64__)
static_assert(__GCC_DESTRUCTIVE_SIZE == 64,
              "the tree separates two shared atomics by 64 bytes, and --param destructive-interference-size gives "
              "GCC another distance. Before you change the distance, audit each alignas(64).");
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

// One object for the whole process, whatever shared library reads it.  The
// build hides symbols by default, so each shared library that compiles a
// header holds its own copy of each inline variable and of each static of an
// inline function in that header.  With default visibility, GCC emits such an
// object, and the guard of a static local, as a unique global symbol.  The
// dynamic linker binds every copy of a unique symbol to the first copy that
// it loads, also across RTLD_LOCAL, which is how Python loads the vessel.
//
// Put the marker on an inline variable, or on the inline function that holds
// the object as a static local.  On a thread_local object, each thread then
// has one object for the process.  The marker has no effect in a template: an
// instantiation takes the smallest visibility of the template and of its
// arguments, and each type of this tree is hidden.  A templated object that
// must be one per process goes through a non-template object with a key.
// utils/scripts/check-process-wide-state.py reads every object with static storage
// duration, and holds each one to this marker or to its roster.
#define CRUCIBLE_PROCESS_WIDE [[gnu::visibility("default")]]

// The pause hint tells the core that the loop it is in is a spin. It changes
// power draw and the pipeline-flush penalty on loop exit, not the wait itself.
// The platform floor admits x86_64 and aarch64 only, and each has a hint.

#if defined(__x86_64__)
#include <immintrin.h>
#define CRUCIBLE_SPIN_PAUSE _mm_pause()
#elif defined(__aarch64__)
#define CRUCIBLE_SPIN_PAUSE __asm__ volatile("yield")
#endif

#if defined(__clang__)
#define CRUCIBLE_CAPABILITY(name) __attribute__((capability(name)))

#define CRUCIBLE_GUARDED_BY(cap) __attribute__((guarded_by(cap)))
#define CRUCIBLE_PT_GUARDED_BY(cap) __attribute__((pt_guarded_by(cap)))

#define CRUCIBLE_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define CRUCIBLE_REQUIRES_SHARED(...) __attribute__((requires_shared_capability(__VA_ARGS__)))
#define CRUCIBLE_EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))

#define CRUCIBLE_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define CRUCIBLE_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define CRUCIBLE_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))

#define CRUCIBLE_NO_THREAD_SAFETY __attribute__((no_thread_safety_analysis))

#define CRUCIBLE_ASSERT_CAPABILITY(cap) __attribute__((assert_capability(cap)))
#else
#define CRUCIBLE_CAPABILITY(name)
#define CRUCIBLE_GUARDED_BY(cap)
#define CRUCIBLE_PT_GUARDED_BY(cap)
#define CRUCIBLE_REQUIRES(...)
#define CRUCIBLE_REQUIRES_SHARED(...)
#define CRUCIBLE_EXCLUDES(...)
#define CRUCIBLE_ACQUIRE(...)
#define CRUCIBLE_RELEASE(...)
#define CRUCIBLE_TRY_ACQUIRE(...)
#define CRUCIBLE_NO_THREAD_SAFETY
#define CRUCIBLE_ASSERT_CAPABILITY(cap)
#endif

// These four expand to nothing, on every build this project produces,
// and that is stated here rather than left to be discovered.
//
// The previous definition guarded a clang attribute behind
// `#if defined(__clang__)`, which read as "two compilers, one of them
// arms this".  Both halves were false.  GCC 16 is the only supported
// compiler, and it recognises none of these: __has_cpp_attribute
// answers 0 for clang::lifetimebound, gnu::lifetimebound, the
// unqualified lifetimebound, gsl::Owner, gsl::Pointer and
// clang::unsafe_buffer_usage.  So 121 annotated sites across the two
// trees had never once been armed, and the shape of the definition hid
// that behind a compiler nobody builds with.
//
// CRUCIBLE_LIFETIMEBOUND is therefore a CLAIM, not a mechanism.  What
// enforces it is ordinary C++ that GCC does honour: a deleted overload
// taking an rvalue reference in the annotated position, which refuses a
// temporary at the call site.  utils/scripts/check-lifetime-twin.py reads the
// token and requires that overload, so the claim is checked even though
// no compiler understands it.  Where no overload can refuse the
// argument, as with a raw pointer whose pointee's lifetime no overload
// set can see, the site states the reason instead and carries no
// annotation: an annotation that enforces nothing is a decoration.
//
// The other three have no consumer in this tree and are kept as names
// so a clang build, if one is ever made, does not fail to compile.
#define CRUCIBLE_LIFETIMEBOUND
#define CRUCIBLE_OWNER
#define CRUCIBLE_POINTER
#define CRUCIBLE_UNSAFE_BUFFER_USAGE

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
// Written inline, that arm is a tracer probe, three address loads, a format
// call and a trap, which the compiler lays down next to the code being
// guarded. The guard itself is one compare, so the diagnostic outweighs it
// by an order of magnitude and shares the instruction line with a hot body
// that never executes it. Outlined behind a cold call, a use site keeps the
// compare and a five-instruction call stub, and the rest moves to
// .text.unlikely. That is the shape the code guide asks for.
//
// noreturn is what lets the caller drop everything after the call. It is the
// same promise std::abort already carries, so the failure arm was terminal
// before this and stays terminal now: a SIGABRT handler that jumps out, as
// the test probe does, leaves through the jump rather than returning here.
//
// The tracer is read once. Written out, the check ran twice, once for the
// message and once inside breakpoint_if_debugging.
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
