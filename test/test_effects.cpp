// Compile-time tests for crucible::effects cap-tag + context types.
//
// Validates the post-Met(X) capability surface that replaces the legacy
// crucible/Effects.h fx::* tree.  Every claim is a type-system property —
// runtime behaviour is zero.  Tests are mostly static_asserts that verify
// cap::* tag types, Bg/Init/Test contexts, and the Met(X) substrate
// (Effect enum, IsEffect concept, Row<>, Subrow, Computation<>).

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>

#include "test_assert.h"

#include <cstdio>
#include <type_traits>

using namespace crucible::effects;

// Struct that holds no capability — approximates hot-path code.
struct HotPath {};

static void test_context_sizes_are_one_byte() {
    // [[no_unique_address]] collapses empty cap::* members; the whole
    // context is 1 byte regardless of how many member tokens.
    static_assert(sizeof(Bg)   == 1);
    static_assert(sizeof(Init) == 1);
    static_assert(sizeof(Test) == 1);

    // The cap::* tokens are themselves single-byte empty structs.
    static_assert(sizeof(cap::Alloc) == 1);
    static_assert(sizeof(cap::IO)    == 1);
    static_assert(sizeof(cap::Block) == 1);
    std::printf("  test_context_sizes:             PASSED\n");
}

static void test_top_level_aliases() {
    // The short form `effects::Alloc` (and IO, Block) is the canonical
    // production-call-site spelling — it MUST resolve to the cap::*
    // original so member access on Bg / Init / Test agrees.
    static_assert(std::is_same_v<Alloc, cap::Alloc>);
    static_assert(std::is_same_v<IO,    cap::IO>);
    static_assert(std::is_same_v<Block, cap::Block>);
    std::printf("  test_top_level_aliases:         PASSED\n");
}

static void test_cap_tag_layout_traits() {
    // Cap tokens are default-constructible empty structs — trivial in
    // every meaningful sense.  Bg / Init / Test default-construct
    // without throwing, so a context can be minted anywhere.
    static_assert(std::is_default_constructible_v<cap::Alloc>);
    static_assert(std::is_default_constructible_v<cap::IO>);
    static_assert(std::is_default_constructible_v<cap::Block>);
    static_assert(std::is_trivially_copyable_v<cap::Alloc>);
    static_assert(std::is_trivially_copyable_v<cap::IO>);
    static_assert(std::is_trivially_copyable_v<cap::Block>);
    static_assert(std::is_trivially_destructible_v<cap::Alloc>);
    static_assert(std::is_trivially_destructible_v<cap::IO>);
    static_assert(std::is_trivially_destructible_v<cap::Block>);

    // fixy-A3-005: Bg/Init/Test default ctors are PRIVATE — the only
    // construction path is via mint_*_context(passkey).  Tests/bench
    // code construct them via the friended TestWitness namespace.
    static_assert(!std::is_default_constructible_v<Bg>);
    static_assert(!std::is_default_constructible_v<Init>);
    static_assert(!std::is_default_constructible_v<Test>);
    // The minter factories themselves are nothrow.
    static_assert(noexcept(testing::bg()));
    static_assert(noexcept(testing::init()));
    static_assert(noexcept(testing::test()));
    std::printf("  test_cap_tag_layout_traits:     PASSED\n");
}

static void test_context_member_access() {
    // fixy-A3-005: mint via the testing path; the default ctors of
    // Bg/Init/Test are private and only the minters can call them.
    auto bg   = testing::bg();
    auto init = testing::init();
    auto test = testing::test();

    // Assigning to a cap-typed local is the canonical pattern callers
    // use to thread permission through a call chain.
    [[maybe_unused]] cap::Alloc a_bg   = bg.alloc;
    [[maybe_unused]] cap::IO    io_bg  = bg.io;
    [[maybe_unused]] cap::Block blk_bg = bg.block;

    [[maybe_unused]] cap::Alloc a_init  = init.alloc;
    [[maybe_unused]] cap::IO    io_init = init.io;

    [[maybe_unused]] cap::Alloc a_test   = test.alloc;
    [[maybe_unused]] cap::IO    io_test  = test.io;
    [[maybe_unused]] cap::Block blk_test = test.block;
    std::printf("  test_context_member_access:     PASSED\n");
}

// Helper that only compiles when called with a cap::Alloc value.  The
// tag type IS the parameter shape — there is nothing else to gate on.
[[nodiscard]] static int with_alloc(cap::Alloc /*tok*/) {
    return 42;
}

static void test_cap_param_propagation() {
    auto bg = testing::bg();
    int r = with_alloc(bg.alloc);
    assert(r == 42);
    // with_alloc(int{}) would fail to compile — the cap::Alloc parameter
    // type rejects implicit conversions from non-cap-typed values.
    std::printf("  test_cap_param_propagation:     PASSED\n");
}

static void test_metx_substrate_accessible() {
    // The Met(X) effect-row substrate is reachable via this header set.
    // EffectRow.h tests ARE the substrate's load-bearing self-test;
    // here we just confirm the public surface compiles when a caller
    // pulls Capabilities + Computation + EffectRow together.
    static_assert(IsEffect<Effect::Alloc>);
    static_assert(IsEffect<Effect::IO>);
    static_assert(IsEffect<Effect::Block>);
    static_assert(IsEffect<Effect::Bg>);
    static_assert(IsEffect<Effect::Init>);
    static_assert(IsEffect<Effect::Test>);

    using R = Row<Effect::Bg, Effect::IO>;
    static_assert(row_size_v<R> == 2);
    static_assert( Subrow<Row<Effect::Bg>, R>);
    static_assert(!Subrow<Row<Effect::Block>, R>);

    auto pure = Computation<Row<>, int>::mk(7);
    assert(pure.extract() == 7);
    std::printf("  test_metx_substrate:            PASSED\n");
}

// fixy-A5-039 HS14 fixture #2: runtime witness for the variadic
// row-membership lifts.  Concept-constrained function templates only
// instantiate when their requires-clause holds, so this test EITHER
// compiles AND prints PASSED, OR fails to compile when the gate is
// broken — there is no third outcome.  The point is the function-
// signature-level naming: a reviewer reading `requires CtxOwnsAnyOf
// <Ctx, Effect::Init, Effect::Bg>` sees the authorization shape at
// one glance; the verbose row_contains_v<row_type_of_t<Ctx>, ...>
// expansion needs four substitutions to parse.
template <class Ctx>
    requires CtxOwnsAnyOf<Ctx, Effect::Init, Effect::Bg>
[[nodiscard]] constexpr int needs_init_or_bg(Ctx const&) noexcept {
    return 42;
}

template <class Ctx>
    requires CtxOwnsAllOf<Ctx, Effect::Bg, Effect::Alloc>
[[nodiscard]] constexpr int needs_bg_and_alloc(Ctx const&) noexcept {
    return 99;
}

static void test_variadic_row_membership_lifts() {
    // POSITIVE path: a Ctx whose row contains a required atom calls
    // through the concept-constrained template successfully.  The
    // returned value is irrelevant — what we're proving is that the
    // overload resolves at all.
    BgDrainCtx  bg{};
    ColdInitCtx init{};
    assert(needs_init_or_bg(bg)   == 42);  // Bg ∈ Row<Bg, Alloc>
    assert(needs_init_or_bg(init) == 42);  // Init ∈ Row<Init, Alloc, IO>
    assert(needs_bg_and_alloc(bg) == 99);  // Bg ∧ Alloc both ∈ row

    // NEGATIVE path: assert the concept ITSELF evaluates to false at
    // compile time for Ctx whose row lacks the required atom(s).  This
    // is the SAME predicate the function template's requires-clause
    // consults — proving the concept rejects is sufficient evidence
    // the call site would also fail to instantiate.  (We avoid the
    // requires-expression-around-a-call form because GCC 16 treats an
    // unresolved constrained-template call as a hard error inside the
    // discarded requires-expression body, rather than as substitution
    // failure as the standard would suggest.)
    static_assert(CtxOwnsAnyOf<BgDrainCtx,  Effect::Init, Effect::Bg>);
    static_assert(CtxOwnsAnyOf<ColdInitCtx, Effect::Init, Effect::Bg>);
    static_assert(!CtxOwnsAnyOf<HotFgCtx,   Effect::Init, Effect::Bg>,
        "fixy-A5-039: HotFgCtx (empty row) must NOT satisfy "
        "CtxOwnsAnyOf<Ctx, Init, Bg> — concept gate must reject");

    static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::Alloc>);
    static_assert(!CtxOwnsAllOf<ColdInitCtx, Effect::Bg, Effect::Alloc>,
        "fixy-A5-039: ColdInitCtx::row = Row<Init, Alloc, IO> — "
        "conjunctive lift must REJECT because Bg is not in the row, "
        "even though Alloc is");
    static_assert(!CtxOwnsAllOf<HotFgCtx, Effect::Bg>,
        "fixy-A5-039: empty-row Ctx must fail the conjunctive gate "
        "for any non-empty atom pack");

    // Cross-axis identity: empty-pack edge cases.
    static_assert(!CtxOwnsAnyOf<BgDrainCtx>);  // OR over {} is false
    static_assert( CtxOwnsAllOf<BgDrainCtx>);  // AND over {} is true

    std::printf("  test_variadic_row_membership_lifts: PASSED\n");
}

int main() {
    test_context_sizes_are_one_byte();
    test_top_level_aliases();
    test_cap_tag_layout_traits();
    test_context_member_access();
    test_cap_param_propagation();
    test_metx_substrate_accessible();
    test_variadic_row_membership_lifts();
    std::printf("test_effects: 7 groups, all passed\n");
    return 0;
}
