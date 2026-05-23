#pragma once

// ── crucible::algebra::lattices::StackUseLattice ────────────────────
//
// SCAFFOLDING header for FIXY-V-241 (1/3).  Ships the `StackUse`
// sub-axis enum + its `ChainLatticeOps`-based lattice + `At<T>`
// singleton + reflection-driven self-test for the
// `DimensionAxis::StackUse` axis (dim 26, Tier-S Semiring, 2026-05-23).
// V-242 wraps it as a `safety/StackUse.h` Graded carrier; V-243 adds
// the CollisionCatalog rules; V-246 (fixy/grant/Stack.h) routes the
// stack-bound grants onto this axis.
//
// ── Why a dedicated StackUse axis (DimensionAxis::StackUse, dim 26) ──
//
// Stack-overflow is a structural failure Crucible must rule out on the
// foreground path, yet "how much stack does this function use, and how
// is the bound established" was invisible at the type level.  This axis
// is the DUAL of CallShape (V-240): a `CallShape::BoundedRecurses`
// shape is precisely what lets StackUse derive a `BoundedByParam` bound
// (recursion depth × frame size); a `CallShape::Unbounded` shape forces
// `StackUse::Unbounded`.  Gates it drives:
//
//   1. Forge phase E.RecipeSelect admits a kernel to the foreground hot
//      path ONLY if `stack_use ⊑ ConstantFrame` (stack overflow then
//      structurally impossible — the frame is a compile-time constant).
//   2. Warden deadline-watchdog: a deadline-bounded path with an
//      `Unbounded` stack use is rejected (execution cannot be bounded
//      without bounding stack growth).
//   3. The CallShape × StackUse pairing lets a `BoundedByParam` stack
//      bound be derived from a declared recursion depth N.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// StackUse is `TierKind::Semiring` per `tier_of_axis(StackUse)`.  The
// composition reading is "stack-bound union": two sites composing in
// sequence or parallel admit the JOIN (the weaker / less-bounded
// commitment) of their declared bounds.
//
// ── Chain order — subset-inclusion of stack-bound guarantees ────────
//
//   ConstantFrame ⊏ BoundedByParam ⊏ BoundedDynamic ⊏ Unbounded
//
// Ordinal 0 = ConstantFrame (strongest bound — compile-time constant
// frame); ordinal 3 = Unbounded (no bound at all).  A function
// declaring `StackUse = X` ASSERTS its actual stack use ⊆ X's allowed
// set; hot-path admission `stack_use ⊑ ConstantFrame` requires the
// bottom tier exactly.  Per-tier rationale (each strictly weaker than
// the one below):
//
//   ConstantFrame  = 0 — stack frame is a compile-time constant: no
//                         recursion, no VLA, no alloca, no data-
//                         dependent stack arrays.  Stack overflow is
//                         structurally impossible.  Hot-path target.
//   BoundedByParam = 1 — stack ≤ a statically-known function of the
//                         parameters (bounded recursion to depth N, or
//                         a VLA / alloca whose size is a bounded
//                         parameter).  Analyzable GIVEN the bound, but
//                         not constant.  Pairs with CallShape::
//                         BoundedRecurses.
//   BoundedDynamic = 2 — stack bounded only by a RUNTIME guard (an
//                         explicit depth counter / ceiling check).  A
//                         bound exists but is unknown at compile time.
//   Unbounded      = 3 — no static OR dynamic bound: unbounded
//                         recursion, unbounded alloca, data-dependent
//                         recursion with no guard.  Stack overflow
//                         possible.  Top of the chain; pairs with
//                         CallShape::Unbounded.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — strong scoped enum (`enum class : uint8_t`); cross-axis
//                mixing requires `std::to_underlying`.
//   InitSafe — explicit ordinals; reflection-driven coverage fires if a
//                switch arm is forgotten.
//   DetSafe  — lattice ops are `constexpr` (a runtime Graded carrier can
//                enforce `pre (L::leq(...))`).
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  The enum is one uint8_t; the `At<T>` element_type is empty and
// EBO-collapses at every future use site (V-242 wrapper, V-246 grants).
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-242 — safety/StackUse.h: Graded<Absolute, At<T>, P> carrier.
//   FIXY-V-243 — safety/CollisionCatalog.h: stack cross-axis rules.
//   FIXY-V-246 — fixy/grant/Stack.h: constant_frame / bounded_by_param
//                / bounded_dynamic / unbounded grant tags.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── StackUse — stack-footprint boundedness taxonomy ─────────────────
//
// Chain ordering: each tier is a strictly weaker stack-bound guarantee
// than the one below it.  Ordinal 0 = strongest (ConstantFrame); 3 =
// weakest (Unbounded).
enum class StackUse : std::uint8_t {
    ConstantFrame  = 0,  // bottom — compile-time-constant frame; overflow impossible
    BoundedByParam = 1,  // stack ≤ static f(params) (bounded recursion depth / bounded VLA)
    BoundedDynamic = 2,  // stack bounded only by a runtime guard / ceiling
    Unbounded      = 3,  // top — no static or dynamic bound
};

[[nodiscard]] consteval std::string_view stack_use_name(StackUse t) noexcept {
    switch (t) {
        case StackUse::ConstantFrame:  return "ConstantFrame";
        case StackUse::BoundedByParam: return "BoundedByParam";
        case StackUse::BoundedDynamic: return "BoundedDynamic";
        case StackUse::Unbounded:      return "Unbounded";
        default:                       return std::string_view{"<unknown StackUse>"};
    }
}

struct StackUseLattice : ChainLatticeOps<StackUse> {
    [[nodiscard]] static constexpr StackUse bottom() noexcept { return StackUse::ConstantFrame; }
    [[nodiscard]] static constexpr StackUse top()    noexcept { return StackUse::Unbounded; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "StackUseLattice"; }

    template <StackUse T>
    struct At {
        struct element_type {
            using stack_use_value_type = StackUse;
            [[nodiscard]] constexpr operator stack_use_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr StackUse tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case StackUse::ConstantFrame:  return "StackUseLattice::At<ConstantFrame>";
                case StackUse::BoundedByParam: return "StackUseLattice::At<BoundedByParam>";
                case StackUse::BoundedDynamic: return "StackUseLattice::At<BoundedDynamic>";
                case StackUse::Unbounded:      return "StackUseLattice::At<Unbounded>";
                default:                       return "StackUseLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-241 scaffolding sanity) ────────────────────────────
namespace detail::stack_use_lattice_self_test {

inline constexpr std::size_t stack_use_count =
    std::meta::enumerators_of(^^StackUse).size();

static_assert(stack_use_count == 4,
    "StackUse diverged from {ConstantFrame, BoundedByParam, "
    "BoundedDynamic, Unbounded} per V-241 §taxonomy.  Adding a new tier "
    "requires (a) appending at the next free ordinal (append-only per "
    "FOUND-I04), (b) the matching stack_use_name() arm, (c) the matching "
    "At<T> name() arm.");

static_assert(std::to_underlying(StackUse::ConstantFrame) == 0);
static_assert(std::to_underlying(StackUse::Unbounded)     == 3);
static_assert(std::is_same_v<std::underlying_type_t<StackUse>, std::uint8_t>);

[[nodiscard]] consteval bool every_stack_use_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^StackUse));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = stack_use_name([:en:]);
        if (n == std::string_view{"<unknown StackUse>"}) return false;
        if (n.empty())                                   return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_stack_use_has_name(),
    "stack_use_name() switch missing an arm for at least one StackUse "
    "enumerator.");

static_assert(::crucible::algebra::Lattice<StackUseLattice>);
static_assert(::crucible::algebra::BoundedLattice<StackUseLattice>);
static_assert(!::crucible::algebra::Semiring<StackUseLattice>);

static_assert(verify_chain_lattice_exhaustive<StackUseLattice>(),
    "StackUseLattice chain-order lattice axioms failed — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<StackUseLattice>(),
    "StackUseLattice chain failed distributivity — leq/join/meet defect.");

static_assert(StackUseLattice::bottom() == StackUse::ConstantFrame);
static_assert(StackUseLattice::top()    == StackUse::Unbounded);
static_assert(StackUseLattice::name() == std::string_view{"StackUseLattice"});

static_assert( StackUseLattice::leq(StackUse::ConstantFrame, StackUse::Unbounded));
static_assert(!StackUseLattice::leq(StackUse::Unbounded, StackUse::ConstantFrame));

static_assert(StackUseLattice::leq(StackUse::ConstantFrame,  StackUse::BoundedByParam));
static_assert(StackUseLattice::leq(StackUse::BoundedByParam, StackUse::BoundedDynamic));
static_assert(StackUseLattice::leq(StackUse::BoundedDynamic, StackUse::Unbounded));

static_assert(!StackUseLattice::leq(StackUse::BoundedByParam, StackUse::ConstantFrame));
static_assert(!StackUseLattice::leq(StackUse::Unbounded,      StackUse::BoundedDynamic));

// par=join (weaker bound dominates); ConstantFrame is the join identity.
static_assert(StackUseLattice::join(StackUse::BoundedByParam, StackUse::BoundedDynamic)
              == StackUse::BoundedDynamic);
static_assert(StackUseLattice::join(StackUse::ConstantFrame, StackUse::BoundedByParam)
              == StackUse::BoundedByParam);
// and=meet (stronger bound floor).
static_assert(StackUseLattice::meet(StackUse::Unbounded, StackUse::BoundedByParam)
              == StackUse::BoundedByParam);

static_assert(std::is_empty_v<StackUseLattice::At<StackUse::ConstantFrame>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::BoundedByParam>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::BoundedDynamic>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::Unbounded>::element_type>);
static_assert(StackUseLattice::At<StackUse::BoundedByParam>::tier == StackUse::BoundedByParam);

// Runtime smoke — non-constant operands (per
// feedback_algebra_runtime_smoke_test_discipline).
inline void stack_use_lattice_runtime_smoke_test() {
    StackUse a = StackUse::ConstantFrame;
    StackUse b = StackUse::Unbounded;
    [[maybe_unused]] bool     rl = StackUseLattice::leq(a, b);
    [[maybe_unused]] StackUse rj = StackUseLattice::join(a, b);
    [[maybe_unused]] StackUse rm = StackUseLattice::meet(a, b);

    StackUse c = StackUse::BoundedByParam;
    StackUse d = StackUse::BoundedDynamic;
    [[maybe_unused]] StackUse rj2 = StackUseLattice::join(c, d);
    [[maybe_unused]] StackUse rm2 = StackUseLattice::meet(c, d);

    StackUseLattice::At<StackUse::BoundedDynamic>::element_type bd_pin{};
    [[maybe_unused]] StackUse bd_recovered = bd_pin;
}

}  // namespace detail::stack_use_lattice_self_test

}  // namespace crucible::algebra::lattices
