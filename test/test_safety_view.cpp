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
using crucible::safety::mint_view;
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

int main() {
    std::printf("test_safety_view (Tier 1 + Tier 2)\n");
    test_construct_via_mint();
    test_copy_construct_multi_borrow();
    test_pass_to_callee();
    test_optional_storage_via_emplace();
    test_state_transition_remints();
    std::printf("\nAll positive ScopedView tests passed.\n");
    return 0;
}
