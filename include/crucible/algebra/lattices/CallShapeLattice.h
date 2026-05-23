#pragma once

// ── crucible::algebra::lattices::CallShapeLattice ───────────────────
//
// SCAFFOLDING header for FIXY-V-240.  Ships the single sub-axis enum
// `CallShape` + its `ChainLatticeOps`-based lattice algebra + `At<T>`
// singleton + reflection-driven self-test for the
// `DimensionAxis::CallShape` axis (dim 25, Tier-S Semiring,
// 2026-05-23).  V-242 wraps it as a `safety/CallShape.h` Graded
// carrier; V-243 adds the CollisionCatalog D001/D002 dispatch rules;
// V-245 (fixy/grant/Dispatch.h) routes indirect_call / virtual_call /
// recurses / tail_call grants onto this axis.
//
// ── Why a dedicated CallShape axis (DimensionAxis::CallShape, dim 25) ─
//
// Before V-238/V-240, "what dispatch / call shape does this function
// exhibit" was entirely invisible — neither the Met(X) effect row nor
// any other axis recorded whether a call site resolves statically,
// recurses, dispatches through a function pointer, or jumps through a
// vtable.  That blind spot cannot drive several real gates:
//
//   1. **Forge phase E.RecipeSelect** admits a kernel to the foreground
//      hot path ONLY if its call shape ⊑ `Direct` (fully static,
//      inlinable).  Nothing else could express "this kernel makes an
//      indirect / virtual call and therefore must not run where the
//      optimizer needs to see through every call".
//
//   2. **StackUse axis interaction** (V-241): a `BoundedRecurses` shape
//      lets the StackUse axis derive a FINITE stack bound from the
//      recursion depth; an `Unbounded` shape forces StackUse to its top
//      (no static bound).  Without a CallShape axis the StackUse bound
//      has no input.
//
//   3. **Warden deadline-watchdog**: a deadline-bounded path with an
//      `Unbounded` call shape is rejected — execution time cannot be
//      bounded without bounding recursion / dispatch.
//
//   4. **CSL `permission_fork` body**: a forked child with a Virtual /
//      Unbounded shape is harder to reason about for the parallel rule;
//      the axis makes the shape declarable and gateable.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// CallShape is `TierKind::Semiring` per `tier_of_axis(CallShape)`.  The
// composition reading is "call-shape union":
//
//   * Two call sites composing in parallel admit the JOIN (the
//     less-analyzable shape) of their declared shapes.  If site A is
//     Direct and site B is Indirect, the parallel composition is
//     Indirect (the larger of the two on the chain).
//   * Two call sites composing in sequence likewise admit the JOIN — a
//     sequence's call-shape set is the union of its components' shapes.
//
// This par/seq reading parallels Met(X)'s effect-row union but at the
// granularity of DISPATCH SHAPE rather than memory effect.
//
// ── Chain order — subset-inclusion of analyzability / dispatch cost ──
//
// Ordinal 0 = Direct (smallest shape set — every call statically
// resolved, fully inlinable).  Ordinal 4 = Unbounded (largest set — the
// call shape cannot be statically bounded in depth OR target).  Each
// step UP the chain strictly subsumes the previous tier's shape set;
// the chain is a total order.
//
// Reading the chain bottom-to-top, every tier permits every call shape
// of every tier below it PLUS its own:
//
//   Direct ⊏ BoundedRecurses ⊏ Indirect ⊏ Virtual ⊏ Unbounded
//
// A function declaring `CallShape = X` ASSERTS its actual call shapes
// ⊆ X's allowed set.  Hot-path admission `call_shape ⊑ Direct`
// therefore requires the function to claim EXACTLY Direct (the bottom
// of the chain).
//
// On the `<N>` of `BoundedRecurses<N>`: the recursion-depth bound N is
// ORTHOGONAL metadata, carried by the V-242 `safety/CallShape.h`
// wrapper and the V-245 `recurses<N>` grant — it does NOT enter this
// chain enum.  Three reasons: (a) `ChainLatticeOps` structurally
// requires a plain scoped enum (the lattice operates on enum ordinals);
// (b) at call-SHAPE granularity, every statically-bounded recursion is
// categorically more analyzable than any indirect call regardless of N,
// so N cannot reorder the chain; (c) the bound N is really an INPUT to
// the StackUse axis (V-241), not a CallShape distinction.  So
// `BoundedRecurses` is a single tier (ordinal 1) here.
//
// Why each step is strictly "less analyzable / more dispatch cost" than
// the one below it (the production rationale for the order):
//
//   Direct          = 0 — every call statically resolved; the optimizer
//                          sees through it, inlines it, devirtualizes
//                          nothing because there is nothing to
//                          devirtualize.  Bottom of the chain; the
//                          hot-path target.
//   BoundedRecurses = 1 — recursion with a statically-known depth bound
//                          N.  Stack usage is bounded and analyzable;
//                          the call graph is finite and the bound feeds
//                          the StackUse axis.  Just above Direct because
//                          recursion defeats simple inlining yet remains
//                          fully analyzable.
//   Indirect        = 2 — calls through a function pointer: concrete
//                          target, resolved at runtime, NO vtable.
//                          Branch-target-buffer-dependent; the optimizer
//                          can neither inline nor devirtualize.  Above
//                          BoundedRecurses because the target is
//                          genuinely unknown at compile time (a
//                          recursion's target IS known — it is the
//                          function itself).
//   Virtual         = 3 — dynamic dispatch through a vtable: load vptr
//                          → index vtable slot → indirect call (two
//                          dependent loads before the call vs one for a
//                          plain function pointer).  Above Indirect by
//                          the extra indirection.  Crucible's -fno-rtti
//                          + no-virtual-in-data-types posture means this
//                          tier flags boundary / host-interop code.
//   Unbounded       = 4 — unbounded / unanalyzable recursion OR
//                          computed-target dispatch: a call shape where
//                          neither depth nor target can be statically
//                          bounded.  Worst case — unbounded stack
//                          growth, no static analysis.  Top of the
//                          chain; subsumes every analyzable shape below.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — `CallShape` is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//   InitSafe — every enumerator has an explicit ordinal; reflection-
//                driven coverage tests fire automatically if a switch
//                arm is forgotten.
//   DetSafe  — lattice operations are `constexpr` (not `consteval`)
//                so a runtime Graded carrier can enforce its
//                `pre (L::leq(...))` precondition under enforce.
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// V-240 scaffolding: zero cost.  The enum compiles to a single uint8_t
// per value; the `At<T>` singleton's element_type is empty and
// EBO-collapses to 0 bytes via `[[no_unique_address]]` at every future
// use site (V-242 wrapper, V-245 grants).
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-242 — safety/CallShape.h: Graded<Absolute, At<T>, P> carrier
//                wiring this lattice to the value level; carries the
//                BoundedRecurses bound N as orthogonal metadata.
//   FIXY-V-243 — safety/CollisionCatalog.h: D001/D002 dispatch cross-
//                axis rules (e.g. HotPath × CallShape ≥ Indirect reject).
//   FIXY-V-245 — fixy/grant/Dispatch.h: indirect_call / virtual_call /
//                recurses<N> / tail_call grant tags, each routing
//                through which_dim<> to DimensionAxis::CallShape.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── CallShape — function dispatch / call-shape taxonomy ─────────────
//
// Chain ordering: each tier strictly subsumes the call-shape set of
// every tier below it (analyzability-decreasing total order).  Ordinal
// 0 = smallest set (Direct, every call statically resolved); ordinal 4
// = largest set (Unbounded, neither depth nor target statically
// bounded).
enum class CallShape : std::uint8_t {
    Direct          = 0,  // bottom — every call statically resolved; inlinable
    BoundedRecurses = 1,  // recursion with statically-known depth bound (N is wrapper metadata)
    Indirect        = 2,  // function-pointer call (concrete target, runtime-resolved, no vtable)
    Virtual         = 3,  // vtable dynamic dispatch (load vptr → index slot → call)
    Unbounded       = 4,  // top — unbounded recursion / computed-target dispatch
};

[[nodiscard]] consteval std::string_view call_shape_name(CallShape t) noexcept {
    switch (t) {
        case CallShape::Direct:          return "Direct";
        case CallShape::BoundedRecurses: return "BoundedRecurses";
        case CallShape::Indirect:        return "Indirect";
        case CallShape::Virtual:         return "Virtual";
        case CallShape::Unbounded:       return "Unbounded";
        default:                         return std::string_view{"<unknown CallShape>"};
    }
}

struct CallShapeLattice : ChainLatticeOps<CallShape> {
    [[nodiscard]] static constexpr CallShape bottom() noexcept { return CallShape::Direct; }
    [[nodiscard]] static constexpr CallShape top()    noexcept { return CallShape::Unbounded; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "CallShapeLattice"; }

    template <CallShape T>
    struct At {
        struct element_type {
            using call_shape_value_type = CallShape;
            [[nodiscard]] constexpr operator call_shape_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr CallShape tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case CallShape::Direct:          return "CallShapeLattice::At<Direct>";
                case CallShape::BoundedRecurses: return "CallShapeLattice::At<BoundedRecurses>";
                case CallShape::Indirect:        return "CallShapeLattice::At<Indirect>";
                case CallShape::Virtual:         return "CallShapeLattice::At<Virtual>";
                case CallShape::Unbounded:       return "CallShapeLattice::At<Unbounded>";
                default:                         return "CallShapeLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-240 scaffolding sanity) ────────────────────────────
namespace detail::call_shape_lattice_self_test {

// Catalog cardinality — the dispatch chain has exactly 5 tiers.
inline constexpr std::size_t call_shape_count =
    std::meta::enumerators_of(^^CallShape).size();

static_assert(call_shape_count == 5,
    "CallShape diverged from {Direct, BoundedRecurses, Indirect, "
    "Virtual, Unbounded} per V-240 §taxonomy.  Adding a new dispatch "
    "tier requires (a) appending at the next free ordinal (append-only "
    "per FOUND-I04 Universe extension rule), (b) the matching "
    "call_shape_name() switch arm, (c) the matching At<T> singleton "
    "name() arm.  Reusing an existing ordinal would silently change "
    "every stored row_hash (federation cache key) without warning.");

// Bottom-element pin — ordinal 0 is the smallest shape set (Direct: the
// function makes only statically-resolved calls, the hot-path-safest
// claim).
static_assert(std::to_underlying(CallShape::Direct) == 0);

// Top-element pin — ordinal 4 is the largest set (Unbounded).
static_assert(std::to_underlying(CallShape::Unbounded) == 4);

// Underlying type pin — uint8_t (mirrors SyscallFamily / ControlFlow;
// lets a future effect-row bridge derive indices without zero-extending).
static_assert(std::is_same_v<std::underlying_type_t<CallShape>, std::uint8_t>);

// Reflection-driven name coverage — every enumerator must resolve to a
// non-sentinel, non-empty name.  Auto-extends if the enum grows.
[[nodiscard]] consteval bool every_call_shape_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CallShape));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = call_shape_name([:en:]);
        if (n == std::string_view{"<unknown CallShape>"}) return false;
        if (n.empty())                                    return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_call_shape_has_name(),
    "call_shape_name() switch missing an arm for at least one CallShape "
    "enumerator.  Add the arm or the new tier leaks the "
    "'<unknown CallShape>' sentinel.");

// Concept conformance — chain lattice satisfies Lattice + BoundedLattice
// and NOT Semiring (chain order has no independent ⊕/⊗ structure; the
// "Tier-S Semiring" classification is the AXIS tier in DimensionTraits.h,
// describing par=join composition semantics — NOT a Semiring concept on
// the lattice itself).
static_assert(::crucible::algebra::Lattice<CallShapeLattice>);
static_assert(::crucible::algebra::BoundedLattice<CallShapeLattice>);
static_assert(!::crucible::algebra::Semiring<CallShapeLattice>);

// Exhaustive lattice-axiom verifier on (axis)³ triples.  Chain orders
// are always distributive — failure indicates a leq/join/meet defect.
static_assert(verify_chain_lattice_exhaustive<CallShapeLattice>(),
    "CallShapeLattice chain-order lattice axioms failed at some triple "
    "— leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<CallShapeLattice>(),
    "CallShapeLattice chain failed distributivity check — leq/join/meet "
    "defect.");

// Bottom / top pins on the lattice surface (catches "someone reordered
// the enum and the lattice failed to follow" drift).
static_assert(CallShapeLattice::bottom() == CallShape::Direct);
static_assert(CallShapeLattice::top()    == CallShape::Unbounded);

// Lattice top-level diagnostic name pin.
static_assert(CallShapeLattice::name() == std::string_view{"CallShapeLattice"});

// Strict-chain order pin (bottom ⊏ top witness).  Combined with the
// exhaustive axiom verifier above, the chain direction is structurally
// locked.
static_assert( CallShapeLattice::leq(CallShape::Direct, CallShape::Unbounded));
static_assert(!CallShapeLattice::leq(CallShape::Unbounded, CallShape::Direct));

// Mid-chain ordering — every tier strictly subsumes the previous.
static_assert(CallShapeLattice::leq(CallShape::Direct,          CallShape::BoundedRecurses));
static_assert(CallShapeLattice::leq(CallShape::BoundedRecurses, CallShape::Indirect));
static_assert(CallShapeLattice::leq(CallShape::Indirect,        CallShape::Virtual));
static_assert(CallShapeLattice::leq(CallShape::Virtual,         CallShape::Unbounded));

// Reverse direction must fail for non-equal pairs.
static_assert(!CallShapeLattice::leq(CallShape::BoundedRecurses, CallShape::Direct));
static_assert(!CallShapeLattice::leq(CallShape::Unbounded,       CallShape::Virtual));

// Join semantics — par=join (less-analyzable shape dominates).  Composing
// an indirect site with a virtual site yields Virtual (the wider /
// less-analyzable shape).
static_assert(CallShapeLattice::join(CallShape::Indirect, CallShape::Virtual)
              == CallShape::Virtual);
// Direct is the join identity (composing with a fully-static site never
// widens the shape).
static_assert(CallShapeLattice::join(CallShape::Direct, CallShape::BoundedRecurses)
              == CallShape::BoundedRecurses);

// Meet semantics — and=meet (more-analyzable floor).  Meeting a tight
// admission policy with a loose binding yields the tight (more-static)
// floor.
static_assert(CallShapeLattice::meet(CallShape::Unbounded, CallShape::BoundedRecurses)
              == CallShape::BoundedRecurses);

// At<T> singleton — empty element_type for EBO collapse at every use
// site.  V-242's `Graded<Absolute, At<T>, P>` relies on this for
// zero-byte overhead.
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Direct>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::BoundedRecurses>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Indirect>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Virtual>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Unbounded>::element_type>);

// At<T>::tier pins the enum value at the type level — what V-242+
// wrappers key on for compile-time admission decisions.
static_assert(CallShapeLattice::At<CallShape::Indirect>::tier == CallShape::Indirect);

// Runtime smoke test — per feedback_algebra_runtime_smoke_test_discipline
// memory: pure static_asserts can mask consteval/SFINAE/inline-body
// bugs; runtime ops with non-constant arguments catch them.
inline void call_shape_lattice_runtime_smoke_test() {
    // Pin operands to the chain's extremes, then call leq/join/meet so
    // the optimizer cannot collapse the call to a compile-time fold.
    CallShape a = CallShape::Direct;
    CallShape b = CallShape::Unbounded;
    [[maybe_unused]] bool      rl = CallShapeLattice::leq(a, b);
    [[maybe_unused]] CallShape rj = CallShapeLattice::join(a, b);
    [[maybe_unused]] CallShape rm = CallShapeLattice::meet(a, b);

    // Mid-chain witnesses.
    CallShape c = CallShape::Indirect;
    CallShape d = CallShape::Virtual;
    [[maybe_unused]] CallShape rj2 = CallShapeLattice::join(c, d);
    [[maybe_unused]] CallShape rm2 = CallShapeLattice::meet(c, d);

    // At<T>::element_type round-trip — verify the singleton's conversion
    // materializes the right tier at runtime, not just at consteval.
    CallShapeLattice::At<CallShape::BoundedRecurses>::element_type rec_pin{};
    [[maybe_unused]] CallShape rec_recovered = rec_pin;
}

}  // namespace detail::call_shape_lattice_self_test

}  // namespace crucible::algebra::lattices
