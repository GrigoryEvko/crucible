#include <crucible/safety/ScopedView.h>

#include "test_assert.h"
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

struct DummyCarrier {
    int value = 42;
    bool active = false;
    int transitions = 0;
};

namespace dummy_state {
struct Inactive {};
struct Active {};
}  // namespace dummy_state

// Found by argument-dependent lookup, so these must live in the same
// namespace as the carrier they take.
constexpr bool view_ok(DummyCarrier const& c, std::type_identity<dummy_state::Inactive>) noexcept { return !c.active; }
constexpr bool view_ok(DummyCarrier const& c, std::type_identity<dummy_state::Active>) noexcept { return c.active; }

using InactiveView = ScopedView<DummyCarrier, dummy_state::Inactive>;
using ActiveView = ScopedView<DummyCarrier, dummy_state::Active>;

struct CleanStruct {
    int a = 0;
    DummyCarrier carrier{};
    std::string name;
};
static_assert(no_scoped_view_field_check<CleanStruct>(), "CleanStruct has no ScopedView fields — audit must pass");

struct OuterClean {
    CleanStruct inner;
    int x = 0;
};
static_assert(no_scoped_view_field_check<OuterClean>(), "OuterClean nested clean — audit must pass");

static_assert(is_scoped_view_v<ActiveView>);
static_assert(!is_scoped_view_v<int>);
static_assert(!is_scoped_view_v<DummyCarrier>);

static_assert(contains_scoped_view<ActiveView>());
static_assert(contains_scoped_view<std::optional<ActiveView>>());
static_assert(contains_scoped_view<std::vector<ActiveView>>());
static_assert(!contains_scoped_view<int>());
static_assert(!contains_scoped_view<DummyCarrier>());
static_assert(!contains_scoped_view<CleanStruct>());

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
    auto v2 = v1;
    auto v3 = v1;
    assert(v1->value == 42);
    assert(v2->value == 42);
    assert(v3->value == 42);
    std::printf("  copy_construct_multi_borrow:  PASSED\n");
}

static void use_active(ActiveView const& v) { assert(v->value == 42); }

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

    // Assigning into the optional does not compile: the deleted assignment
    // operator propagates through it.  Emplacement constructs instead.
    opt.emplace(mint_view<dummy_state::Active>(c));
    assert(opt.has_value());
    assert((*opt)->value == 42);

    opt.reset();
    assert(!opt.has_value());
    std::printf("  optional_storage_via_emplace: PASSED\n");
}

static void test_state_transition_remints() {
    DummyCarrier c{};
    {
        auto v = mint_view<dummy_state::Inactive>(c);
        assert(&v.carrier() == &c);
    }

    c.active = true;
    c.transitions++;

    {
        auto v = mint_view<dummy_state::Active>(c);
        assert(v->value == 42);
    }
    std::printf("  state_transition_remints:     PASSED\n");
}

// The linear wrapper holds one const pointer, so the composition adds no
// storage at all over the view it wraps.
static_assert(sizeof(LinearScopedView<DummyCarrier, dummy_state::Active>) == sizeof(void*),
              "LinearScopedView<C, T> must remain single-pointer-sized");

// The reflective audit has to see through the wrapper's single member to
// the view inside, or the field-storage check stops working the moment a
// view is composed.
static_assert(contains_scoped_view<LinearScopedView<DummyCarrier, dummy_state::Active>>(),
              "the audit must see through Linear<ScopedView<...>>");

static_assert(!std::is_copy_constructible_v<LinearScopedView<DummyCarrier, dummy_state::Active>>,
              "LinearScopedView must not be copy-constructible (Linear deletes)");
static_assert(!std::is_copy_assignable_v<LinearScopedView<DummyCarrier, dummy_state::Active>>,
              "LinearScopedView must not be copy-assignable");
static_assert(!std::is_move_assignable_v<LinearScopedView<DummyCarrier, dummy_state::Active>>,
              "LinearScopedView must not be move-assignable "
              "(ScopedView's deleted op= propagates through Linear)");
static_assert(std::is_move_constructible_v<LinearScopedView<DummyCarrier, dummy_state::Active>>,
              "LinearScopedView must remain move-constructible (factories rely on this)");

static void test_linear_view_mint_and_consume() {
    DummyCarrier c{};
    c.active = true;

    // The factory checks the same view_ok predicate as the plain mint.
    auto token = mint_linear_view<dummy_state::Active>(c);

    assert(token.peek()->value == 42);
    assert(&token.peek().carrier() == &c);

    auto inner = std::move(token).consume();
    assert(inner->value == 42);
    assert(&inner.carrier() == &c);
    std::printf("  linear_view_mint_consume:     PASSED\n");
}

static void transition_consume(LinearScopedView<DummyCarrier, dummy_state::Active>&& tok) noexcept {
    // Consuming the token is the point: this models a one-shot operation
    // that is callable only while the caller still holds it.
    auto inner = std::move(tok).consume();
    assert(inner->value == 42);
}

static void test_linear_view_transition_pattern() {
    DummyCarrier c{};
    c.active = true;
    auto token = mint_linear_view<dummy_state::Active>(c);
    transition_consume(std::move(token));
    // The token is moved from.  Any use here trips -Werror=use-after-move.
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
