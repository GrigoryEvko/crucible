#pragma once

// ── crucible::algebra::lattices::GlobalStateLattice ─────────────────
//
// SCAFFOLDING header for FIXY-V-241 (2/3).  Ships the `GlobalState`
// sub-axis enum + its `ChainLatticeOps`-based lattice + `At<T>`
// singleton + reflection-driven self-test for the
// `DimensionAxis::GlobalState` axis (dim 27, Tier-S Semiring,
// 2026-05-23).  V-242 wraps it as a `safety/GlobalState.h` Graded
// carrier; V-243 adds the §6.8 S004 collision rule; V-246 (fixy/grant/
// Global.h) routes the global-state grants onto this axis.
//
// ── Why a dedicated GlobalState axis (DimensionAxis::GlobalState, 27) ─
//
// "How does this function interact with global / static mutable state"
// was invisible at the type level — yet it is the load-bearing input
// to the static-initialization-order-fiasco and Meyers-singleton
// init-cycle hazards.  Gates it drives:
//
//   1. V-248 Meyers-singleton init-cycle detection (S004 collision
//      rule): a function at `InitOrderHazard` reached during another
//      singleton's init signals a potential init cycle.
//   2. Forge phase E.RecipeSelect hot-path admission `⊑ ConstGlobal`:
//      hot-path code may read constinit tables but must not touch
//      mutable global state (which would defeat content-addressed
//      determinism and add cross-thread coupling).
//   3. DetSafe replay: a `MutableGlobal` / `InitOrderHazard` function
//      introduces history-dependent state that breaks bit-exact replay
//      unless the state is itself part of the event-sourced Cipher.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// GlobalState is `TierKind::Semiring` per `tier_of_axis(GlobalState)`.
// The composition reading is "global-interaction union": two sites
// composing admit the JOIN (the higher-hazard interaction).
//
// ── Chain order — subset-inclusion of global-state interactions ─────
//
//   Stateless ⊏ ConstGlobal ⊏ MutableGlobal ⊏ InitOrderHazard
//
// Ordinal 0 = Stateless (no global interaction); ordinal 3 =
// InitOrderHazard (the worst — static-init-order / lazy-init hazard).
// A function declaring `GlobalState = X` ASSERTS its actual global
// interactions ⊆ X's allowed set.  Per-tier rationale (each strictly
// higher-hazard than the one below):
//
//   Stateless       = 0 — reads / writes NO global or static mutable
//                          state; a pure function of its arguments
//                          (w.r.t. globals).  Trivially replay-safe and
//                          thread-safe.  Hot-path target.
//   ConstGlobal     = 1 — reads global state that is const / constinit
//                          / immutable-after-static-init (lookup tables,
//                          constexpr constants).  Read-only; no init-
//                          order hazard for constant-initialized
//                          globals; safe to read concurrently.
//   MutableGlobal   = 2 — reads / writes mutable global or function-
//                          local-static state that is SYNCHRONIZED
//                          (atomic or guarded) or thread-partitioned
//                          (thread_local).  Thread-safe but introduces
//                          global coupling and history-dependence.
//   InitOrderHazard = 3 — touches mutable global state with a static-
//                          initialization-order or lazy-init (Meyers
//                          singleton) hazard: non-deterministic init
//                          order across translation units, potential
//                          init cycle.  Top of the chain; what V-248's
//                          S004 detector keys on.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — strong scoped enum (`enum class : uint8_t`).
//   InitSafe — explicit ordinals; reflection-driven name coverage.
//   DetSafe  — `constexpr` lattice ops.
//   LeakSafe — zero-state enum.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  One uint8_t; empty `At<T>` element_type EBO-collapses.
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-242 — safety/GlobalState.h: Graded<Absolute, At<T>, P> carrier.
//   FIXY-V-243 — safety/CollisionCatalog.h: S004 init-cycle rule.
//   FIXY-V-246 — fixy/grant/Global.h: thread_local_<Tag> (→ MutableGlobal)
//                / singleton<Tag> (→ InitOrderHazard) grant tags.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── GlobalState — global / static mutable-state interaction taxonomy ─
//
// Chain ordering: each tier is a strictly higher-hazard global
// interaction than the one below it.  Ordinal 0 = Stateless (no
// interaction); 3 = InitOrderHazard (init-order / lazy-init hazard).
enum class GlobalState : std::uint8_t {
    Stateless       = 0,  // bottom — no global/static mutable interaction
    ConstGlobal     = 1,  // reads const / constinit globals only
    MutableGlobal   = 2,  // reads/writes synchronized or thread-local mutable globals
    InitOrderHazard = 3,  // top — static-init-order / Meyers-singleton lazy-init hazard
};

[[nodiscard]] consteval std::string_view global_state_name(GlobalState t) noexcept {
    switch (t) {
        case GlobalState::Stateless:       return "Stateless";
        case GlobalState::ConstGlobal:     return "ConstGlobal";
        case GlobalState::MutableGlobal:   return "MutableGlobal";
        case GlobalState::InitOrderHazard: return "InitOrderHazard";
        default:                           return std::string_view{"<unknown GlobalState>"};
    }
}

struct GlobalStateLattice : ChainLatticeOps<GlobalState> {
    [[nodiscard]] static constexpr GlobalState bottom() noexcept { return GlobalState::Stateless; }
    [[nodiscard]] static constexpr GlobalState top()    noexcept { return GlobalState::InitOrderHazard; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "GlobalStateLattice"; }

    template <GlobalState T>
    struct At {
        struct element_type {
            using global_state_value_type = GlobalState;
            [[nodiscard]] constexpr operator global_state_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr GlobalState tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case GlobalState::Stateless:       return "GlobalStateLattice::At<Stateless>";
                case GlobalState::ConstGlobal:     return "GlobalStateLattice::At<ConstGlobal>";
                case GlobalState::MutableGlobal:   return "GlobalStateLattice::At<MutableGlobal>";
                case GlobalState::InitOrderHazard: return "GlobalStateLattice::At<InitOrderHazard>";
                default:                           return "GlobalStateLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-241 scaffolding sanity) ────────────────────────────
namespace detail::global_state_lattice_self_test {

inline constexpr std::size_t global_state_count =
    std::meta::enumerators_of(^^GlobalState).size();

static_assert(global_state_count == 4,
    "GlobalState diverged from {Stateless, ConstGlobal, MutableGlobal, "
    "InitOrderHazard} per V-241 §taxonomy.  Adding a new tier requires "
    "(a) appending at the next free ordinal (append-only per FOUND-I04), "
    "(b) the matching global_state_name() arm, (c) the matching At<T> "
    "name() arm.");

static_assert(std::to_underlying(GlobalState::Stateless)       == 0);
static_assert(std::to_underlying(GlobalState::InitOrderHazard) == 3);
static_assert(std::is_same_v<std::underlying_type_t<GlobalState>, std::uint8_t>);

[[nodiscard]] consteval bool every_global_state_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^GlobalState));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = global_state_name([:en:]);
        if (n == std::string_view{"<unknown GlobalState>"}) return false;
        if (n.empty())                                      return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_global_state_has_name(),
    "global_state_name() switch missing an arm for at least one "
    "GlobalState enumerator.");

static_assert(::crucible::algebra::Lattice<GlobalStateLattice>);
static_assert(::crucible::algebra::BoundedLattice<GlobalStateLattice>);
static_assert(!::crucible::algebra::Semiring<GlobalStateLattice>);

static_assert(verify_chain_lattice_exhaustive<GlobalStateLattice>(),
    "GlobalStateLattice chain-order lattice axioms failed — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<GlobalStateLattice>(),
    "GlobalStateLattice chain failed distributivity — leq/join/meet defect.");

static_assert(GlobalStateLattice::bottom() == GlobalState::Stateless);
static_assert(GlobalStateLattice::top()    == GlobalState::InitOrderHazard);
static_assert(GlobalStateLattice::name() == std::string_view{"GlobalStateLattice"});

static_assert( GlobalStateLattice::leq(GlobalState::Stateless, GlobalState::InitOrderHazard));
static_assert(!GlobalStateLattice::leq(GlobalState::InitOrderHazard, GlobalState::Stateless));

static_assert(GlobalStateLattice::leq(GlobalState::Stateless,     GlobalState::ConstGlobal));
static_assert(GlobalStateLattice::leq(GlobalState::ConstGlobal,   GlobalState::MutableGlobal));
static_assert(GlobalStateLattice::leq(GlobalState::MutableGlobal, GlobalState::InitOrderHazard));

static_assert(!GlobalStateLattice::leq(GlobalState::ConstGlobal,     GlobalState::Stateless));
static_assert(!GlobalStateLattice::leq(GlobalState::InitOrderHazard, GlobalState::MutableGlobal));

// par=join (higher-hazard dominates); Stateless is the join identity.
static_assert(GlobalStateLattice::join(GlobalState::ConstGlobal, GlobalState::MutableGlobal)
              == GlobalState::MutableGlobal);
static_assert(GlobalStateLattice::join(GlobalState::Stateless, GlobalState::ConstGlobal)
              == GlobalState::ConstGlobal);
// and=meet (lower-hazard floor).
static_assert(GlobalStateLattice::meet(GlobalState::InitOrderHazard, GlobalState::ConstGlobal)
              == GlobalState::ConstGlobal);

// ── FIXY-FOUND-076 audit pin: cross-tree convention misalignment ─────
//
// AUDIT RESULT for GlobalStateLattice (2026-05-25, corrected): INVERTED.
//
// Initial classification of this lattice as ALIGNED on commit e288420e
// was a mis-read: I conflated the propagation-reading ("compose two
// regions and the hazardier dominates") with the strictness-reading
// ("which value is the most restrictive admission policy").
//
// Re-classified per the same lens FOUND-009/010 use:
//   * chain direction: Stateless (bottom, MOST restrictive — "no global
//     state allowed") → ConstGlobal → MutableGlobal → InitOrderHazard
//     (top, LEAST restrictive — "init-order hazards permitted")
//   * "strictest" in cross-tree contract = most-restrictive admission
//     policy = Stateless = chain-min = MEET, NOT JOIN
//   * join(low, high) returns InitOrderHazard = LOOSEST claim
//   * meet(low, high) returns Stateless = STRICTEST claim
//   * cross-tree reading: "par=join, strictest-wins" ✗ — JOIN returns
//     the LOOSEST hazard policy, NOT the strictest
//
// SAME family of defect as FOUND-009 (MemOrderLattice), FOUND-010
// (HwInstructionLattice), and FOUND-076 PART A (StackUseLattice).
// A CSL/admission gate calling join() to enforce "no global state
// minimum" would silently admit InitOrderHazard — equivalent to the
// FOUND-010 PrivilegedMsr escalation pattern for global-state hazard.
//
// The LOCAL doc comment "par=join (higher-hazard dominates)" at L206
// accurately describes the propagation semantics (a region containing
// a hazardful function inherits the hazard).  That propagation reading
// is correct AT THE LATTICE LEVEL.  The cross-tree contract requires
// the CALLER to choose the operator (MEET for strictness, JOIN for
// propagation), NOT the lattice's join to match strictness directly.
//
// Polarity-witness pin: a refactor inverting the chain (so InitOrderHazard
// moves to bottom) would red these asserts in lockstep with the
// FOUND-009/010 convention.
static_assert(GlobalStateLattice::join(GlobalState::Stateless,
                                       GlobalState::InitOrderHazard)
              == GlobalState::InitOrderHazard,
    "FIXY-FOUND-076: GlobalStateLattice's JOIN gives LOOSEST-hazard-"
    "policy (top=InitOrderHazard).  A consumer treating compose as "
    "'strictest-wins global-state minimization' would silently admit "
    "InitOrderHazard.  Consumers wanting the tightest hazard policy "
    "MUST call MEET — SAME defect family as FOUND-009/010 (MemOrder/"
    "HwInstruction) and FOUND-076 PART A (StackUse).");
static_assert(GlobalStateLattice::meet(GlobalState::Stateless,
                                       GlobalState::InitOrderHazard)
              == GlobalState::Stateless,
    "FIXY-FOUND-076: GlobalStateLattice's MEET gives strictest-hazard-"
    "policy (bottom=Stateless).  CSL/admission gates wanting capability-"
    "minimization (admit only the tightest hazard policy every "
    "participant claims) MUST call MEET — calling JOIN silently admits "
    "the most-permissive party's hazard.");

static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::Stateless>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::ConstGlobal>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::MutableGlobal>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::InitOrderHazard>::element_type>);
static_assert(GlobalStateLattice::At<GlobalState::MutableGlobal>::tier == GlobalState::MutableGlobal);

// Runtime smoke — non-constant operands.
inline void global_state_lattice_runtime_smoke_test() {
    GlobalState a = GlobalState::Stateless;
    GlobalState b = GlobalState::InitOrderHazard;
    [[maybe_unused]] bool        rl = GlobalStateLattice::leq(a, b);
    [[maybe_unused]] GlobalState rj = GlobalStateLattice::join(a, b);
    [[maybe_unused]] GlobalState rm = GlobalStateLattice::meet(a, b);

    GlobalState c = GlobalState::ConstGlobal;
    GlobalState d = GlobalState::MutableGlobal;
    [[maybe_unused]] GlobalState rj2 = GlobalStateLattice::join(c, d);
    [[maybe_unused]] GlobalState rm2 = GlobalStateLattice::meet(c, d);

    GlobalStateLattice::At<GlobalState::MutableGlobal>::element_type mg_pin{};
    [[maybe_unused]] GlobalState mg_recovered = mg_pin;
}

}  // namespace detail::global_state_lattice_self_test

}  // namespace crucible::algebra::lattices
