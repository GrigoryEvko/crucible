// Sentinel TU for fixy/Borrowed.h: the three views cost one pointer or
// one span, the owner tag keeps two borrows apart, a borrow of a
// temporary selects a deleted constructor, the detection surface
// answers through the one reflection query, and the header's runtime
// smoke test runs under the test flags.  The WeakRef null-dereference
// contract is shown to abort.
//
// Ported from test/test_is_borrowed.cpp, test/test_is_borrowed_ref.cpp
// and the Borrowed and WeakRef calls of test/test_smoke_safety_wrappers.cpp.

#include <fixy/Borrowed.h>

#include "../foundation/abort_probe.h"

#include <cstddef>
#include <meta>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ::fixy::Borrowed;
using ::fixy::BorrowedRef;
using ::fixy::WeakRef;
using ::foundation::test::aborts;

struct OwnerA {
    int dummy = 0;
};
struct OwnerB {
    int dummy = 0;
};

using B_A = Borrowed<int, OwnerA>;
using B_B = Borrowed<int, OwnerB>;

// One span or one pointer, nothing more.
static_assert(sizeof(B_A) == sizeof(std::span<int>));
static_assert(sizeof(BorrowedRef<int>) == sizeof(int*));
static_assert(sizeof(WeakRef<int>) == sizeof(int*));

// A borrow of a temporary selects the deleted twin.  The lvalue forms
// stay open, so the twins remove exactly the dangling shape.
static_assert(!std::is_constructible_v<BorrowedRef<int const>, int>);
static_assert(std::is_constructible_v<BorrowedRef<int const>, int const&>);
static_assert(!std::is_constructible_v<WeakRef<int const>, int>);
static_assert(std::is_constructible_v<WeakRef<int const>, int const&>);
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
constexpr auto is_borrowed_ref = []<typename T>() { return ::fixy::is_borrowed_ref_v<T>; };
constexpr auto is_weak_ref = []<typename T>() { return ::fixy::is_weak_ref_v<T>; };
constexpr auto is_none = []<typename T>() {
    return !::fixy::is_borrowed_v<T> && !::fixy::is_borrowed_ref_v<T> && !::fixy::is_weak_ref_v<T>;
};

static_assert(all_forms_satisfy<qualified_forms<B_A>, is_borrowed>());
static_assert(all_forms_satisfy<qualified_forms<Borrowed<char const, OwnerB>>, is_borrowed>());
static_assert(all_forms_satisfy<qualified_forms<BorrowedRef<int>>, is_borrowed_ref>());
static_assert(all_forms_satisfy<qualified_forms<BorrowedRef<int const>>, is_borrowed_ref>());
static_assert(all_forms_satisfy<qualified_forms<WeakRef<int>>, is_weak_ref>());

// The carriers underneath the wrappers, and the wrappers under the
// wrong question, all answer no.
static_assert(all_forms_satisfy<qualified_forms<std::span<int>>, is_none>());
static_assert(all_forms_satisfy<qualified_forms<int*>, is_none>());
static_assert(all_forms_satisfy<qualified_forms<int>, is_none>());
static_assert(!::fixy::is_borrowed_ref_v<B_A>);
static_assert(!::fixy::is_borrowed_v<BorrowedRef<int>>);
static_assert(!::fixy::is_weak_ref_v<BorrowedRef<int>>);
static_assert(!::fixy::is_borrowed_v<void>);

static_assert(std::is_same_v<::fixy::borrowed_value_t<B_A const&>, int>);
static_assert(std::is_same_v<::fixy::borrowed_source_t<B_B&&>, OwnerB>);
static_assert(std::is_same_v<::fixy::borrowed_ref_value_t<BorrowedRef<double>>, double>);
static_assert(std::is_same_v<::fixy::weak_ref_value_t<WeakRef<OwnerA>>, OwnerA>);

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

    BorrowedRef<int> third{owner[2]};
    *third = 30;
    if (whole[2] != 30) return 13;

    WeakRef<int> weak{owner[4]};
    if (!weak || *weak != 5) return 14;
    weak.reset();
    if (weak.has_value()) return 15;
    return 0;
}

int check_weak_ref_null_aborts() {
    WeakRef<int> empty{};
    if (empty.try_get() != nullptr) return 20;
    if (!aborts([&] { (void)empty.get(); })) return 21;
    if (!aborts([&] { (void)*empty; })) return 22;
    if (!aborts([&] { (void)empty.operator->(); })) return 23;
    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::borrowed_self_test::runtime_smoke_test();

    if (int rc = check_views_over_a_vector(); rc != 0) return rc;
    if (int rc = check_weak_ref_null_aborts(); rc != 0) return rc;

    return 0;
}
