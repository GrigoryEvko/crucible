// Sentinel TU for fixy/Borrowed.h: the three views cost one pointer or
// one span, the owner tag keeps two borrows apart, a borrow of a
// temporary selects a deleted constructor, the detection surface
// answers through the one reflection query, and every accessor of the
// three views answers on a value.  The WeakRef null-dereference contract
// is shown to abort.
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

struct Holder {
    int v = 0;
};

// Every accessor of the three views, and the equality each one carries.
// A view is a value, so two views over the same range compare equal and
// two over different ranges do not.
int check_every_accessor() {
    int x = 42;
    BorrowedRef<int> r{x};
    if (r.get() != 42) return 30;
    if (*r != 42) return 31;
    if (r.raw_ptr() != &x) return 32;

    int y = 99;
    BorrowedRef<int> other{y};
    if (r == other) return 33;
    BorrowedRef<int> r_copy = r;
    if (!(r == r_copy)) return 34;

    if (BorrowedRef<int>::from_raw_nonnull(&x).get() != 42) return 35;

    Holder h{};
    h.v = 7;
    BorrowedRef<Holder> rh{h};
    if (rh->v != 7) return 36;

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

    WeakRef<int> w_empty{};
    if (w_empty.has_value() || static_cast<bool>(w_empty)) return 48;
    if (w_empty.try_get() != nullptr) return 49;

    volatile int seed = 77;
    int box = static_cast<int>(seed);
    WeakRef<int> w{box};
    if (!w.has_value() || !static_cast<bool>(w)) return 50;
    if (w.try_get() != &box) return 51;
    if (w.get() != box || *w != box) return 52;

    struct Pair {
        int a;
        int b;
    };
    Pair p{box, box + 1};
    WeakRef<Pair> wp{p};
    if (wp->a != box || wp->b != box + 1) return 53;

    w.reset();
    if (w.has_value() || w.try_get() != nullptr) return 54;

    // from_raw is the arm that accepts a null, unlike the reference
    // constructor, so both of its outcomes are walked.
    if (WeakRef<int>::from_raw(nullptr).has_value()) return 55;
    WeakRef<int> from_full = WeakRef<int>::from_raw(&box);
    if (!from_full.has_value() || from_full.try_get() != &box) return 56;

    WeakRef<int> wa{box};
    WeakRef<int> wb = wa;
    if (!(wa == wb) || wa.try_get() != wb.try_get()) return 57;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_every_accessor(); rc != 0) return rc;
    if (int rc = check_views_over_a_vector(); rc != 0) return rc;
    if (int rc = check_weak_ref_null_aborts(); rc != 0) return rc;

    return 0;
}
