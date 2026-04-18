// Compile-time tests for fx::* capability tokens.
//
// Every axiom-enforcement claim in Effects.h is a type-system assertion.
// Runtime behaviour is zero.  Tests are mostly static_asserts that check
// CanAlloc / CanIO / CanBlock / Pure concepts on every context.

#include <crucible/Effects.h>

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace crucible::fx;

// Struct that can't create any token — approximates hot-path code.
struct HotPath {};

static void test_context_sizes_are_one_byte() {
    // [[no_unique_address]] collapses empty members; the whole context
    // is 1 byte regardless of how many token members.
    static_assert(sizeof(Bg)   == 1);
    static_assert(sizeof(Init) == 1);
    static_assert(sizeof(Test) == 1);
    std::printf("  test_context_sizes:             PASSED\n");
}

static void test_concept_satisfaction() {
    // Bg has all three capabilities.
    static_assert(CanAlloc<Bg>);
    static_assert(CanIO<Bg>);
    static_assert(CanBlock<Bg>);
    static_assert(!Pure<Bg>);

    // Init can alloc + IO but cannot block.
    static_assert(CanAlloc<Init>);
    static_assert(CanIO<Init>);
    static_assert(!CanBlock<Init>);
    static_assert(!Pure<Init>);

    // Test is unrestricted.
    static_assert(CanAlloc<Test>);
    static_assert(CanIO<Test>);
    static_assert(CanBlock<Test>);
    static_assert(!Pure<Test>);

    // Non-context types: no capabilities, Pure.
    static_assert(!CanAlloc<int>);
    static_assert(!CanIO<int>);
    static_assert(!CanBlock<int>);
    static_assert(Pure<int>);

    static_assert(!CanAlloc<HotPath>);
    static_assert(Pure<HotPath>);
    std::printf("  test_concepts:                  PASSED\n");
}

static void test_token_default_ctors_are_private() {
    // Direct construction of Alloc/IO/Block from outside a friend
    // context must be rejected.  We verify the type trait: no default
    // constructor is accessible.
    //
    // (A code-path attempt would be a compile error; we express this
    // as a negative static_assert so a regression is caught here.)
    static_assert(!std::is_default_constructible_v<Alloc>);
    static_assert(!std::is_default_constructible_v<IO>);
    static_assert(!std::is_default_constructible_v<Block>);

    // But they ARE copyable — once a context creates one, it flows.
    static_assert(std::is_copy_constructible_v<Alloc>);
    static_assert(std::is_copy_constructible_v<IO>);
    static_assert(std::is_copy_constructible_v<Block>);
    std::printf("  test_private_ctors:             PASSED\n");
}

// Helper that only compiles with a CanAlloc context.  Existence proves
// the concept works as a template constraint.
template <CanAlloc Ctx>
[[nodiscard]] int with_alloc(Ctx /*ctx*/) {
    return 42;
}

static void test_template_constraint() {
    Bg bg;
    [[maybe_unused]] auto r = with_alloc(bg);
    assert(r == 42);
    // with_alloc(int{}) would fail to compile — the constraint rejects it.
    std::printf("  test_template_constraint:       PASSED\n");
}

int main() {
    test_context_sizes_are_one_byte();
    test_concept_satisfaction();
    test_token_default_ctors_are_private();
    test_template_constraint();
    std::printf("test_effects: 4 groups, all passed\n");
    return 0;
}
