#include <crucible/effects/_EffectRowLattice.h>

#include <crucible/algebra/_Lattice.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_FxAliases.h>

#include <cstdio>
#include <cstdlib>

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
namespace alg = ::crucible::algebra;
using L = fx::EffectRowLattice;
using EL = L::element_type;

// These mirror how a graded consumer constrains on the lattice concept.
// Calling them means a rewrite that stops satisfying the concept fails
// here, rather than silently making every downstream instantiation
// reject the lattice.

template <alg::Lattice Lat>
constexpr bool admits_lattice() noexcept {
    return true;
}

template <alg::BoundedLattice Lat>
constexpr bool admits_bounded_lattice() noexcept {
    return true;
}

void test_lattice_concept_satisfied() {
    EXPECT_TRUE(admits_lattice<L>());
    EXPECT_TRUE(admits_bounded_lattice<L>());
}

void test_bottom_top_runtime_values() {
    // Reading the bounds at runtime catches a regression that leaves
    // them correct only at consteval.
    EL b = L::bottom();
    EL t = L::top();
    EXPECT_TRUE(b == 0);
    EXPECT_TRUE(t == ((EL{1} << fx::effect_count) - 1));
    EXPECT_TRUE(L::leq(b, t));
    EXPECT_TRUE(!L::leq(t, b));
}

void test_join_meet_runtime() {
    EL a = EL{1} << static_cast<unsigned>(fx::Effect::Alloc);
    EL i = EL{1} << static_cast<unsigned>(fx::Effect::IO);
    EL u = L::join(a, i);
    EL p = L::meet(a, i);
    EXPECT_TRUE(u == (a | i));
    EXPECT_TRUE(p == 0);  // disjoint singletons
    EXPECT_TRUE(L::join(L::bottom(), a) == a);
    EXPECT_TRUE(L::meet(L::top(), a) == a);
}

void test_row_descriptor_bridge_runtime() {
    EL pure_d = fx::row_descriptor_v<fx::Row<>>;
    EL alloc_d = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>;
    EL all_d = fx::row_descriptor_v<fx::AllRow>;

    EXPECT_TRUE(pure_d == 0);
    EXPECT_TRUE(alloc_d == (EL{1} << static_cast<unsigned>(fx::Effect::Alloc)));
    EXPECT_TRUE(all_d == L::top());

    // The named rows stand for: pure is empty, div carries Block, the
    // state row carries Block with Alloc and IO, and all is the
    // universe.
    EXPECT_TRUE(fx::row_descriptor_v<fx::PureRow> == 0);
    EXPECT_TRUE(fx::row_descriptor_v<fx::DivRow> == (EL{1} << static_cast<unsigned>(fx::Effect::Block)));
    EXPECT_TRUE(
        (fx::row_descriptor_v<fx::STRow> & fx::row_descriptor_v<fx::DivRow>) == fx::row_descriptor_v<fx::DivRow>);
    EXPECT_TRUE(fx::row_descriptor_v<fx::AllRow> == L::top());
}

void test_lattice_subrow_bridge_agreement_runtime() {
    // The subrow relation at the type level must agree with the
    // lattice order over the two bitmasks.  Checking it at a call site
    // means a regression on either surface fires here.

    auto bridge_check = []<typename R1, typename R2>() noexcept {
        constexpr bool type_level = fx::is_subrow_v<R1, R2>;
        constexpr bool bitmask = L::leq(fx::row_descriptor_v<R1>, fx::row_descriptor_v<R2>);
        return type_level == bitmask;
    };

    EXPECT_TRUE((bridge_check.template operator()<fx::Row<>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE(
        (bridge_check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Alloc, fx::Effect::IO>>()));
    EXPECT_TRUE(
        (bridge_check.template operator()<fx::Row<fx::Effect::Alloc, fx::Effect::IO>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((bridge_check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((bridge_check.template operator()<fx::PureRow, fx::AllRow>()));
    EXPECT_TRUE((bridge_check.template operator()<fx::AllRow, fx::PureRow>()));
}

void test_subrow_leq_agreement_exhaustive() {
    // The same agreement, enumerated over every pair of singleton rows
    // and both extremes, rather than the hand-picked pairs above.  The
    // pairs are written out rather than walked by reflection, so a
    // renumbering of the effect enum fails one assertion per affected
    // pair instead of one for the whole walk.
    //
    // Thirty-six singleton pairs, six empty-to-singleton, six
    // singleton-to-universe and the empty-to-universe edge make
    // forty-nine cases.

    auto check = []<typename R1, typename R2>() noexcept -> bool {
        constexpr bool type_level = fx::is_subrow_v<R1, R2>;
        constexpr bool bitmask = L::leq(fx::row_descriptor_v<R1>, fx::row_descriptor_v<R2>);
        return type_level == bitmask;
    };

    // Between two singletons the order holds only when they are equal.
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<fx::Effect::Test>>()));

    // The empty row is below every singleton and no singleton is below
    // it.
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::Row<fx::Effect::Test>>()));

    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::Row<>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::Row<>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::Row<>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::Row<>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::Row<>>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::Row<>>()));

    // Every singleton is below the universe and the universe is below
    // no singleton.
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Alloc>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::IO>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Block>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Bg>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Init>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::Row<fx::Effect::Test>, fx::AllRow>()));

    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::IO>>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::Block>>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::Bg>>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::Init>>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<fx::Effect::Test>>()));

    // The empty row is below the universe and not the reverse.
    EXPECT_TRUE((check.template operator()<fx::Row<>, fx::AllRow>()));
    EXPECT_TRUE((check.template operator()<fx::AllRow, fx::Row<>>()));
}

void test_lattice_is_distributive_at_runtime() {
    // A powerset lattice distributes, which the header already asserts
    // at compile time.  These are the runtime witnesses.
    EL a = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>;
    EL b = fx::row_descriptor_v<fx::Row<fx::Effect::IO>>;
    EL c = fx::row_descriptor_v<fx::Row<fx::Effect::Block>>;

    EXPECT_TRUE(L::meet(a, L::join(b, c)) == L::join(L::meet(a, b), L::meet(a, c)));
    EXPECT_TRUE(L::join(a, L::meet(b, c)) == L::meet(L::join(a, b), L::join(a, c)));
}

void test_lattice_name_runtime() {
    auto nm = alg::lattice_name<L>();
    EXPECT_TRUE(nm == "EffectRow");
}

void test_runtime_consistency() {
    // Repeating the operations catches an accessor that degrades once
    // it is called outside a constant expression.
    constexpr EL canon_top = L::top();
    EXPECT_TRUE(canon_top == ((EL{1} << fx::effect_count) - 1));

    volatile std::size_t const cap = 50;
    for (std::size_t k = 0; k < cap; ++k) {
        EXPECT_TRUE(L::top() == canon_top);
        EXPECT_TRUE(L::bottom() == 0);
        EXPECT_TRUE(L::join(0, 0xFF) == 0xFF);
        EXPECT_TRUE(L::meet(0xFF, 0) == 0);
        EXPECT_TRUE(L::leq(0, canon_top));
        EXPECT_TRUE(!L::leq(canon_top, 0));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_effect_row_lattice:\n");
    run_test("test_lattice_concept_satisfied", test_lattice_concept_satisfied);
    run_test("test_bottom_top_runtime_values", test_bottom_top_runtime_values);
    run_test("test_join_meet_runtime", test_join_meet_runtime);
    run_test("test_row_descriptor_bridge_runtime", test_row_descriptor_bridge_runtime);
    run_test("test_lattice_subrow_bridge_agreement_runtime", test_lattice_subrow_bridge_agreement_runtime);
    run_test("test_subrow_leq_agreement_exhaustive", test_subrow_leq_agreement_exhaustive);
    run_test("test_lattice_is_distributive_at_runtime", test_lattice_is_distributive_at_runtime);
    run_test("test_lattice_name_runtime", test_lattice_name_runtime);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
