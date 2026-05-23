#pragma once

// ── crucible::fixy::grant::ctrl — control-flow grant family (FIXY-V-244) ─
//
// Six control-flow grant tags + two marker tags that engage
// `DimensionAxis::ControlFlow` (substrate ordinal 24, added by V-238).
// Each grant declares HOW a function binding may transfer control out
// of band — by throwing, aborting, longjmp'ing, calling exit(),
// trapping, or suspending as a coroutine.  Every tag routes to
// `DimensionAxis::ControlFlow` via `which_dim` so Reject.h's engagement
// walk treats this axis uniformly, and so the CollisionCatalog hazard
// rules (C001 abort×non-AbortOnly, P003 fork×throw, L006 Linear×longjmp,
// V-243) can read the ControlFlow tier off the binding.
//
// ── The LOAD-BEARING grant: abort<Rationale> ──────────────────────────
//
// `abort<"reason">` is the keystone of this header.  Crucible has ~30
// production `std::abort` / `crucible_abort` sites (PoolAllocator OOM,
// CKernel table overflow, contract-violation handler, ...).  Each is an
// unrecoverable-by-design exit.  `abort<Rationale>` makes every one of
// them DECLARE its rationale in the type — grep-discoverable, reviewable,
// and (via row_hash on the value site) folded into the federation cache
// key.  The rationale is MANDATORY: `abort` carries no default template
// argument, so a bare `grant::ctrl::abort` is ill-formed.  An abort site
// that wants the grant MUST write the reason.
//
// ── Re-homing the V-087 forward-pioneer ───────────────────────────────
//
// V-087 shipped `crucible::fixy::ctrl::throws` (a NON-parametrized empty
// tag, in `fixy/ctrl/Throws.h`) before this header existed, so
// `mint_permission_fork`'s no-throw rail had a tag to search for.  Its
// header promised: "When V-238 + V-244 materialize the full ControlFlow
// axis, this tag re-homes via a thin using alias."  This header is that
// home: `grant::ctrl::throws<ExceptionFamily = any_exception>` is the
// canonical parametrized form, and `fixy/ctrl/Throws.h` re-homes
// `fixy::ctrl::throws` to `grant::ctrl::throws<>` so PermissionFork's
// type-tree search (which looks for the un-parametrized form) is
// type-identical to the new default specialization — no caller changes.
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  Every grant tag is an empty `final` struct inheriting
// `grant_base`; `sizeof == 1` standalone, EBO-collapses to 0 bytes in
// any aggregator.  `rationale<N>` is a compile-time fixed-string NTTP
// type that never materializes at runtime.  `which_dim_v<G>` is a single
// integral_constant member access.
//
// ── Substrate consumed ────────────────────────────────────────────────
//
//   crucible::fixy::dim::DimensionAxis   — enum cited by which_dim
//   crucible::fixy::grant::grant_base    — structural marker (Grant.h)
//   crucible::fixy::grant::which_dim     — primary template (Grant.h)
//   crucible::fixy::grant::accept_default_strict_for — engagement marker
//
// Per the namespace-purity discipline (CR-09), all `which_dim`
// specializations live syntactically inside `namespace
// crucible::fixy::grant`.  This header reopens that namespace;
// `scripts/check-fixy-grant-namespace-purity.sh` allowlists it alongside
// Grant.h / Fp.h / Fs.h / Mmap.h / Io.h / syscall/*.  The `grant::ctrl`
// sub-namespace open is NOT the locked namespace (the purity regex
// matches only the bare `grant {` reopen) and needs no allowlist entry.

#include <crucible/fixy/Grant.h>            // grant_base, which_dim, accept_default_strict_for
#include <crucible/fixy/Dim.h>              // dim::DimensionAxis::ControlFlow

#include <cstddef>
#include <type_traits>

// ═════════════════════════════════════════════════════════════════════
// ── The six grant tags + policy tags + markers (grant::ctrl) ──────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::ctrl {

// ─── rationale<N> — P3491R3 compile-time fixed-string NTTP type ───────
//
// A structural class type so `abort<"reason">` deduces `rationale<N>`
// from the bare string literal via CTAD-for-NTTP (P1907R1, GCC 16).
// String literals cannot be NTTPs as `const char*` (no address
// constancy); a fixed-string class is the standard idiom.  The
// `consteval` ctor copies the literal into `data[N]` so two abort sites
// with different rationale strings are DISTINCT types (distinct
// federation cache slots).
template <std::size_t N>
struct rationale final {
    char data[N]{};

    consteval rationale(const char (&literal)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) {
            data[index] = literal[index];
        }
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }
};

// ─── Policy tags (template arguments to the grants — NOT grants) ──────
//
// These do NOT inherit grant_base: they are never placed directly in a
// Grants pack (you write `exit<at_exit>`, not `at_exit`).  IsGrantTag is
// therefore false for them, so an accidental bare use in a pack is
// rejected by AllGrantsWellFormed.

// Default exception family for `throws<>` — "may throw, family
// unspecified".  This is the un-parametrized V-087 semantics.
struct any_exception final {};

// exit<CleanupPolicy> ∈ {at_exit, no_cleanup, exit_immediate}.
//   at_exit         — std::exit: runs atexit handlers + static dtors.
//   no_cleanup      — std::exit with no further cleanup expected.
//   exit_immediate  — std::_Exit / _exit: NO atexit, NO static dtors.
struct at_exit        final {};
struct no_cleanup     final {};
struct exit_immediate final {};

// coroutine<SuspensionPolicy> ∈ {co_await_only, generator, async_task}.
struct co_await_only final {};
struct generator     final {};
struct async_task    final {};

// ─── (a) throws<ExceptionFamily> — callable may throw (ThrowOnly tier) ─
//
// Opt-in marker for the ControlFlow ThrowOnly tier.  Banned by
// `-fno-exceptions` at the call site today, but the tag exists so a
// future `-fexceptions` island can declare its exception family.
// Default `any_exception` IS the re-homed V-087 `fixy::ctrl::throws`.
template <class ExceptionFamily = any_exception>
struct throws final : grant_base {};

// ─── (b) abort<Rationale> — LOAD-BEARING; rationale is mandatory ──────
//
// No default template argument: every abort grant MUST carry a reason.
template <rationale Reason>
struct abort final : grant_base {};

// ─── (c) longjmp_unsafe<Rationale> — MayLongjmp tier opt-in ───────────
//
// longjmp performs a non-local jump that SKIPS destructors (RAII-unsafe);
// a Linear resource held across a longjmp leaks (CollisionCatalog L006).
// Rationale mandatory, same discipline as abort.
template <rationale Reason>
struct longjmp_unsafe final : grant_base {};

// ─── (d) exit<CleanupPolicy> — process-exit grant ─────────────────────
template <class CleanupPolicy>
struct exit final : grant_base {};

// ─── (e) builtin_trap_ok / unreachable_ok — empty trap markers ────────
//
// For the existing `std::unreachable()` / `__builtin_trap()` sites
// (Graph.h, ExprPool.h, Platform.h).  Named without the leading
// double-underscore of the builtin (reserved-identifier rule).
struct builtin_trap_ok final : grant_base {};
struct unreachable_ok  final : grant_base {};

// ─── (f) coroutine<SuspensionPolicy> — suspension-point grant ─────────
template <class SuspensionPolicy>
struct coroutine final : grant_base {};

}  // namespace crucible::fixy::grant::ctrl

// ═════════════════════════════════════════════════════════════════════
// ── which_dim routing + engagement alias (grant — CR-09 locked) ───────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant {

template <class ExceptionFamily>
struct which_dim<ctrl::throws<ExceptionFamily>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <ctrl::rationale Reason>
struct which_dim<ctrl::abort<Reason>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <ctrl::rationale Reason>
struct which_dim<ctrl::longjmp_unsafe<Reason>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <class CleanupPolicy>
struct which_dim<ctrl::exit<CleanupPolicy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <>
struct which_dim<ctrl::builtin_trap_ok>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <>
struct which_dim<ctrl::unreachable_ok>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

template <class SuspensionPolicy>
struct which_dim<ctrl::coroutine<SuspensionPolicy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::ControlFlow> {};

// ─── Mandatory engagement tag for the ControlFlow axis ────────────────
//
// "I have read the ControlFlow discipline and accept the strict default
// (Pure) for this binding."  which_dim is handled by the generic
// `accept_default_strict_for<D>` specialization in Grant.h.
using accept_default_strict_for_ControlFlow =
    accept_default_strict_for<dim::DimensionAxis::ControlFlow>;

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::detail::ctrl_grant_self_test {

using D = dim::DimensionAxis;

// Sample exception families / policies for instantiation.
struct sample_exception final {};

// ─── (1) IsGrantTag — every grant tag participates ────────────────────
static_assert(IsGrantTag<ctrl::throws<>>);
static_assert(IsGrantTag<ctrl::throws<sample_exception>>);
static_assert(IsGrantTag<ctrl::abort<"oom unrecoverable">>);
static_assert(IsGrantTag<ctrl::longjmp_unsafe<"setjmp island">>);
static_assert(IsGrantTag<ctrl::exit<ctrl::at_exit>>);
static_assert(IsGrantTag<ctrl::exit<ctrl::exit_immediate>>);
static_assert(IsGrantTag<ctrl::builtin_trap_ok>);
static_assert(IsGrantTag<ctrl::unreachable_ok>);
static_assert(IsGrantTag<ctrl::coroutine<ctrl::generator>>);

// Policy tags are NOT grants (rejected in a pack by AllGrantsWellFormed).
static_assert(!IsGrantTag<ctrl::any_exception>);
static_assert(!IsGrantTag<ctrl::at_exit>);
static_assert(!IsGrantTag<ctrl::co_await_only>);

// ─── (2) sizeof — EBO-collapsible standalone marker (1 byte) ──────────
static_assert(sizeof(ctrl::throws<>)                 == 1);
static_assert(sizeof(ctrl::abort<"x">)               == 1);
static_assert(sizeof(ctrl::longjmp_unsafe<"x">)      == 1);
static_assert(sizeof(ctrl::exit<ctrl::at_exit>)      == 1);
static_assert(sizeof(ctrl::builtin_trap_ok)          == 1);
static_assert(sizeof(ctrl::unreachable_ok)           == 1);
static_assert(sizeof(ctrl::coroutine<ctrl::generator>) == 1);

// ─── (3) which_dim routing — every tag → ControlFlow ──────────────────
static_assert(which_dim_v<ctrl::throws<>>                   == D::ControlFlow);
static_assert(which_dim_v<ctrl::throws<sample_exception>>   == D::ControlFlow);
static_assert(which_dim_v<ctrl::abort<"oom">>               == D::ControlFlow);
static_assert(which_dim_v<ctrl::longjmp_unsafe<"jmp">>      == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::at_exit>>        == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::no_cleanup>>     == D::ControlFlow);
static_assert(which_dim_v<ctrl::exit<ctrl::exit_immediate>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::builtin_trap_ok>            == D::ControlFlow);
static_assert(which_dim_v<ctrl::unreachable_ok>             == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::co_await_only>> == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::generator>>     == D::ControlFlow);
static_assert(which_dim_v<ctrl::coroutine<ctrl::async_task>>    == D::ControlFlow);
static_assert(which_dim_v<accept_default_strict_for_ControlFlow> == D::ControlFlow);

// ─── (4) Distinctness — different grant kinds are different types ─────
static_assert(!std::is_same_v<ctrl::throws<>, ctrl::abort<"x">>);
static_assert(!std::is_same_v<ctrl::abort<"x">, ctrl::longjmp_unsafe<"x">>);
static_assert(!std::is_same_v<ctrl::builtin_trap_ok, ctrl::unreachable_ok>);
static_assert(!std::is_same_v<ctrl::exit<ctrl::at_exit>, ctrl::exit<ctrl::no_cleanup>>);
static_assert(!std::is_same_v<ctrl::coroutine<ctrl::generator>,
                              ctrl::coroutine<ctrl::async_task>>);
static_assert(!std::is_same_v<ctrl::throws<>, ctrl::throws<sample_exception>>);

// ─── (5) Rationale carries identity — distinct strings, distinct types ─
static_assert(!std::is_same_v<ctrl::abort<"reason A">, ctrl::abort<"reason B">>);
static_assert(std::is_same_v<ctrl::abort<"same">, ctrl::abort<"same">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);  // distinct N
static_assert(ctrl::rationale{"oom"}.size() == 4);  // 3 chars + NUL

}  // namespace crucible::fixy::grant::detail::ctrl_grant_self_test
