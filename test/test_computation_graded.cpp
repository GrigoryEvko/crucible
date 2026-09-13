// Instantiating the alias is itself part of the claim.  A header that
// only ships embedded static_asserts is never checked under the test
// target's warning matrix until some translation unit uses its surface.

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

#define ENSURE(cond)                                              \
    do {                                                          \
        if (!(cond)) {                                            \
            std::fprintf(stderr, "  ENSURE failed: %s\n", #cond); \
            throw TestFailure{};                                  \
        }                                                         \
    } while (0)

namespace cef = ::crucible::effects;
namespace alg = ::crucible::algebra;

void test_row_to_at_pattern_match() {
    static_assert(std::is_same_v<cef::effect_row_to_at_t<cef::Row<>>, cef::EffectRowLattice::At<>>);
    static_assert(std::is_same_v<cef::effect_row_to_at_t<cef::Row<cef::Effect::Alloc>>,
                                 cef::EffectRowLattice::At<cef::Effect::Alloc>>);
    static_assert(std::is_same_v<cef::effect_row_to_at_t<cef::Row<cef::Effect::Bg, cef::Effect::Alloc>>,
                                 cef::EffectRowLattice::At<cef::Effect::Bg, cef::Effect::Alloc>>);

    // The helper carries the atoms across in order and never sorts them,
    // so two rows over the same set are two types.
    static_assert(!std::is_same_v<cef::effect_row_to_at_t<cef::Row<cef::Effect::Bg, cef::Effect::Alloc>>,
                                  cef::effect_row_to_at_t<cef::Row<cef::Effect::Alloc, cef::Effect::Bg>>>);
}

void test_alias_typedefs() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    static_assert(std::is_same_v<G::value_type, int>);
    static_assert(std::is_same_v<G::lattice_type, cef::EffectRowLattice::At<cef::Effect::Bg>>);
    static_assert(std::is_same_v<G::modality_kind_type, alg::ModalityKind>);
    static_assert(G::modality == alg::ModalityKind::Relative);

    static_assert(std::is_empty_v<G::grade_type>);
}

// The check runs through a template so that a wrong modality on any one
// row instantiation surfaces here rather than only where that row is
// used.
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
    static_assert(admits_relative<cef::Row<cef::Effect::Alloc, cef::Effect::IO, cef::Effect::Block, cef::Effect::Bg,
                                           cef::Effect::Init, cef::Effect::Test>>());
}

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};
struct TwentyFourByteValue {
    unsigned long long a{0}, b{0}, c{0};
};

template <typename R, typename T>
constexpr bool zero_cost_layout() {
    return sizeof(cef::ComputationGraded<R, T>) == sizeof(T) && alignof(cef::ComputationGraded<R, T>) == alignof(T);
}

void test_zero_cost_layout_universal() {
    static_assert(zero_cost_layout<cef::Row<>, char>());
    static_assert(zero_cost_layout<cef::Row<>, int>());
    static_assert(zero_cost_layout<cef::Row<>, double>());
    static_assert(zero_cost_layout<cef::Row<>, OneByteValue>());
    static_assert(zero_cost_layout<cef::Row<>, EightByteValue>());
    static_assert(zero_cost_layout<cef::Row<>, TwentyFourByteValue>());

    static_assert(zero_cost_layout<cef::Row<cef::Effect::Bg>, int>());
    static_assert(zero_cost_layout<cef::Row<cef::Effect::Bg>, EightByteValue>());

    static_assert(zero_cost_layout<cef::Row<cef::Effect::Alloc, cef::Effect::IO, cef::Effect::Bg>, EightByteValue>());

    static_assert(zero_cost_layout<cef::Row<cef::Effect::Alloc, cef::Effect::IO, cef::Effect::Block, cef::Effect::Bg,
                                            cef::Effect::Init, cef::Effect::Test>,
                                   TwentyFourByteValue>());
}

// The wrapper has to stay as trivially copyable as the value it wraps,
// because values like this are persisted by a straight byte copy.
void test_trivially_copyable_parity() {
    static_assert(std::is_trivially_copyable_v<int>);
    static_assert(std::is_trivially_copyable_v<cef::ComputationGraded<cef::Row<>, int>>);
    static_assert(std::is_trivially_copyable_v<cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>>);

    static_assert(std::is_trivially_destructible_v<int>);
    static_assert(std::is_trivially_destructible_v<cef::ComputationGraded<cef::Row<>, int>>);
}

// Each probe below is a named concept rather than an inline requires
// expression.  An inline one written against a constrained member
// function is a hard error on some compilers instead of a false result.
template <typename G>
concept HasPeekMut = requires(G& g) { g.peek_mut(); };
template <typename G>
concept HasSwap = requires(G& a, G& b) { a.swap(b); };
template <typename G>
concept HasComonadExtract = requires(G g) { std::move(g).extract(); };
template <typename G>
concept HasRelMonadInject = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };
template <typename G>
concept HasWeaken = requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G>
concept HasCompose = requires(G g, G const& o) { std::move(g).compose(o); };
template <typename G>
concept HasPeek = requires(G const& g) { g.peek(); };
template <typename G>
concept HasConsume = requires(G g) { std::move(g).consume(); };
template <typename G>
concept HasGrade = requires(G const& g) { g.grade(); };

void test_capability_gates() {
    using GPure = cef::ComputationGraded<cef::Row<>, int>;
    using GBg = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    static_assert(HasPeek<GPure>);
    static_assert(HasConsume<GPure>);
    static_assert(HasGrade<GPure>);
    static_assert(HasWeaken<GPure>);
    static_assert(HasCompose<GPure>);

    // Mutable access survives the relative modality because the gate also
    // admits any wrapper whose grade is an empty type, and these are.
    static_assert(HasPeekMut<GPure>);
    static_assert(HasPeekMut<GBg>);
    static_assert(HasSwap<GPure>);
    static_assert(HasSwap<GBg>);

    static_assert(!HasComonadExtract<GPure>);
    static_assert(!HasComonadExtract<GBg>);
    static_assert(!HasRelMonadInject<GPure>);
    static_assert(!HasRelMonadInject<GBg>);
}

void test_diagnostic_names() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;
    static_assert(G::modality_name() == "Relative");
    static_assert(G::lattice_name() == "EffectRow::At");
}

// The copy-returning overloads of weaken and compose are gated on the
// value being copy constructible.  A move-only value therefore keeps only
// the rvalue overload, which is what the assertions below distinguish.
struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

template <typename G>
concept HasConstWeaken = requires(G const& g, typename G::grade_type r) { g.weaken(r); };

void test_move_only_value_admitted() {
    using G_movable = cef::ComputationGraded<cef::Row<>, int>;
    using G_moveOnly = cef::ComputationGraded<cef::Row<>, MoveOnlyValue>;

    static_assert(sizeof(G_moveOnly) == sizeof(MoveOnlyValue));

    static_assert(HasConstWeaken<G_movable>);
    static_assert(HasWeaken<G_movable>);

    static_assert(!HasConstWeaken<G_moveOnly>);
    static_assert(HasWeaken<G_moveOnly>);
}

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

void test_runtime_smoke_drive() { cef::runtime_smoke_test_computation_graded(); }

void test_runtime_construction_and_access() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    // The seed is a runtime value, so none of the accesses below fold
    // into constants.
    int seed = 7;
    G g{seed * 6, G::grade_type{}};
    ENSURE(g.peek() == 42);

    G g_default{};
    ENSURE(g_default.peek() == 0);

    g_default.peek_mut() = seed * 11;
    ENSURE(g_default.peek() == 77);

    g.swap(g_default);
    ENSURE(g.peek() == 77);
    ENSURE(g_default.peek() == 42);

    int consumed = std::move(g).consume();
    ENSURE(consumed == 77);
}

void test_emptyrow_alias_agreement() {
    static_assert(std::is_same_v<cef::ComputationGraded<cef::EmptyRow, int>, cef::ComputationGraded<cef::Row<>, int>>);
    static_assert(
        std::is_same_v<cef::ComputationGraded<cef::EmptyRow, double>, cef::ComputationGraded<cef::Row<>, double>>);
    static_assert(sizeof(cef::ComputationGraded<cef::EmptyRow, int>) == sizeof(int));

    using G_via_alias = cef::ComputationGraded<cef::EmptyRow, int>;
    using G_via_direct = cef::ComputationGraded<cef::Row<>, int>;
    static_assert(std::is_same_v<G_via_alias, G_via_direct>);

    G_via_alias via_alias{17, G_via_alias::grade_type{}};
    G_via_direct via_direct{17, G_via_direct::grade_type{}};
    ENSURE(via_alias.peek() == via_direct.peek());
}

// The factory exists for every lattice that is bounded below, and this
// one is, so it must be reachable here.
void test_at_bottom_factory_reachable() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    int seed = 31;
    G g_bot = G::at_bottom(seed);

    ENSURE(g_bot.peek() == 31);

    // This sub-lattice has one element, so its bottom is also its top.
    // That is why the ordering holds in both directions below.
    auto bottom_grade = G::lattice_type::bottom();
    ENSURE(G::lattice_type::leq(g_bot.grade(), bottom_grade));
    ENSURE(G::lattice_type::leq(bottom_grade, g_bot.grade()));
}

void test_runtime_weaken_and_compose() {
    using G = cef::ComputationGraded<cef::Row<cef::Effect::Bg>, int>;

    G a{10, G::grade_type{}};
    G b{20, G::grade_type{}};

    // Over a one-element lattice a weakening is a move and nothing else.
    G a_weakened = std::move(a).weaken(G::grade_type{});
    ENSURE(a_weakened.peek() == 10);

    // Composition keeps the left value and joins the two grades, so the
    // result here is the left value and the same single grade.
    G composed = a_weakened.compose(b);
    ENSURE(composed.peek() == 10);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_graded:\n");
    run_test("test_row_to_at_pattern_match", test_row_to_at_pattern_match);
    run_test("test_alias_typedefs", test_alias_typedefs);
    run_test("test_modality_is_relative_uniformly", test_modality_is_relative_uniformly);
    run_test("test_zero_cost_layout_universal", test_zero_cost_layout_universal);
    run_test("test_trivially_copyable_parity", test_trivially_copyable_parity);
    run_test("test_capability_gates", test_capability_gates);
    run_test("test_diagnostic_names", test_diagnostic_names);
    run_test("test_move_only_value_admitted", test_move_only_value_admitted);
    run_test("test_object_semantics", test_object_semantics);
    run_test("test_runtime_smoke_drive", test_runtime_smoke_drive);
    run_test("test_runtime_construction_and_access", test_runtime_construction_and_access);
    run_test("test_runtime_weaken_and_compose", test_runtime_weaken_and_compose);
    run_test("test_emptyrow_alias_agreement", test_emptyrow_alias_agreement);
    run_test("test_at_bottom_factory_reachable", test_at_bottom_factory_reachable);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
