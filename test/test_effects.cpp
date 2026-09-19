// Every claim here is a property of the type system, so the assertions
// are mostly static.  The capability surface has no runtime behaviour to
// observe.

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_Computation.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>

#include "test_assert.h"

#include <cstdio>
#include <type_traits>

using namespace crucible::effects;

// Struct that holds no capability — approximates hot-path code.
struct HotPath {};

static void test_context_sizes_are_one_byte() {
    // [[no_unique_address]] collapses the empty member tokens, so a
    // context stays one byte however many of them it carries.
    static_assert(sizeof(Bg) == 1);
    static_assert(sizeof(Init) == 1);
    static_assert(sizeof(Test) == 1);

    static_assert(sizeof(cap::Alloc) == 1);
    static_assert(sizeof(cap::IO) == 1);
    static_assert(sizeof(cap::Block) == 1);
    std::printf("  test_context_sizes:             PASSED\n");
}

static void test_top_level_aliases() {
    // The short form is the spelling production call sites use, so it
    // must resolve to the original for member access on a context to
    // agree with it.
    static_assert(std::is_same_v<Alloc, cap::Alloc>);
    static_assert(std::is_same_v<IO, cap::IO>);
    static_assert(std::is_same_v<Block, cap::Block>);
    std::printf("  test_top_level_aliases:         PASSED\n");
}

static void test_cap_tag_layout_traits() {
    static_assert(std::is_default_constructible_v<cap::Alloc>);
    static_assert(std::is_default_constructible_v<cap::IO>);
    static_assert(std::is_default_constructible_v<cap::Block>);
    static_assert(std::is_trivially_copyable_v<cap::Alloc>);
    static_assert(std::is_trivially_copyable_v<cap::IO>);
    static_assert(std::is_trivially_copyable_v<cap::Block>);
    static_assert(std::is_trivially_destructible_v<cap::Alloc>);
    static_assert(std::is_trivially_destructible_v<cap::IO>);
    static_assert(std::is_trivially_destructible_v<cap::Block>);

    // A context has a private default constructor, so the only way to
    // build one is through a minter holding the passkey.  Test and bench
    // code goes through the friended testing namespace below.
    static_assert(!std::is_default_constructible_v<Bg>);
    static_assert(!std::is_default_constructible_v<Init>);
    static_assert(!std::is_default_constructible_v<Test>);
    static_assert(noexcept(testing::bg()));
    static_assert(noexcept(testing::init()));
    static_assert(noexcept(testing::test()));
    std::printf("  test_cap_tag_layout_traits:     PASSED\n");
}

static void test_context_member_access() {
    auto bg = testing::bg();
    auto init = testing::init();
    auto test = testing::test();

    // Assigning to a cap-typed local is how a caller threads permission
    // down a call chain.
    [[maybe_unused]] cap::Alloc a_bg = bg.alloc;
    [[maybe_unused]] cap::IO io_bg = bg.io;
    [[maybe_unused]] cap::Block blk_bg = bg.block;

    [[maybe_unused]] cap::Alloc a_init = init.alloc;
    [[maybe_unused]] cap::IO io_init = init.io;

    [[maybe_unused]] cap::Alloc a_test = test.alloc;
    [[maybe_unused]] cap::IO io_test = test.io;
    [[maybe_unused]] cap::Block blk_test = test.block;
    std::printf("  test_context_member_access:     PASSED\n");
}

// The tag type is the entire gate.  There is no other parameter to
// check, and nothing else admits a call.
[[nodiscard]] static int with_alloc(cap::Alloc /*tok*/) { return 42; }

static void test_cap_param_propagation() {
    auto bg = testing::bg();
    int r = with_alloc(bg.alloc);
    assert(r == 42);
    // Calling with_alloc(int{}) does not compile, because the parameter
    // type admits no implicit conversion from an untagged value.
    std::printf("  test_cap_param_propagation:     PASSED\n");
}

static void test_metx_substrate_accessible() {
    // The row substrate has its own test.  The claim here is narrower:
    // that its public surface compiles when a caller pulls the
    // capability, computation and row headers together.
    static_assert(IsEffect<Effect::Alloc>);
    static_assert(IsEffect<Effect::IO>);
    static_assert(IsEffect<Effect::Block>);
    static_assert(IsEffect<Effect::Bg>);
    static_assert(IsEffect<Effect::Init>);
    static_assert(IsEffect<Effect::Test>);

    using R = Row<Effect::Bg, Effect::IO>;
    static_assert(row_size_v<R> == 2);
    static_assert(Subrow<Row<Effect::Bg>, R>);
    static_assert(!Subrow<Row<Effect::Block>, R>);

    auto pure = Computation<Row<>, int>::mk(7);
    assert(pure.extract() == 7);
    std::printf("  test_metx_substrate:            PASSED\n");
}

// A concept-constrained template instantiates only when its clause
// holds, so this either compiles and passes or fails to build.  There is
// no third outcome.  The named lifts earn their place at the signature:
// a reviewer reads the authorization shape in one line, where the
// expanded row-membership form takes four substitutions to parse.
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
    // The returned values carry no meaning.  What the calls prove is
    // that the overloads resolve at all.
    BgDrainCtx bg{};
    ColdInitCtx init{};
    assert(needs_init_or_bg(bg) == 42);  // Bg ∈ Row<Bg, Alloc>
    assert(needs_init_or_bg(init) == 42);  // Init ∈ Row<Init, Alloc, IO>
    assert(needs_bg_and_alloc(bg) == 99);  // Bg ∧ Alloc both ∈ row

    // The rejection side asserts on the concept rather than on a call.
    // It is the same predicate the requires-clause consults, so a
    // concept that rejects is enough to show the call site would not
    // instantiate.  Wrapping a call in a requires-expression is the
    // alternative, and GCC 16 treats an unresolved constrained call
    // inside one as a hard error instead of a substitution failure.
    static_assert(CtxOwnsAnyOf<BgDrainCtx, Effect::Init, Effect::Bg>);
    static_assert(CtxOwnsAnyOf<ColdInitCtx, Effect::Init, Effect::Bg>);
    static_assert(!CtxOwnsAnyOf<HotFgCtx, Effect::Init, Effect::Bg>, "HotFgCtx has an empty row and must not satisfy "
                                                                     "CtxOwnsAnyOf<Ctx, Init, Bg>");

    static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::Alloc>);
    static_assert(!CtxOwnsAllOf<ColdInitCtx, Effect::Bg, Effect::Alloc>,
                  "ColdInitCtx has row Row<Init, Alloc, IO>, so the conjunctive "
                  "lift must reject: Bg is absent even though Alloc is present");
    static_assert(!CtxOwnsAllOf<HotFgCtx, Effect::Bg>, "A Ctx with an empty row must fail the conjunctive gate for any "
                                                       "non-empty atom pack");

    static_assert(!CtxOwnsAnyOf<BgDrainCtx>);  // OR over {} is false
    static_assert(CtxOwnsAllOf<BgDrainCtx>);  // AND over {} is true

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
