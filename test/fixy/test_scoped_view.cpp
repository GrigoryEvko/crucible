// Ported from test/test_safety_view.cpp.  The escape audit's container
// coverage, which the old test spelled as one static_assert per shape,
// is one roster walked by reflection: every shape in it must be seen
// through, and every shape in the clean roster must audit clean.

#include <fixy/ScopedView.h>

#include <array>
#include <cstdio>
#include <deque>
#include <expected>
#include <inplace_vector>
#include <list>
#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using fixy::contains_scoped_view;
using fixy::is_scoped_view_v;
using fixy::LinearScopedView;
using fixy::mint_linear_view;
using fixy::mint_view;
using fixy::no_scoped_view_field_check;
using fixy::ScopedView;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

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

}  // namespace

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

// A carrier that keeps its members private audits the same as one that
// does not: the walk reads private members.  The escaping form is
// test/fixy/neg/neg_scoped_view_escapes_through_private_member.cpp.
class PrivateClean {
    CleanStruct inner_;
    std::optional<int> slot_;

public:
    [[nodiscard]] int a() const noexcept { return inner_.a; }
    [[nodiscard]] bool has_slot() const noexcept { return slot_.has_value(); }
};
static_assert(no_scoped_view_field_check<PrivateClean>());

// The associated-type rule can point back at an enclosing class.  The
// walk must terminate, and answer for the view the class does hold.
struct SelfReferentialClean {
    std::vector<SelfReferentialClean> children;
    std::unique_ptr<SelfReferentialClean> next;
    int payload = 0;
};
struct SelfReferentialEscape {
    std::vector<SelfReferentialEscape> children;
    ActiveView leak;
};
static_assert(!contains_scoped_view<SelfReferentialClean>());
static_assert(contains_scoped_view<SelfReferentialEscape>());

static_assert(is_scoped_view_v<ActiveView>);
static_assert(!is_scoped_view_v<int>);
static_assert(!is_scoped_view_v<DummyCarrier>);

// Every shape a view can hide in, and every shape it cannot.  A shape
// added to either roster is audited by the walk below without a new
// assertion.
namespace roster {

struct Base {
    ActiveView held;
};
struct Derived : Base {
    int extra = 0;
};
union Storage {
    int as_int;
    ActiveView as_view;
};
struct PrivateHolder {
private:
    ActiveView held_;
};

using escapes =
    std::tuple<ActiveView, std::optional<ActiveView>, std::vector<ActiveView>, std::array<ActiveView, 3>,
               std::unique_ptr<ActiveView>, std::shared_ptr<ActiveView>, std::weak_ptr<ActiveView>, ActiveView[4],
               std::pair<int, ActiveView>, std::tuple<int, double, ActiveView>, std::variant<int, ActiveView>,
               LinearScopedView<DummyCarrier, dummy_state::Active>, std::deque<ActiveView>, std::list<ActiveView>,
               std::span<ActiveView>, std::inplace_vector<ActiveView, 4>, std::expected<ActiveView, int>,
               std::map<int, ActiveView>, std::optional<std::vector<std::array<ActiveView, 2>>>, Derived, Storage,
               PrivateHolder, ActiveView const, ActiveView&>;

using clean =
    std::tuple<int, DummyCarrier, CleanStruct, OuterClean, PrivateClean, std::optional<int>, std::vector<std::string>,
               std::variant<int, std::string>, std::tuple<int, std::vector<int>>, std::unique_ptr<DummyCarrier>,
               std::array<CleanStruct, 2>, int[3], ActiveView*, std::map<int, std::string>, SelfReferentialClean>;

template <typename Roster, bool Expected>
consteval bool every_shape_answers() {
    static constexpr auto shapes = std::define_static_array(std::meta::template_arguments_of(^^Roster));
    bool all = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto shape : shapes) {
        if (contains_scoped_view<typename[:shape:]>() != Expected) all = false;
    }
#pragma GCC diagnostic pop
    return all;
}

static_assert(every_shape_answers<escapes, true>(), "a shape in the escape roster audited clean");
static_assert(every_shape_answers<clean, false>(), "a shape in the clean roster audited as an escape");

}  // namespace roster

static void test_construct_via_mint() {
    DummyCarrier c{};
    c.active = true;
    auto view = mint_view<dummy_state::Active>(c);
    CRUCIBLE_TEST_REQUIRE(view->value == 42);
    CRUCIBLE_TEST_REQUIRE(&view.carrier() == &c);
}

static void test_copy_construct_multi_borrow() {
    DummyCarrier c{};
    c.active = true;
    auto v1 = mint_view<dummy_state::Active>(c);
    auto v2 = v1;
    auto v3 = v1;
    CRUCIBLE_TEST_REQUIRE(v1->value == 42);
    CRUCIBLE_TEST_REQUIRE(v2->value == 42);
    CRUCIBLE_TEST_REQUIRE(v3->value == 42);
}

static void use_active(ActiveView const& v) { CRUCIBLE_TEST_REQUIRE(v->value == 42); }

static void test_pass_to_callee() {
    DummyCarrier c{};
    c.active = true;
    use_active(mint_view<dummy_state::Active>(c));
}

static void test_optional_storage_via_emplace() {
    DummyCarrier c{};
    c.active = true;
    std::optional<ActiveView> opt;
    CRUCIBLE_TEST_REQUIRE(!opt.has_value());

    // Assigning into the optional does not compile: the deleted assignment
    // operator propagates through it.  Emplacement constructs instead.
    opt.emplace(mint_view<dummy_state::Active>(c));
    CRUCIBLE_TEST_REQUIRE(opt.has_value());
    CRUCIBLE_TEST_REQUIRE((*opt)->value == 42);

    opt.reset();
    CRUCIBLE_TEST_REQUIRE(!opt.has_value());
}

static void test_state_transition_remints() {
    DummyCarrier c{};
    {
        auto v = mint_view<dummy_state::Inactive>(c);
        CRUCIBLE_TEST_REQUIRE(&v.carrier() == &c);
    }

    c.active = true;
    c.transitions++;

    {
        auto v = mint_view<dummy_state::Active>(c);
        CRUCIBLE_TEST_REQUIRE(v->value == 42);
    }
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

    CRUCIBLE_TEST_REQUIRE(token.peek()->value == 42);
    CRUCIBLE_TEST_REQUIRE(&token.peek().carrier() == &c);

    auto inner = std::move(token).consume();
    CRUCIBLE_TEST_REQUIRE(inner->value == 42);
    CRUCIBLE_TEST_REQUIRE(&inner.carrier() == &c);
}

// The callee names the token's brand rather than the erased spelling:
// a linear token cannot be erased by conversion, because a conversion
// would copy what must be consumed.
template <typename Brand>
static void transition_consume(LinearScopedView<DummyCarrier, dummy_state::Active, Brand>&& tok) noexcept {
    // Consuming the token is the point: this models a one-shot operation
    // that is callable only while the caller still holds it.
    auto inner = std::move(tok).consume();
    if (inner->value != 42) std::abort();
}

static void test_linear_view_transition_pattern() {
    DummyCarrier c{};
    c.active = true;
    auto token = mint_linear_view<dummy_state::Active>(c);
    transition_consume(std::move(token));
    // The token is moved from.  Any use here trips -Werror=use-after-move.
}

int main() {
    std::fprintf(stderr, "test_scoped_view:\n");
    run_test("construct_via_mint", test_construct_via_mint);
    run_test("copy_construct_multi_borrow", test_copy_construct_multi_borrow);
    run_test("pass_to_callee", test_pass_to_callee);
    run_test("optional_storage_via_emplace", test_optional_storage_via_emplace);
    run_test("state_transition_remints", test_state_transition_remints);
    run_test("linear_view_mint_consume", test_linear_view_mint_and_consume);
    run_test("linear_view_transition", test_linear_view_transition_pattern);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
