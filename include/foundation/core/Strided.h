#pragma once

// The strided views of the Region family: the elements of a tensor of rank R
// in one run, with a size and a stride for each axis.
//
//   StridedView<T, Rank>   the view of a static rank from 0 to
//                       max_strided_rank
//   StridedView<T>         the view of a dynamic rank (dynamic_rank): the rank
//                       is a value from 0 to max_strided_rank
//
// A size and a stride count elements, not bytes.  A stride is zero or more,
// so the element at the zero index has the lowest address of the view.  The
// element at the index (i0, ..., iR-1) is the element at the zero index plus
// i0 * stride0 + ... + iR-1 * strideR-1.  No member and no free function of
// this header gives the address of an element, so the view never exposes its
// pointer.  A view owns nothing, and it copies freely, as a View does.  Two
// indexes of a view with a stride of zero, or with strides that overlap, read
// one element.  The view of a broadcast thus has a const element.
//
// THE DOOR
//   mint_strided_view(run, sizes, strides, offset)
//                       the view of the elements of the View run from the
//                       position offset, or no value.  The rank is the fixed
//                       extent of sizes or of strides, or a dynamic rank when
//                       the two have a dynamic extent
//
// THE INVARIANT OF A VIEW
//   - The count of the elements, the product of the sizes, is at most
//     max_strided_count, and so is each size.
//   - When the view holds an element, each element lies inside the run of
//     the door.  The door proves the highest offset inside the run with
//     checked arithmetic, and each operation keeps the set of offsets or
//     makes it smaller.
// So a read at an index below the sizes needs no check.
//
// THE READS
//   rank(), count()     the rank, and the count of the elements
//   sizes(), strides()  the View of the sizes and of the strides, of rank()
//                       elements
//   size(axis), stride(axis)
//                       the size and the stride of an axis of a static rank
//   is_dense()          true when the strides are the strides of the
//                       row-major order of the sizes, for each axis with a
//                       size above one
//
// THE OPERATIONS
// Each operation makes a view of the same run, changes only the sizes, the
// strides and the element at the zero index, and costs O(rank).  An axis of a
// static rank is an Index<Rank>, so an axis past the rank does not compile.
// An axis of a dynamic rank is a number, and an axis at or past the rank
// gives no value.
//   transpose(a, b)     the axes a and b change places
//   permute(order)      the axis order[i] of this view is the axis i of the
//                       result, or no value when order is no permutation of
//                       the axes
//   slice(axis, start, count, step)
//                       the positions start, start + step, ... of count
//                       positions of one axis, or no value when they do not
//                       fit or when step is zero
//   select(axis, position)
//                       the view of one position of an axis, of one rank
//                       less, or no value past the size
//   broadcast_to(sizes) the view of the new sizes with the rule of numpy:
//                       each axis of this view meets the axis at the same
//                       place from the end, a size of one becomes any size
//                       with a stride of zero, and each new leading axis has
//                       a stride of zero.  No value when an axis does not
//                       meet that rule.  The element of the result is const
//   reshape(sizes)      the view of the same elements in the same row-major
//                       order with the new sizes, or a ReshapeError
//   ranked<R>()         the view of the static rank R of a view of a dynamic
//                       rank, or no value when its rank is not R
// A view of a static rank converts to a view of a dynamic rank, and a view of
// T converts to a view of T const.
//
// THE RULE OF RESHAPE
// The rule is the rule of computeStride of PyTorch.  The walk reads the axes
// of this view from the last axis, in groups.  A group is a run of axes in
// which each axis with a size above one has the stride of the axis after it
// times the size of that axis.  The new sizes take the groups from the last
// axis, and each group of new axes must hold the same count of elements as
// its group of old axes.  The new strides of a group then come from the
// stride of its last old axis.  When the count of the elements is zero, the
// strides are the strides of the row-major order of the new sizes.  The rule
// gives a view exactly when the offsets of the elements in row-major order
// are the offsets of a strided view of the new sizes.  The test file holds
// that proof as a property test with a naive oracle.
//
// THE UNIQUE VIEW
//   mint_strided_view_mut(run, sizes, strides, offset)
//                       the StridedViewMut<T, Rank, Brand> of the elements of
//                       the ViewMut run of a frame (foundation/core/Frame.h),
//                       or no value.  The mint consumes run.  It makes the
//                       checks of mint_strided_view, and it refuses a view in
//                       which two indexes can name one element: it sorts the
//                       axes of a size above one by stride, and each stride
//                       must be larger than the span of the axes before it
// A unique view is the one writer of its elements, and it does not copy.  It
// has the reads of a view, the plain view(), and a transpose of a static
// rank.  The pointwise walk of foundation/core/Pointwise.h writes through it.
//
// THE WALK
//   walk(body, views...)
//                       calls body(element, ...) one time for each index of
//                       the sizes, with the element at that index of each
//                       view.  The views must have equal sizes, else the walk
//                       gives ShapeMismatch and calls body for no index
// The order of the walk is the row-major order of the logical sizes: the last
// axis changes first.  The order does not depend on the strides, on the
// target or on the path that the walk takes, so a body that reduces the
// elements adds them in one stated order on each target.  The walk drops each
// axis of size one, and it merges two neighbor axes when the outer stride of
// each view is the inner stride times the inner size.  Such a merge keeps the
// row-major order.  When each view then has a stride of one on the last axis,
// the inner loop reads consecutive elements, and the optimizer can make it a
// vector loop.  When only the first view has a stride of one there, such as
// the output of a kernel with a transposed input, the inner loop writes
// consecutive elements and steps over the elements of the other views.
//
// THE SCOPE
//   mint_strided_scope(view, body)
//                       calls body with the BrandedStridedView of view, with
//                       the fresh brand of the instance of the mint, and
//                       gives the result of body
//   BrandedStridedView<T, Rank, Brand>
//                       the view of one scope.  Its sizes never change, so an
//                       index that it proves stays below its sizes
//   StridedIndex<Rank, Brand>
//                       the coordinates of an index below the sizes of the
//                       view of Brand.  An index holds coordinates, and not an
//                       offset, so one index reads each parallel view
// The operations of a branded strided view:
//   view(), rank(), sizes()
//   index(coordinates)  Option<StridedIndex<Rank, Brand>>: one compare for
//                       each axis
//   indices()           each StridedIndex<Rank, Brand>, in the row-major order
//                       of the walk
//   at(index)           the element at a StridedIndex<Rank, Brand>, with no
//                       check: rank() products and sums
//   parallel(other)     Option<BrandedStridedView<U, Rank, Brand>>: the view
//                       other with the brand of the scope when its sizes are
//                       the same.  Each index of the scope then reads each
//                       parallel view with no check
// The result of the body must not hold the brand, as for mint_branded_scope
// of foundation/core/Branded.h.  The brand is a scope brand, so the rule
// brand-escape of the quarantine plugin refuses an object of static or
// thread storage duration that holds it.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The largest rank of a strided view: the largest rank of a tensor that the
// record path holds.
inline constexpr std::size_t max_strided_rank = 8;

// The rank of a strided view whose rank is a value and not a part of its type.
inline constexpr std::size_t dynamic_rank = ~std::size_t{0};

// The largest count of the elements of a strided view, and the largest size
// of one axis.  A count at most this bound has a signed difference, and a sum
// of two such counts does not wrap.
inline constexpr std::size_t max_strided_count = static_cast<std::size_t>(__PTRDIFF_MAX__);

// A rank that a strided view can carry.
template <std::size_t Rank>
concept StridedRank = Rank == dynamic_rank || Rank <= max_strided_rank;

template <class T, std::size_t Rank = dynamic_rank>
    requires ViewElement<T> && StridedRank<Rank>
class StridedView;

template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
    requires StridedRank<Rank>
class StridedIndex;

template <class T, std::size_t Rank, ::foundation::brand::IsBrand Brand>
    requires ViewElement<T> && StridedRank<Rank>
class BrandedStridedView;

// The error of a walk over views whose sizes differ.
struct ShapeMismatch final {};

// The error of a reshape.
enum class ReshapeError : std::uint8_t {
    // The new sizes hold a different count of elements, or a count past
    // max_strided_count.
    CountMismatch,
    // The strides of the view give no strides for the new sizes.
    NotAView,
    // The new sizes have more than max_strided_rank axes.
    RankPastLimit,
};

namespace detail {

// The sizes and the strides of a view of a static rank.  A view of rank zero
// keeps one lane of each that no axis reads.
template <std::size_t Rank>
struct StridedLanes {
    static constexpr std::size_t capacity = Rank == 0 ? 1 : Rank;
    std::size_t sizes[capacity]{};
    std::size_t strides[capacity]{};

    [[nodiscard]] static constexpr std::size_t rank() noexcept { return Rank; }
    [[nodiscard]] static constexpr StridedLanes of_rank(std::size_t /*rank*/) noexcept { return StridedLanes{}; }
};

// The sizes and the strides of a view of a dynamic rank, and the rank.
template <>
struct StridedLanes<dynamic_rank> {
    static constexpr std::size_t capacity = max_strided_rank;
    std::size_t sizes[capacity]{};
    std::size_t strides[capacity]{};
    std::size_t rank_ = 0;

    // The rank, which each door bounds by max_strided_rank.  The assume
    // reads a local copy, so the view stays in registers.
    [[nodiscard]] constexpr std::size_t rank() const noexcept {
        std::size_t const rank = rank_;
        [[assume(rank <= max_strided_rank)]];
        return rank;
    }
    [[nodiscard]] static constexpr StridedLanes of_rank(std::size_t rank) noexcept {
        StridedLanes lanes{};
        lanes.rank_ = rank;
        return lanes;
    }
};

// The coordinates of an index of a static rank.
template <std::size_t Rank>
struct StridedCoordinates {
    static constexpr std::size_t capacity = Rank == 0 ? 1 : Rank;
    std::size_t values[capacity]{};

    [[nodiscard]] static constexpr std::size_t rank() noexcept { return Rank; }
    [[nodiscard]] static constexpr StridedCoordinates of_rank(std::size_t /*rank*/) noexcept {
        return StridedCoordinates{};
    }
};

// The coordinates of an index of a dynamic rank, and the rank.
template <>
struct StridedCoordinates<dynamic_rank> {
    static constexpr std::size_t capacity = max_strided_rank;
    std::size_t values[capacity]{};
    std::size_t rank_ = 0;

    [[nodiscard]] constexpr std::size_t rank() const noexcept {
        std::size_t const rank = rank_;
        [[assume(rank <= max_strided_rank)]];
        return rank;
    }
    [[nodiscard]] static constexpr StridedCoordinates of_rank(std::size_t rank) noexcept {
        StridedCoordinates coordinates{};
        coordinates.rank_ = rank;
        return coordinates;
    }
};

// The axis of a view of a static rank is an Index<Rank>, and the axis of a
// view of a dynamic rank is a number.  A view of rank zero has no axis.  The
// constraint of each operation that takes an axis refuses rank zero, so the
// type Index<1> of its parameter there is never read.
template <std::size_t Rank>
struct strided_axis {
    using type = Index<Rank>;
};
template <>
struct strided_axis<0> {
    using type = Index<1>;
};
template <>
struct strided_axis<dynamic_rank> {
    using type = std::size_t;
};
template <std::size_t Rank>
using strided_axis_t = typename strided_axis<Rank>::type;

// The rank of a view whose sizes come in a View of the extent Extent.
template <std::size_t Extent>
inline constexpr std::size_t rank_of_extent = Extent == dynamic_extent ? dynamic_rank : Extent;

// The rank of a view of one rank less.
template <std::size_t Rank>
inline constexpr std::size_t rank_below = Rank == dynamic_rank ? dynamic_rank : Rank - 1;

// Writes the value of a checked word into out and gives true, or gives false
// when the check gave no value.
[[nodiscard]] constexpr bool take_word_(Option<std::size_t> checked, std::size_t& out) noexcept {
    return static_cast<Option<std::size_t>&&>(checked).match(
        [&out](std::size_t value) noexcept {
            out = value;
            return true;
        },
        [] noexcept { return false; });
}

// The count of the elements of lanes: the product of the sizes, or no value
// when a size or the product is past max_strided_count.  A size of zero gives
// zero, also when the other sizes are large.  O(rank).
template <class Lanes>
[[nodiscard]] constexpr Option<std::size_t> checked_count_(Lanes const& lanes) noexcept {
    bool is_empty = false;
    for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
        std::size_t const size = lanes.sizes[axis];
        if (size > max_strided_count) return Option<std::size_t>{none};
        is_empty = is_empty || size == 0;
    }
    if (is_empty) return Option<std::size_t>::some(std::size_t{0});
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
        if (!take_word_(checked_mul(count, lanes.sizes[axis]), count) || count > max_strided_count) {
            return Option<std::size_t>{none};
        }
    }
    return Option<std::size_t>::some(count);
}

// The highest offset of an element of lanes from the element at the zero
// index, for lanes that hold an element, or no value when the sum is past
// max_strided_count.  O(rank).
template <class Lanes>
[[nodiscard]] constexpr Option<std::size_t> checked_span_(Lanes const& lanes) noexcept {
    std::size_t span = 0;
    for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
        std::size_t reach = 0;
        if (!take_word_(checked_mul(lanes.sizes[axis] - 1, lanes.strides[axis]), reach)
            || !take_word_(checked_add(span, reach), span) || span > max_strided_count) {
            return Option<std::size_t>{none};
        }
    }
    return Option<std::size_t>::some(span);
}

// True when the old axis before old_axis starts a new group of a reshape:
// its size is above one, and its stride is not the count of the elements of
// the group times the stride of the group.  A product that wraps is no
// stride of a view, so it starts a group too.
template <class Lanes>
[[nodiscard]] constexpr bool starts_reshape_group_(Lanes const& lanes, std::size_t old_axis, std::size_t group_count,
                                                   std::size_t group_stride) noexcept {
    if (lanes.sizes[old_axis - 1] == 1) return false;
    std::size_t contiguous = 0;
    return !take_word_(checked_mul(group_count, group_stride), contiguous) || lanes.strides[old_axis - 1] != contiguous;
}

// The strides of the row-major order of the sizes of lanes, with a size of
// zero read as one, as the reshape of an empty view gives them.  The product
// of the sizes is at most max_strided_count, so no product wraps.  O(rank).
template <class Lanes>
constexpr void write_row_major_strides_(Lanes& lanes) noexcept {
    std::size_t stride = 1;
    for (std::size_t axis = lanes.rank(); axis-- > 0;) {
        lanes.strides[axis] = stride;
        std::size_t const size = lanes.sizes[axis];
        stride *= size == 0 ? 1 : size;
    }
}

// True when the two lanes have one rank and the same size on each axis.
template <class Lanes>
[[nodiscard]] constexpr bool same_sizes_(Lanes const& left, Lanes const& right) noexcept {
    if (left.rank() != right.rank()) return false;
    for (std::size_t axis = 0; axis < left.rank(); ++axis) {
        if (left.sizes[axis] != right.sizes[axis]) return false;
    }
    return true;
}

// The key of the doors of the base layer to the parts of a strided view: the
// constructor from an address and lanes, which checks nothing, and the
// friends strided_first_of_ and strided_lanes_of_, which give the element at
// the zero index and the lanes.  The mint and the operations of a view make
// each view through the key.  StridedDoor is the one friend of the key, and
// only StridedDoor builds it.  The key, the door and the friends are no
// templates, so no explicit specialization of a template reaches the parts of
// a view.  The rule owners keeps each caller outside the base out of detail.
struct StridedDoor;

class [[nodiscard]] StridedKey final {
    friend struct StridedDoor;

    constexpr StridedKey() noexcept {}
    constexpr StridedKey(StridedKey const&) noexcept {}

public:
    StridedKey& operator=(StridedKey const&) = delete("a key of a strided view lives for one construction");
};

struct StridedDoor {
    [[nodiscard]] static constexpr StridedKey key() noexcept { return StridedKey{}; }
};

// The door of mint_strided_scope to the private constructors of a branded
// view and of an index.  Only the mint and the branded view call it.
struct StridedScopeDoor;

template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
class StridedIndexRange;
template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
class StridedIndexCursor;

// The end of a loop over the indexes of a branded strided view.  A cursor
// compares equal to it when no index remains.
struct StridedIndexEnd final {};

}  // namespace detail

template <class T, std::size_t Rank>
    requires ViewElement<T> && StridedRank<Rank>
class StridedView {
    using Lanes = detail::StridedLanes<Rank>;
    using Axis = detail::strided_axis_t<Rank>;

    // The element at the zero index when the view holds an element, else the
    // first element of the run of the door, which can be null.
    T* first_ = nullptr;
    Lanes lanes_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr StridedView(T* first, Lanes const& lanes) noexcept : first_{first}, lanes_{lanes} {}

    // The number of an axis.  An Index<Rank> proves its bound.
    [[nodiscard]] static constexpr std::size_t axis_number_(Axis axis) noexcept {
        if constexpr (Rank == dynamic_rank) {
            return axis;
        } else {
            return axis.raw();
        }
    }

    // True when the axis is below the rank.  A static axis proves it.
    [[nodiscard]] constexpr bool holds_axis_(Axis axis) const noexcept {
        if constexpr (Rank == dynamic_rank) {
            return axis < lanes_.rank();
        } else {
            return true;
        }
    }

    // The view of the lanes with the element at the zero index moved by
    // shift elements.  A view with no element keeps first_, so no address
    // past the run is made.
    template <class U, std::size_t OtherRank>
    [[nodiscard]] constexpr StridedView<U, OtherRank> rebuilt_(detail::StridedLanes<OtherRank> const& lanes,
                                                               std::size_t shift) const noexcept {
        std::size_t const count =
            detail::checked_count_(lanes).expect("an operation keeps the count at most max_strided_count");
        U* const first = count == 0 ? first_ : first_ + shift;
        return StridedView<U, OtherRank>{detail::StridedDoor::key(), first, lanes};
    }

    // The view with the axes first and second in place of each other.
    [[nodiscard]] constexpr StridedView transposed_(std::size_t first, std::size_t second) const noexcept {
        Lanes lanes = lanes_;
        lanes.sizes[first] = lanes_.sizes[second];
        lanes.strides[first] = lanes_.strides[second];
        lanes.sizes[second] = lanes_.sizes[first];
        lanes.strides[second] = lanes_.strides[first];
        return StridedView{first_, lanes};
    }

public:
    using element_type = T;
    static constexpr std::size_t static_rank = Rank;

    // The door of the base layer: the view of the lanes with the element at
    // the zero index at first, with no check.  The caller proves the
    // invariant of a view for these parts, as the mint and the operations
    // do.
    constexpr StridedView(detail::StridedKey const& /*key*/, T* first, Lanes const& lanes) noexcept
        : StridedView{first, lanes} {}

    // The element at the zero index and the lanes, for the walk and the
    // scope of the base layer.  They are friends that are no templates, so no
    // explicit specialization reaches them, and the key keeps each caller
    // outside the base out.  Argument-dependent lookup finds them.
    [[nodiscard]] friend constexpr T* strided_first_of_(detail::StridedKey const& /*key*/,
                                                        StridedView const& view) noexcept {
        return view.first_;
    }
    [[nodiscard]] friend constexpr Lanes const& strided_lanes_of_(detail::StridedKey const& /*key*/,
                                                                  StridedView const& view) noexcept {
        return view.lanes_;
    }

    // Adds a const to the element, or drops the static rank, or both.
    template <class U, std::size_t OtherRank>
        requires std::same_as<std::remove_const_t<U>, std::remove_const_t<T>>
              && (std::is_const_v<T> || !std::is_const_v<U>)
              && (Rank == OtherRank || (Rank == dynamic_rank && OtherRank != dynamic_rank))
              && (!std::same_as<U, T> || Rank != OtherRank)
    constexpr StridedView(StridedView<U, OtherRank> const& other) noexcept
        : first_{strided_first_of_(detail::StridedDoor::key(), other)} {
        detail::StridedLanes<OtherRank> const& lanes = strided_lanes_of_(detail::StridedDoor::key(), other);
        lanes_ = Lanes::of_rank(lanes.rank());
        for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
            lanes_.sizes[axis] = lanes.sizes[axis];
            lanes_.strides[axis] = lanes.strides[axis];
        }
    }

    [[nodiscard]] constexpr std::size_t rank() const noexcept { return lanes_.rank(); }

    // The product of the sizes, which the invariant bounds.  O(rank).
    [[nodiscard]] constexpr std::size_t count() const noexcept {
        std::size_t count = 1;
        for (std::size_t axis = 0; axis < rank(); ++axis) {
            count *= lanes_.sizes[axis];
        }
        [[assume(count <= max_strided_count)]];
        return count;
    }

    // The sizes and the strides, rank() elements each.  The View borrows this
    // view, so a View of a temporary view is refused.
    [[nodiscard]] constexpr View<std::size_t const> sizes() const& noexcept {
        return View<std::size_t const>{detail::ViewDoor::key(), lanes_.sizes, rank()};
    }
    void sizes() const&& = delete("the sizes of a temporary view dangle at the end of the full expression");
    [[nodiscard]] constexpr View<std::size_t const> strides() const& noexcept {
        return View<std::size_t const>{detail::ViewDoor::key(), lanes_.strides, rank()};
    }
    void strides() const&& = delete("the strides of a temporary view dangle at the end of the full expression");

    // The size and the stride of an axis of a static rank.
    [[nodiscard]] constexpr std::size_t size(Axis axis) const noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return lanes_.sizes[axis.raw()];
    }
    [[nodiscard]] constexpr std::size_t stride(Axis axis) const noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return lanes_.strides[axis.raw()];
    }

    // True when each axis with a size above one has the stride of the
    // row-major order of the sizes.  A view with no element is dense.  The
    // walk reads such a view as one run.  O(rank).
    [[nodiscard]] constexpr bool is_dense() const noexcept {
        if (count() == 0) return true;
        std::size_t expected = 1;
        for (std::size_t axis = rank(); axis-- > 0;) {
            std::size_t const size = lanes_.sizes[axis];
            if (size == 1) continue;
            if (lanes_.strides[axis] != expected) return false;
            expected *= size;
        }
        return true;
    }

    // The view with the axes first and second in place of each other.  The
    // type of a static axis proves it below the rank.
    [[nodiscard]] constexpr StridedView transpose(Axis first, Axis second) const noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return transposed_(first.raw(), second.raw());
    }

    // The same for a dynamic rank, or no value when an axis is at or past the
    // rank.  Each member that gives an Option or a Result of a view deduces
    // its return type, so the Option is named only where this class is
    // complete.
    [[nodiscard]] constexpr auto transpose(std::size_t first, std::size_t second) const noexcept
        requires(Rank == dynamic_rank)
    {
        using Transposed = Option<StridedView>;
        if (first >= rank() || second >= rank()) return Transposed{none};
        return Transposed::some(transposed_(first, second));
    }

    // The view whose axis i is the axis order[i] of this view, or no value
    // when order does not name each axis exactly one time.  A View of a fixed
    // extent other than a static rank does not compile.  O(rank).
    template <class S, std::size_t Extent>
        requires std::same_as<std::remove_const_t<S>, std::size_t>
              && (Extent == dynamic_extent || Rank == dynamic_rank || Extent == Rank)
    [[nodiscard]] constexpr auto permute(View<S, Extent> order) const noexcept {
        using Permuted = Option<StridedView>;
        if (order.size() != rank()) return Permuted{none};
        Lanes lanes = lanes_;
        std::uint32_t seen = 0;
        std::size_t position = 0;
        for (std::size_t const axis : order) {
            if (axis >= rank() || (seen & (std::uint32_t{1} << axis)) != 0) return Permuted{none};
            seen |= std::uint32_t{1} << axis;
            lanes.sizes[position] = lanes_.sizes[axis];
            lanes.strides[position] = lanes_.strides[axis];
            ++position;
        }
        return Permuted::some(StridedView{first_, lanes});
    }

    // The count positions start, start + step, ... of the axis, or no value
    // when step is zero or when the last position is at or past the size.  A
    // count of zero fits at each start up to the size.  O(rank).
    [[nodiscard]] constexpr auto slice(Axis axis, std::size_t start, std::size_t count,
                                       std::size_t step = 1) const noexcept
        requires(Rank != 0)
    {
        using Sliced = Option<StridedView>;
        if (!holds_axis_(axis) || step == 0) return Sliced{none};
        std::size_t const number = axis_number_(axis);
        std::size_t const size = lanes_.sizes[number];
        if (count == 0) {
            if (start > size) return Sliced{none};
        } else {
            // The last position, start + (count - 1) * step, must be below the
            // size.
            std::size_t reach = 0;
            if (start >= size || !detail::take_word_(checked_mul(count - 1, step), reach) || reach > size - 1 - start) {
                return Sliced{none};
            }
        }
        Lanes lanes = lanes_;
        lanes.sizes[number] = count;
        // A step past the second position never reaches the stride product,
        // so an axis of one position keeps its stride.  The other products are
        // at most the span of the view.
        if (count > 1) lanes.strides[number] = lanes_.strides[number] * step;
        return Sliced::some(rebuilt_<T, Rank>(lanes, start * lanes_.strides[number]));
    }

    // The view of one position of the axis, of one rank less, or no value
    // when the position is at or past the size.  O(rank).
    [[nodiscard]] constexpr auto select(Axis axis, std::size_t position) const noexcept
        requires(Rank != 0)
    {
        using Selected = Option<StridedView<T, detail::rank_below<Rank>>>;
        if (!holds_axis_(axis)) return Selected{none};
        std::size_t const number = axis_number_(axis);
        if (position >= lanes_.sizes[number]) return Selected{none};
        auto lanes = detail::StridedLanes<detail::rank_below<Rank>>::of_rank(rank() - 1);
        std::size_t target = 0;
        for (std::size_t source = 0; source < rank(); ++source) {
            if (source == number) continue;
            lanes.sizes[target] = lanes_.sizes[source];
            lanes.strides[target] = lanes_.strides[source];
            ++target;
        }
        return Selected::some(rebuilt_<T, detail::rank_below<Rank>>(lanes, position * lanes_.strides[number]));
    }

    // The view of the sizes, by the rule of broadcast of numpy, or no value
    // when an axis does not meet the rule, when sizes has fewer axes than this
    // view or more than max_strided_rank, or when the count is past
    // max_strided_count.  Two indexes of the result can read one element, so
    // the element is const.  A View of a fixed extent below a static rank
    // does not compile.  O(rank).
    template <class S, std::size_t Extent>
        requires std::same_as<std::remove_const_t<S>, std::size_t>
              && (Extent == dynamic_extent || (Extent <= max_strided_rank && (Rank == dynamic_rank || Extent >= Rank)))
    [[nodiscard]] constexpr auto broadcast_to(View<S, Extent> sizes) const noexcept {
        constexpr std::size_t target_rank = detail::rank_of_extent<Extent>;
        using Broadcast = Option<StridedView<T const, target_rank>>;
        std::size_t const new_rank = sizes.size();
        if (new_rank > max_strided_rank || new_rank < rank()) return Broadcast{none};
        auto lanes = detail::StridedLanes<target_rank>::of_rank(new_rank);
        std::size_t const lead = new_rank - rank();
        std::size_t axis = 0;
        for (std::size_t const size : sizes) {
            lanes.sizes[axis] = size;
            if (axis >= lead) {
                std::size_t const source = axis - lead;
                std::size_t const old_size = lanes_.sizes[source];
                if (old_size == size) {
                    lanes.strides[axis] = lanes_.strides[source];
                } else if (old_size != 1) {
                    return Broadcast{none};
                }
            }
            ++axis;
        }
        if (detail::checked_count_(lanes).is_none()) return Broadcast{none};
        return Broadcast::some(rebuilt_<T const, target_rank>(lanes, 0));
    }

    // The view of the same elements in the same row-major order with the new
    // sizes, by THE RULE OF RESHAPE.  The error is CountMismatch when the
    // counts differ, NotAView when the strides give no view, and
    // RankPastLimit when sizes has more than max_strided_rank axes.  O(rank +
    // the new rank).
    template <class S, std::size_t Extent>
        requires std::same_as<std::remove_const_t<S>, std::size_t>
              && (Extent == dynamic_extent || Extent <= max_strided_rank)
    [[nodiscard]] constexpr auto reshape(View<S, Extent> sizes) const noexcept {
        constexpr std::size_t target_rank = detail::rank_of_extent<Extent>;
        using Reshaped = Result<StridedView<T, target_rank>, ReshapeError>;
        std::size_t const new_rank = sizes.size();
        if (new_rank > max_strided_rank) return Reshaped{err(ReshapeError::RankPastLimit)};
        auto lanes = detail::StridedLanes<target_rank>::of_rank(new_rank);
        std::size_t position = 0;
        for (std::size_t const size : sizes) {
            lanes.sizes[position] = size;
            ++position;
        }
        std::size_t new_count = 0;
        std::size_t const old_count = count();
        if (!detail::take_word_(detail::checked_count_(lanes), new_count) || new_count != old_count) {
            return Reshaped{err(ReshapeError::CountMismatch)};
        }
        if (old_count == 0) {
            detail::write_row_major_strides_(lanes);
            return Reshaped{rebuilt_<T, target_rank>(lanes, 0)};
        }
        if (rank() == 0) {
            // Each new size is one, so each new stride reads the one element.
            for (std::size_t axis = 0; axis < new_rank; ++axis) {
                lanes.strides[axis] = 1;
            }
            return Reshaped{rebuilt_<T, target_rank>(lanes, 0)};
        }
        // The groups of the old axes, from the last axis.  view_axis counts
        // the new axes that remain, from the end.
        std::size_t view_axis = new_rank;
        std::size_t base_stride = lanes_.strides[rank() - 1];
        std::size_t old_numel = 1;
        std::size_t new_numel = 1;
        for (std::size_t old_axis = rank(); old_axis-- > 0;) {
            old_numel *= lanes_.sizes[old_axis];
            if (old_axis != 0 && !detail::starts_reshape_group_(lanes_, old_axis, old_numel, base_stride)) continue;
            while (view_axis > 0 && (new_numel < old_numel || lanes.sizes[view_axis - 1] == 1)) {
                if (!detail::take_word_(checked_mul(new_numel, base_stride), lanes.strides[view_axis - 1])) {
                    return Reshaped{err(ReshapeError::NotAView)};
                }
                new_numel *= lanes.sizes[view_axis - 1];
                --view_axis;
            }
            if (new_numel != old_numel) return Reshaped{err(ReshapeError::NotAView)};
            if (old_axis > 0) {
                base_stride = lanes_.strides[old_axis - 1];
                old_numel = 1;
                new_numel = 1;
            }
        }
        if (view_axis != 0) return Reshaped{err(ReshapeError::NotAView)};
        return Reshaped{rebuilt_<T, target_rank>(lanes, 0)};
    }

    // The view of the static rank R of this view of a dynamic rank, or no
    // value when its rank is not R.
    template <std::size_t R>
        requires(Rank == dynamic_rank && R <= max_strided_rank)
    [[nodiscard]] constexpr auto ranked() const noexcept {
        using Ranked = Option<StridedView<T, R>>;
        if (rank() != R) return Ranked{none};
        detail::StridedLanes<R> lanes{};
        for (std::size_t axis = 0; axis < R; ++axis) {
            lanes.sizes[axis] = lanes_.sizes[axis];
            lanes.strides[axis] = lanes_.strides[axis];
        }
        return Ranked::some(StridedView<T, R>{detail::StridedDoor::key(), first_, lanes});
    }
};

// ── The door ────────────────────────────────────────────────────────────

// The arguments of mint_strided_view: sizes and strides of std::size_t, with
// one fixed extent at most max_strided_rank when the two have fixed extents.
template <class S, std::size_t SizesExtent, class D, std::size_t StridesExtent>
concept CanMintStridedView =
    std::same_as<std::remove_const_t<S>, std::size_t> && std::same_as<std::remove_const_t<D>, std::size_t>
    && (SizesExtent == dynamic_extent || SizesExtent <= max_strided_rank)
    && (StridesExtent == dynamic_extent || StridesExtent <= max_strided_rank)
    && (SizesExtent == dynamic_extent || StridesExtent == dynamic_extent || SizesExtent == StridesExtent);

namespace detail {

// The rank of the view of mint_strided_view: the fixed extent of sizes or of
// strides, or a dynamic rank.
template <std::size_t SizesExtent, std::size_t StridesExtent>
inline constexpr std::size_t minted_rank = SizesExtent != dynamic_extent ? SizesExtent : rank_of_extent<StridesExtent>;

}  // namespace detail

// The view of the elements of run with the sizes and the strides, with the
// element at the zero index at the position offset of run, or no value.  The
// mint gives no value when the counts of sizes and strides differ, when the
// rank is past max_strided_rank, when a size or the count is past
// max_strided_count, and, for a view that holds an element, when the element
// at the highest offset lies outside run.  A view with no element does not
// read offset.  The checks use checked arithmetic, so no input wraps.  O(rank).
template <class T, std::size_t RunExtent, class S, std::size_t SizesExtent, class D, std::size_t StridesExtent>
    requires CanMintStridedView<S, SizesExtent, D, StridesExtent>
[[nodiscard]] constexpr Option<StridedView<T, detail::minted_rank<SizesExtent, StridesExtent>>>
mint_strided_view(View<T, RunExtent> run, View<S, SizesExtent> sizes, View<D, StridesExtent> strides,
                  std::size_t offset) noexcept {
    constexpr std::size_t rank = detail::minted_rank<SizesExtent, StridesExtent>;
    using Minted = Option<StridedView<T, rank>>;
    if (sizes.size() != strides.size() || sizes.size() > max_strided_rank) return Minted{none};
    if constexpr (rank != dynamic_rank) {
        if (sizes.size() != rank) return Minted{none};
    }
    auto lanes = detail::StridedLanes<rank>::of_rank(sizes.size());
    std::size_t axis = 0;
    for (std::size_t const size : sizes) {
        lanes.sizes[axis] = size;
        ++axis;
    }
    axis = 0;
    for (std::size_t const stride : strides) {
        lanes.strides[axis] = stride;
        ++axis;
    }
    std::size_t count = 0;
    if (!detail::take_word_(detail::checked_count_(lanes), count)) return Minted{none};
    T* const first = first_of_(detail::ViewDoor::key(), run);
    if (count == 0) return Minted::some(StridedView<T, rank>{detail::StridedDoor::key(), first, lanes});
    std::size_t span = 0;
    std::size_t last = 0;
    if (!detail::take_word_(detail::checked_span_(lanes), span) || !detail::take_word_(checked_add(offset, span), last)
        || last >= run.size()) {
        return Minted{none};
    }
    return Minted::some(StridedView<T, rank>{detail::StridedDoor::key(), first + offset, lanes});
}

// ── The unique view ─────────────────────────────────────────────────────

namespace detail {

// True when no two indexes of the lanes name one element.  The test sorts
// the axes of a size above one by stride, and each stride must be larger
// than the span of the axes before it: the highest offset that those axes
// reach.  Then the coordinate of each axis is the quotient of the offset by
// its stride, from the largest stride down, so the offset gives the index.
// The test is sufficient and not necessary: it refuses some layouts that do
// not overlap, such as sizes 3 and 2 with strides 2 and 3.  A stride of zero
// on an axis of a size above one is refused.  The span of a view is at most
// max_strided_count, so no sum wraps.  O(rank^2) for the sort.
template <class Lanes>
[[nodiscard]] constexpr bool has_no_overlap_(Lanes const& lanes) noexcept {
    std::size_t order[Lanes::capacity]{};
    std::size_t kept = 0;
    for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
        if (lanes.sizes[axis] <= 1) continue;
        std::size_t place = kept;
        while (place > 0 && lanes.strides[order[place - 1]] > lanes.strides[axis]) {
            order[place] = order[place - 1];
            --place;
        }
        order[place] = axis;
        ++kept;
    }
    std::size_t span = 0;
    for (std::size_t position = 0; position < kept; ++position) {
        std::size_t const axis = order[position];
        if (lanes.strides[axis] <= span) return false;
        span += (lanes.sizes[axis] - 1) * lanes.strides[axis];
    }
    return true;
}

}  // namespace detail

// The unique borrow of a strided view of the run of a frame: the run of a
// ViewMut of foundation/core/Region.h.  No two indexes of the view name one
// element, so a write at one index changes no other index.  The brand is the
// brand of the frame, so two unique views of two brands name runs that do not
// overlap (THE ALIAS PROOF of foundation/core/Frame.h).  A StridedViewMut does
// not copy.  A move leaves the source with its parts, and the rule
// use-after-move refuses a later use of the source.
//
//   rank(), count(), sizes(), strides(), size(axis), stride(axis), is_dense()
//                       as the StridedView of the same parts gives them
//   view()              the plain StridedView, with no brand
//   transpose(a, b)     of a static rank: consumes the view, and gives the
//                       unique view with the two axes in place of each other
// A unique view of a different layout of the same run comes from the mint
// with that layout.  An Option of this class reads its members through
// reflection, because the class does not copy, so no member gives an Option
// of this class.
template <class T, std::size_t Rank, ::foundation::brand::IsBrand Brand>
    requires ViewElement<T> && (!std::is_const_v<T>) && StridedRank<Rank>
class [[nodiscard]] StridedViewMut final {
    using Axis = detail::strided_axis_t<Rank>;

    // The seal of the view keeps this class from being trivially copyable,
    // so std::bit_cast builds no unique view.
    StridedView<T, Rank> view_;

public:
    using element_type = T;
    using brand_type = Brand;
    static constexpr std::size_t static_rank = Rank;

    // The door of the base layer: the unique view of view, with no check.
    // The caller proves that no two indexes of view name one element and
    // that the run of view belongs to the frame of Brand, as the mint does.
    constexpr StridedViewMut(detail::StridedKey const& /*key*/, StridedView<T, Rank> view) noexcept : view_{view} {}

    StridedViewMut(StridedViewMut const&) = delete(
        "a StridedViewMut is the one writer of its elements: a copy would be "
        "a second writer");
    StridedViewMut& operator=(StridedViewMut const&) = delete("a StridedViewMut is the one writer of its elements: a "
                                                              "copy would be a second writer");
    constexpr StridedViewMut(StridedViewMut&&) noexcept = default;
    constexpr StridedViewMut& operator=(StridedViewMut&&) noexcept = default;
    ~StridedViewMut() = default;

    // The plain view, for the walks of the base layer.  The key keeps each
    // caller outside the base out.  Argument-dependent lookup finds it.
    [[nodiscard]] friend constexpr StridedView<T, Rank> strided_view_of_(detail::StridedKey const& /*key*/,
                                                                         StridedViewMut const& view) noexcept {
        return view.view_;
    }

    [[nodiscard]] constexpr std::size_t rank() const noexcept { return view_.rank(); }
    [[nodiscard]] constexpr std::size_t count() const noexcept { return view_.count(); }
    [[nodiscard]] constexpr bool is_dense() const noexcept { return view_.is_dense(); }

    // The sizes and the strides, rank() elements each.  The View borrows
    // this view.
    [[nodiscard]] constexpr View<std::size_t const> sizes() const& noexcept { return view_.sizes(); }
    void sizes() const&& = delete("the sizes of a temporary view dangle at the end of the full expression");
    [[nodiscard]] constexpr View<std::size_t const> strides() const& noexcept { return view_.strides(); }
    void strides() const&& = delete("the strides of a temporary view dangle at the end of the full expression");

    [[nodiscard]] constexpr std::size_t size(Axis axis) const noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return view_.size(axis);
    }
    [[nodiscard]] constexpr std::size_t stride(Axis axis) const noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return view_.stride(axis);
    }

    // The plain view of the elements.  It has no brand, so it proves no
    // difference of two runs, and the write through it is the write of this
    // borrow.
    [[nodiscard]] constexpr StridedView<T, Rank> view() & noexcept { return view_; }
    [[nodiscard]] constexpr StridedView<T const, Rank> view() const& noexcept { return view_; }

    // The view with the axes first and second in place of each other.  A new
    // order of the axes keeps each index on its own element.
    [[nodiscard]] constexpr StridedViewMut transpose(Axis first, Axis second) && noexcept
        requires(Rank != dynamic_rank && Rank > 0)
    {
        return StridedViewMut{detail::StridedDoor::key(), view_.transpose(first, second)};
    }
};

// The unique view of the elements of the ViewMut run with the sizes and the
// strides, with the element at the zero index at the position offset of run,
// or no value.  The mint consumes run, so the view is the one writer of the
// run.  It gives no value in each case where mint_strided_view gives none,
// and for a view that holds an element, when two indexes can name one element
// by the test of detail::has_no_overlap_.  O(rank^2).
template <class T, class Brand, class S, std::size_t SizesExtent, class D, std::size_t StridesExtent>
    requires CanMintStridedView<S, SizesExtent, D, StridesExtent>
[[nodiscard]] constexpr Option<StridedViewMut<T, detail::minted_rank<SizesExtent, StridesExtent>, Brand>>
mint_strided_view_mut(ViewMut<T, Brand> run, View<S, SizesExtent> sizes, View<D, StridesExtent> strides,
                      std::size_t offset) noexcept {
    constexpr std::size_t rank = detail::minted_rank<SizesExtent, StridesExtent>;
    using Minted = Option<StridedViewMut<T, rank, Brand>>;
    // The arms of a match would give the brand of the frame through a return
    // type that no template argument of the match names, so the mint reads
    // the Option with a test and an expect.
    Option<StridedView<T, rank>> plain = mint_strided_view(run.view(), sizes, strides, offset);
    if (plain.is_none()) return Minted{none};
    StridedView<T, rank> const view = std::move(plain).expect("the test above");
    if (view.count() != 0 && !detail::has_no_overlap_(strided_lanes_of_(detail::StridedDoor::key(), view))) {
        return Minted{none};
    }
    return Minted::some(StridedViewMut<T, rank, Brand>{detail::StridedDoor::key(), view});
}

// ── The walk ────────────────────────────────────────────────────────────

namespace detail {

// The axes of a walk after the walk drops each axis of size one and merges
// each pair of neighbor axes that each view reads as one axis: the sizes, and
// the strides of each view.
template <std::size_t Views, std::size_t Capacity>
struct WalkPlan {
    std::size_t rank = 0;
    std::size_t sizes[Capacity]{};
    std::size_t strides[Views][Capacity]{};
};

// The plan of a walk over views that hold an element and have the sizes of
// shape.  Two neighbor axes merge when the outer stride of each view is the
// inner stride times the inner size: the outer axis then steps over exactly
// the inner run, and the merged axis visits the same offsets in the same
// row-major order.  Each kept axis has a size above one and the count is at
// most max_strided_count, so no product wraps.  O(rank * views).
template <class Shape, class... Lanes>
[[nodiscard]] constexpr WalkPlan<sizeof...(Lanes), Shape::capacity> plan_walk_(Shape const& shape,
                                                                               Lanes const&... lanes) noexcept {
    constexpr std::size_t views = sizeof...(Lanes);
    WalkPlan<views, Shape::capacity> plan{};
    for (std::size_t axis = 0; axis < shape.rank(); ++axis) {
        std::size_t const size = shape.sizes[axis];
        if (size == 1) continue;
        std::size_t const strides[views] = {lanes.strides[axis]...};
        if (plan.rank > 0) {
            std::size_t const last = plan.rank - 1;
            bool is_mergeable = true;
            for (std::size_t view = 0; view < views; ++view) {
                is_mergeable = is_mergeable && plan.strides[view][last] == strides[view] * size;
            }
            if (is_mergeable) {
                plan.sizes[last] *= size;
                for (std::size_t view = 0; view < views; ++view) {
                    plan.strides[view][last] = strides[view];
                }
                continue;
            }
        }
        plan.sizes[plan.rank] = size;
        for (std::size_t view = 0; view < views; ++view) {
            plan.strides[view][plan.rank] = strides[view];
        }
        ++plan.rank;
    }
    return plan;
}

// The inner loop of a walk when each view reads consecutive elements.
template <class Body, class... Ts>
constexpr void walk_dense_row_(Body& body, std::size_t length, Ts*... rows) noexcept {
    for (std::size_t position = 0; position < length; ++position) {
        body(rows[position]...);
    }
}

// The inner loop of a walk when the first view reads consecutive elements and
// a different view steps over elements.  The first view of a kernel is
// usually its output, so the store goes to consecutive elements, and the
// optimizer can build each vector of a stepped view from its elements.  steps
// holds the step of each view, the first view too.
template <class Body, std::size_t... Is, class First, class... Rest>
constexpr void walk_first_dense_row_(Body& body, std::index_sequence<Is...> /*rest*/, std::size_t length,
                                     std::size_t const (&steps)[1 + sizeof...(Rest)], First* first,
                                     Rest*... rest) noexcept {
    for (std::size_t position = 0; position < length; ++position) {
        body(first[position], rest[position * steps[Is + 1]]...);
    }
}

// The inner loop of a walk when the first view steps over elements.
template <class Body, std::size_t... Is, class First, class... Rest>
constexpr void walk_strided_row_(Body& body, std::index_sequence<Is...> /*rest*/, std::size_t length,
                                 std::size_t const (&steps)[1 + sizeof...(Rest)], First* first,
                                 Rest*... rest) noexcept {
    for (std::size_t position = 0; position < length; ++position) {
        body(first[position * steps[0]], rest[position * steps[Is + 1]]...);
    }
}

// One step of the outer axes of a walk: the counters and the offsets of the
// next row.  The step changes the inner outer axis first, so the rows come in
// row-major order.  After the last row, the counters and the offsets come
// back to zero, and the walk reads no element at them.
template <std::size_t Views, std::size_t Capacity>
constexpr void step_rows_(WalkPlan<Views, Capacity> const& plan, std::size_t (&counters)[Capacity],
                          std::size_t (&offsets)[Views]) noexcept {
    for (std::size_t axis = plan.rank - 1; axis-- > 0;) {
        if (++counters[axis] < plan.sizes[axis]) {
            for (std::size_t view = 0; view < Views; ++view) {
                offsets[view] += plan.strides[view][axis];
            }
            return;
        }
        counters[axis] = 0;
        for (std::size_t view = 0; view < Views; ++view) {
            offsets[view] -= (plan.sizes[axis] - 1) * plan.strides[view][axis];
        }
    }
}

// The loops of a walk over the plan of count elements: each row of the inner
// axis, in row-major order.  The count of the rows is a number before the
// first row, so the outer loop is a counted loop around the inner loop, with
// one exit.  The tests of the inner strides do not change during the walk,
// so each kind of row has a loop of its own: each view dense, the first view
// dense, and no view known dense.
// The index pack counts the views after the first one.
template <class Body, std::size_t... Is, std::size_t Capacity, class First, class... Rest>
constexpr void run_walk_(Body& body, std::index_sequence<Is...> rest_views,
                         WalkPlan<1 + sizeof...(Rest), Capacity> const& plan, std::size_t count, First* first,
                         Rest*... rest) noexcept {
    if (plan.rank == 0) {
        body(*first, *rest...);
        return;
    }
    std::size_t const inner = plan.rank - 1;
    std::size_t const length = plan.sizes[inner];
    std::size_t const rows = count / length;
    std::size_t counters[Capacity]{};
    std::size_t offsets[1 + sizeof...(Rest)]{};
    bool const is_first_dense = plan.strides[0][inner] == 1;
    if (is_first_dense && ((plan.strides[Is + 1][inner] == 1) && ...)) {
        for (std::size_t row = 0; row < rows; ++row) {
            walk_dense_row_(body, length, first + offsets[0], (rest + offsets[Is + 1])...);
            step_rows_(plan, counters, offsets);
        }
        return;
    }
    std::size_t const steps[1 + sizeof...(Rest)] = {plan.strides[0][inner], plan.strides[Is + 1][inner]...};
    if (is_first_dense) {
        for (std::size_t row = 0; row < rows; ++row) {
            walk_first_dense_row_(body, rest_views, length, steps, first + offsets[0], (rest + offsets[Is + 1])...);
            step_rows_(plan, counters, offsets);
        }
        return;
    }
    for (std::size_t row = 0; row < rows; ++row) {
        walk_strided_row_(body, rest_views, length, steps, first + offsets[0], (rest + offsets[Is + 1])...);
        step_rows_(plan, counters, offsets);
    }
}

}  // namespace detail

// The arguments of a walk: a body that takes the element of each view with
// no throw.
template <class Body, class... Ts>
concept CanWalk = std::is_nothrow_invocable_v<Body&, Ts&...>;

// Calls body one time for each index of the sizes of the views, in row-major
// order, with the element at that index of each view: THE WALK.  The views
// have one rank parameter.  The walk gives ShapeMismatch, and calls body for
// no index, when the sizes of a view differ from the sizes of first.
// O(count) calls of body, and O(rank * views) for the plan.
template <class Body, class First, class... Rest, std::size_t Rank>
    requires CanWalk<Body, First, Rest...>
[[nodiscard]] constexpr Result<Unit, ShapeMismatch> walk(Body&& body, StridedView<First, Rank> first,
                                                         StridedView<Rest, Rank>... rest) noexcept {
    auto const& shape = strided_lanes_of_(detail::StridedDoor::key(), first);
    if (!(detail::same_sizes_(shape, strided_lanes_of_(detail::StridedDoor::key(), rest)) && ...)) {
        return err(ShapeMismatch{});
    }
    std::size_t const count = first.count();
    if (count == 0) return Unit{};
    auto const plan = detail::plan_walk_(shape, shape, strided_lanes_of_(detail::StridedDoor::key(), rest)...);
    detail::run_walk_(body, std::index_sequence_for<Rest...>{}, plan, count,
                      strided_first_of_(detail::StridedDoor::key(), first),
                      strided_first_of_(detail::StridedDoor::key(), rest)...);
    return Unit{};
}

// ── The scope ───────────────────────────────────────────────────────────

// The coordinates of an index below the sizes of the BrandedStridedView of
// Brand.  Only that view makes one: index() checks it, and indices() gives
// each one in row-major order.
template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
    requires StridedRank<Rank>
class StridedIndex final {
    detail::StridedCoordinates<Rank> coordinates_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, std::size_t OtherRank, ::foundation::brand::IsBrand OtherBrand>
        requires ViewElement<T> && StridedRank<OtherRank>
    friend class BrandedStridedView;
    friend class detail::StridedIndexCursor<Rank, Brand>;

    constexpr explicit StridedIndex(detail::StridedCoordinates<Rank> const& coordinates) noexcept
        : coordinates_{coordinates} {}

public:
    // The coordinates, one for each axis.  The View borrows this index.
    [[nodiscard]] constexpr View<std::size_t const> coordinates() const& noexcept {
        return View<std::size_t const>{detail::ViewDoor::key(), coordinates_.values, coordinates_.rank()};
    }
    void
    coordinates() const&& = delete("the coordinates of a temporary index dangle at the end of the full expression");

    [[nodiscard]] friend constexpr bool operator==(StridedIndex const& left, StridedIndex const& right) noexcept {
        if (left.coordinates_.rank() != right.coordinates_.rank()) return false;
        for (std::size_t axis = 0; axis < left.coordinates_.rank(); ++axis) {
            if (left.coordinates_.values[axis] != right.coordinates_.values[axis]) return false;
        }
        return true;
    }
};

namespace detail {

// The position of a loop over the indexes of a branded strided view: the
// next index, the sizes, and the count of the indexes that remain.  A read or
// a step with no index left ends the process in each build.  The loop
// compares the cursor with the end first, so the optimizer removes the two
// checks.  The step changes the last axis first: the row-major order of the
// walk.
template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
class StridedIndexCursor {
    StridedCoordinates<Rank> at_{};
    StridedCoordinates<Rank> sizes_{};
    std::size_t remaining_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class StridedIndexRange<Rank, Brand>;

    constexpr StridedIndexCursor(StridedCoordinates<Rank> const& sizes, std::size_t remaining) noexcept
        : at_{StridedCoordinates<Rank>::of_rank(sizes.rank())}, sizes_{sizes}, remaining_{remaining} {}

public:
    [[nodiscard]] constexpr StridedIndex<Rank, Brand> operator*() const noexcept {
        if (remaining_ == 0) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the indexes of a strided view");
        }
        return StridedIndex<Rank, Brand>{at_};
    }

    constexpr StridedIndexCursor& operator++() noexcept {
        if (remaining_ == 0) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the indexes of a strided view");
        }
        --remaining_;
        if (remaining_ == 0) return *this;
        // An index remains, so a carry stops at an axis before the first.
        for (std::size_t axis = sizes_.rank(); axis-- > 0;) {
            if (++at_.values[axis] < sizes_.values[axis]) break;
            at_.values[axis] = 0;
        }
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(StridedIndexCursor const& cursor, StridedIndexEnd) noexcept {
        return cursor.remaining_ == 0;
    }
};

// Each index of a branded strided view, in row-major order.
template <std::size_t Rank, ::foundation::brand::IsBrand Brand>
class StridedIndexRange {
    StridedCoordinates<Rank> sizes_{};
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, std::size_t OtherRank, ::foundation::brand::IsBrand OtherBrand>
        requires ViewElement<T> && StridedRank<OtherRank>
    friend class ::foundation::core::BrandedStridedView;

    constexpr StridedIndexRange(StridedCoordinates<Rank> const& sizes, std::size_t count) noexcept
        : sizes_{sizes}, count_{count} {}

public:
    [[nodiscard]] constexpr StridedIndexCursor<Rank, Brand> begin() const noexcept {
        return StridedIndexCursor<Rank, Brand>{sizes_, count_};
    }
    [[nodiscard]] static constexpr StridedIndexEnd end() noexcept { return StridedIndexEnd{}; }
};

struct StridedScopeDoor {
    template <::foundation::brand::IsBrand Brand, class T, std::size_t Rank>
    [[nodiscard]] static constexpr BrandedStridedView<T, Rank, Brand> open(StridedView<T, Rank> view) noexcept {
        return BrandedStridedView<T, Rank, Brand>{view};
    }
};

}  // namespace detail

// The strided view of one scope.  It comes only from mint_strided_scope and
// from parallel(), and its sizes never change, so each StridedIndex<Rank,
// Brand> stays below them.
template <class T, std::size_t Rank, ::foundation::brand::IsBrand Brand>
    requires ViewElement<T> && StridedRank<Rank>
class BrandedStridedView final {
    StridedView<T, Rank> view_;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::StridedScopeDoor;

    constexpr explicit BrandedStridedView(StridedView<T, Rank> view) noexcept : view_{view} {}

    // The coordinates as an index, or no value when a coordinate is at or
    // past its size.
    [[nodiscard]] constexpr Option<StridedIndex<Rank, Brand>>
    checked_index_(detail::StridedCoordinates<Rank> const& coordinates) const noexcept {
        auto const& lanes = strided_lanes_of_(detail::StridedDoor::key(), view_);
        for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
            if (coordinates.values[axis] >= lanes.sizes[axis]) return Option<StridedIndex<Rank, Brand>>{none};
        }
        return Option<StridedIndex<Rank, Brand>>::some(StridedIndex<Rank, Brand>{coordinates});
    }

public:
    // The plain view.  It has no brand, so an index of the scope proves
    // nothing about it.
    [[nodiscard]] constexpr StridedView<T, Rank> view() const noexcept { return view_; }

    [[nodiscard]] constexpr std::size_t rank() const noexcept { return view_.rank(); }
    [[nodiscard]] constexpr View<std::size_t const> sizes() const& noexcept { return view_.sizes(); }
    void sizes() const&& = delete("the sizes of a temporary view dangle at the end of the full expression");

    // The index of the coordinates, one for each axis of a static rank, or no
    // value when a coordinate is at or past its size: one compare for each
    // axis.  A count of coordinates other than the rank does not compile.
    template <class... Coordinates>
        requires(Rank != dynamic_rank && sizeof...(Coordinates) == Rank
                 && (std::same_as<Coordinates, std::size_t> && ...))
    [[nodiscard]] constexpr Option<StridedIndex<Rank, Brand>> index(Coordinates... coordinates) const noexcept {
        detail::StridedCoordinates<Rank> values{};
        std::size_t axis = 0;
        ((values.values[axis++] = coordinates), ...);
        return checked_index_(values);
    }

    // The index of the coordinates in a View, or no value when its count is
    // not the rank or when a coordinate is at or past its size.
    template <class S, std::size_t Extent>
        requires std::same_as<std::remove_const_t<S>, std::size_t>
              && (Extent == dynamic_extent || Rank == dynamic_rank || Extent == Rank)
    [[nodiscard]] constexpr Option<StridedIndex<Rank, Brand>> index(View<S, Extent> coordinates) const noexcept {
        if (coordinates.size() != rank()) return Option<StridedIndex<Rank, Brand>>{none};
        auto values = detail::StridedCoordinates<Rank>::of_rank(rank());
        std::size_t axis = 0;
        for (std::size_t const coordinate : coordinates) {
            values.values[axis] = coordinate;
            ++axis;
        }
        return checked_index_(values);
    }

    // Each index of this view, in the row-major order of the walk: the loop
    // `for (auto index : view.indices())`.  A view with no element gives no
    // index, and a view of rank zero gives one.
    [[nodiscard]] constexpr detail::StridedIndexRange<Rank, Brand> indices() const noexcept {
        auto const& lanes = strided_lanes_of_(detail::StridedDoor::key(), view_);
        auto sizes = detail::StridedCoordinates<Rank>::of_rank(lanes.rank());
        for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
            sizes.values[axis] = lanes.sizes[axis];
        }
        return detail::StridedIndexRange<Rank, Brand>{sizes, view_.count()};
    }

    // The element at an index of this view.  The brand proves each
    // coordinate below its size, so the invariant of the view proves the
    // element inside its run, and the read has no check.  The cost is rank()
    // products and sums.
    [[nodiscard]] constexpr T& at(StridedIndex<Rank, Brand> const& index) const noexcept {
        auto const& lanes = strided_lanes_of_(detail::StridedDoor::key(), view_);
        std::size_t offset = 0;
        for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
            offset += index.coordinates_.values[axis] * lanes.strides[axis];
        }
        return strided_first_of_(detail::StridedDoor::key(), view_)[offset];
    }

    // The branded view of other with the brand of this scope, or no value
    // when other has a different rank or different sizes: one compare for
    // each axis.  The sizes are equal and never change, so each index of the
    // scope is below the sizes of the result too, and at() of the result has
    // no check.  The inputs and the output of an elementwise kernel then
    // share one index.
    template <class U>
    [[nodiscard]] constexpr auto parallel(StridedView<U, Rank> other) const noexcept {
        using Parallel = Option<BrandedStridedView<U, Rank, Brand>>;
        if (!detail::same_sizes_(strided_lanes_of_(detail::StridedDoor::key(), view_),
                                 strided_lanes_of_(detail::StridedDoor::key(), other))) {
            return Parallel{none};
        }
        return Parallel::some(detail::StridedScopeDoor::open<Brand>(other));
    }
};

// The arguments of mint_strided_scope: a body that takes the branded view of
// the scope with no throw and gives a value that does not hold the brand.
template <class T, std::size_t Rank, class Body, class Brand>
concept CanMintStridedScope =
    ::foundation::brand::IsBrand<Brand> && std::is_nothrow_invocable_v<Body&&, BrandedStridedView<T, Rank, Brand>>
    && !std::is_void_v<std::invoke_result_t<Body&&, BrandedStridedView<T, Rank, Brand>>>
    && !detail::names_brand(^^std::invoke_result_t<Body&&, BrandedStridedView<T, Rank, Brand>>, ^^Brand);

// Calls body with the branded view of view, with the fresh brand of the
// instance of the mint, and gives the result of body.  The pack refuses each
// explicit argument, so no caller names the brand.
template <class... Refused, class T, std::size_t Rank, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0 && CanMintStridedScope<T, Rank, Body, Fresh>)
[[nodiscard]] constexpr auto mint_strided_scope(StridedView<T, Rank> view, Body&& body) noexcept {
    return static_cast<Body&&>(body)(detail::StridedScopeDoor::open<Fresh>(view));
}

}  // namespace foundation::core
