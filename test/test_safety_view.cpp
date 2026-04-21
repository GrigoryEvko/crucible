// Positive tests for safety::ScopedView.
//
// Exercises every supported usage:
//   - Construction via mint_view at a state-transition point
//   - Use via operator-> and carrier()
//   - Copy-construction (multi-borrow)
//   - Storage in std::optional via emplace+copy
//   - Tier 2 audit passes for compliant types
//
// The negative-compile cases live in test/safety_neg/*.cpp and are
// driven by the safety_view_neg CMake target.

#include <crucible/safety/ScopedView.h>

#include <cassert>
#include <cstdio>
#include <optional>
#include <string>

using crucible::safety::ScopedView;
using crucible::safety::LinearScopedView;
using crucible::safety::mint_view;
using crucible::safety::mint_linear_view;
using crucible::safety::contains_scoped_view;
using crucible::safety::no_scoped_view_field_check;
using crucible::safety::is_scoped_view_v;

// ── Test carrier ────────────────────────────────────────────────────

struct DummyCarrier {
    int    value      = 42;
    bool   active     = false;
    int    transitions = 0;
};

namespace dummy_state {
    struct Inactive {};
    struct Active   {};
}

// view_ok overloads (found via ADL on DummyCarrier — defined in same
// namespace as the carrier).
constexpr bool view_ok(DummyCarrier const& c, std::type_identity<dummy_state::Inactive>) noexcept {
    return !c.active;
}
constexpr bool view_ok(DummyCarrier const& c, std::type_identity<dummy_state::Active>) noexcept {
    return c.active;
}

using InactiveView = ScopedView<DummyCarrier, dummy_state::Inactive>;
using ActiveView   = ScopedView<DummyCarrier, dummy_state::Active>;

// ── Tier 2 audit positive cases ─────────────────────────────────────

// A struct that does NOT contain a ScopedView passes the audit.
struct CleanStruct {
    int       a = 0;
    DummyCarrier carrier{};
    std::string name;
};
static_assert(no_scoped_view_field_check<CleanStruct>(),
              "CleanStruct has no ScopedView fields — audit must pass");

// A nested clean struct also passes.
struct OuterClean {
    CleanStruct inner;
    int         x = 0;
};
static_assert(no_scoped_view_field_check<OuterClean>(),
              "OuterClean nested clean — audit must pass");

// Trait sanity.
static_assert(is_scoped_view_v<ActiveView>);
static_assert(!is_scoped_view_v<int>);
static_assert(!is_scoped_view_v<DummyCarrier>);

// contains_scoped_view positive direct + indirect.
static_assert(contains_scoped_view<ActiveView>());
static_assert(contains_scoped_view<std::optional<ActiveView>>());
static_assert(contains_scoped_view<std::vector<ActiveView>>());
static_assert(!contains_scoped_view<int>());
static_assert(!contains_scoped_view<DummyCarrier>());
static_assert(!contains_scoped_view<CleanStruct>());

// ── Functional tests ────────────────────────────────────────────────

static void test_construct_via_mint() {
    DummyCarrier c{};
    c.active = true;
    auto view = mint_view<dummy_state::Active>(c);
    assert(view->value == 42);
    assert(&view.carrier() == &c);
    std::printf("  construct_via_mint:           PASSED\n");
}

static void test_copy_construct_multi_borrow() {
    DummyCarrier c{};
    c.active = true;
    auto v1 = mint_view<dummy_state::Active>(c);
    auto v2 = v1;  // copy ctor
    auto v3 = v1;
    assert(v1->value == 42);
    assert(v2->value == 42);
    assert(v3->value == 42);
    std::printf("  copy_construct_multi_borrow:  PASSED\n");
}

static void use_active(ActiveView const& v) {
    assert(v->value == 42);
}

static void test_pass_to_callee() {
    DummyCarrier c{};
    c.active = true;
    use_active(mint_view<dummy_state::Active>(c));
    std::printf("  pass_to_callee:               PASSED\n");
}

static void test_optional_storage_via_emplace() {
    DummyCarrier c{};
    c.active = true;
    std::optional<ActiveView> opt;
    assert(!opt.has_value());

    // Direct assignment (opt = mint_view(...)) does NOT compile —
    // ScopedView's deleted operator= propagates to optional.  The
    // supported path is emplace, which constructs via the public
    // move ctor from a freshly-minted view.
    opt.emplace(mint_view<dummy_state::Active>(c));
    assert(opt.has_value());
    assert((*opt)->value == 42);

    opt.reset();
    assert(!opt.has_value());
    std::printf("  optional_storage_via_emplace: PASSED\n");
}

static void test_state_transition_remints() {
    DummyCarrier c{};
    // Start inactive
    {
        auto v = mint_view<dummy_state::Inactive>(c);
        assert(&v.carrier() == &c);
    }  // view goes out of scope

    // Transition to active
    c.active = true;
    c.transitions++;

    {
        auto v = mint_view<dummy_state::Active>(c);
        assert(v->value == 42);
    }
    std::printf("  state_transition_remints:     PASSED\n");
}

// ── LinearScopedView = Linear<ScopedView<C, T>> nested composition ──

// Layout — Linear wraps a const pointer, so LinearScopedView is a
// single pointer too.  This is the key zero-cost guarantee: the
// composition adds no storage beyond what ScopedView already needs.
static_assert(sizeof(LinearScopedView<DummyCarrier, dummy_state::Active>) == sizeof(void*),
              "LinearScopedView<C, T> must remain single-pointer-sized");

// Trait: the reflection audit must see through Linear's single `value_`
// member and detect the nested ScopedView.  This is what lets the
// Tier 2 field-storage check continue to work on composed views.
static_assert(contains_scoped_view<LinearScopedView<DummyCarrier, dummy_state::Active>>(),
              "Tier 2 audit must see through Linear<ScopedView<...>>");

// Copy is deleted by Linear<T>'s rule; assignment is deleted by
// ScopedView's rule propagating through Linear's defaulted move-assign.
// Move-construction remains available (needed by factories).
static_assert(!std::is_copy_constructible_v<
    LinearScopedView<DummyCarrier, dummy_state::Active>>,
    "LinearScopedView must not be copy-constructible (Linear deletes)");
static_assert(!std::is_copy_assignable_v<
    LinearScopedView<DummyCarrier, dummy_state::Active>>,
    "LinearScopedView must not be copy-assignable");
static_assert(!std::is_move_assignable_v<
    LinearScopedView<DummyCarrier, dummy_state::Active>>,
    "LinearScopedView must not be move-assignable "
    "(ScopedView's deleted op= propagates through Linear)");
static_assert(std::is_move_constructible_v<
    LinearScopedView<DummyCarrier, dummy_state::Active>>,
    "LinearScopedView must remain move-constructible (factories rely on this)");

static void test_linear_view_mint_and_consume() {
    DummyCarrier c{};
    c.active = true;

    // Mint via factory — fires the same view_ok precondition as mint_view.
    auto token = mint_linear_view<dummy_state::Active>(c);

    // Borrow without consuming: Linear<T>::peek().  Returns the inner
    // ScopedView; we can then deref it.  The token is still live.
    assert(token.peek()->value == 42);
    assert(&token.peek().carrier() == &c);

    // Consume — produces the inner ScopedView, Linear is gone.
    auto inner = std::move(token).consume();
    assert(inner->value == 42);
    assert(&inner.carrier() == &c);
    std::printf("  linear_view_mint_consume:     PASSED\n");
}

static void transition_consume(
    LinearScopedView<DummyCarrier, dummy_state::Active>&& tok) noexcept
{
    // A transition function must consume the token.  This models the
    // pattern where a one-shot method (e.g. detach, seal, finalize)
    // is only callable while the caller holds the linear token.
    auto inner = std::move(tok).consume();
    assert(inner->value == 42);
}

static void test_linear_view_transition_pattern() {
    DummyCarrier c{};
    c.active = true;
    auto token = mint_linear_view<dummy_state::Active>(c);
    transition_consume(std::move(token));
    // token is moved-from; any use is caught by -Werror=use-after-move.
    std::printf("  linear_view_transition:       PASSED\n");
}

int main() {
    std::printf("test_safety_view (Tier 1 + Tier 2)\n");
    test_construct_via_mint();
    test_copy_construct_multi_borrow();
    test_pass_to_callee();
    test_optional_storage_via_emplace();
    test_state_transition_remints();
    test_linear_view_mint_and_consume();
    test_linear_view_transition_pattern();
    std::printf("\nAll positive ScopedView tests passed.\n");
    return 0;
}
