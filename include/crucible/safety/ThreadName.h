#pragma once

// ── crucible::safety::ThreadName — pthread_setname_np as an Init-row mint ──
//
// FIXY-V-189 (Agent 6 §3.3 item 6).  A DIFFERENT shape from its four
// sibling wrappers (ClockSource / SchedClass / CpuPinned / SuspendBehavior,
// V-185..188): those are value-level Graded lattice carriers; THIS is a
// bounded compile-time thread-name type plus a ctx-gated syscall mint.  No
// lattice, no DimensionAxis, no row_hash — a thread name is not a graded
// value, it is a one-time kernel side effect performed during init.
//
// ── The two bug classes this header eliminates ──────────────────────
//
//   (1) SILENT TRUNCATION.  The Linux kernel caps a thread name at
//       TASK_COMM_LEN = 16 bytes (15 visible chars + NUL) and SILENTLY
//       truncates anything longer — no errno, no diagnostic.  A
//       `ThreadNameLiteral<N>` makes N > 16 a COMPILE ERROR, so the
//       truncation can never reach the kernel.
//
//   (2) WRONG-PHASE NAMING.  `pthread_setname_np` is a syscall (writes
//       /proc/self/task/<tid>/comm).  `mint_thread_name` admits only an
//       init-phase context (`effects::Init`, or an ExecCtx whose row
//       contains `Effect::Init`).  Hot-path code holds NO context, so it
//       structurally cannot perform the syscall.
//
// ── §XXI Universal Mint Pattern ─────────────────────────────────────
//
//   mint_thread_name<Name>(ctx) -> ThreadNamed<Name>
//     requires CtxIsInitPhase<Ctx>
//
// The mint names the CALLING thread (pthread_self) — the canonical model
// is "each thread names itself as its init step", which is exactly where
// the thread holds its own init-phase proof.  The returned `ThreadNamed<Name>`
// is a phantom witness (sizeof == 1) that observe/perf code can demand as
// proof a thread went through the naming ritual (correlating PERF_RECORD_COMM).
//
// Because the only documented failure of `pthread_setname_np(self, name)` is
// ERANGE (name too long) and `ThreadNameLiteral` excludes that statically,
// the mint cannot fail at runtime — it returns the witness directly, with no
// std::expected ceremony.
//
// HS14 negative coverage (two distinct mismatch classes):
//   - neg_thread_name_too_long   (TASK_COMM_LEN static_assert at the literal)
//   - neg_thread_name_wrong_ctx  (CtxIsInitPhase rejects a Bg context)

#include <crucible/Platform.h>
#include <crucible/effects/ExecCtx.h>          // IsExecCtx, row_type_of_t, row_contains_v, Effect, Init

#include <pthread.h>                            // pthread_setname_np, pthread_self

#include <cstddef>                              // std::size_t
#include <string_view>                          // self-test name comparison
#include <type_traits>                          // remove_cvref_t, is_same_v

namespace crucible::safety {

// ── ThreadNameLiteral<N> — the TASK_COMM_LEN-bounded fixed string ───
//
// A structural class type usable as a non-type template parameter (the
// `template <ThreadNameLiteral Name>` form deduces N via CTAD).  `N`
// counts the trailing NUL of the source string literal, so the kernel
// limit of 15 visible characters is `N <= 16`.
template <std::size_t N>
struct ThreadNameLiteral {
    // TASK_COMM_LEN == 16 (15 visible chars + NUL).  A source literal of
    // length N (NUL inclusive) names N-1 visible chars; N must fit the cap.
    static_assert(N >= 1, "ThreadNameLiteral: degenerate empty literal");
    static_assert(N <= 16,
        "FIXY-V-189: thread name exceeds TASK_COMM_LEN (15 visible chars + "
        "NUL); the Linux kernel would SILENTLY truncate it — shorten the name.");

    char data[N]{};

    consteval ThreadNameLiteral(const char (&literal)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) {
            data[index] = literal[index];
        }
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return data; }

    // Visible-character count (excludes the trailing NUL).
    static constexpr std::size_t visible_length = N - 1;
};

// ── ThreadNamed<Name> — the phantom naming witness ──────────────────
//
// Empty proof token (sizeof == 1, EBO-collapsible) returned by the mint.
// Carries the name at the type level so a downstream consumer can require
// `IsThreadNamed` proof without re-reading /proc.
template <ThreadNameLiteral Name>
struct [[nodiscard]] ThreadNamed {
    static constexpr ThreadNameLiteral name = Name;

    [[nodiscard]] static constexpr const char* c_str() noexcept { return Name.c_str(); }
    [[nodiscard]] static constexpr std::size_t visible_length() noexcept {
        return Name.visible_length;
    }
};

// ── IsThreadNamed concept + extractor ───────────────────────────────
namespace detail::thread_name_extract {

template <typename T>
inline constexpr bool is_thread_named_v = false;
template <ThreadNameLiteral Name>
inline constexpr bool is_thread_named_v<ThreadNamed<Name>> = true;

}  // namespace detail::thread_name_extract

template <typename T>
concept IsThreadNamed = detail::thread_name_extract::is_thread_named_v<std::remove_cvref_t<T>>;

// ── CtxIsInitPhase — the single §XXI mint gate ──────────────────────
//
// Admits either the bare `effects::Init` context (what `effects::testing
// ::init()` produces) OR any ExecCtx whose effect-row contains Effect::Init.
// The `||` short-circuits: when the left atom is false the right is not
// evaluated, so `row_type_of_t<Ctx>` is never named on a non-ExecCtx (the
// same guard pattern as ExecCtx.h's CtxAdmitsCap).
template <typename Ctx>
concept CtxIsInitPhase =
    std::same_as<std::remove_cvref_t<Ctx>, ::crucible::effects::Init>
    || (::crucible::effects::IsExecCtx<std::remove_cvref_t<Ctx>>
        && ::crucible::effects::row_contains_v<
               ::crucible::effects::row_type_of_t<std::remove_cvref_t<Ctx>>,
               ::crucible::effects::Effect::Init>);

// ── mint_thread_name — the Init-row syscall mint (§XXI) ─────────────
//
// §XXI carve-out: cx=alloc — the mint drops compile-time evaluation
// because it performs a real kernel side effect (the same carve-out a
// genuinely-allocating mint takes).  noexcept: the only documented failure
// mode (ERANGE) is statically excluded by ThreadNameLiteral's bound.
template <ThreadNameLiteral Name, typename Ctx>
    requires CtxIsInitPhase<Ctx>
[[nodiscard]] inline ThreadNamed<Name> mint_thread_name(Ctx const&) noexcept {
    // Self-name: the calling thread is the one being set up under `ctx`.
    // Return is ignored — ERANGE is impossible (visible_length <= 15) and
    // there is no other failure for a self-target on Linux.
    (void)::pthread_setname_np(::pthread_self(), Name.c_str());
    return ThreadNamed<Name>{};
}

// ── Layout invariants ───────────────────────────────────────────────
static_assert(sizeof(ThreadNamed<"x">) == 1, "ThreadNamed must be an empty witness");
static_assert(ThreadNameLiteral<2>{"x"}.visible_length == 1);
static_assert(ThreadNameLiteral<16>{"123456789012345"}.visible_length == 15);

// ── Self-test ────────────────────────────────────────────────────────
namespace detail::thread_name_self_test {

using namespace ::crucible::safety::detail::thread_name_extract;

// Distinct names yield distinct witness types.
static_assert(!std::is_same_v<ThreadNamed<"a">, ThreadNamed<"b">>);
static_assert( std::is_same_v<ThreadNamed<"a">, ThreadNamed<"a">>);

// Concept extractor.
static_assert( IsThreadNamed<ThreadNamed<"crucible-bg">>);
static_assert(!IsThreadNamed<int>);

// Compile-time name readout.
static_assert(ThreadNamed<"crucible-fg">::visible_length() == 11);
static_assert(std::string_view{ThreadNamed<"crucible-fg">::c_str()} == "crucible-fg");

// CtxIsInitPhase admits the bare Init context, rejects Bg / Test.
static_assert( CtxIsInitPhase<::crucible::effects::Init>);
static_assert(!CtxIsInitPhase<::crucible::effects::Bg>);
static_assert(!CtxIsInitPhase<::crucible::effects::Test>);

// Runtime smoke: actually mint a name through the init witness.  The
// returned witness's static accessors must agree with the literal.  (This
// renames the running test thread to "crux-smoke" — harmless.)
inline void runtime_smoke_test() {
    auto init = ::crucible::effects::testing::init();
    auto witness = mint_thread_name<"crux-smoke">(init);
    CRUCIBLE_INVARIANT(std::string_view{witness.c_str()} == "crux-smoke");
    CRUCIBLE_INVARIANT(witness.visible_length() == 10);
    static_assert(IsThreadNamed<decltype(witness)>);
}

}  // namespace detail::thread_name_self_test

}  // namespace crucible::safety
