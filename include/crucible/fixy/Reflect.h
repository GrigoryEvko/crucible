#pragma once

// ── crucible::fixy — Reflect.h (FIXY-G8) ───────────────────────────────
//
// Runtime grade reflection.  Projects a `fixy::fn<T, Grants...>`'s
// 20-axis grade vector into a runtime-accessible `GradeDescriptor`
// struct so observability tooling (Augur, Sentry-style telemetry,
// JSON emitters) can address bindings by name without re-running the
// compile-time engagement machinery.
//
// **Surface.**
//
//   fixy::GradeDescriptor                  — 20-dim runtime descriptor.
//   fixy::reflect_grade<F>() -> GradeDescriptor
//                                          — constexpr-accessible at
//                                            runtime, computed via
//                                            P2996 reflection over the
//                                            dim::DimAxis enumerators.
//   fixy::stable_grade_hash<F>             — inline constexpr uint64_t
//                                            FNV-1a + fmix64 mix over
//                                            the 20 (dim_name, grant_
//                                            name) pairs in canonical
//                                            order.
//
// **Mechanism.**  Each entry in `descriptor.dims[]` carries:
//   * `dim_name`     — string_view from substrate's dimension_axis_name.
//   * `is_relaxed`   — true iff some grant in F's Grants pack carries
//                       relaxes == this dim AND is NOT
//                       accept_default_strict_for<this dim>.
//   * `grant_name`   — stable_name_of<G> for the engaging grant; falls
//                       back to "strict-default" when the engagement
//                       came via accept_default_strict_for<D>.
//
// Order is the canonical dim::DimAxis enumerator order — Type, Refinement,
// Usage, Effect, ..., Staleness.  Grade-equivalent bindings produce equal
// `stable_grade_hash` values; differing bindings produce different hashes
// (collision probability ~2^-32 per FNV-1a literature).
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — GradeDescriptor is an aggregate; every member has NSDMI
//                or default-initializes.
//   TypeSafe   — DimEntry's `dim_name` / `grant_name` are string_view
//                over compile-time string literals (no nullptr backing).
//   NullSafe   — `[[nodiscard]]` on every accessor.
//   MemSafe    — pure value types; zero allocation.
//   BorrowSafe — read-only descriptor; no mutation surface.
//   ThreadSafe — descriptor is a value; computation is pure consteval.
//   LeakSafe   — no resources beyond stack-bound array.
//   DetSafe    — output is bit-identical across compiles for a given F;
//                stable_grade_hash uses FNV-1a + fmix64 + combine_ids
//                (Boost-style avalanche-strong order-sensitive combiner).
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — runtime grade reflection
//   safety/diag/StableName.h              — stable_name_of, combine_ids,
//                                           FNV1A_OFFSET_BASIS
//   safety/diag/RowHashFold.h             — fmix64 fold pattern
//   fixy/Dim.h                            — 20-dim canonical order
//   fixy/Reject.h                         — engages_dim_v predicate

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/diag/StableName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::fixy {

// ─── DimEntry / GradeDescriptor ────────────────────────────────────
//
// One descriptor row per dim::DimAxis enumerator (20 total).  All
// members default-init for InitSafe.

struct DimEntry final {
    std::string_view dim_name{};
    bool             is_relaxed{false};
    std::string_view grant_name{"strict-default"};
};

inline constexpr std::size_t GRADE_DIM_COUNT = dim::count_v;

struct GradeDescriptor final {
    std::array<DimEntry, GRADE_DIM_COUNT> dims{};
    std::uint64_t                          stable_hash{0};
};

// ─── IsFixyFn concept ──────────────────────────────────────────────
//
// reflect_grade<F> rejects on non-fn types — the static_assert below
// reports the violation by name in the diagnostic.  Grep-target:
// `IsFixyFn` is the single concept guarding the reflection surface.

namespace detail {

template <typename>
inline constexpr bool is_fixy_fn_v = false;

template <typename T, typename... Gs>
inline constexpr bool is_fixy_fn_v<::crucible::fixy::fn<T, Gs...>> = true;

}  // namespace detail

template <typename F>
concept IsFixyFn = detail::is_fixy_fn_v<std::remove_cvref_t<F>>;

// ─── Per-grant introspection ───────────────────────────────────────
//
// `engaging_grant_name_for<D, Grants...>` walks the Grants pack
// in source order, returning stable_name_of<G> for the FIRST grant
// engaging dim D.  When the engaging grant is an
// `accept_default_strict_for<D>`, returns "strict-default" so the
// is_relaxed flag stays decoupled from the name.

namespace detail {

template <dim::DimAxis D, typename G>
[[nodiscard]] consteval bool grant_engages(G const* = nullptr) noexcept {
    if constexpr (engages_dim_v<G, D>) {
        return true;
    } else {
        return false;
    }
}

// True iff G is the universal accept_default_strict_for<D> tag for the
// exact dim D.  This branch reports `is_relaxed=false` because the
// author chose the substrate strict default rather than relaxing.
template <dim::DimAxis D, typename G>
inline constexpr bool is_strict_ack_for_v = false;

template <dim::DimAxis D>
inline constexpr bool
is_strict_ack_for_v<D, ::crucible::fixy::accept_default_strict_for<D>> = true;

}  // namespace detail

// ─── reflect_grade<F>() ────────────────────────────────────────────
//
// Computes the GradeDescriptor for the binding F.  Constexpr (not
// consteval) so runtime smoke tests can probe it with non-constant
// arguments per the feedback_algebra_runtime_smoke_test_discipline
// rule.

namespace detail {

// Per-grant single-dim observation: returns true iff G engages D, and
// (if so) emits the (is_relaxed, grant_name) pair via out-params.
template <dim::DimAxis D, typename G>
[[nodiscard]] consteval bool observe_grant_for_dim(
    bool& out_is_relaxed,
    std::string_view& out_grant_name) noexcept
{
    if constexpr (engages_dim_v<G, D>) {
        if constexpr (is_strict_ack_for_v<D, G>) {
            out_is_relaxed = false;
            out_grant_name = std::string_view{"strict-default"};
        } else {
            out_is_relaxed = true;
            out_grant_name = ::crucible::safety::diag::stable_name_of<G>;
        }
        return true;
    } else {
        return false;
    }
}

// For one dim D, scan Grants... in source order and pick the FIRST
// grant satisfying engages_dim_v<G, D>.  Returns DimEntry populated.
template <dim::DimAxis D, typename... Grants>
[[nodiscard]] consteval DimEntry compute_dim_entry() noexcept {
    DimEntry entry{};
    entry.dim_name = dim::name(D);
    bool found = false;
    // Short-circuit fold: each `||` step tries the next grant; once a
    // grant engages, subsequent calls bypass via the `found` guard.
    auto try_one = [&]<typename G>() consteval -> bool {
        if (found) return true;
        bool relaxed = false;
        std::string_view gname{};
        if (observe_grant_for_dim<D, G>(relaxed, gname)) {
            entry.is_relaxed = relaxed;
            entry.grant_name = gname;
            found = true;
        }
        return found;
    };
    (try_one.template operator()<Grants>() || ...);
    return entry;
}

template <typename F>
struct reflect_grade_impl;

template <typename T, typename... Grants>
struct reflect_grade_impl<::crucible::fixy::fn<T, Grants...>> {
    [[nodiscard]] static consteval GradeDescriptor compute() noexcept {
        GradeDescriptor desc{};
        static constexpr auto enumerators =
            std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
        std::size_t i = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto en : enumerators) {
            desc.dims[i] = compute_dim_entry<([:en:]), Grants...>();
            ++i;
        }
#pragma GCC diagnostic pop
        // Hash composition: combine_ids over (dim_name_hash, grant_name_hash)
        // pairs in canonical 20-axis order.  Seed with FNV1A_OFFSET_BASIS
        // XOR'd with cardinality to disambiguate from the empty-pack case.
        std::uint64_t h = ::crucible::safety::diag::detail::FNV1A_OFFSET_BASIS
                          ^ static_cast<std::uint64_t>(GRADE_DIM_COUNT);
        h = ::crucible::detail::fmix64(h);
        for (std::size_t j = 0; j < GRADE_DIM_COUNT; ++j) {
            const auto dim_hash =
                ::crucible::safety::diag::detail::hash_name(desc.dims[j].dim_name);
            const auto grant_hash =
                ::crucible::safety::diag::detail::hash_name(desc.dims[j].grant_name);
            // is_relaxed contributes one extra bit so a grant_name collision
            // between strict-default and a relaxed grant of identical
            // serialized name (impossible in practice but cheap to guard)
            // produces a different hash.
            const std::uint64_t relaxed_bit =
                desc.dims[j].is_relaxed ? 1ULL : 0ULL;
            h = ::crucible::safety::diag::detail::combine_ids(h, dim_hash);
            h = ::crucible::safety::diag::detail::combine_ids(h, grant_hash);
            h = ::crucible::safety::diag::detail::combine_ids(h, relaxed_bit);
        }
        desc.stable_hash = h;
        return desc;
    }
};

}  // namespace detail

template <typename F>
[[nodiscard]] constexpr GradeDescriptor reflect_grade() noexcept {
    static_assert(IsFixyFn<F>,
        "fixy::reflect_grade<F> requires F to be a fixy::fn<Type, Grants...> "
        "instantiation.  Bare types and non-fn templates have no grade "
        "vector to reflect.  Wrap your value via mint_fn<Grants...>(v) "
        "first, or use stable_name_of<T> if you only need the type name.");
    return detail::reflect_grade_impl<std::remove_cvref_t<F>>::compute();
}

// ─── stable_grade_hash<F> ──────────────────────────────────────────
//
// Single-symbol variable template for federation-cache key composition.
// Pre-computed at template instantiation; consumers can use it as an
// NTTP without re-running the consteval reflection.
//
// Requires F::IsAccepted to already hold (every dim engaged) — calling
// this on a partially-engaged Grants pack triggers a compile error
// FROM `fn<>`'s static_assert before it reaches us.  The neg-compile
// fixture `neg_fixy_stable_grade_hash_requires_engaged` pins this.

template <typename F>
    requires IsFixyFn<F>
inline constexpr std::uint64_t stable_grade_hash =
    detail::reflect_grade_impl<std::remove_cvref_t<F>>::compute().stable_hash;

// ─── Self-tests ────────────────────────────────────────────────────

namespace self_test {

// Reflect-grade on the all-strict binding produces 20 entries, every
// is_relaxed=false, every grant_name == "strict-default".
using AllStrictFn = fn<int,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

inline constexpr auto all_strict_desc = reflect_grade<AllStrictFn>();
static_assert(all_strict_desc.dims.size() == GRADE_DIM_COUNT);
static_assert(all_strict_desc.dims[0].dim_name == "Type");
static_assert(all_strict_desc.dims[19].dim_name == "Staleness");
static_assert(!all_strict_desc.dims[0].is_relaxed);
static_assert(all_strict_desc.dims[0].grant_name == "strict-default");
static_assert(all_strict_desc.stable_hash != 0);

inline constexpr std::uint64_t all_strict_hash =
    stable_grade_hash<AllStrictFn>;
static_assert(all_strict_hash == all_strict_desc.stable_hash);

// Reflect on a binding with one relaxation flips the is_relaxed bit for
// that dim and changes the hash.
using CopyUsageFn = fn<int,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    grant::copy,                                          // Usage relaxed
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

inline constexpr auto copy_usage_desc = reflect_grade<CopyUsageFn>();
static_assert(copy_usage_desc.dims[2].dim_name == "Usage");
static_assert(copy_usage_desc.dims[2].is_relaxed,
    "Usage relaxed via grant::copy must report is_relaxed=true.");
static_assert(copy_usage_desc.stable_hash != all_strict_desc.stable_hash,
    "Grade-different bindings must produce distinct stable_grade_hash "
    "values; FNV-1a collision probability ~2^-32.");

}  // namespace self_test

}  // namespace crucible::fixy
