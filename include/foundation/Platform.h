#pragma once

// The compiler floor, the platform floor, the attribute vocabulary and the
// invariant macros for the foundation layer.  This header names nothing above
// foundation:: and std::, so every foundation header can include it.
//
// The macro prefix stays CRUCIBLE_ on purpose.  Macros have no namespace, the
// repository is crucible, and the layer rule (the rules layer-boundary and layer-residual)
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

#include <type_traits>

// GCC reports 202400L for `-std=c++26`, the draft value. The floor sits there
// rather than at the published 202600L.
static_assert(__cplusplus >= 202400L, "foundation requires C++26 (-std=c++26)");

#if !defined(__clang__)
static_assert(__GNUC__ >= 16, "foundation requires GCC 16 for -fcontracts and -freflection");
#endif

static_assert(sizeof(void*) == 8,
              "foundation supports 64-bit targets only. Before you add a 32-bit target, audit each size and offset "
              "calculation, each struct with a fixed size, and each tag in the bits of a pointer.");

// GCC gives the byte order of the target as a predefined macro, so the check
// includes no header.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
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

// THE TARGET NAMESPACE
//   The source of some inline functions of the base reads the vector
//   extensions of the target: an arm on a macro such as __AVX512F__, or a
//   constant that such an arm sets, such as simd::native_lanes.  If a program
//   links two units that two -march flags compile, such a function has two
//   bodies with one name.  The link keeps one body for the two units, so a
//   body of AVX-512 can run in the unit of a smaller target.
//
//   So each such function, and each such constant, is in the inline namespace
//   CRUCIBLE_SIMD_TARGET of its namespace.  The name of its symbol then holds
//   the target, and each unit keeps its own body.  A name of the outer
//   namespace finds it, so no caller writes the target.  foundation/Simd.h
//   gives the rule for new code.
//
//   The name of the target is the highest step of the ladder of the macros
//   that the arms read: sse2, sse42, avx, avx2, avx512f and avx512bw on
//   x86_64, and neon on aarch64.  A build with no step has the name
//   target_scalar.  GCC sets each lower step with a higher step, so the steps
//   of a build are a prefix of the ladder, and the check below refuses each
//   other set.  An arm on a macro outside the ladder needs a step of its own
//   first.
//
//   The namespace holds only a function whose source reads the target, and
//   an out-of-line function that such a function calls, such as the exact
//   form of fp::fma (foundation/core/Float.h).  GCC also picks the
//   instructions of each other inline function from the flags, and no name
//   holds that choice.  So a binary compiles each of its units for one
//   target, and a test links two targets only to probe the namespace.
//
//   A function that a source of a library defines, and that a header
//   declares for other units, is not in the namespace.  Its name then holds
//   no target, so a unit of another level links a call of it, and the call
//   runs the body of the level of the library.  Its body calls the helpers of
//   its source in the namespace.  An example is detail::compute_keyed_hash_
//   of foundation/core/KeyedHash.h.
#if defined(__x86_64__)
#if (defined(__AVX512BW__) && !defined(__AVX512F__)) || (defined(__AVX512F__) && !defined(__AVX2__)) \
    || (defined(__AVX2__) && !defined(__AVX__)) || (defined(__AVX__) && !defined(__SSE4_2__))        \
    || (defined(__SSE4_2__) && !defined(__SSE2__))
#error \
    "the vector extensions of the target skip a step of the ladder sse2, sse42, avx, avx2, avx512f, avx512bw. Each step implies the steps below it, and CRUCIBLE_SIMD_TARGET names the highest step. Before you add such a target, give it a step of its own."
#endif
#if defined(__AVX512BW__)
#define CRUCIBLE_SIMD_TARGET target_avx512bw
#elif defined(__AVX512F__)
#define CRUCIBLE_SIMD_TARGET target_avx512f
#elif defined(__AVX2__)
#define CRUCIBLE_SIMD_TARGET target_avx2
#elif defined(__AVX__)
#define CRUCIBLE_SIMD_TARGET target_avx
#elif defined(__SSE4_2__)
#define CRUCIBLE_SIMD_TARGET target_sse42
#elif defined(__SSE2__)
#define CRUCIBLE_SIMD_TARGET target_sse2
#else
#define CRUCIBLE_SIMD_TARGET target_scalar
#endif
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#define CRUCIBLE_SIMD_TARGET target_neon
#else
#define CRUCIBLE_SIMD_TARGET target_scalar
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
// The rule process-wide-state of the quarantine plugin reads every object with
// static storage duration, and holds each one to this marker or to its ledger.
// With the plugin, the marker also carries the attribute
// quarantine::process_wide, which the plugin reads.  The attribute visibility
// can also export a function, so the plugin does not read it.
#if defined(CRUCIBLE_QUARANTINE_ACTIVE)
#define CRUCIBLE_PROCESS_WIDE [[gnu::visibility("default"), quarantine::process_wide]]
#else
#define CRUCIBLE_PROCESS_WIDE [[gnu::visibility("default")]]
#endif

// The pause hint tells the core that the loop it is in is a spin. It changes
// power draw and the pipeline-flush penalty on loop exit, not the wait itself.
// The platform floor admits x86_64 and aarch64 only, and each has a hint.
// The x86_64 hint is the builtin that _mm_pause wraps, so no translation unit
// pays for the parse of an intrinsics header.

#if defined(__x86_64__)
#define CRUCIBLE_SPIN_PAUSE __builtin_ia32_pause()
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

// The last three of these four expand to nothing, on every build this
// project produces, and that is stated here rather than left to be
// discovered.  CRUCIBLE_LIFETIMEBOUND expands to the attribute
// quarantine::lifetimebound when the build loads the quarantine plugin, and
// to nothing with no plugin.  No code generator reads that attribute.
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
// temporary at the call site.  The rule lifetime-twin of the quarantine
// plugin reads the attribute and requires that overload, so the claim is
// checked although no code generator understands it.  Where no overload
// can refuse the argument, as with a raw pointer whose pointee's lifetime no overload
// set can see, the site states the reason instead and carries no
// annotation: an annotation that enforces nothing is a decoration.
//
// The other three have no consumer in this tree and are kept as names
// so a clang build, if one is ever made, does not fail to compile.
#if defined(CRUCIBLE_QUARANTINE_ACTIVE)
#define CRUCIBLE_LIFETIMEBOUND [[quarantine::lifetimebound]]
#else
#define CRUCIBLE_LIFETIMEBOUND
#endif
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
// is_debugger_present in <debugging> but ships no definitions for them.
// These are stand-ins with the same behaviour.  No failure arm calls them:
// each failure arm ends by SIGABRT, also under a tracer, so the end of a
// death test does not depend on a debugger.
//
// src/foundation/Platform.cpp holds the bodies of is_debugger_present and
// fail_invariant.  So no includer of this header compiles them, and this
// header includes no system header and no stdio.  Each program that expands
// an assertion macro links the library foundation.

namespace foundation::detail {

// True when a tracer, such as a debugger, is attached to the process.  The
// probe reads TracerPid of /proc/self/status one time for each call.  A
// failed read gives false.
[[nodiscard, gnu::cold]] bool is_debugger_present() noexcept;

[[gnu::always_inline]] inline void breakpoint() noexcept { __builtin_trap(); }

[[gnu::always_inline]] inline void breakpoint_if_debugging() noexcept {
    if (is_debugger_present()) [[unlikely]]
        __builtin_trap();
}

// The failure arm of the check macro below.  It writes
// "foundation: WHAT failed: PREDICATE (FILE:LINE)" to standard error, and
// then it calls abort, also under a tracer.  A debugger stops at the
// SIGABRT, and its stack holds the frame of the failure.  The write uses the
// sink of the Report family and no stdio, so a call from a signal handler is
// safe.
//
// The call is cold and is not inline, so a use site keeps one compare and
// one call, and the arm stays out of the instruction lines of the hot code.
// noreturn lets the caller drop each instruction after the call.  A SIGABRT
// handler that jumps out, as the test probe does, leaves through the jump.
[[noreturn, gnu::cold]] void fail_invariant(const char* what, const char* predicate, const char* file,
                                            int line) noexcept;

}  // namespace foundation::detail

// The check of a fact in the middle of a body, which correct code always
// holds.  A precondition at the start of a body is CRUCIBLE_PRE, and a
// postcondition before a return is CRUCIBLE_POST (foundation/contracts/).
// This check does the test in each build, under NDEBUG and under each
// contract semantic.  So it gives the optimizer no assumption, also in a
// unit with the ignore semantic, where CRUCIBLE_PRE is an [[assume]].  A
// false predicate ends the process through fail_invariant.
//
// Use it in place of CRUCIBLE_PRE in two cases:
//   - The check must hold under the ignore semantic, for example a check
//     that reads a load, which an [[assume]] cannot hold.
//   - The check follows a test that the optimizer can fold it into, such
//     as a switch on the same kind.  GCC 16 folds this plain branch.  It
//     keeps the test of a contract_assert after such a switch.
#define CRUCIBLE_FATAL_INVARIANT(cond)                                                          \
    do {                                                                                        \
        if (!(cond)) [[unlikely]] {                                                             \
            ::foundation::detail::fail_invariant("fatal invariant", #cond, __FILE__, __LINE__); \
        }                                                                                       \
    } while (0)
