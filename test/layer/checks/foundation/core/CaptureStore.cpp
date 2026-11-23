// The compile-time checks of foundation/core/CaptureStore.h.

#include <foundation/core/CaptureStore.h>

#include <foundation/Brand.h>
#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/List.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <utility>

namespace foundation::core {

namespace detail::capture_store_checks {

// Objects that a body can capture.  A Holder holds a View in a member, a Cache
// stores a View through a method, and a Cell holds a mutable View.
struct Holder {
    View<int> view{};
};
class Cache {
public:
    void remember(View<int> run) noexcept { last_ = run; }
    [[nodiscard]] std::size_t size() const noexcept { return last_.size(); }

private:
    View<int> last_{};
};
struct Cell {
    mutable View<int> cache{};
};
struct Opaque;
template <class Tag>
struct Tagged {
    int value = 0;
};
struct UndeclaredTag;

// A brand of one instance that names no scope, as the brand of an erased Ref
// or of a Ref that a body keeps.
struct CheckBrand {};

// True when the walk refuses a body of the class Body that the door takes
// with no name.
template <class Body>
[[nodiscard]] consteval bool is_refused() {
    return stores_borrow_outside_(^^Body, BodyForm::unnamed);
}

// A capture by reference of a borrow slot refuses the body: a View of const
// elements, an Option of a Ref of a brand that names no scope, a list of
// Views, and an object that holds or stores a View.
consteval bool are_slot_captures_refused() {
    View<int const> stored{};
    Option<Ref<int, CheckBrand>> escaped{none};
    FixedList<View<int const>, 2> views{};
    Holder holder{};
    Cache cache{};
    auto into_view = [&stored](auto run) noexcept { stored = run; };
    auto into_ref = [&escaped](auto run) noexcept { static_cast<void>(run); };
    auto into_list = [&views](auto run) noexcept { static_cast<void>(run); };
    auto into_holder = [&holder](auto run) noexcept { static_cast<void>(run); };
    auto into_cache = [&cache](auto run) noexcept { static_cast<void>(run); };
    return is_refused<decltype(into_view)>() && is_refused<decltype(into_ref)>() && is_refused<decltype(into_list)>()
        && is_refused<decltype(into_holder)>() && is_refused<decltype(into_cache)>();
}
static_assert(are_slot_captures_refused());

// The same slots behind an indirection: the field of [this] of an object that
// holds a View, a Ref of it by value, an owner of Views by reference, a
// mutable member of a const object, a class that the walk cannot read, and
// the elements of a ViewMut of an outer frame that are Views.
struct HolderWithBody {
    View<int> view{};
    [[nodiscard]] consteval auto body() {
        return [this](auto run) noexcept { static_cast<void>(run); };
    }
};
using ThisOfHolder = decltype(std::declval<HolderWithBody&>().body());
struct RefOfHolder {
    Ref<Holder, CheckBrand> holder;
};
struct OwnerOfViews {
    HeapArray<View<int>>& views;
};
struct ConstCell {
    Cell const& cell;
};
struct RefOfOpaque {
    Ref<Opaque, CheckBrand> opaque;
};
// A frame door gives its body the scope brand of the frame, so the shapes of
// an outer frame take the brand as a template argument, and the brand stays in
// the body of the check that makes it.
template <class Brand>
struct OuterRunOfViews {
    ViewMut<View<int>, Brand>& outer;
};
consteval bool is_outer_run_of_views_refused() {
    using FrameScope = CRUCIBLE_SCOPE_BRAND;
    return is_refused<OuterRunOfViews<FrameScope>>();
}
static_assert(is_refused<ThisOfHolder>() && is_refused<RefOfHolder>() && is_refused<OwnerOfViews>());
static_assert(is_refused<ConstCell>() && is_refused<RefOfOpaque>() && is_outer_run_of_views_refused());

// A capture that writes no borrow slot passes: an int by reference, a View by
// const reference or by value, an inline owner of ints, an owner of ints on
// the heap, an arena, a ViewMut and a Ref of an outer frame, a class of an
// undeclared tag, and a function pointer.
consteval bool are_plain_captures_admitted() {
    int count = 0;
    View<int const> const seen{};
    View<int> run_copy{};
    FixedArray<int, 4> array{};
    auto counts = [&count](auto run) noexcept { count += static_cast<int>(run.size()); };
    auto reads = [&seen](auto run) noexcept { static_cast<void>(run); };
    auto copies = [run_copy](auto run) noexcept { static_cast<void>(run); };
    auto writes_ints = [&array](auto run) noexcept { static_cast<void>(run); };
    return !is_refused<decltype(counts)>() && !is_refused<decltype(reads)>() && !is_refused<decltype(copies)>()
        && !is_refused<decltype(writes_ints)>();
}
static_assert(are_plain_captures_admitted());

struct OwnerOfInts {
    HeapArray<int>& values;
};
// The family tag of the arena that a capture below names.
struct[[= ::foundation::brand::arena_family_brand{}]] CheckArena final {};
struct ArenaOfEpoch {
    BumpArena<CheckArena>& arena;
};
template <class Brand>
struct OuterRun {
    ViewMut<int, Brand>& outer;
};
template <class Brand>
struct OuterRef {
    Ref<int, Brand>& outer;
};
consteval bool are_outer_borrows_admitted() {
    using FrameScope = CRUCIBLE_SCOPE_BRAND;
    return !is_refused<OuterRun<FrameScope>>() && !is_refused<OuterRef<FrameScope>>();
}
struct TaggedValue {
    Tagged<UndeclaredTag>& tagged;
};
struct Callback {
    void (*&callback)();
};
static_assert(!is_refused<OwnerOfInts>() && !is_refused<ArenaOfEpoch>() && are_outer_borrows_admitted());
static_assert(!is_refused<TaggedValue>() && !is_refused<Callback>());

// A named class of a body is a borrow slot of its own: the caller reads its
// members after the call.  The same class with no name passes, and a const
// named class writes nothing.
struct NamedBody {
    View<int> stored{};
};
static_assert(stores_borrow_outside_(^^NamedBody, BodyForm::named));
static_assert(!stores_borrow_outside_(^^NamedBody, BodyForm::unnamed));
static_assert(!stores_borrow_outside_(^^NamedBody const, BodyForm::named));
static_assert(!StoresNoBorrowOutside<NamedBody&> && StoresNoBorrowOutside<NamedBody&&>);

}  // namespace detail::capture_store_checks

}  // namespace foundation::core
