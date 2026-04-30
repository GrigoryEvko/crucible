// ═══════════════════════════════════════════════════════════════════
// test_effect_row_lattice — FOUND-H01 dedicated test
//
// Pins the EffectRowLattice<>'s Lattice-concept conformance + the
// row_descriptor_v bridge through templated callers that mimic the
// production shape downstream Graded<EffectRowLattice, _, T> code
// will use in H03+.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/EffectRowLattice.h>

#include <crucible/algebra/Lattice.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace fx  = ::crucible::effects;
namespace alg = ::crucible::algebra;
using L  = fx::EffectRowLattice;
using EL = L::element_type;

// ── Templated callers — production-shape Lattice consumer surface ──
//
// Mirrors how Graded<Modality, EffectRowLattice, T>::weaken() will
// invoke leq() at runtime in H03.  Constraining on the Lattice
// concept ensures any future EffectRowLattice rewrite still satisfies
// the concept; substitution failure here means downstream Graded
// instantiations would reject EffectRowLattice silently.

template <alg::Lattice Lat>
constexpr bool admits_lattice() noexcept { return true; }

template <alg::BoundedLattice Lat>
constexpr bool admits_bounded_lattice() noexcept { return true; }

// ── Tests ─────────────────────────────────────────────────────────

void test_lattice_concept_satisfied() {
    // The static_asserts in the header already prove these — but the
    // templated-caller form catches ANY future regression in concept
    // body that the static_assert wall might miss.  Same audit
    // pattern as G79's test_strictness_at_each_lattice_step.
    EXPECT_TRUE(admits_lattice<L>());
    EXPECT_TRUE(admits_bounded_lattice<L>());
}

void test_bottom_top_runtime_values() {
    // Runtime read of the bounded constants — catches consteval-only
    // regressions that the static_assert wall wouldn't.
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
    EXPECT_TRUE(L::meet(L::top(),    a) == a);
}

void test_row_descriptor_bridge_runtime() {
    // Cross-check at runtime that the bridge agrees with the bitmask
    // computed manually.
    EL pure_d  = fx::row_descriptor_v<fx::Row<>>;
    EL alloc_d = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>;
    EL all_d   = fx::row_descriptor_v<fx::AllRow>;

    EXPECT_TRUE(pure_d == 0);
    EXPECT_TRUE(alloc_d == (EL{1} << static_cast<unsigned>(fx::Effect::Alloc)));
    EXPECT_TRUE(all_d == L::top());

    // FxAliases-level rows: PureRow = empty, DivRow = {Block},
    // STRow = {Block, Alloc, IO}, AllRow = universe.
    EXPECT_TRUE(fx::row_descriptor_v<fx::PureRow> == 0);
    EXPECT_TRUE(fx::row_descriptor_v<fx::DivRow>
                == (EL{1} << static_cast<unsigned>(fx::Effect::Block)));
    EXPECT_TRUE((fx::row_descriptor_v<fx::STRow>
                 & fx::row_descriptor_v<fx::DivRow>)
                == fx::row_descriptor_v<fx::DivRow>);
    EXPECT_TRUE(fx::row_descriptor_v<fx::AllRow> == L::top());
}

void test_lattice_subrow_bridge_agreement_runtime() {
    // The semantic guarantee: is_subrow_v<R1, R2> at the type level
    // must match L::leq(row_descriptor_v<R1>, row_descriptor_v<R2>)
    // at the bitmask level.  Tested at the call site so a future
    // regression in either surface fires immediately.

    auto bridge_check = []<typename R1, typename R2>() noexcept {
        constexpr bool type_level = fx::is_subrow_v<R1, R2>;
        constexpr bool bitmask    = L::leq(
            fx::row_descriptor_v<R1>, fx::row_descriptor_v<R2>);
        return type_level == bitmask;
    };

    EXPECT_TRUE((bridge_check.template operator()<
                 fx::Row<>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((bridge_check.template operator()<
                 fx::Row<fx::Effect::Alloc>, fx::Row<fx::Effect::Alloc, fx::Effect::IO>>()));
    EXPECT_TRUE((bridge_check.template operator()<
                 fx::Row<fx::Effect::Alloc, fx::Effect::IO>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((bridge_check.template operator()<
                 fx::Row<fx::Effect::IO>, fx::Row<fx::Effect::Alloc>>()));
    EXPECT_TRUE((bridge_check.template operator()<
                 fx::PureRow, fx::AllRow>()));
    EXPECT_TRUE((bridge_check.template operator()<
                 fx::AllRow, fx::PureRow>()));
}

void test_lattice_is_distributive_at_runtime() {
    // Powerset lattice is distributive — verify at runtime witnesses
    // (the static_assert wall already proves it at compile time).
    EL a = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>;
    EL b = fx::row_descriptor_v<fx::Row<fx::Effect::IO>>;
    EL c = fx::row_descriptor_v<fx::Row<fx::Effect::Block>>;

    EXPECT_TRUE(L::meet(a, L::join(b, c))
                == L::join(L::meet(a, b), L::meet(a, c)));
    EXPECT_TRUE(L::join(a, L::meet(b, c))
                == L::meet(L::join(a, b), L::join(a, c)));
}

void test_lattice_name_runtime() {
    auto nm = alg::lattice_name<L>();
    EXPECT_TRUE(nm == "EffectRow");
}

void test_runtime_consistency() {
    // Verify the lattice operations are invariant across 50
    // invocations — catches consteval/inline-body regressions where
    // a constexpr accessor accidentally degrades.
    constexpr EL canon_top = L::top();
    EXPECT_TRUE(canon_top == ((EL{1} << fx::effect_count) - 1));

    volatile std::size_t const cap = 50;
    for (std::size_t k = 0; k < cap; ++k) {
        EXPECT_TRUE(L::top()         == canon_top);
        EXPECT_TRUE(L::bottom()      == 0);
        EXPECT_TRUE(L::join(0, 0xFF) == 0xFF);
        EXPECT_TRUE(L::meet(0xFF, 0) == 0);
        EXPECT_TRUE( L::leq(0, canon_top));
        EXPECT_TRUE(!L::leq(canon_top, 0));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_effect_row_lattice:\n");
    run_test("test_lattice_concept_satisfied",
             test_lattice_concept_satisfied);
    run_test("test_bottom_top_runtime_values",
             test_bottom_top_runtime_values);
    run_test("test_join_meet_runtime",
             test_join_meet_runtime);
    run_test("test_row_descriptor_bridge_runtime",
             test_row_descriptor_bridge_runtime);
    run_test("test_lattice_subrow_bridge_agreement_runtime",
             test_lattice_subrow_bridge_agreement_runtime);
    run_test("test_lattice_is_distributive_at_runtime",
             test_lattice_is_distributive_at_runtime);
    run_test("test_lattice_name_runtime",
             test_lattice_name_runtime);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
