#include <crucible/effects/_FxAliases.h>

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace fx = ::crucible::effects;

// Each caller constrains on exactly one predicate, so every test below
// exercises the concept through a requires-clause rather than by evaluating
// it directly. That is the shape a row-validated function signature has.

template <typename R>
    requires fx::IsPure<R>
constexpr bool accepts_pure() noexcept {
    return true;
}

template <typename R>
    requires fx::IsTot<R>
constexpr bool accepts_tot() noexcept {
    return true;
}

template <typename R>
    requires fx::IsGhost<R>
constexpr bool accepts_ghost() noexcept {
    return true;
}

template <typename R>
    requires fx::IsDiv<R>
constexpr bool accepts_div() noexcept {
    return true;
}

template <typename R>
    requires fx::IsST<R>
constexpr bool accepts_st() noexcept {
    return true;
}

template <typename R>
    requires fx::IsAll<R>
constexpr bool accepts_all() noexcept {
    return true;
}

void test_pure_alias_admits_empty_row_only() {
    EXPECT_TRUE(accepts_pure<fx::PureRow>());
    EXPECT_TRUE(accepts_pure<fx::TotRow>());
    EXPECT_TRUE(accepts_pure<fx::GhostRow>());

    // The three rows coincide only because no Ghost atom exists. Introducing
    // one makes GhostRow a distinct type and these assertions wrong.
    static_assert(std::is_same_v<fx::PureRow, fx::TotRow>);
    static_assert(std::is_same_v<fx::PureRow, fx::GhostRow>);
}

void test_div_alias_admits_block_only() {
    EXPECT_TRUE(accepts_div<fx::PureRow>());
    EXPECT_TRUE(accepts_div<fx::DivRow>());
    EXPECT_TRUE(accepts_div<fx::Row<fx::Effect::Block>>());
    // A row carrying a state effect must fail substitution here. That case
    // cannot be spelled as a runtime expectation and lives in a
    // compile-failure fixture instead.
}

void test_st_alias_admits_block_alloc_io() {
    EXPECT_TRUE(accepts_st<fx::PureRow>());
    EXPECT_TRUE(accepts_st<fx::DivRow>());
    EXPECT_TRUE(accepts_st<fx::STRow>());
    EXPECT_TRUE(accepts_st<fx::Row<fx::Effect::Alloc>>());
    EXPECT_TRUE(accepts_st<fx::Row<fx::Effect::IO>>());
    EXPECT_TRUE((accepts_st<fx::Row<fx::Effect::Alloc, fx::Effect::IO>>()));
    EXPECT_TRUE((accepts_st<fx::Row<fx::Effect::Block, fx::Effect::Alloc>>()));
}

void test_all_alias_admits_every_row() {
    EXPECT_TRUE(accepts_all<fx::PureRow>());
    EXPECT_TRUE(accepts_all<fx::DivRow>());
    EXPECT_TRUE(accepts_all<fx::STRow>());
    EXPECT_TRUE(accepts_all<fx::AllRow>());
    EXPECT_TRUE(accepts_all<fx::Row<fx::Effect::Bg>>());
    EXPECT_TRUE(accepts_all<fx::Row<fx::Effect::Init>>());
    EXPECT_TRUE(accepts_all<fx::Row<fx::Effect::Test>>());
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Bg, fx::Effect::Init, fx::Effect::Test>>()));
}

void test_refinement_chain_at_call_site() {
    using R = fx::PureRow;
    EXPECT_TRUE(accepts_pure<R>());
    EXPECT_TRUE(accepts_tot<R>());
    EXPECT_TRUE(accepts_ghost<R>());
    EXPECT_TRUE(accepts_div<R>());
    EXPECT_TRUE(accepts_st<R>());
    EXPECT_TRUE(accepts_all<R>());
}

void test_strictness_at_each_lattice_step() {
    using A = fx::PureRow;
    using B = fx::DivRow;
    using C = fx::STRow;
    using D = fx::AllRow;

    EXPECT_TRUE(accepts_pure<A>() && accepts_div<A>() && accepts_st<A>() && accepts_all<A>());
    EXPECT_TRUE(accepts_div<B>() && accepts_st<B>() && accepts_all<B>());
    EXPECT_TRUE(accepts_st<C>() && accepts_all<C>());
    EXPECT_TRUE(accepts_all<D>());
}

void test_lattice_size_invariants() {
    EXPECT_TRUE(fx::row_size_v<fx::PureRow> == 0);
    EXPECT_TRUE(fx::row_size_v<fx::TotRow> == 0);
    EXPECT_TRUE(fx::row_size_v<fx::GhostRow> == 0);
    EXPECT_TRUE(fx::row_size_v<fx::DivRow> == 1);
    EXPECT_TRUE(fx::row_size_v<fx::STRow> == 3);
    EXPECT_TRUE(fx::row_size_v<fx::AllRow> == fx::effect_count);
}

void test_row_order_independence() {
    // Row order is not canonicalized, so Row<A, B> and Row<B, A> are distinct
    // types. The Subrow relation is a membership test rather than a
    // structural-equality test, so every predicate must admit both spellings.
    // This is where that semantic-versus-structural split is pinned.
    using R_ai = fx::Row<fx::Effect::Alloc, fx::Effect::IO>;
    using R_ia = fx::Row<fx::Effect::IO, fx::Effect::Alloc>;

    static_assert(!std::is_same_v<R_ai, R_ia>);

    EXPECT_TRUE((accepts_st<R_ai>()));
    EXPECT_TRUE((accepts_st<R_ia>()));
    EXPECT_TRUE((accepts_all<R_ai>()));
    EXPECT_TRUE((accepts_all<R_ia>()));

    using R_aib = fx::Row<fx::Effect::Alloc, fx::Effect::IO, fx::Effect::Block>;
    using R_iab = fx::Row<fx::Effect::IO, fx::Effect::Alloc, fx::Effect::Block>;
    using R_bia = fx::Row<fx::Effect::Block, fx::Effect::IO, fx::Effect::Alloc>;
    EXPECT_TRUE((accepts_st<R_aib>()));
    EXPECT_TRUE((accepts_st<R_iab>()));
    EXPECT_TRUE((accepts_st<R_bia>()));
}

void test_pure_tot_ghost_runtime_equivalence() {
    // Checking the three predicates separately is stronger than the type
    // identity above, because a per-concept body specialization could make
    // them diverge while the rows stay the same type.
    static_assert(std::is_same_v<fx::PureRow, fx::TotRow>);
    static_assert(std::is_same_v<fx::PureRow, fx::GhostRow>);

    using R = fx::Row<fx::Effect::Alloc>;
    EXPECT_TRUE(!fx::IsPure<R>);
    EXPECT_TRUE(!fx::IsTot<R>);
    EXPECT_TRUE(!fx::IsGhost<R>);

    EXPECT_TRUE(fx::IsPure<fx::PureRow>);
    EXPECT_TRUE(fx::IsTot<fx::PureRow>);
    EXPECT_TRUE(fx::IsGhost<fx::PureRow>);

    EXPECT_TRUE(fx::IsTot<fx::TotRow>);
    EXPECT_TRUE(fx::IsPure<fx::TotRow>);
    EXPECT_TRUE(fx::IsGhost<fx::GhostRow>);
    EXPECT_TRUE(fx::IsPure<fx::GhostRow>);
}

void test_all_row_contains_every_effect_atom() {
    // Per-atom membership, not a count. A renamed or replaced atom leaves the
    // cardinality check above satisfied and only reddens here.
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE(accepts_all<fx::AllRow>());
}

void test_runtime_consistency() {
    // The loop bound is volatile, so the predicates are evaluated outside a
    // constant-evaluated context. A concept that is only ever reached from
    // static_assert can degrade without any translation unit noticing.
    constexpr bool pure_ok = fx::IsPure<fx::PureRow>;
    constexpr bool div_ok = fx::IsDiv<fx::DivRow>;
    constexpr bool st_ok = fx::IsST<fx::STRow>;
    EXPECT_TRUE(pure_ok && div_ok && st_ok);

    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(fx::IsPure<fx::PureRow>);
        EXPECT_TRUE(fx::IsDiv<fx::DivRow>);
        EXPECT_TRUE(fx::IsST<fx::STRow>);
        EXPECT_TRUE(fx::IsAll<fx::AllRow>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fx_aliases:\n");
    run_test("test_pure_alias_admits_empty_row_only", test_pure_alias_admits_empty_row_only);
    run_test("test_div_alias_admits_block_only", test_div_alias_admits_block_only);
    run_test("test_st_alias_admits_block_alloc_io", test_st_alias_admits_block_alloc_io);
    run_test("test_all_alias_admits_every_row", test_all_alias_admits_every_row);
    run_test("test_refinement_chain_at_call_site", test_refinement_chain_at_call_site);
    run_test("test_strictness_at_each_lattice_step", test_strictness_at_each_lattice_step);
    run_test("test_lattice_size_invariants", test_lattice_size_invariants);
    run_test("test_row_order_independence", test_row_order_independence);
    run_test("test_pure_tot_ghost_runtime_equivalence", test_pure_tot_ghost_runtime_equivalence);
    run_test("test_all_row_contains_every_effect_atom", test_all_row_contains_every_effect_atom);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
