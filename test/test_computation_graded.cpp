// ═══════════════════════════════════════════════════════════════════
// test_computation_graded — FOUND-H03 dedicated coverage
//
// Verifies the ComputationGraded<R, T> alias matches the substrate
// invariants documented at ComputationGraded.h:
//
//   - R → At<Es...> conversion via effect_row_to_at_t
//   - Modality::Relative is the substrate modality
//   - sizeof(ComputationGraded<R, T>) == sizeof(T) regardless of R
//   - Capability surface: weaken/compose/peek_mut/swap reachable;
//     extract/inject NOT reachable (Comonad/RelativeMonad-only)
//   - Trivial-copyability parity (Cipher serialization)
//   - Lattice diagnostic forwarding (modality_name, lattice_name)
//   - Runtime smoke test exercises every accessor with non-const args
//
// Sister tests:
//   test_effect_row_lattice  — H01 lattice + At<> + bridge
//   test_os_universe         — H02 Universe descriptor
//   test_effects_compile     — sentinel TU forcing the full -Werror
//                              matrix on every effects/* header
//
// Per the load-bearing static-assert blind-spot discipline (memory
// `feedback_header_only_static_assert_blind_spot`): a header that
// only ships embedded static_asserts isn't verified under the test
// target's full warning matrix unless a .cpp TU instantiates its
// surface.  This file is that .cpp for ComputationGraded.h, mirroring
// the H01/H02 pattern.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Modality.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

#define ENSURE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "  ENSURE failed: %s\n", #cond);         \
            throw TestFailure{};                                          \
        }                                                                 \
    } while (0)

namespace cef = ::crucible::effects;
namespace alg = ::crucible::algebra;

// ── 1. effect_row_to_at_t pattern-match ─────────────────────────────
//
// Verifies the Row<Es...> → At<Es...> bridge that drives the alias.

void test_row_to_at_pattern_match() {
    static_assert(std::is_same_v<
        cef::effect_row_to_at_t<cef::Row<>>,
        cef::EffectRowLattice::At<>>);
    static_assert(std::is_same_v<
        cef::effect_row_to_at_t<cef::Row<cef::Effect::Alloc>>,
        cef::EffectRowLattice::At<cef::Effect::Alloc>>);
    static_assert(std::is_same_v<
        cef::effect_row_to_at_t<
            cef::Row<cef::Effect::Bg, cef::Effect::Alloc>>,
        cef::EffectRowLattice::At<cef::Effect::Bg, cef::Effect::Alloc>>);

    // Order is preserved through the helper (the helper does NOT
    // canonicalize); set-equivalence in bits() is enforced separately
    // at the At<> level by the order-independence agreement asserts.
    static_assert(!std::is_same_v<
        cef::effect_row_to_at_t<
            cef::Row<cef::Effect::Bg, cef::Effect::Alloc>>,
        cef::effect_row_to_at_t<
            cef::Row<cef::Effect::Alloc, cef::Effect::Bg>>>);
}

// ── 2. Type-level identity of the alias ─────────────────────────────

void test_alias_typedefs() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    static_assert(std::is_same_v<G::value_type, int>);
    static_assert(std::is_same_v<G::lattice_type,
        cef::EffectRowLattice::At<cef::Effect::Bg>>);
    static_assert(std::is_same_v<G::modality_kind_type,
        alg::ModalityKind>);
    static_assert(G::modality == alg::ModalityKind::Relative);

    // grade_type IS the lattice's element_type — empty struct.
    static_assert(std::is_empty_v<G::grade_type>);
}

// ── 3. Modality::Relative across every Row ──────────────────────────
//
// Templated caller so an accidental Modality::Absolute or Comonad on
// any single Row instantiation surfaces here, not just at the doc-
// embedded asserts in ComputationGraded.h.

template <typename R>
constexpr bool admits_relative() {
    using G = cef::ComputationGraded<R, int>;
    return G::modality == alg::ModalityKind::Relative;
}

void test_modality_is_relative_uniformly() {
    static_assert(admits_relative<cef::Row<>>());
    static_assert(admits_relative<cef::Row<cef::Effect::Alloc>>());
    static_assert(admits_relative<cef::Row<cef::Effect::IO>>());
    static_assert(admits_relative<cef::Row<cef::Effect::Block>>());
    static_assert(admits_relative<cef::Row<cef::Effect::Bg>>());
    static_assert(admits_relative<cef::Row<cef::Effect::Init>>());
    static_assert(admits_relative<cef::Row<cef::Effect::Test>>());
    static_assert(admits_relative<cef::Row<
        cef::Effect::Alloc, cef::Effect::IO, cef::Effect::Block,
        cef::Effect::Bg,    cef::Effect::Init, cef::Effect::Test>>());
}

// ── 4. Layout: sizeof(Graded) == sizeof(T) across every regime ──────

struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };
struct TwentyFourByteValue { unsigned long long a{0}, b{0}, c{0}; };

template <typename R, typename T>
constexpr bool zero_cost_layout() {
    return sizeof(cef::ComputationGraded<R, T>) == sizeof(T)
        && alignof(cef::ComputationGraded<R, T>) == alignof(T);
}

void test_zero_cost_layout_universal() {
    // Empty row × every value width.
    static_assert(zero_cost_layout<cef::Row<>, char>());
    static_assert(zero_cost_layout<cef::Row<>, int>());
    static_assert(zero_cost_layout<cef::Row<>, double>());
    static_assert(zero_cost_layout<cef::Row<>, OneByteValue>());
    static_assert(zero_cost_layout<cef::Row<>, EightByteValue>());
    static_assert(zero_cost_layout<cef::Row<>, TwentyFourByteValue>());

    // Single-atom row × every value width.
    static_assert(zero_cost_layout<cef::Row<cef::Effect::Bg>, int>());
    static_assert(zero_cost_layout<cef::Row<cef::Effect::Bg>,
        EightByteValue>());

    // Multi-atom row.
    static_assert(zero_cost_layout<
        cef::Row<cef::Effect::Alloc, cef::Effect::IO,
                 cef::Effect::Bg>,
        EightByteValue>());

    // All-atoms row.
    static_assert(zero_cost_layout<cef::Row<
        cef::Effect::Alloc, cef::Effect::IO, cef::Effect::Block,
        cef::Effect::Bg,    cef::Effect::Init, cef::Effect::Test>,
        TwentyFourByteValue>());
}

// ── 5. Trivial-copyability parity (Cipher serialization) ────────────

void test_trivially_copyable_parity() {
    static_assert(std::is_trivially_copyable_v<int>);
    static_assert(std::is_trivially_copyable_v<
        cef::ComputationGraded<cef::Row<>, int>>);
    static_assert(std::is_trivially_copyable_v<
        cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>>);

    static_assert(std::is_trivially_destructible_v<int>);
    static_assert(std::is_trivially_destructible_v<
        cef::ComputationGraded<cef::Row<>, int>>);
}

// ── 6. Capability surface — weaken/compose/peek_mut reachable;
//      extract/inject NOT reachable under Relative modality ───────────
//
// Concepts SFINAE cleanly; inline `requires(...) { ... }` against
// member-function constraints emits a hard error in some GCC versions
// (per algebra/Graded.h:1054 indirection).  Same pattern here.

template <typename G> concept HasPeekMut =
    requires(G& g) { g.peek_mut(); };
template <typename G> concept HasSwap =
    requires(G& a, G& b) { a.swap(b); };
template <typename G> concept HasComonadExtract =
    requires(G g) { std::move(g).extract(); };
template <typename G> concept HasRelMonadInject =
    requires { G::inject(typename G::value_type{},
                         typename G::grade_type{}); };
template <typename G> concept HasWeaken =
    requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G> concept HasCompose =
    requires(G g, G const& o) { std::move(g).compose(o); };
template <typename G> concept HasPeek =
    requires(G const& g) { g.peek(); };
template <typename G> concept HasConsume =
    requires(G g) { std::move(g).consume(); };
template <typename G> concept HasGrade =
    requires(G const& g) { g.grade(); };

void test_capability_gates() {
    using GPure = cef::ComputationGraded<cef::Row<>, int>;
    using GBg   = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    // Always reachable through Graded primary template.
    static_assert(HasPeek<GPure>);
    static_assert(HasConsume<GPure>);
    static_assert(HasGrade<GPure>);
    static_assert(HasWeaken<GPure>);
    static_assert(HasCompose<GPure>);

    // peek_mut/swap admitted via the empty-grade clause of the
    // refined gate (`AbsoluteModality<M> || std::is_empty_v<grade_type>`).
    static_assert(HasPeekMut<GPure>);
    static_assert(HasPeekMut<GBg>);
    static_assert(HasSwap<GPure>);
    static_assert(HasSwap<GBg>);

    // Comonad/RelativeMonad-only ops MUST NOT be reachable under
    // Modality::Relative.
    static_assert(!HasComonadExtract<GPure>);
    static_assert(!HasComonadExtract<GBg>);
    static_assert(!HasRelMonadInject<GPure>);
    static_assert(!HasRelMonadInject<GBg>);
}

// ── 7. Diagnostic forwarding ────────────────────────────────────────

void test_diagnostic_names() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;
    static_assert(G::modality_name() == "Relative");
    static_assert(G::lattice_name()  == "EffectRow::At");
}

// ── 8. Move-only T compatibility (regime-1 EBO) ─────────────────────
//
// The alias must accept move-only T cleanly.  Graded gates copy-
// returning overloads of weaken/compose on copy_constructible<T>; the
// && rvalue overload remains available for move-only T.

struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

template <typename G> concept HasConstWeaken =
    requires(G const& g, typename G::grade_type r) { g.weaken(r); };

void test_move_only_value_admitted() {
    using G_movable  = cef::ComputationGraded<cef::Row<>, int>;
    using G_moveOnly = cef::ComputationGraded<cef::Row<>, MoveOnlyValue>;

    // Layout still zero-cost on move-only T.
    static_assert(sizeof(G_moveOnly) == sizeof(MoveOnlyValue));

    // Movable T → both const& and && weaken overloads available.
    static_assert(HasConstWeaken<G_movable>);
    static_assert(HasWeaken<G_movable>);

    // Move-only T → const& weaken SFINAEs away (gated on
    // copy_constructible<T>); only && remains.
    static_assert(!HasConstWeaken<G_moveOnly>);
    static_assert(HasWeaken<G_moveOnly>);
}

// ── 9. Object semantics (defaulted copy/move) ───────────────────────

void test_object_semantics() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    static_assert(std::is_default_constructible_v<G>);
    static_assert(std::is_copy_constructible_v<G>);
    static_assert(std::is_move_constructible_v<G>);
    static_assert(std::is_copy_assignable_v<G>);
    static_assert(std::is_move_assignable_v<G>);
    static_assert(std::is_destructible_v<G>);
    static_assert(std::is_nothrow_move_constructible_v<G>);
    static_assert(std::is_nothrow_destructible_v<G>);
}

// ── 10. Runtime smoke — exercise every accessor at runtime ──────────
//
// Drives the header's runtime_smoke_test_computation_graded so the
// test target actually instantiates the bodies under the full
// -Werror matrix (per the runtime smoke-test discipline).

void test_runtime_smoke_drive() {
    cef::runtime_smoke_test_computation_graded();
}

// ── 11. Runtime construction + access via non-constant args ─────────

void test_runtime_construction_and_access() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    int seed = 7;
    G g{seed * 6, G::grade_type{}};   // 42
    ENSURE(g.peek() == 42);

    G g_default{};
    ENSURE(g_default.peek() == 0);

    g_default.peek_mut() = seed * 11;   // 77
    ENSURE(g_default.peek() == 77);

    g.swap(g_default);
    ENSURE(g.peek() == 77);
    ENSURE(g_default.peek() == 42);

    int consumed = std::move(g).consume();
    ENSURE(consumed == 77);
}

// ── AUDIT-1a. EmptyRow alias agreement (FOUND-H03-AUDIT-1) ──────────

void test_emptyrow_alias_agreement() {
    static_assert(std::is_same_v<
        cef::ComputationGraded<cef::EmptyRow, int>,
        cef::ComputationGraded<cef::Row<>, int>>);
    static_assert(std::is_same_v<
        cef::ComputationGraded<cef::EmptyRow, double>,
        cef::ComputationGraded<cef::Row<>, double>>);
    static_assert(sizeof(cef::ComputationGraded<cef::EmptyRow, int>)
                  == sizeof(int));

    // Also drive a runtime construction through both spellings to
    // confirm value semantics are identical.
    using G_via_alias  = cef::ComputationGraded<cef::EmptyRow, int>;
    using G_via_direct = cef::ComputationGraded<cef::Row<>, int>;
    static_assert(std::is_same_v<G_via_alias, G_via_direct>);

    G_via_alias  via_alias{17, G_via_alias::grade_type{}};
    G_via_direct via_direct{17, G_via_direct::grade_type{}};
    ENSURE(via_alias.peek() == via_direct.peek());
}

// ── AUDIT-1b. at_bottom() factory reachability (FOUND-H03-AUDIT-1) ──
//
// At<> satisfies BoundedBelowLattice (proved at H01); Graded provides
// at_bottom(value) for every BoundedBelowLattice instantiation.  Drive
// the factory with non-constant args + verify the constructed result
// has bottom-equivalent grade.

void test_at_bottom_factory_reachable() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    int seed = 31;
    G g_bot = G::at_bottom(seed);

    // Value preserved, grade is the singleton bottom (== top, in this
    // degenerate sub-lattice).
    ENSURE(g_bot.peek() == 31);

    // The grade type is empty — operator==(empty, empty) returns true
    // by the H01 At<> definition.  Spot-witness via the lattice's leq.
    auto bottom_grade = G::lattice_type::bottom();
    ENSURE(G::lattice_type::leq(g_bot.grade(), bottom_grade));
    ENSURE(G::lattice_type::leq(bottom_grade, g_bot.grade()));
}

// ── 12. weaken/compose at runtime via the empty-grade no-op path ────

void test_runtime_weaken_and_compose() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    G a{10, G::grade_type{}};
    G b{20, G::grade_type{}};

    // weaken on the singleton lattice is a no-op move; must succeed.
    G a_weakened = std::move(a).weaken(G::grade_type{});
    ENSURE(a_weakened.peek() == 10);

    // compose preserves *this's value, joins grades (which are
    // singleton, so the join is the same singleton).
    G composed = a_weakened.compose(b);
    ENSURE(composed.peek() == 10);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_graded:\n");
    run_test("test_row_to_at_pattern_match",      test_row_to_at_pattern_match);
    run_test("test_alias_typedefs",               test_alias_typedefs);
    run_test("test_modality_is_relative_uniformly",
             test_modality_is_relative_uniformly);
    run_test("test_zero_cost_layout_universal",   test_zero_cost_layout_universal);
    run_test("test_trivially_copyable_parity",    test_trivially_copyable_parity);
    run_test("test_capability_gates",             test_capability_gates);
    run_test("test_diagnostic_names",             test_diagnostic_names);
    run_test("test_move_only_value_admitted",     test_move_only_value_admitted);
    run_test("test_object_semantics",             test_object_semantics);
    run_test("test_runtime_smoke_drive",          test_runtime_smoke_drive);
    run_test("test_runtime_construction_and_access",
             test_runtime_construction_and_access);
    run_test("test_runtime_weaken_and_compose",   test_runtime_weaken_and_compose);
    run_test("test_emptyrow_alias_agreement",     test_emptyrow_alias_agreement);
    run_test("test_at_bottom_factory_reachable",  test_at_bottom_factory_reachable);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
