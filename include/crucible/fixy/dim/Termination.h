#pragma once

// ── crucible::fixy::dim — Termination.h (FIXY-G12) ────────────────────
//
// dim::Resources is the bounded-resource axis #22.  Today's
// `cg::complexity_linear<N>` is abstract — it declares "linear in N" at
// the big-O level but doesn't say "for fixed N, terminates in bounded
// wallclock".  A binding declaring linear complexity can spin forever
// if N is unbounded.  The warden deadline watchdog catches at runtime;
// fixy claims compile-time SHAPE checking but lacked the axis.
//
// Ground in lightweight totality checking (F* Tot effect, Coq's
// Function with measure).  fixy doesn't ship a termination checker —
// declaration is Asserted witness by default (G9 mechanism).  CI tests
// tighten the witness to Tested.  Future analyzer could discharge to
// FormallyVerified — the substrate is ready.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   inline constexpr dim::DimAxis Resources  — value 21 (fixy-layer)
//
//   cg::terminating                — Frame; structural termination claim
//   cg::bounded_alloc<MaxBytes>    — Frame; total allocation ≤ MaxBytes
//   cg::bounded_io<MaxOps>         — Frame; syscall count ≤ MaxOps
//   cg::wallclock_budget<Nanos>    — Frame; runtime deadline (warden arms)
//   cg::loop_bound<N>              — Frame; explicit upper bound
//   cg::unbounded_resources        — strict default
//
//   cg::bounded_alloc_e<W, MaxBytes>  — evidenced variant
//   cg::wallclock_budget_e<W, Nanos>  — evidenced variant
//
//   HasResourcesGrant<F>           — consumer-side predicate
//   wallclock_budget_v<F>          — extract deadline (UINT64_MAX = unset)
//   bounded_alloc_v<F>             — extract alloc cap
//   bounded_io_v<F>                — extract IO cap
//   loop_bound_v<F>                — extract loop bound
//   is_terminating_v<F>            — true iff cg::terminating engaged
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G12 termination axis
//   warden/Policy.h                       — wallclock deadline runtime
//   fixy/dim/Cost.h                       — cost-budget cross-check
//   fixy/Modality.h                       — Frame modality

#include <crucible/algebra/Modality.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Modality.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::dim {

// ── Resources — fixy-layer DimAxis value #22 ──────────────────────
//
// Lives at value 21 (next after Cost=20).  Append-only Universe
// extension policy preserved.

inline constexpr DimAxis Resources = static_cast<DimAxis>(21);

static_assert(static_cast<std::uint8_t>(Resources) == 21,
    "fixy::dim::Resources MUST be value 21 — the next free position "
    "after Cost at 20.");

[[nodiscard]] constexpr std::string_view resources_axis_name(DimAxis d) noexcept {
    return (d == Resources) ? std::string_view{"Resources"} : std::string_view{""};
}

}  // namespace crucible::fixy::dim

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── Bounded-resource grants (all Frame modality) ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each grant is a Frame-modality declaration: the property is
// invariant of the value.  The grant captures the BOUND; downstream
// runtime enforcement (warden::DeadlineWatchdog for wallclock_budget,
// arena allocator caps for bounded_alloc) consumes the type-level
// claim.

struct terminating final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr bool claims_terminating = true;
};

struct unbounded_resources final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr bool claims_terminating = false;
};

template <std::uint64_t MaxBytes>
struct bounded_alloc final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t max_bytes = MaxBytes;
    static_assert(MaxBytes > 0,
        "cg::bounded_alloc<MaxBytes> requires MaxBytes > 0.  Use "
        "accept_default_strict_for<dim::Resources> for zero-alloc paths.");
};

template <std::uint64_t MaxOps>
struct bounded_io final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t max_ops = MaxOps;
};

template <std::uint64_t Nanos>
struct wallclock_budget final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t budget_ns = Nanos;
    static_assert(Nanos > 0,
        "cg::wallclock_budget<Nanos> requires Nanos > 0.  Use "
        "accept_default_strict_for<dim::Resources> for unbounded paths.");
};

template <std::uint64_t N>
struct loop_bound final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t max_iterations = N;
    static_assert(N > 0,
        "cg::loop_bound<N> requires N > 0.");
    static_assert(N != UINT64_MAX,
        "cg::loop_bound<UINT64_MAX> is structurally inconsistent: "
        "the value is the sentinel for 'no bound declared'.  Pair "
        "with cg::terminating + cg::wallclock_budget<...> for an "
        "actual bounded-recursion claim.  Use a concretely-numbered "
        "upper bound (e.g., 4096) for genuine bounded loops.");
};

// ── Evidenced variants ────────────────────────────────────────────

template <::crucible::safety::witness::IsWitness W, std::uint64_t MaxBytes>
struct bounded_alloc_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t max_bytes = MaxBytes;
    using witness_t = W;
};

template <::crucible::safety::witness::IsWitness W, std::uint64_t Nanos>
struct wallclock_budget_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr std::uint64_t budget_ns = Nanos;
    using witness_t = W;
};

template <::crucible::safety::witness::IsWitness W>
struct terminating_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Resources;
    static constexpr bool claims_terminating = true;
    using witness_t = W;
};

}  // namespace crucible::fixy::grant

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── HasResourcesGrant<F> + per-grant extractors ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename G>
inline constexpr bool grant_engages_resources_v = false;

template <>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::terminating> = true;

template <>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::unbounded_resources> = true;

template <std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::bounded_alloc<N>> = true;

template <std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::bounded_io<N>> = true;

template <std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::wallclock_budget<N>> = true;

template <std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::loop_bound<N>> = true;

template <typename W, std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::bounded_alloc_e<W, N>> = true;

template <typename W, std::uint64_t N>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::wallclock_budget_e<W, N>> = true;

template <typename W>
inline constexpr bool grant_engages_resources_v<::crucible::fixy::grant::terminating_e<W>> = true;

template <typename F>
struct fn_has_resources_grant;

template <typename T, typename... Grants>
struct fn_has_resources_grant<::crucible::fixy::fn<T, Grants...>> {
    static constexpr bool value =
        (grant_engages_resources_v<std::remove_cvref_t<Grants>> || ... || false);
};

}  // namespace detail

template <typename F>
inline constexpr bool HasResourcesGrant =
    detail::fn_has_resources_grant<std::remove_cvref_t<F>>::value;

// ── wallclock_budget extraction ───────────────────────────────────

namespace detail {

template <typename G>
struct wallclock_of_grant {
    static constexpr std::uint64_t value = UINT64_MAX;  // unset sentinel
};

template <std::uint64_t N>
struct wallclock_of_grant<::crucible::fixy::grant::wallclock_budget<N>> {
    static constexpr std::uint64_t value = N;
};

template <typename W, std::uint64_t N>
struct wallclock_of_grant<::crucible::fixy::grant::wallclock_budget_e<W, N>> {
    static constexpr std::uint64_t value = N;
};

template <typename... Grants>
struct first_wallclock_in {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <typename G, typename... Rest>
struct first_wallclock_in<G, Rest...> {
private:
    static constexpr std::uint64_t head_v =
        wallclock_of_grant<std::remove_cvref_t<G>>::value;
public:
    static constexpr std::uint64_t value =
        (head_v != UINT64_MAX) ? head_v : first_wallclock_in<Rest...>::value;
};

template <typename F>
struct fn_wallclock_budget;

template <typename T, typename... Grants>
struct fn_wallclock_budget<::crucible::fixy::fn<T, Grants...>> {
    static constexpr std::uint64_t value =
        first_wallclock_in<Grants...>::value;
};

}  // namespace detail

template <typename F>
inline constexpr std::uint64_t wallclock_budget_v =
    detail::fn_wallclock_budget<std::remove_cvref_t<F>>::value;

// ── bounded_alloc extraction ──────────────────────────────────────

namespace detail {

template <typename G>
struct alloc_of_grant {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <std::uint64_t N>
struct alloc_of_grant<::crucible::fixy::grant::bounded_alloc<N>> {
    static constexpr std::uint64_t value = N;
};

template <typename W, std::uint64_t N>
struct alloc_of_grant<::crucible::fixy::grant::bounded_alloc_e<W, N>> {
    static constexpr std::uint64_t value = N;
};

template <typename... Grants>
struct first_alloc_in {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <typename G, typename... Rest>
struct first_alloc_in<G, Rest...> {
private:
    static constexpr std::uint64_t head_v =
        alloc_of_grant<std::remove_cvref_t<G>>::value;
public:
    static constexpr std::uint64_t value =
        (head_v != UINT64_MAX) ? head_v : first_alloc_in<Rest...>::value;
};

template <typename F>
struct fn_bounded_alloc;

template <typename T, typename... Grants>
struct fn_bounded_alloc<::crucible::fixy::fn<T, Grants...>> {
    static constexpr std::uint64_t value = first_alloc_in<Grants...>::value;
};

}  // namespace detail

template <typename F>
inline constexpr std::uint64_t bounded_alloc_v =
    detail::fn_bounded_alloc<std::remove_cvref_t<F>>::value;

// ── bounded_io extraction ─────────────────────────────────────────

namespace detail {

template <typename G>
struct io_of_grant {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <std::uint64_t N>
struct io_of_grant<::crucible::fixy::grant::bounded_io<N>> {
    static constexpr std::uint64_t value = N;
};

template <typename... Grants>
struct first_io_in {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <typename G, typename... Rest>
struct first_io_in<G, Rest...> {
private:
    static constexpr std::uint64_t head_v = io_of_grant<std::remove_cvref_t<G>>::value;
public:
    static constexpr std::uint64_t value =
        (head_v != UINT64_MAX) ? head_v : first_io_in<Rest...>::value;
};

template <typename F>
struct fn_bounded_io;

template <typename T, typename... Grants>
struct fn_bounded_io<::crucible::fixy::fn<T, Grants...>> {
    static constexpr std::uint64_t value = first_io_in<Grants...>::value;
};

}  // namespace detail

template <typename F>
inline constexpr std::uint64_t bounded_io_v =
    detail::fn_bounded_io<std::remove_cvref_t<F>>::value;

// ── loop_bound extraction ─────────────────────────────────────────

namespace detail {

template <typename G>
struct loop_bound_of_grant {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <std::uint64_t N>
struct loop_bound_of_grant<::crucible::fixy::grant::loop_bound<N>> {
    static constexpr std::uint64_t value = N;
};

template <typename... Grants>
struct first_loop_bound_in {
    static constexpr std::uint64_t value = UINT64_MAX;
};

template <typename G, typename... Rest>
struct first_loop_bound_in<G, Rest...> {
private:
    static constexpr std::uint64_t head_v =
        loop_bound_of_grant<std::remove_cvref_t<G>>::value;
public:
    static constexpr std::uint64_t value =
        (head_v != UINT64_MAX) ? head_v : first_loop_bound_in<Rest...>::value;
};

template <typename F>
struct fn_loop_bound;

template <typename T, typename... Grants>
struct fn_loop_bound<::crucible::fixy::fn<T, Grants...>> {
    static constexpr std::uint64_t value = first_loop_bound_in<Grants...>::value;
};

}  // namespace detail

template <typename F>
inline constexpr std::uint64_t loop_bound_v =
    detail::fn_loop_bound<std::remove_cvref_t<F>>::value;

// ── is_terminating_v ──────────────────────────────────────────────

namespace detail {

template <typename G>
inline constexpr bool grant_claims_terminating_v = false;

template <>
inline constexpr bool grant_claims_terminating_v<::crucible::fixy::grant::terminating> = true;

template <typename W>
inline constexpr bool grant_claims_terminating_v<::crucible::fixy::grant::terminating_e<W>> = true;

template <typename F>
struct fn_is_terminating;

template <typename T, typename... Grants>
struct fn_is_terminating<::crucible::fixy::fn<T, Grants...>> {
    static constexpr bool value =
        (grant_claims_terminating_v<std::remove_cvref_t<Grants>> || ... || false);
};

}  // namespace detail

template <typename F>
inline constexpr bool is_terminating_v =
    detail::fn_is_terminating<std::remove_cvref_t<F>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace termination_self_test {

namespace cg = ::crucible::fixy::grant;

// Engagement on dim::Resources.
static_assert(cg::terminating::relaxes == dim::Resources);
static_assert(cg::bounded_alloc<4096>::relaxes == dim::Resources);
static_assert(cg::wallclock_budget<1000>::relaxes == dim::Resources);
static_assert(cg::bounded_io<0>::relaxes == dim::Resources);
static_assert(cg::loop_bound<256>::relaxes == dim::Resources);
static_assert(cg::unbounded_resources::relaxes == dim::Resources);

// Per-grant projection at the grant level.
static_assert(cg::bounded_alloc<4096>::max_bytes == 4096);
static_assert(cg::wallclock_budget<1'000'000>::budget_ns == 1'000'000);
static_assert(cg::bounded_io<0>::max_ops == 0);
static_assert(cg::loop_bound<256>::max_iterations == 256);
static_assert(cg::terminating::claims_terminating);

}  // namespace termination_self_test

}  // namespace crucible::fixy
