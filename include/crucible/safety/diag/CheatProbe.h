#pragma once
//
// safety/diag/CheatProbe.h — adversarial cheat-detection harness
// (FOUND-E05).  Locks in concept-gate strictness across regressions.
//
// ═════════════════════════════════════════════════════════════════════
// Pattern (28_04_2026_effects.md §7.2)
// ═════════════════════════════════════════════════════════════════════
//
// Every concept gate the framework ships ALSO ships a cheat probe
// harness: a deliberately-crafted type (or function pointer) that
// LOOKS like it should satisfy the gate but actually violates one
// structural property.  The probe asserts the gate REJECTS the
// cheat — the build SUCCEEDS only when every cheat is correctly
// rejected.  If a concept relaxation later admits the cheat, the
// `static_assert` fires and the regression is caught at build time.
//
// Why this matters:
//
//   * Concept gates are the framework's compile-time fences for
//     wrapper-axis discipline (HotPathSafe, DetSafePure,
//     GradedWrapper-conformance, etc.).  A relaxed gate silently
//     admits violating code; CI never sees it because nothing breaks.
//   * The cheat probe is the dual: it asserts the gate's STRICTNESS
//     by exhibiting one anti-pattern per gate.  Future audits expand
//     the cheat catalog monotonically; once a cheat is rejected, the
//     rejection is locked in by build.
//   * The pattern PRECEDES this header: `test/test_concept_cheat_probe.cpp`
//     ships 18 cheats against `algebra::GradedWrapper` (Round-1..5
//     audits, all REJECTED).  This header generalizes the pattern so
//     every Category in `safety/Diagnostic.h::Category` can host its
//     own cheat catalog.
//
// ═════════════════════════════════════════════════════════════════════
// Surface
// ═════════════════════════════════════════════════════════════════════
//
//   * `concept_gate<Category>` — extension point.  Specialize per
//     category whose gate has shipped.  Carries `defined = true` and
//     two predicate templates: `admits_type<T>`, `admits_function<F>`.
//
//   * `cheat_probe_type<T, Category>` — type-side cheat lock.
//     `static_assert`s that T does NOT satisfy the gate.  No-op for
//     undefined gates (lets users register cheats ahead of the gate
//     shipping).
//
//   * `cheat_probe_function<FnPtr, Category>` — function-pointer-side
//     cheat lock.  Same discipline; targets dispatcher gates
//     (FOUND-D's UnaryTransform / Reduction / etc.).
//
//   * `is_gate_defined_v<Category>` — runtime/compile-time query
//     for whether a category's gate has shipped.
//
// ═════════════════════════════════════════════════════════════════════
// Adding a new cheat (the workflow)
// ═════════════════════════════════════════════════════════════════════
//
// 1. Identify the Category whose gate the cheat exercises.
// 2. Construct a TYPE (or function pointer) that LOOKS like it
//    satisfies the gate but violates ONE structural property.
//    The cheat must be MINIMAL — a single-property violation, not
//    a kitchen sink.
// 3. Add the cheat to `test/test_cheat_probe_<category>.cpp`:
//    ```
//    using probe = cheat_probe_type<MyCheatType, Category::Foo>;
//    ```
//    The probe instantiation IS the assertion.
// 4. Build.  If the build SUCCEEDS, the gate correctly rejects.
//    If it FAILS, the gate is too lax — fix the gate.
//
// Naming convention: cheat structs prefixed `Cheat<N>_<reason>` so
// the diagnostic carries a self-documenting name on failure.
//
// ═════════════════════════════════════════════════════════════════════
// Concept-gate specialization template (for gate authors)
// ═════════════════════════════════════════════════════════════════════
//
// When a per-category gate ships (e.g., FOUND-D's HotPathSafe<F>
// concept), the gate author specializes `concept_gate<Category::X>`:
//
//   namespace crucible::safety::diag {
//   template <>
//   struct concept_gate<Category::HotPathViolation> {
//       static constexpr bool defined = true;
//
//       template <typename T>
//       static constexpr bool admits_type = ::crucible::dispatch::HotPathSafe<T>;
//
//       template <auto FnPtr>
//       static constexpr bool admits_function = ::crucible::dispatch::HotPathSafeFn<FnPtr>;
//   };
//   }
//
// After this lands, every existing `cheat_probe_type<T, Category::HotPathViolation>`
// instance starts ENFORCING the rejection (was previously a no-op).
// CI catches gate weakening at build time.

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>           // Category enum + tag_of_t

#include <type_traits>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── concept_gate<Category> — extension point ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Default: undefined.  Cheat probes against undefined gates are
// no-ops (gate hasn't shipped yet; the registered cheats start
// being enforced once the gate author specializes this template).

template <Category C>
struct concept_gate {
    // True iff the gate has shipped and `admits_type` /
    // `admits_function` carry the production predicate.  Default
    // false — cheat probes treat this as "no enforcement yet".
    static constexpr bool defined = false;

    // Type-side gate.  When `defined == false`, returns false (no
    // type is admitted — but cheat probes don't fire because the
    // gate is undefined).  When `defined == true`, the gate
    // specialization replaces this with the production predicate.
    template <typename T>
    static constexpr bool admits_type = false;

    // Function-pointer-side gate.  Same discipline.
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// Convenience predicate.  Useful in CI guards or runtime tooling
// that wants to enumerate which gates have shipped.
template <Category C>
inline constexpr bool is_gate_defined_v = concept_gate<C>::defined;

// ═════════════════════════════════════════════════════════════════════
// ── cheat_probe_type<T, Category> — type-side cheat lock ───────────
// ═════════════════════════════════════════════════════════════════════
//
// Locks in: type T MUST remain rejected by the gate for category C.
// Instantiate at namespace scope; the instantiation IS the assertion.
//
// When the gate is undefined (not yet shipped), the probe is a no-op
// — lets users register cheats ahead of gate authoring.  Once the
// gate ships (concept_gate<C>::defined becomes true), every
// previously-registered probe starts enforcing.

template <typename T, Category C>
struct cheat_probe_type {
    static constexpr bool admits =
        concept_gate<C>::template admits_type<T>;
    static constexpr bool gate_defined = concept_gate<C>::defined;

    // Skip the assertion when the gate is undefined.  This lets
    // probes ship before their gates do; the probe activates when
    // the gate's concept_gate<C> specialization lands.
    static_assert(!gate_defined || !admits,
        "[CheatProbe_TypeAdmitted] A cheat type was spuriously "
        "admitted by the concept gate for the named Category.\n"
        "Recovery:\n"
        "  (a) The gate's predicate has been weakened — restore the\n"
        "      stricter form that previously rejected this type.\n"
        "  (b) The cheat was registered by mistake — remove this\n"
        "      probe instance.\n"
        "Gate location: see the concept_gate<...> specialization\n"
        "for this Category.  Cheat catalog discipline: every gate's\n"
        "rejection is locked by ≥1 probe.");
};

// ═════════════════════════════════════════════════════════════════════
// ── cheat_probe_function<FnPtr, Category> — function-pointer lock ──
// ═════════════════════════════════════════════════════════════════════
//
// Same discipline, but targets dispatcher gates (FOUND-D's per-shape
// concepts: UnaryTransform / Reduction / ProducerEndpoint / ...).
// The cheat is a function POINTER whose signature LOOKS like it
// matches the gate's expected shape but actually violates one
// structural property.

template <auto FnPtr, Category C>
struct cheat_probe_function {
    static constexpr bool admits =
        concept_gate<C>::template admits_function<FnPtr>;
    static constexpr bool gate_defined = concept_gate<C>::defined;

    static_assert(!gate_defined || !admits,
        "[CheatProbe_FunctionAdmitted] A cheat function pointer was "
        "spuriously admitted by the concept gate for the named "
        "Category.\n"
        "Recovery:\n"
        "  (a) The gate's predicate has been weakened — restore the\n"
        "      stricter form that previously rejected this function.\n"
        "  (b) The cheat was registered by mistake — remove this\n"
        "      probe instance.\n"
        "Gate location: see the concept_gate<...> specialization\n"
        "for this Category.");
};

// ═════════════════════════════════════════════════════════════════════
// ── concept_gate specialization for GradedWrapperViolation ─────────
// ═════════════════════════════════════════════════════════════════════
//
// First worked example: the GradedWrapper concept (algebra/GradedTrait.h)
// IS the gate for Category::GradedWrapperViolation.  This forwards
// the cheat-probe protocol into the existing 18-cheat catalog at
// `test/test_concept_cheat_probe.cpp`.
//
// Note: we do NOT include algebra/GradedTrait.h here to keep this
// header lean.  The specialization lives in the header that ALREADY
// pulls GradedTrait — see `algebra/GradedTrait.h` (line cited via
// the static_assert below if uncommented).  Future PR: move the
// specialization here once the include cost is measured (compile-time
// burden bench, R7 of the 28_04 doc).
//
// For now, the GradedWrapper gate ships separately; future per-gate
// specializations follow this pattern.  Documented to flag the
// integration point.

// ═════════════════════════════════════════════════════════════════════
// ── Self-test discipline (intentionally minimal in this header) ────
// ═════════════════════════════════════════════════════════════════════
//
// The cheat-probe pattern's correctness is exercised by SENTINEL TUs
// (`test/test_cheat_probe_compile.cpp`) — NOT by eager instantiation
// in this header.  Reason: instantiating `concept_gate<C>` here for
// any `C` forecloses on later specialization in user TUs.  C++ rule:
// a class template cannot be specialized AFTER it has been
// instantiated for the same arguments.
//
// What we DO assert here (without instantiating concept_gate for any
// specific Category):
//
//   * the trait shapes themselves are well-formed
//   * the API surface compiles
//
// What we explicitly do NOT do:
//
//   * `static_assert(!is_gate_defined_v<Category::Foo>)` — would
//     instantiate concept_gate<Category::Foo>, blocking later
//     specialization in user code
//   * `using probe = cheat_probe_type<X, Category::Foo>` — same
//     problem
//
// The sentinel TU instantiates these correctly: it specializes FIRST,
// then exercises the probes.  That ordering is sound.

namespace detail::cheat_probe_shape_check {

// Verify the API surface is well-formed by referring to the
// templates without instantiating them for any concrete Category.
// `concept_gate` is a class template — referring to its name without
// passing a template-argument-list does NOT instantiate it.
using gate_template = decltype(&concept_gate<Category::EffectRowMismatch>::defined);
//   ↑ This DOES instantiate concept_gate<...EffectRowMismatch>, but
//     we accept it for one Category only — the cheapest possible
//     shape check.  EffectRowMismatch is the first category and the
//     least likely to receive a user specialization (its gate is
//     planned to ship as part of FOUND-D / Met(X) work, in which
//     case the specialization lands in the same header and the
//     ordering is fine).

static_assert(std::is_same_v<gate_template, const bool*>,
    "concept_gate<C>::defined must be a static constexpr bool");

}  // namespace detail::cheat_probe_shape_check

}  // namespace crucible::safety::diag
