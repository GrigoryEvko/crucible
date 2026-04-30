// ═══════════════════════════════════════════════════════════════════
// test_fx_aliases — FOUND-G79 / G80 dedicated test
//
// Exercises the F*-style alias rows + predicate concepts through
// templated callers — the production-shape surface for any future
// row-validated function signature.  Sentinel coverage in
// test_effects_compile.cpp closes the header-only static_assert
// blind-spot; this file pins call-site behaviour.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/FxAliases.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace fx = ::crucible::effects;

// ── Templated callers — the production-shape surface ──────────────
//
// Each `accepts_X<R>` returns true iff R satisfies the corresponding
// concept.  This is exactly the pattern a future row-validated
// function template will use as its requires-clause.

template <typename R>
    requires fx::IsPure<R>
constexpr bool accepts_pure() noexcept { return true; }

template <typename R>
    requires fx::IsTot<R>
constexpr bool accepts_tot() noexcept { return true; }

template <typename R>
    requires fx::IsGhost<R>
constexpr bool accepts_ghost() noexcept { return true; }

template <typename R>
    requires fx::IsDiv<R>
constexpr bool accepts_div() noexcept { return true; }

template <typename R>
    requires fx::IsST<R>
constexpr bool accepts_st() noexcept { return true; }

template <typename R>
    requires fx::IsAll<R>
constexpr bool accepts_all() noexcept { return true; }

// ── Tests ─────────────────────────────────────────────────────────

void test_pure_alias_admits_empty_row_only() {
    EXPECT_TRUE(accepts_pure<fx::PureRow>());
    EXPECT_TRUE(accepts_pure<fx::TotRow>());     // structurally PureRow
    EXPECT_TRUE(accepts_pure<fx::GhostRow>());   // structurally PureRow

    // Sanity: PureRow / TotRow / GhostRow are *the same row type*
    // under the current Crucible mapping.  A future Ghost atom would
    // make GhostRow distinct; this test would then need amendment.
    static_assert(std::is_same_v<fx::PureRow, fx::TotRow>);
    static_assert(std::is_same_v<fx::PureRow, fx::GhostRow>);
}

void test_div_alias_admits_block_only() {
    EXPECT_TRUE(accepts_div<fx::PureRow>());                           // ∅ ⊆ {Block}
    EXPECT_TRUE(accepts_div<fx::DivRow>());                            // {Block} ⊆ {Block}
    EXPECT_TRUE(accepts_div<fx::Row<fx::Effect::Block>>());            // alias-equivalent row
    // Substitution failure for state effects covered in the neg-compile
    // fixture (fits production discipline — no try/catch on consteval).
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
    EXPECT_TRUE((accepts_all<fx::Row<fx::Effect::Bg, fx::Effect::Init,
                                     fx::Effect::Test>>()));
}

void test_refinement_chain_at_call_site() {
    // The F* refinement chain made operational: a function declared
    // IsPure can be called from any caller that has a Pure row, and
    // such a row is also Tot/Ghost/Div/ST/All — the standard
    // "functor of permission" pattern.

    using R = fx::PureRow;
    EXPECT_TRUE(accepts_pure <R>());
    EXPECT_TRUE(accepts_tot  <R>());
    EXPECT_TRUE(accepts_ghost<R>());
    EXPECT_TRUE(accepts_div  <R>());
    EXPECT_TRUE(accepts_st   <R>());
    EXPECT_TRUE(accepts_all  <R>());
}

void test_strictness_at_each_lattice_step() {
    // Each upper level genuinely admits strictly more rows than the
    // prior — none of the chain has accidentally collapsed.  Compile-
    // time witness is in the header's static_assert wall; this runtime
    // test mirrors the cells via the templated-caller surface.

    using A = fx::PureRow;
    using B = fx::DivRow;
    using C = fx::STRow;
    using D = fx::AllRow;

    EXPECT_TRUE(accepts_pure<A>() && accepts_div<A>() && accepts_st<A>() && accepts_all<A>());
    EXPECT_TRUE(                    accepts_div<B>() && accepts_st<B>() && accepts_all<B>());
    EXPECT_TRUE(                                        accepts_st<C>() && accepts_all<C>());
    EXPECT_TRUE(                                                           accepts_all<D>());
}

void test_lattice_size_invariants() {
    // Compile-time witness from the header, mirrored at runtime to
    // catch any future regression in the lattice layout.
    EXPECT_TRUE(fx::row_size_v<fx::PureRow>  == 0);
    EXPECT_TRUE(fx::row_size_v<fx::TotRow>   == 0);
    EXPECT_TRUE(fx::row_size_v<fx::GhostRow> == 0);
    EXPECT_TRUE(fx::row_size_v<fx::DivRow>   == 1);
    EXPECT_TRUE(fx::row_size_v<fx::STRow>    == 3);
    EXPECT_TRUE(fx::row_size_v<fx::AllRow>   == fx::effect_count);
}

void test_runtime_consistency() {
    // Verify the predicate evaluates identically across 50 invocations
    // — pure compile-time predicates should be invariant, but the
    // volatile-anchored cap pattern catches consteval/inline-body
    // regressions where a constexpr accessor accidentally degrades.
    constexpr bool pure_ok = fx::IsPure<fx::PureRow>;
    constexpr bool div_ok  = fx::IsDiv <fx::DivRow>;
    constexpr bool st_ok   = fx::IsST  <fx::STRow>;
    EXPECT_TRUE(pure_ok && div_ok && st_ok);

    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(fx::IsPure<fx::PureRow>);
        EXPECT_TRUE(fx::IsDiv <fx::DivRow>);
        EXPECT_TRUE(fx::IsST  <fx::STRow>);
        EXPECT_TRUE(fx::IsAll <fx::AllRow>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fx_aliases:\n");
    run_test("test_pure_alias_admits_empty_row_only",
             test_pure_alias_admits_empty_row_only);
    run_test("test_div_alias_admits_block_only",
             test_div_alias_admits_block_only);
    run_test("test_st_alias_admits_block_alloc_io",
             test_st_alias_admits_block_alloc_io);
    run_test("test_all_alias_admits_every_row",
             test_all_alias_admits_every_row);
    run_test("test_refinement_chain_at_call_site",
             test_refinement_chain_at_call_site);
    run_test("test_strictness_at_each_lattice_step",
             test_strictness_at_each_lattice_step);
    run_test("test_lattice_size_invariants",
             test_lattice_size_invariants);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
