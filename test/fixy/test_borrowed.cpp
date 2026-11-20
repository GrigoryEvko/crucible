// Sentinel TU for fixy/Borrowed.h: the view costs one span, the owner tag
// keeps two borrows apart, a borrow of a temporary selects a deleted
// constructor, the detection surface answers through the one reflection
// query, and every accessor of the view answers on a value.

#include <fixy/Borrowed.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <meta>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ::fixy::Borrowed;
using ::fixy::check::expect_dies;

struct OwnerA {
    int dummy = 0;
};
struct OwnerB {
    int dummy = 0;
};
struct RefBrand {};

using B_A = Borrowed<int, OwnerA>;
using B_B = Borrowed<int, OwnerB>;

// One span, nothing more.
static_assert(sizeof(B_A) == sizeof(std::span<int>));

// A borrow of a temporary selects the deleted twin.  The lvalue forms
// stay open, so the twins remove exactly the dangling shape.
static_assert(!std::is_constructible_v<Borrowed<int const, OwnerA>, int const (&&)[2]>);
static_assert(std::is_constructible_v<Borrowed<int const, OwnerA>, int const (&)[2]>);

// The detection surface, walked over a roster of qualifications rather
// than one static_assert per spelling.  Every entry of the roster must
// answer the same as the bare type.
template <typename T>
using qualified_forms = std::tuple<T, T const, T volatile, T&, T const&, T&&, T const&&>;

template <typename Roster, auto Predicate>
consteval bool all_forms_satisfy() {
    static constexpr auto forms = std::define_static_array(std::meta::template_arguments_of(^^Roster));
    bool all = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto form : forms) {
        if (!Predicate.template operator()<typename[:form:]>()) all = false;
    }
#pragma GCC diagnostic pop
    return all;
}

constexpr auto is_borrowed = []<typename T>() { return ::fixy::is_borrowed_v<T>; };
constexpr auto is_none = []<typename T>() { return !::fixy::is_borrowed_v<T>; };

static_assert(all_forms_satisfy<qualified_forms<B_A>, is_borrowed>());
static_assert(all_forms_satisfy<qualified_forms<Borrowed<char const, OwnerB>>, is_borrowed>());

// The carriers underneath the wrapper, and a Ref, all answer no.
static_assert(all_forms_satisfy<qualified_forms<std::span<int>>, is_none>());
static_assert(all_forms_satisfy<qualified_forms<int*>, is_none>());
static_assert(all_forms_satisfy<qualified_forms<int>, is_none>());
static_assert(all_forms_satisfy<qualified_forms<::fixy::Ref<int, RefBrand>>, is_none>());
static_assert(!::fixy::is_borrowed_v<void>);

static_assert(std::is_same_v<::fixy::borrowed_value_t<B_A const&>, int>);
static_assert(std::is_same_v<::fixy::borrowed_source_t<B_B&&>, OwnerB>);

// Two owners are two types.
static_assert(!std::is_assignable_v<B_A&, B_B>);
static_assert(!std::is_convertible_v<B_A, B_B>);

int check_views_over_a_vector() {
    std::vector<int> owner{1, 2, 3, 4, 5};

    B_A whole{owner.data(), owner.size()};
    if (whole.size() != 5 || whole.front() != 1 || whole.back() != 5) return 10;

    B_A tail = whole.subview(2, 3);
    if (tail.size() != 3 || tail[0] != 3) return 11;
    if (tail == whole) return 12;

    owner[2] = 30;
    if (whole[2] != 30) return 13;

    // Each accessor checks its index or its window in each build, the release
    // build too, so a read past the borrow ends the process.  The bounds check
    // of the standard library is on only in Debug.  Each read runs in a child
    // through expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    auto const nothing = whole.subview(5, 0);
    expect_dies(
        test, [&whole] noexcept { (void)whole[whole.size()]; },
        ::fixy::TextView{"foundation: contract violation: i < size()"});
    expect_dies(
        test, [&nothing] noexcept { (void)nothing.front(); },
        ::fixy::TextView{"foundation: contract violation: !empty()"});
    expect_dies(
        test, [&nothing] noexcept { (void)nothing.back(); },
        ::fixy::TextView{"foundation: contract violation: !empty()"});
    expect_dies(
        test, [&whole] noexcept { (void)whole.subview(4, 2); },
        ::fixy::TextView{"foundation: contract violation: offset <= size() && count <= size() - offset"});
    return 0;
}

// Every accessor of the view, and the equality it carries.  A view is a
// value, so two views over the same range compare equal and two over
// different ranges do not.
int check_every_accessor() {
    B_A empty{};
    if (!empty.empty() || empty.size() != 0) return 37;

    int arr[5] = {10, 20, 30, 40, 50};
    B_A b{arr};
    if (b.size() != 5) return 38;
    if (b.front() != 10 || b.back() != 50 || b[2] != 30) return 39;

    int sum = 0;
    for (int v : b)
        sum += v;
    if (sum != 150) return 40;

    B_A prefix{arr, 2};
    if (prefix.size() != 2) return 41;
    if (b == prefix) return 42;
    B_A b_copy = b;
    if (!(b == b_copy)) return 43;

    if (b.as_span().size() != 5) return 44;

    auto window = b.subview(1, 3);
    if (window.size() != 3) return 45;
    if (window[0] != 20 || window[2] != 40) return 46;
    if (!b.subview(0, 0).empty()) return 47;

    return 0;
}

// A span passed as an rvalue is a borrowed range: its elements outlive
// it.  The mint takes it through its own overload, and the borrow it
// returns reads the array the span viewed.
int check_mint_of_a_span_rvalue() {
    int arr[3] = {4, 5, 6};
    auto borrow = ::fixy::mint_borrowed<OwnerA>(std::span<int>{arr});
    static_assert(::foundation::brand::IsBranded<decltype(borrow)>);
    if (borrow.size() != 3 || borrow[1] != 5 || borrow.data() != arr) return 60;
    return 0;
}

// A type that is not a range but converts to a span.
struct ConvertsToSpan {
    int* first = nullptr;
    std::size_t count = 0;
    operator std::span<int>() const noexcept { return std::span<int>{first, count}; }
};

// The deleted rvalue-range twin of the span constructor refuses only a
// temporary range that owns its elements.  Each construction below is
// one the twin must leave to the constructors it stands beside: a named
// range, a branded borrow that erases, a type that converts to a span
// but is not a range, and a temporary range that does not own its
// elements.  A twin that took any of them would refuse it.
int check_span_constructor_keeps_its_callers() {
    std::vector<int> named{7, 8, 9};
    B_A of_named{named};
    if (of_named.size() != 3 || of_named[0] != 7) return 70;

    int arr[3] = {1, 2, 3};
    B_A erased{::fixy::mint_borrowed<OwnerA>(arr)};
    if (erased.size() != 3 || erased.data() != arr) return 71;

    B_A converted{ConvertsToSpan{arr, 2}};
    if (converted.size() != 2 || converted[1] != 2) return 72;

    Borrowed<char const, OwnerA> of_view{std::string_view{"abc"}};
    if (of_view.size() != 3 || of_view[2] != 'c') return 73;
    return 0;
}

// A mint takes a run of the base: a View and a TextView, also as a
// temporary, and an owner through its view().  The borrow reads and writes
// the elements of the owner, and each mint gives a fresh brand.
int check_mint_of_base_runs() {
    ::fixy::FixedArray<int, 3> array{};
    int next = 10;
    for (int& element : array)
        element = next++;
    auto of_array = ::fixy::mint_borrowed<OwnerA>(array);
    static_assert(std::is_same_v<::fixy::borrowed_value_t<decltype(of_array)>, int>);
    static_assert(::foundation::brand::IsBranded<decltype(of_array)>);
    if (of_array.size() != 3 || of_array[1] != 11) return 80;
    of_array[2] = 42;
    int last = 0;
    for (int const element : array)
        last = element;
    if (last != 42) return 81;

    ::fixy::FixedArray<int, 3> const& read_only = array;
    auto of_const = ::fixy::mint_borrowed<OwnerA>(read_only);
    static_assert(std::is_same_v<::fixy::borrowed_value_t<decltype(of_const)>, int const>);
    static_assert(!::foundation::brand::SameBrand<decltype(of_array), decltype(of_const)>,
                  "each mint gives a fresh brand");
    if (of_const.data() != of_array.data()) return 82;

    auto of_view = ::fixy::mint_borrowed<OwnerA>(array.view());
    if (of_view.size() != 3 || of_view.data() != of_array.data()) return 83;

    ::fixy::FixedList<int, 4> list{};
    static_cast<void>(list.append(5).expect("the list has room"));
    static_cast<void>(list.append(6).expect("the list has room"));
    auto of_list = ::fixy::mint_borrowed<OwnerA>(list);
    if (of_list.size() != 2 || of_list[1] != 6) return 84;

    ::fixy::FixedText<8> const text{"hello"};
    auto of_text = ::fixy::mint_borrowed<OwnerA>(text);
    static_assert(std::is_same_v<::fixy::borrowed_value_t<decltype(of_text)>, char const>);
    if (of_text.size() != 5 || of_text[4] != 'o') return 85;

    auto of_literal = ::fixy::mint_borrowed<OwnerA>(::fixy::TextView{"abc"});
    if (of_literal.size() != 3 || of_literal[0] != 'a') return 86;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_every_accessor(); rc != 0) return rc;
    if (int rc = check_views_over_a_vector(); rc != 0) return rc;
    if (int rc = check_mint_of_a_span_rvalue(); rc != 0) return rc;
    if (int rc = check_span_constructor_keeps_its_callers(); rc != 0) return rc;
    if (int rc = check_mint_of_base_runs(); rc != 0) return rc;

    return 0;
}
