// The compile-time checks of fixy/SelfContained.h.

#include <fixy/SelfContained.h>

namespace fixy {

namespace detail::self_contained_self_test {

struct Plain {
    int count = 0;
    double ratio = 0.0;
};
struct Holds {
    Plain inner{};
    int values[4]{};
};
struct Points {
    int* target = nullptr;
};
struct PointsDeep {
    Holds holds{};
    Points points{};
};
struct DerivesPoints : Points {};
union EitherValue {
    int whole;
    float part;
};
union EitherPointer {
    int whole;
    int* target;
};
struct HoldsMutable {
    mutable int cache = 0;
};
struct NestsMutable {
    Plain plain{};
    HoldsMutable inner{};
};
struct DerivesMutable : HoldsMutable {};

// A program-defined range that says nothing false and still borrows: it
// declares no view and no borrow, and its elements live outside it.
struct BorrowsElements {
    int const* first = nullptr;
    int const* last = nullptr;
    [[nodiscard]] constexpr int const* begin() const noexcept { return first; }
    [[nodiscard]] constexpr int const* end() const noexcept { return last; }
};

static_assert(SelfContained<int> && SelfContained<Plain> && SelfContained<Holds> && SelfContained<EitherValue>);
static_assert(!SelfContained<int*> && !SelfContained<int const*> && !SelfContained<int&>);
static_assert(!SelfContained<void> && !SelfContained<int()>, "void and a function type are not objects");
static_assert(!SelfContained<Points> && !SelfContained<PointsDeep> && !SelfContained<DerivesPoints>);
static_assert(!SelfContained<EitherPointer>, "a union member that points is a way out, active or not");
static_assert(!SelfContained<HoldsMutable> && !SelfContained<NestsMutable> && !SelfContained<DerivesMutable>,
              "a mutable member anywhere on a by-value path is writable through a const reference");
static_assert(std::is_same_v<outside_reach_t<NestsMutable>, HoldsMutable>);
static_assert(SelfContained<std::vector<int>> && !SelfContained<std::vector<int*>>,
              "a container holds its elements, and its elements are read");
static_assert(!SelfContained<BorrowsElements>, "a range the standard does not declare is walked by its members");
static_assert(std::is_same_v<outside_reach_t<PointsDeep>, int*>, "the diagnostic names the part that reaches out");
static_assert(std::is_same_v<outside_reach_t<Holds>, self_contained::nothing_reaches_out>);

// The cases over the rest of the standard library, a comparator and an
// allocator among them, live in test/fixy/test_versioned_budgeted_attacks.cpp
// so that a header that includes this one does not pay for them.

}  // namespace detail::self_contained_self_test

}  // namespace fixy
