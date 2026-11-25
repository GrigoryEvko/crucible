#pragma once

// The Region family: one contiguous run of elements, and its views.
//
// View<T> borrows a run of elements: a pointer and a count.  View<T, N>
// borrows a run of exactly N elements: a pointer alone, because the type
// holds the count.  A View owns nothing, and an owner gives it out.  The
// constructors from raw parts are private, so no pointer that a caller holds
// becomes a View.  Each door to them carries the proof of its run, as the
// comment on the doors of a View below tells: an owner on the core of
// foundation/core/Owner.h, a door of an owner above this header, a typed door
// of this header, or the mint of a foreign run.  Each door uses them with no
// check and no test of a null pointer, apart from the mint of a foreign run,
// which checks the parts that a foreign ABI gives.  The seal refuses
// std::bit_cast, so no bytes become a View either.
//
// The operations:
//
//   size()            the count of elements
//   for (x : view)    each element, with the count as the only bound
//   window(o, c)      the c elements from o, or no value when they do not
//                     fit: the one explicit branch of the family
//   window<N>(o)      the same, as a View of the fixed extent N
//   copy(dst, src)    the elements of src into dst.  Two Views of one
//                     fixed extent cannot fail.  Two Views of a dynamic
//                     extent give a Result, with LengthMismatch when their
//                     counts differ
//   fill(dst, value)  value into each element of dst.  An element of one
//                     byte takes one memset, the code of the raw form
//   copy(dst, src) and write_each(out, op, inputs...) of a ViewMut
//                     the doors of a kernel, after copy(View, View): a
//                     copy or an elementwise loop into the run of a frame,
//                     with restrict pointers when the brands prove that no
//                     input overlaps the output
//   fill(dst, value) of a ViewMut
//                     the fill of a View into the run of a frame
//   copy(dst, src) of a ViewMut and a View of one fixed extent
//                     the copy of two Views of one fixed extent into the
//                     run of a frame, which cannot fail
//   byte_view(run)    the bytes of a run of unsigned chars as std::byte, of
//                     the same count and extent and the same const.  A
//                     ViewMut gives its bytes by move, as a ViewMut
//   offset_of(whole, part)
//                     the position of part inside whole, or no value when
//                     part does not lie inside whole or starts between two
//                     of its elements: the inverse of window(o, c)
//   offset_of(whole, e)
//                     the position of the element e inside whole, or no
//                     value when e is no element of whole: the inverse of
//                     get(position)
//   classify_overlap(left, right)
//                     how the runs of two Views share storage: Disjoint,
//                     InPlace or Shared, for a door that reads one run and
//                     writes the other on two threads
//
// A View<T> converts to a View<T const>, and a View<T, N> converts to a
// View<T>.  No conversion adds a const or removes it the other way, and no
// conversion gives a fixed extent: window<N> is the checked road.
//
// Unique view
//   ViewMut<T, Brand>   the unique borrow of the run of an owner that a frame
//                       door holds (foundation/core/Frame.h).  The frame doors
//                       of the owners give it, and no other code makes one.  It
//                       does not copy, and its brand is the brand of its frame
//
// The window is the fit test of a caller.  A producer that appends a run
// to a segment asks the segment for the window of the run, and the empty
// result is its slow path.  The check of the window is then the only check
// of the append, and the copy into the window compiles to the memmove of
// the raw form.
//
// The loop of a View gives the cursor of foundation/core/Cursor.h, which a
// TextView of foundation/core/Text.h gives too.
//
// Walk
//   view.walk()         the head of a lazy chain of adapters and terminals of
//                       foundation/core/Walk.h, such as
//                       view.walk().map(fn).fold(init, step).  Each step of
//                       the root stage detail::ViewStage gives T&, in the
//                       counted loop of the View
//   reverse(), windows<W>(), chunks<W>(), enumerate()
//                       the iteration verbs: each gives a walk whose root
//                       stage is in this header, so `for (x : view.reverse())`
//                       and view.reverse().map(fn).fold(init, step) both work
// Walk.h is below this header, and it gives dynamic_extent and
// LengthMismatch.
//
// Foreign doors
//   mint_foreign_view(first, count)   the read-only View of a run that a
//                       foreign ABI gives, such as the bytes of the fuzz
//                       entry or the data of a tensor.  Each bit pattern of
//                       the element is a value
//   mint_foreign_text(first, count)   the ForeignText of such a run of
//                       characters, which keeps that source in its type
//   mint_foreign_text(bytes)   the ForeignText of the bytes of a View of data
//                       from outside, such as the image of a file, one
//                       character for each byte.  The View proves the run,
//                       so the mint checks nothing, and a call needs no
//                       opt-out region
//   foreign_c_str(text)   the address of the first character of a CText,
//                       for a foreign reader that reads up to the zero
//   foreign_first(view)   the address of the first element of a View, for a
//                       raw loop: a foreign reader, the measured loop of a
//                       bench, or a probe of raw behavior
// The two mints of a pointer check the count, the null pointer and the
// alignment, and they cannot prove that the run lives.  A call of each door stands only at
// the edge of a foreign ABI, in an opt-out region of the class ABI, or in the
// measured loop of a bench, in an opt-out region of the class MEASURE.  A
// call of foreign_first can also stand in a probe of raw behavior, in an
// opt-out region of the class PROBE, such as a test that reads the address of
// the first element of a run as an integer.
//
// get(position) gives an Option of a Ref, so this header includes the Ref
// family and defines the body of get itself.  A unit that includes only
// this header then cannot give get a body of its own.
//
// Pairs
//   view.enumerate()    each element of a View of a fixed extent with its
//                       proven index: a Record<Index<N>, Ref<T>>.  A View of
//                       a dynamic extent has no indices(), so it has no
//                       enumerate()
//   first.walk().zip(second.walk()), first.walk().try_zip(second.walk())
//                       each pair of the elements at one position of two
//                       Views: a Record<Ref<A>, Ref<B>> (foundation/core/Walk.h).
//                       Two walks of one fixed extent give the loop, because
//                       the type proves the counts equal.  When a walk has a
//                       dynamic extent, try_zip gives a Result, with
//                       LengthMismatch when the counts differ
//
// A Record refuses a reference part, so a pair borrows each element through
// a Ref, which is never null and has the size of a pointer.  The binding
// `auto [index, element] = pair;` gives the parts, and *element reaches the
// element.  This header includes the Record family and defines the stage and
// the cursor of enumerate().
//
// Transfer
//   A View refers to its elements as a reference does (refers of
//   foundation/Transfer.h).  So a View<T const> moves to another thread and
//   two threads read it when T is Shareable.  A View<T> that is not const
//   moves to another thread only when two threads can write a T, because a
//   const View<T> gives each element as T&, and a copy of it writes too.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Lifetime.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Cursor.h>
#include <foundation/core/Index.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/core/Walk.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The element of a View: an object type that is not an array and not
// volatile.  A const element gives a read-only View.
template <class T>
concept ViewElement = std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>;

// The largest count of a View whose element has element_bytes bytes.  A
// count larger than this has no address range, and a count of
// dynamic_extent marks an empty Option.  The bound is a function that is
// not a template, so no specialization changes it.
[[nodiscard]] constexpr std::size_t max_view_count(std::size_t element_bytes) noexcept {
    return static_cast<std::size_t>(__PTRDIFF_MAX__) / element_bytes;
}

// A fixed extent that a View can carry: not zero, and not past the largest
// count.
template <class T, std::size_t Extent>
concept ViewExtent = Extent == dynamic_extent || (Extent > 0 && Extent <= max_view_count(sizeof(T)));

// An element that copy() and fill() can write.  The write is a byte copy,
// so it must agree with the assignment of the element, and the element
// must start its lifetime with its bytes.
template <class T>
concept RegionCopyElement = std::is_trivially_copyable_v<T> && std::is_trivially_copy_assignable_v<T>
                         && std::is_implicit_lifetime_v<T> && !std::is_const_v<T>;

template <class T, std::size_t Extent = dynamic_extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class View;

// The element of a run that a foreign ABI writes, such as the bytes of the
// fuzz entry, the data of a PyTorch tensor or a record of a BPF ring buffer.
// The foreign side can write each bit pattern, so each bit pattern of the
// element is a value: no bool, no enumeration with an enumerator and no
// pointer (BitValid).
template <class T>
concept ForeignElement = ViewElement<T> && !std::is_const_v<T> && BitValid<T>;

// The View of a run that a foreign ABI gives, with the doors of a foreign run
// below.  View names it as a friend, and this header defines it.
template <class T>
    requires ForeignElement<T>
[[nodiscard]] View<T const> mint_foreign_view(T const* first, std::size_t count) noexcept;

namespace detail {

template <std::size_t Extent>
struct ViewCount {
    [[nodiscard]] static constexpr std::size_t value() noexcept { return Extent; }
};

template <>
struct ViewCount<dynamic_extent> {
    std::size_t count = 0;
    [[nodiscard]] constexpr std::size_t value() const noexcept { return count; }
};

// The doors of a View.  The constructors of a View from raw parts check
// nothing, so an owner that proves its run gets a View with no compare, also
// with -fno-delete-null-pointer-checks.  Each door names the proof of its run:
//   - An owner on the core of foundation/core/Owner.h, such as a HeapArray,
//     passes the key of the core (detail::OwnerRunKey) for its own state.
//   - An owner above this header with an invariant that no type proves, such
//     as a FixedList or the stable sort, has a plain door class that View
//     names as a friend, as the comment on the doors of the owners above this
//     header tells.
//   - A typed door of this header, such as view_of_array_ or view_of_object_,
//     takes a reference whose type proves the elements.
//   - mint_foreign_view checks the parts that a foreign ABI gives.
// No door is a template that a unit can specialize to reach the
// constructors: the rule specialization-home refuses each specialization of
// a template of the tree outside its home, and the rule owners keeps each
// caller outside the base out of detail.

// The tag of first_of_, the read of the pointer of the first element of a
// View or a ViewMut by the base layer.  The raw loops, the kernels and the
// system doors of the base read the pointer of a View that they hold.  The
// read makes no View, so each unit of the base builds the tag.  The tag is in
// namespace detail, and its constructor is explicit, so a caller outside the
// base names detail to call first_of_, and the rule owners keeps it out.
// first_of_ is a friend that is no template, so no explicit specialization
// reaches it.  first_of_ takes a const reference to a temporary tag: GCC 16
// then gives the code of the former key, and a tag by value or a reference
// to a named tag gave a sort loop four more instructions.
struct BaseLayerRead final {
    explicit BaseLayerRead() = default;
};

// The root stages of the walks of the iteration verbs reverse(),
// windows<W>(), chunks<W>() and enumerate() of a View, and their cursors.
// This header defines each of them.
template <class Element>
class ReverseStage;
template <class Element>
class ReverseCursor;
template <class Element, std::size_t Width>
class WindowStage;
template <class Element, std::size_t Width>
class WindowCursor;
template <class Element, std::size_t Width>
class ChunkStage;
template <class Element, std::size_t Width>
class ChunkCursor;
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
class EnumerateStage;
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor;

// The root stage of the walk of a View.
template <class T, std::size_t Extent>
class ViewStage;

// Typed doors: each makes the View of a run that the type of its argument
// proves, with no key and no check.  This header defines them.  The names of
// the template parameters of a View of the doors are T and Extent: GCC names
// a View of the parameters of any template with the names of the first
// declaration that forms it, and the diagnostics of the family read T and
// Extent.
template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
[[nodiscard]] constexpr View<T, Extent> view_of_array_(T (&array)[Extent]) noexcept;
template <class T>
    requires ViewElement<T>
[[nodiscard]] constexpr View<T, 1> view_of_object_(T& object) noexcept;
[[nodiscard]] constexpr View<char const> view_of_text_(TextView text) noexcept;
template <std::size_t Capacity>
[[nodiscard]] constexpr View<char, Capacity> buffer_of_text_(FixedText<Capacity>& text) noexcept;

// The extent of the storage of a run of Extent elements of type T.
template <class T, std::size_t Extent>
inline constexpr std::size_t storage_extent_v = Extent == dynamic_extent ? dynamic_extent : Extent * sizeof(T);

template <class T, std::size_t Extent>
    requires ViewElement<T>
[[nodiscard]] View<std::byte const, storage_extent_v<T, Extent>> storage_of_run_(View<T, Extent> run) noexcept;
template <class T>
    requires std::is_object_v<T>
[[nodiscard]] View<std::byte const, sizeof(T)> storage_of_object_(T const& object) noexcept;

// A body of for_each_chunk_pair_: it takes the Views of one run of Width
// elements of each of two runs, and it does not throw.
template <class Body, class First, class Second, std::size_t Width>
concept ChunkPairBody = std::is_nothrow_invocable_v<Body&, View<First, Width>, View<Second, Width>>;

template <std::size_t Width, class First, class Second, class Body>
    requires(Width != dynamic_extent)
         && ViewExtent<First, Width> && ViewExtent<Second, Width> && ChunkPairBody<Body, First, Second, Width>
CRUCIBLE_INLINE Record<View<First>, View<Second>> for_each_chunk_pair_(View<First> first, View<Second> second,
                                                                       Body& body) noexcept;

}  // namespace detail

// The unique view and the range of the chunks of its split (the unique view).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
class ViewMut;
template <class T, ::foundation::brand::IsBrand Brand>
class ViewMutChunks;

namespace detail {

// The run of a ViewMut as a plain View of its extent, for the doors of the
// base that hold the ViewMut by move or by value, and for the shared view of
// atomic cells of foundation/core/AtomicCells.h.  This header defines it.
template <class T, class Brand, std::size_t Extent>
[[nodiscard]] constexpr View<T, Extent> run_of_(ViewMut<T, Brand, Extent> const& view) noexcept;

// The door of View and ViewMut to the key of a cursor (the doors of the key
// of foundation/core/Cursor.h).  Its one member is private, and View and
// ViewMut are its friends, so no other code of the base builds a cursor over
// a run that it names.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class ViewCursorDoor final : ::foundation::NoObject<ViewCursorDoor> {
    template <class T, std::size_t Extent>
        requires ViewElement<T> && ViewExtent<T, Extent>
    friend class ::foundation::core::View;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
    friend class ::foundation::core::ViewMut;

    [[nodiscard]] static constexpr ViewCursorKey key() noexcept { return ViewCursorKey{}; }
};

}  // namespace detail

// The door of the owners of foundation/core/Array.h to the body of each get
// of a run (RegionRefDoor below).  RegionRefDoor names it as a friend, and
// Array.h defines it.
class ArrayRefDoor;

// The door of the Region family to the door of a Ref (the doors of the owners
// above foundation/core/Ref.h).  Its members are private.  The invariant: a
// run of the family holds count live elements from its first element, and
// each brand is the fresh brand of a public call of get() or of enumerate(),
// or the brand of the frame of a ViewMut.  element_at_ is the body of each
// get of a run, and it compares the position with the count first.
// enumerate_stage_ builds the root stage of enumerate() over a whole View,
// with the brand of the call of the verb.  View, ViewMut, and the stage and
// the cursor of enumerate() are its friends.  The doors of Array.h and List.h
// reach element_at_, and the door of Array.h reaches enumerate_stage_, for
// their owners.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class RegionRefDoor final : ::foundation::NoObject<RegionRefDoor> {
    template <class T, std::size_t Extent>
        requires ViewElement<T> && ViewExtent<T, Extent>
    friend class ::foundation::core::View;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
    friend class ::foundation::core::ViewMut;
    template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend class detail::EnumerateStage;
    template <class Element, std::size_t Count, class Brand>
    friend class detail::EnumerateCursor;
    friend class ArrayRefDoor;
    friend class ListRefDoor;

    // The element of a run with its index, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }

    // The element at position of the count elements from first, as an Option
    // of a Ref of the brand, or no value at or past the count.  first is not
    // null when count is above zero.
    template <::foundation::brand::IsBrand Brand, class T>
    [[nodiscard]] static constexpr auto element_at_(T* first, std::size_t count, std::size_t position) noexcept {
        if (position >= count) [[unlikely]] {
            return Option<Ref<T, Brand>>{none};
        }
        return Option<Ref<T, Brand>>::some(detail::RefDoor::lend<Brand>(first[position]));
    }

    // The root stage of enumerate() over each element of a View of Count
    // elements, from the position 0, with the brand of the call of the verb.
    template <::foundation::brand::IsBrand Brand, class Element, std::size_t Count>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::EnumerateStage<Element, Count, Brand, Count>
    enumerate_stage_(View<Element, Count> elements) noexcept {
        return detail::EnumerateStage<Element, Count, Brand, Count>{elements};
    }
};

// A callable that takes the ViewMut of the brand of the frame with no throw,
// and whose result leaves the frame (FrameResult of Choice.h).
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept FrameCallable = std::is_nothrow_invocable_v<Body&&, ViewMut<T, Brand, Extent>>
                     && FrameResult<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Brand>;

// The gates of a body of a frame door that read the captures of the body and
// no call of it: the body holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h), and it stores no borrow outside the frame
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
template <class Body>
concept FrameGates = ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>;

// The body of a frame door of an owner whose elements are T: a frame callable
// that passes the gates.  Extent is the extent of the run of the owner.
//
// The refused twin of a frame door with a brand of its own states the gates
// alone, and it gives the reason for each body that passes the gates and is no
// frame callable.  For a correct body the two doors bind, and the compiler
// takes the frame door, because its constraints contain those of the twin.  So
// the twin reads no call of the body.  Each call of a generic body
// instantiates it, and the brand of each door is its own, so a twin that reads
// the call would instantiate the innermost body of a nest of d frames 2^d
// times.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept FrameBody = FrameGates<Body> && FrameCallable<Body, T, Brand, Extent>;

// A body whose result does not leave the frame: the refused twin of a door
// whose frame has the brand of the caller binds to it, so it reads the same
// call as the door.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept RefusedFrameBody = std::is_nothrow_invocable_v<Body&&, ViewMut<T, Brand, Extent>>
                        && (!FrameResult<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Brand>)
                        && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>;

// True when a caller names no template argument of a door.  Each frame door
// has a pack that takes such an argument, and the door then does not bind.  A
// door and its refused twin state this concept, and not the bare comparison,
// so the compiler orders the two by their constraints.
template <class... Named>
concept NamesNoArgument = sizeof...(Named) == 0;

// An owner that the caller gives to a free frame door by move: an rvalue that
// is not const.  It is a concept for the same reason.
template <class Owner>
concept MovedOwner = (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>);

// The reason of the refused twin of a frame door: the body does not take the
// ViewMut, it can throw, or its result does not leave the frame.
#define CRUCIBLE_FRAME_BODY_REFUSAL                                                                                    \
    "the body of a frame door takes the ViewMut and throws nothing, and the result of a frame door leaves the frame, " \
    "so it holds no reference and no address, and it names no brand of the frame: give a plain value"

// A body of a frame door that stores a borrow outside the frame: the store
// twin of a door that has a const twin binds to it, so the refusal gives its
// own reason.  The store test comes first, so the twin reads no call of an
// admitted body.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept StoringFrameBody = (!StoresNoBorrowOutside<Body>) && ::foundation::linear::HoldsNoLinearValue<Body>
                        && std::is_invocable_v<Body&&, ViewMut<T, Brand, Extent>>;

// A body of a frame door that holds a linear value by value: the linear
// twin of each door binds to it.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept LinearFrameBody =
    (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, ViewMut<T, Brand, Extent>>;

// The splits and the reborrow of a ViewMut (the definitions below).
// RegionFrameKeyDoor names them as friends, so these declarations stand
// before the door.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr Option<Record<ViewMut<T, Brand, dynamic_extent>, ViewMut<T, Brand, dynamic_extent>>>
split_at(ViewMut<T, Brand, Extent>&& whole, std::size_t offset) noexcept;
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires(Extent != dynamic_extent && Offset > 0 && Offset < Extent)
[[nodiscard]] constexpr Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>
split_at(ViewMut<T, Brand, Extent>&& whole) noexcept;
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ViewMutChunks<T, Brand> split_chunks(ViewMut<T, Brand, Extent>&& whole,
                                                             std::size_t width) noexcept;
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr Option<ViewMut<T, Brand, dynamic_extent>>
take_window(ViewMut<T, Brand, Extent>&& whole, std::size_t offset, std::size_t count) noexcept;
template <std::size_t Count, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires ViewExtent<T, Count> && (Count != dynamic_extent)
[[nodiscard]] constexpr Option<ViewMut<T, Brand, Count>> take_window(ViewMut<T, Brand, Extent>&& whole,
                                                                     std::size_t offset) noexcept;
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ViewMut<T, Brand, dynamic_extent> take_prefix_up_to(ViewMut<T, Brand, Extent>&& whole,
                                                                            std::size_t count) noexcept;

// A body of for_each_chunk: it takes the ViewMut of one part of Width
// elements with the brand of the run, gives void, does not throw, and holds
// no linear value by value.
template <class Body, class T, class Brand, std::size_t Width>
concept ChunkBody = std::is_nothrow_invocable_v<Body&, ViewMut<T, Brand, Width>>
                 && std::is_void_v<detail::body_result_t<Body&, ViewMut<T, Brand, Width>>>
                 && ::foundation::linear::HoldsNoLinearValue<Body>;

template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class Body>
    requires ViewExtent<T, Width> && (Width != dynamic_extent) && ChunkBody<Body, T, Brand, Width>
[[nodiscard]] constexpr ViewMut<T, Brand, dynamic_extent> for_each_chunk(ViewMut<T, Brand, Extent>&& whole,
                                                                         Body body) noexcept;

// A body of for_each_chunk_pair: it takes the ViewMut of one part of Width
// elements with the brand of the run and the View of the part at the same
// position of the second run.  It gives void, does not throw, and holds no
// linear value by value.
template <class Body, class T, class Brand, class U, std::size_t Width>
concept ChunkPairMutBody = std::is_nothrow_invocable_v<Body&, ViewMut<T, Brand, Width>, View<U, Width>>
                        && std::is_void_v<std::invoke_result_t<Body&, ViewMut<T, Brand, Width>, View<U, Width>>>
                        && ::foundation::linear::HoldsNoLinearValue<Body>;

template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class U, class Body>
    requires ViewExtent<T, Width> && ViewExtent<U, Width> && (Width != dynamic_extent)
          && ChunkPairMutBody<Body, T, Brand, U, Width>
[[nodiscard]] CRUCIBLE_INLINE Record<ViewMut<T, Brand, dynamic_extent>, View<U>>
for_each_chunk_pair(ViewMut<T, Brand, Extent>&& first, View<U> second, Body body) noexcept;
template <class T, class Brand, std::size_t Extent, class Body>
    requires FrameBody<Body, T, Brand, Extent>
[[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>,
                                               ViewMut<T, Brand, Extent>>
reborrow(ViewMut<T, Brand, Extent>&& view, Body&& body) noexcept;
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires std::same_as<T, unsigned char>
[[nodiscard]] ViewMut<std::byte, Brand, Extent> byte_view(ViewMut<T, Brand, Extent>&& run) noexcept;

namespace detail {

// The body of erase_brand of a ViewMut (the definition below).
template <class T, class Brand, std::size_t Extent>
[[nodiscard]] constexpr auto erased_run_mut_(ViewMut<T, Brand, Extent>&& view) noexcept;

}  // namespace detail

// The door of the Region family to the key of the frame doors (the doors of
// the owners above foundation/core/Frame.h).  Its one member is private.  The
// invariant: each friend holds a ViewMut by move or by a reference, and it
// builds a ViewMut or a RefMut only over a part of the run of that ViewMut,
// with its brand.  The body of erase_brand builds the ViewMut of the whole
// run on the erased identity, and it consumes the source.  ViewMut,
// ViewMutChunks, the two forms of split_at, split_chunks, the two forms of
// take_window, take_prefix_up_to, for_each_chunk, for_each_chunk_pair,
// reborrow, the byte_view of a ViewMut and the body of erase_brand are its
// friends.  A home owner row of utils/rule-table.txt keeps this definition in
// this header.
class RegionFrameKeyDoor final : ::foundation::NoObject<RegionFrameKeyDoor> {
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
    friend class ::foundation::core::ViewMut;
    template <class T, ::foundation::brand::IsBrand Brand>
    friend class ::foundation::core::ViewMutChunks;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend constexpr auto ::foundation::core::split_at(ViewMut<T, Brand, Extent>&& whole, std::size_t offset) noexcept
        -> Option<Record<ViewMut<T, Brand, dynamic_extent>, ViewMut<T, Brand, dynamic_extent>>>;
    template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires(Extent != dynamic_extent && Offset > 0 && Offset < Extent)
    friend constexpr auto ::foundation::core::split_at(ViewMut<T, Brand, Extent>&& whole) noexcept
        -> Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend constexpr auto ::foundation::core::split_chunks(ViewMut<T, Brand, Extent>&& whole,
                                                           std::size_t width) noexcept -> ViewMutChunks<T, Brand>;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend constexpr auto ::foundation::core::take_window(ViewMut<T, Brand, Extent>&& whole, std::size_t offset,
                                                          std::size_t count) noexcept
        -> Option<ViewMut<T, Brand, dynamic_extent>>;
    template <std::size_t Count, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires ViewExtent<T, Count> && (Count != dynamic_extent)
    friend constexpr auto ::foundation::core::take_window(ViewMut<T, Brand, Extent>&& whole,
                                                          std::size_t offset) noexcept
        -> Option<ViewMut<T, Brand, Count>>;
    template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class Body>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && ChunkBody<Body, T, Brand, Width>
    friend constexpr auto ::foundation::core::for_each_chunk(ViewMut<T, Brand, Extent>&& whole, Body body) noexcept
        -> ViewMut<T, Brand, dynamic_extent>;
    template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class U, class Body>
        requires ViewExtent<T, Width> && ViewExtent<U, Width> && (Width != dynamic_extent)
              && ChunkPairMutBody<Body, T, Brand, U, Width>
    friend auto ::foundation::core::for_each_chunk_pair(ViewMut<T, Brand, Extent>&& first, View<U> second,
                                                        Body body) noexcept
        -> Record<ViewMut<T, Brand, dynamic_extent>, View<U>>;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    friend constexpr auto ::foundation::core::take_prefix_up_to(ViewMut<T, Brand, Extent>&& whole,
                                                                std::size_t count) noexcept
        -> ViewMut<T, Brand, dynamic_extent>;
    template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
        requires std::same_as<T, unsigned char>
    friend auto ::foundation::core::byte_view(ViewMut<T, Brand, Extent>&& run) noexcept
        -> ViewMut<std::byte, Brand, Extent>;
    template <class T, class Brand, std::size_t Extent, class Body>
        requires FrameBody<Body, T, Brand, Extent>
    friend constexpr auto ::foundation::core::reborrow(ViewMut<T, Brand, Extent>&& view, Body&& body) noexcept
        -> detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>, ViewMut<T, Brand, Extent>>;
    template <class T, class Brand, std::size_t Extent>
    friend constexpr auto detail::erased_run_mut_(ViewMut<T, Brand, Extent>&& view) noexcept;

    // The key of the frame doors, for a part of the run of a ViewMut.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

// Doors of the owners above this header
// An owner in a header above this one can hold an invariant that no type
// proves and that OwnerCore of foundation/core/Owner.h does not fit.  Such an
// owner reaches the raw constructors of a View through one plain door class.
// This header declares the class, and View names it as a friend.  The header
// of the owner defines it.  The friends of the door are the parts of the
// owner that hold the invariant: the owner class for most doors, the stable
// sort and the radix sort for SortRunDoor, and the tested window and the
// door of its list for TestedWindowDoor.  The comment of each door states
// the invariant that its owner holds.  A home owner row
// of utils/rule-table.txt keeps the one definition of each door in that
// header, so no other file defines a second door of the same name.
class FixedListSlotsDoor;
class FixedHeapArrayCountDoor;
class RunSetLayoutDoor;
class TestedWindowDoor;
class ArenaArrayDoor;
class CommittedRangeDoor;
class SortRunDoor;

// The bytes of a run of unsigned chars, as a View of std::byte (byte_view
// below).
template <class T, std::size_t Extent>
    requires std::same_as<std::remove_const_t<T>, unsigned char>
[[nodiscard]] View<std::conditional_t<std::is_const_v<T>, std::byte const, std::byte>, Extent>
byte_view(View<T, Extent> run) noexcept;

}  // namespace foundation::core

namespace foundation::lifetime {

// A byte of the bytes of a View that start_as_array takes: unsigned char or
// std::byte, with or without a const.  An array of one of the two types
// provides the storage of an object ([intro.object]).
template <typename Byte>
concept StorageByte =
    std::is_same_v<std::remove_const_t<Byte>, unsigned char> || std::is_same_v<std::remove_const_t<Byte>, std::byte>;

namespace detail {

// The extent of the View of the objects that start over bytes of Extent
// bytes.
template <class T, std::size_t Extent>
inline constexpr std::size_t started_extent_v =
    Extent == ::foundation::core::dynamic_extent ? ::foundation::core::dynamic_extent : Extent / sizeof(T);

// The body of the start of the objects that fill the bytes of a View
// (start_as_array below).  View names it as a friend.
template <ImplicitLifetimeThroughout T, StorageByte Byte, std::size_t Extent>
    requires ::foundation::core::ViewElement<element_for_storage_t<Byte, T>>
          && (Extent == ::foundation::core::dynamic_extent || Extent % sizeof(T) == 0)
[[nodiscard]] CRUCIBLE_INLINE ::foundation::core::View<element_for_storage_t<Byte, T>, started_extent_v<T, Extent>>
objects_over_bytes_(::foundation::core::View<Byte, Extent> bytes) noexcept;

}  // namespace detail

}  // namespace foundation::lifetime

namespace foundation::core {

template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class[[= ::foundation::transfer::refers{}]] View {
    static_assert(!detail::is_loan_key_type_(^^T),
                  "a View of LoanKeys would keep a borrow of the keys past the frame that lent them (THE DISPATCH "
                  "BRAND of foundation/core/Frame.h)");

    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, std::size_t OtherExtent>
        requires ViewElement<U> && ViewExtent<U, OtherExtent>
    friend class View;

    friend struct niche<View>;

    // The doors of this header that make a View from the raw parts of a run
    // that a View or a unique view proves: the root stages and the cursors of
    // the walks, the unique view and the range of its chunks.  Each friend is
    // in this header.  The rule specialization-home makes an explicit
    // specialization of a friend in another file a compile error, so no other
    // file reaches the raw constructors through a friend.
    template <class U, ::foundation::brand::IsBrand Brand, std::size_t OtherExtent>
        requires ViewElement<U> && (!std::is_const_v<U>) && ViewExtent<U, OtherExtent>
    friend class ViewMut;
    template <class U, ::foundation::brand::IsBrand Brand>
    friend class ViewMutChunks;
    template <class U, std::size_t OtherExtent>
    friend class detail::ViewStage;
    template <class Element>
    friend class detail::ReverseStage;
    template <class Element, std::size_t Width>
    friend class detail::WindowStage;
    template <class Element, std::size_t Width>
    friend class detail::WindowCursor;
    template <class Element, std::size_t Width>
    friend class detail::ChunkStage;
    template <class Element, std::size_t Width>
    friend class detail::ChunkCursor;
    template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t OtherExtent>
    friend class detail::EnumerateStage;

    // Doors of the owners above this header.
    friend class FixedListSlotsDoor;
    friend class FixedHeapArrayCountDoor;
    friend class RunSetLayoutDoor;
    friend class TestedWindowDoor;
    friend class ArenaArrayDoor;
    friend class CommittedRangeDoor;
    friend class SortRunDoor;

    // The typed doors and the doors over a run that a View proves.
    template <class U, std::size_t Count>
        requires ViewElement<U> && ViewExtent<U, Count>
    friend constexpr View<U, Count> detail::view_of_array_(U (&array)[Count]) noexcept;
    template <class U>
        requires ViewElement<U>
    friend constexpr View<U, 1> detail::view_of_object_(U& object) noexcept;
    friend constexpr View<char const> detail::view_of_text_(TextView text) noexcept;
    template <std::size_t Capacity>
    friend constexpr View<char, Capacity> detail::buffer_of_text_(FixedText<Capacity>& text) noexcept;
    template <class U, std::size_t OtherExtent>
        requires ViewElement<U>
    friend View<std::byte const, detail::storage_extent_v<U, OtherExtent>>
    detail::storage_of_run_(View<U, OtherExtent> run) noexcept;
    template <class U>
        requires std::is_object_v<U>
    friend View<std::byte const, sizeof(U)> detail::storage_of_object_(U const& object) noexcept;
    template <std::size_t Width, class First, class Second, class Body>
        requires(Width != dynamic_extent) && ViewExtent<First, Width> && ViewExtent<Second, Width>
             && detail::ChunkPairBody<Body, First, Second, Width>
    friend Record<View<First>, View<Second>> detail::for_each_chunk_pair_(View<First> first, View<Second> second,
                                                                          Body& body) noexcept;
    template <class U, class Brand, std::size_t OtherExtent>
    friend constexpr View<U, OtherExtent> detail::run_of_(ViewMut<U, Brand, OtherExtent> const& view) noexcept;
    template <class U>
        requires ForeignElement<U>
    friend View<U const> mint_foreign_view(U const* first, std::size_t count) noexcept;
    template <class U, std::size_t OtherExtent>
        requires std::same_as<std::remove_const_t<U>, unsigned char>
    friend View<std::conditional_t<std::is_const_v<U>, std::byte const, std::byte>, OtherExtent>
    byte_view(View<U, OtherExtent> run) noexcept;
    template <::foundation::lifetime::ImplicitLifetimeThroughout U, ::foundation::lifetime::StorageByte Byte,
              std::size_t OtherExtent>
        requires ViewElement<::foundation::lifetime::element_for_storage_t<Byte, U>>
              && (OtherExtent == dynamic_extent || OtherExtent % sizeof(U) == 0)
    friend View<::foundation::lifetime::element_for_storage_t<Byte, U>,
                ::foundation::lifetime::detail::started_extent_v<U, OtherExtent>>
    foundation::lifetime::detail::objects_over_bytes_(View<Byte, OtherExtent> bytes) noexcept;

    constexpr View(T* first, std::size_t count) noexcept
        requires(Extent == dynamic_extent)
        : first_{first}, count_{count} {}

    constexpr explicit View(T* first) noexcept
        requires(Extent != dynamic_extent)
        : first_{first} {}

public:
    using element_type = T;
    static constexpr std::size_t extent = Extent;

    // The empty View of a dynamic extent.  A View of a fixed extent always
    // borrows its elements, so it has no empty state.
    constexpr View() noexcept
        requires(Extent == dynamic_extent)
    = default;

    // The door of an owner on the core (foundation/core/Owner.h): the count
    // elements of the run that the owner holds, with no check.  The state of
    // the owner proves the elements live, the count at most max_view_count,
    // and first not null when the count is above zero.  Only the Owner of a
    // core, a class of the base, reaches the key, through the private
    // run_key_() of its core.
    constexpr View(detail::OwnerRunKey const& /*key*/, T* first, std::size_t count) noexcept
        requires(Extent == dynamic_extent)
        : View{first, count} {}

    // The door of an owner on the core for a fixed extent: the Extent elements
    // of its run from first, with no check.
    constexpr View(detail::OwnerRunKey const& /*key*/, T* first) noexcept
        requires(Extent != dynamic_extent)
        : View{first} {}

    // The pointer of the first element, for the owners and the free
    // operations of the base layer, such as copy() and fill().  It is a
    // friend that is no template, so no explicit specialization reaches it,
    // and the tag keeps each caller outside the base out.  Argument-dependent
    // lookup finds it: first_of_(detail::BaseLayerRead{}, view).
    [[nodiscard]] friend constexpr T* first_of_(detail::BaseLayerRead const& /*tag*/, View view) noexcept {
        return view.first_;
    }

    // Adds a const to the element, or drops the fixed extent, or both.
    template <class U, std::size_t OtherExtent>
        requires std::same_as<std::remove_const_t<U>, std::remove_const_t<T>>
              && (std::is_const_v<T> || !std::is_const_v<U>) && (Extent == dynamic_extent || Extent == OtherExtent)
              && (!std::same_as<U, T> || Extent != OtherExtent)
    constexpr View(View<U, OtherExtent> other) noexcept : first_{other.first_} {
        if constexpr (Extent == dynamic_extent) count_.count = other.size();
    }

    // The door checks each count against max_view_count in each build, and
    // the optimizer reads that bound in each build: a window of the View is
    // then never the empty value of an Option, with no check.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = count_.value();
        [[assume(count <= max_view_count(sizeof(T)))]];
        return count;
    }

    [[nodiscard]] constexpr ViewCursor<T> begin() const noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T> end() const noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }

    // The walk of the elements of this View, in order: the head of a chain of
    // foundation/core/Walk.h, such as view.walk().map(fn).fold(init, step).
    // Each step gives T&, and a View of a fixed extent gives a walk of that
    // extent.
    [[nodiscard]] constexpr Walk<detail::ViewStage<T, Extent>> walk() const noexcept {
        return Walk<detail::ViewStage<T, Extent>>{detail::ViewStage<T, Extent>{*this}};
    }

    // The element at a proven index of a View of a fixed extent.  The type
    // of the index proves it below the extent, so the read has no check.
    // An index below a smaller count is below the extent too.
    template <std::size_t Count>
        requires(Extent != dynamic_extent && Count <= Extent)
    [[nodiscard]] constexpr T& at(Index<Count> index) const noexcept {
        return first_[index.raw()];
    }

    // The element at the constant index I, which the type proves below the
    // extent.
    template <std::size_t I>
        requires(Extent != dynamic_extent && I < Extent)
    [[nodiscard]] constexpr T& at(StaticIndex<I>) const noexcept {
        return first_[I];
    }

    // Each index of a View of a fixed extent, in order: the loop
    // `for (Index<Extent> index : view.indices())`.
    [[nodiscard]] static constexpr auto indices() noexcept
        requires(Extent != dynamic_extent)
    {
        return IndexRange<Extent>{};
    }

    // The element at position, as an Option of a Ref with the fresh brand of
    // the call, or no value at or past the count: one compare.  The pack
    // refuses each explicit argument, so no caller names the brand.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get(std::size_t position) const noexcept {
        return RegionRefDoor::element_at_<Fresh>(first_, size(), position);
    }

    // The walk of each element with its proven index, in order:
    // `for (auto [index, element] : view.enumerate())`.  Each step gives a
    // Record of the Index<Extent> of indices() and a Ref to the element, and
    // each Ref of one call has the fresh brand of that call.  Only a View of a
    // fixed extent has indices(), so only that View has enumerate().  A limit
    // or a skip of the walk keeps the index of each element in the View.  The
    // pack refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0 && Extent != dynamic_extent)
    [[nodiscard]] constexpr auto enumerate() const noexcept {
        using Stage = detail::EnumerateStage<T, Extent, Fresh, Extent>;
        return Walk<Stage>{RegionRefDoor::enumerate_stage_<Fresh>(*this)};
    }

    // The walk of each element one time, from the last element:
    // `for (T& element : view.reverse())`.
    [[nodiscard]] constexpr auto reverse() const noexcept {
        return Walk<detail::ReverseStage<T>>{detail::ReverseStage<T>{View<T>{*this}}};
    }

    // The walk of each run of Width consecutive elements, with a step of one
    // element: `for (View<T, Width> run : view.windows<Width>())`.  A View of
    // fewer than Width elements gives no run.  A View of a fixed extent
    // refuses a Width past its extent at compile time.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto windows() const noexcept {
        return Walk<detail::WindowStage<T, Width>>{detail::WindowStage<T, Width>{View<T>{*this}}};
    }

    // The walk of each run of Width elements, with a step of Width elements:
    // `for (View<T, Width> run : view.chunks<Width>())`.  rest() of the walk
    // gives the View of the elements after its last run: the last
    // size() % Width elements.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto chunks() const noexcept {
        return Walk<detail::ChunkStage<T, Width>>{detail::ChunkStage<T, Width>{View<T>{*this}}};
    }

    // The count elements from offset, or no value when they do not fit.
    // The result is an Option<View<T>>.  The return type is deduced, so the
    // Option is named only where View<T> is a complete type.
    [[nodiscard]] constexpr auto window(std::size_t offset, std::size_t count) const noexcept {
        using Result = Option<View<T>>;
        if (offset > size() || count > size() - offset) [[unlikely]] {
            return Result{none};
        }
        // The window fits, so its count is at most the count of this View,
        // which its door bounds.  The optimizer then knows that the count
        // is not the empty value of the niche, and the Option needs no test.
        [[assume(count <= max_view_count(sizeof(T)))]];
        return Result::some(View<T>{first_ + offset, count});
    }

    // The first count elements, or each element when this View holds fewer
    // than count: a total prefix, with no Option.  A door that reads data
    // from outside into this View, such as the count of bytes that the kernel
    // wrote, gives the window that it filled with one compare.  The result is
    // a View<T>.  The return type is deduced, as for window(), so the
    // signature does not name View<T>, and a diagnostic prints View<T> with
    // no default extent.
    [[nodiscard]] constexpr auto prefix_up_to(std::size_t count) const noexcept {
        std::size_t const kept = count < size() ? count : size();
        return View<T>{first_, kept};
    }

    // The Window elements from offset, as an Option<View<T, Window>>, or
    // no value when they do not fit.
    template <std::size_t Window>
        requires ViewExtent<T, Window> && (Window != dynamic_extent)
    [[nodiscard]] constexpr auto window(std::size_t offset) const noexcept {
        using Result = Option<View<T, Window>>;
        if (offset > size() || Window > size() - offset) [[unlikely]] {
            return Result{none};
        }
        // The window fits and holds at least one element, so this View holds
        // one, and its door gave it a pointer that is not null.  The
        // optimizer then knows that the window is not the empty value of
        // the niche, and the Option needs no test.  The assume reads a local
        // pointer.  With an assume on the member first_, GCC 16 kept the
        // View in memory and kept the test of the niche, and the probe
        // view_window_fixed of a Release build had 15 more instructions.
        T* const first = first_ + offset;
        [[assume(first != nullptr)]];
        return Result::some(View<T, Window>{first});
    }

    // The first Count elements of a View of a fixed extent.  The type proves
    // them inside the View, so the prefix has no check.
    template <std::size_t Count>
        requires(Extent != dynamic_extent && Count > 0 && Count <= Extent)
    [[nodiscard]] constexpr View<T, Count> prefix() const noexcept {
        return View<T, Count>{first_};
    }

    // The last Count elements of a View of a fixed extent, with no check, as
    // for prefix().
    template <std::size_t Count>
        requires(Extent != dynamic_extent && Count > 0 && Count <= Extent)
    [[nodiscard]] constexpr View<T, Count> suffix() const noexcept {
        return View<T, Count>{first_ + (Extent - Count)};
    }
};

// The empty value of an Option of a View.  A View of a fixed extent has a
// null pointer only as this value.  A View of a dynamic extent has the
// count dynamic_extent only as this value, because no View holds more
// than max_view_count elements.
template <class T, std::size_t Extent>
struct niche<View<T, Extent>> {
    [[nodiscard]] static constexpr View<T, Extent> empty(niche_key<View<T, Extent>> const&) noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T, Extent>{nullptr, dynamic_extent};
        } else {
            return View<T, Extent>{static_cast<T*>(nullptr)};
        }
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<View<T, Extent>> const&,
                                                 View<T, Extent> const& view) noexcept {
        if constexpr (Extent == dynamic_extent) {
            return view.count_.count == dynamic_extent;
        } else {
            return view.first_ == nullptr;
        }
    }
};

namespace detail {

// The View of the elements of a C array.  The type of the array proves Extent
// live elements, as a reference to the array does.  The View borrows the
// array, so it lives no longer than the array.
template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
constexpr View<T, Extent> view_of_array_(T (&array)[Extent]) noexcept {
    return View<T, Extent>{array};
}

// The View of one object.  The reference proves one live object.  The View
// borrows the object, so a temporary object binds to the deleted form below.
template <class T>
    requires ViewElement<T>
constexpr View<T, 1> view_of_object_(T& object) noexcept {
    return View<T, 1>{__builtin_addressof(object)};
}

template <class T>
void view_of_object_(T const&&) = delete("a View of a temporary object dangles at the end of the full expression");

// The characters of a TextView as a View of the same run.  The TextView
// proves its characters live.
constexpr View<char const> view_of_text_(TextView text) noexcept {
    return View<char const>{text_first_(text), text.size()};
}

// The Capacity characters of the storage of a FixedText, for a door that
// writes the characters and then sets the count of the text.  Each character
// of the storage is a live object, so the View holds each of them.
template <std::size_t Capacity>
constexpr View<char, Capacity> buffer_of_text_(FixedText<Capacity>& text) noexcept {
    return View<char, Capacity>{text_bytes_(text)};
}

// The storage of the elements of a run: size() * sizeof(T) bytes, as a View
// of std::byte const.  A std::byte may read each object, and the run proves
// its elements live.  A View holds at most __PTRDIFF_MAX__ bytes, so the count
// of bytes does not wrap.  A byte of the storage can be a byte of padding or
// of an address.  The cast of the address is no constant expression, so the
// door is not constexpr.
template <class T, std::size_t Extent>
    requires ViewElement<T>
View<std::byte const, storage_extent_v<T, Extent>> storage_of_run_(View<T, Extent> run) noexcept {
    std::byte const* const first =
        static_cast<std::byte const*>(static_cast<void const*>(first_of_(BaseLayerRead{}, run)));
    if constexpr (Extent == dynamic_extent) {
        return View<std::byte const>{first, run.size() * sizeof(T)};
    } else {
        return View<std::byte const, storage_extent_v<T, Extent>>{first};
    }
}

// The storage of one object: the sizeof(T) bytes that hold it, as a View of
// std::byte const.  The reference proves the object live, also an object of
// an array type.  The View borrows the object, so a temporary object binds to
// the deleted form below.  The cast of the address is no constant expression,
// so the door is not constexpr.
template <class T>
    requires std::is_object_v<T>
View<std::byte const, sizeof(T)> storage_of_object_(T const& object) noexcept {
    return View<std::byte const, sizeof(T)>{
        static_cast<std::byte const*>(static_cast<void const*>(__builtin_addressof(object)))};
}

template <class T>
void storage_of_object_(T const&&) = delete("the storage of a temporary object ends at the end of the full expression");

// The runs of Width elements of two runs of one count, in lockstep, from the
// first element: body gets the View<First, Width> and the View<Second, Width>
// of each whole run, and the door gives the two Views of the elements after
// the last whole run.  Each run lies inside its View, because the loop counts
// whole runs, so the door makes each View with no check.  Two counts that
// differ end the process before the loop, and the optimizer removes that
// compare for two Views of one count.  Complexity: one call of body for each
// whole run.
template <std::size_t Width, class First, class Second, class Body>
    requires(Width != dynamic_extent)
         && ViewExtent<First, Width> && ViewExtent<Second, Width> && ChunkPairBody<Body, First, Second, Width>
CRUCIBLE_INLINE Record<View<First>, View<Second>> for_each_chunk_pair_(View<First> first, View<Second> second,
                                                                       Body& body) noexcept {
    if (first.size() != second.size()) [[unlikely]] {
        fatal("the chunk pairs of two Views of {} and {} elements", first.size(), second.size());
    }
    First* const first_at = first_of_(BaseLayerRead{}, first);
    Second* const second_at = first_of_(BaseLayerRead{}, second);
    // The loop counts whole runs, so the optimizer and -Warray-bounds read
    // each offset as below the count of the runs.
    std::size_t const runs = first.size() / Width;
    for (std::size_t run = 0; run < runs; ++run) {
        body(View<First, Width>{first_at + run * Width}, View<Second, Width>{second_at + run * Width});
    }
    // The rest is the count modulo the width, so the optimizer reads it as
    // below one run.
    std::size_t const whole = runs * Width;
    std::size_t const rest = first.size() % Width;
    return {View<First>{first_at + whole, rest}, View<Second>{second_at + whole, rest}};
}

// The root stage of the walk of a View (the stages of foundation/core/Walk.h):
// the counted loop over the elements, by reference.  A View of a fixed
// extent keeps the count in the type of the stage, and a limit or a skip
// gives a stage of a dynamic extent.
template <class T, std::size_t Extent>
class ViewStage final {
    View<T, Extent> elements_;

public:
    using element_reference = T&;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = Extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ViewStage(View<T, Extent> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        T* const first = first_of_(BaseLayerRead{}, elements_);
        std::size_t const count = elements_.size();
        for (std::size_t position = 0; position < count; ++position) {
            sink(first[position]);
        }
    }

    // The loop of a walk that can stop is the pointer loop of the cursor of a
    // View: one compare of the pointer with the end, and no count to keep.
    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        T* const first = first_of_(BaseLayerRead{}, elements_);
        T* const end = first + elements_.size();
        for (T* at = first; at != end; ++at) {
            if (sink(*at) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return elements_.size(); }

    // The element at position, which the caller proves below size_() (the
    // stages of foundation/core/Walk.h).
    [[nodiscard]] constexpr T& at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return first_of_(BaseLayerRead{}, elements_)[position];
    }

    // The first count elements, or each element when count is larger.
    [[nodiscard]] constexpr ViewStage<T, dynamic_extent> limited_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        return ViewStage<T, dynamic_extent>{
            View<T>{first_of_(BaseLayerRead{}, elements_), count < size ? count : size}};
    }

    // The elements after the first count, or none when count is larger.
    [[nodiscard]] constexpr ViewStage<T, dynamic_extent> skipped_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const dropped = count < size ? count : size;
        return ViewStage<T, dynamic_extent>{View<T>{first_of_(BaseLayerRead{}, elements_) + dropped, size - dropped}};
    }

    // The cursor of `for (x : walk)`: the cursor of the View.
    [[nodiscard]] constexpr ViewCursor<T> begin_() const noexcept { return elements_.begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end_() const noexcept { return elements_.end(); }
};

}  // namespace detail

// The bytes of a View as an argument of a report: two lower-case
// hexadecimal digits for each byte, in the order of the View (the
// hexadecimal text of foundation/core/Report.h).  It borrows the bytes, as
// the View does, so it lives only for the call of the report.
class HexBytes final {
    View<std::byte const> bytes_{};

public:
    // The mark by which the concept Formattable of foundation/core/Report.h
    // knows the class.
    using report_mark = detail::HexBytesMark;

    constexpr explicit HexBytes(View<std::byte const> bytes) noexcept : bytes_{bytes} {}

    // The bytes whose digits the report writes.
    [[nodiscard]] constexpr View<std::byte const> view() const noexcept { return bytes_; }
};

// ── The unique view ────────────────────────────────────────────────────

// The unique borrow of the run of an owner that a frame door holds
// (foundation/core/Frame.h).  The brand is the brand of the frame.  A
// ViewMut does not copy, so a body cannot hold two ViewMuts of one frame.  A
// move leaves the source with its run, and the rule use-after-move refuses a
// later use of the source.  The move constructor and the destructor are
// trivial, so the ABI passes a ViewMut in two registers, and a ViewMut of a
// fixed extent in one.  The seal makes the move assignment user-provided, so
// std::bit_cast builds no ViewMut.
//
//   size(), for (x : view)   as a View gives them
//   get(position)            the element as an Option<Ref<T const, Brand>>,
//                            which reads, with the brand of the frame, or no
//                            value at or past the count: one compare
//   get_mut(position)        the element as an Option<RefMut<T, Brand>>, the
//                            unique borrow of the element, which does not
//                            copy, or no value at or past the count: one
//                            compare
//   view()                   the View of the run, which reads, with no brand
// A ViewMut is the one writer of its run.  Its View and its Refs copy, so
// they read and do not write.  The writes go through get_mut, the loop over
// the ViewMut, and the doors that take the ViewMut, such as fill, copy,
// write_each and the splits below.
//   reborrow(std::move(view), body)
//                            body gets a ViewMut of the run, and the door
//                            gives the ViewMut back after the body
//   split_at(std::move(view), offset)
//                            the parts [0, offset) and [offset, size()), each a
//                            ViewMut of a dynamic extent, or no value when the
//                            offset is past the count
//   split_at<Offset>(std::move(view))
//                            the same two parts of a ViewMut of a fixed
//                            extent, each of a fixed extent, with no check
//   split_chunks(std::move(view), width)
//                            the range of the parts of width elements, and a
//                            last part of the rest
//   take_window(std::move(view), offset, count)
//                            the part [offset, offset + count) alone, a
//                            ViewMut of a dynamic extent, or no value when it
//                            does not fit
//   take_window<Count>(std::move(view), offset)
//                            the same part of Count elements, a ViewMut of
//                            the fixed extent Count
//   for_each_chunk<Width>(std::move(view), body)
//                            body gets each whole part of Width elements, a
//                            ViewMut of the fixed extent Width, and the door
//                            gives the rest as a ViewMut
//   for_each_chunk_pair<Width>(std::move(view), other, body)
//                            body gets each whole part of Width elements, a
//                            ViewMut of the fixed extent Width, and the View
//                            of the part at the same position of the View
//                            other.  The door gives the rest of view as a
//                            ViewMut and the rest of other as a View
//
// The splits and the windows are free functions.  A member function whose return type names
// ViewMut would be part of the members that the walk of a linear payload
// reads (foundation/Linear.h), and its type depends on that walk.
//
// A ViewMut<T, Brand, N> holds exactly N elements, as a View<T, N> does, and
// it converts to the ViewMut<T, Brand> of the same run.  The frame door of a
// FixedArray gives the fixed extent.
//
// Split
//   A split takes the parent by move, so the parts are the only writers of
//   the run, and no two parts share an element.  Each part keeps the brand of
//   its parent.  So two parts of one split prove no difference to a door of
//   a kernel: a copy between them takes the memmove, and write_each takes the
//   loop that the compiler guards with an alias check.  Each path is correct
//   for two runs that do not overlap.  A fresh brand for each part would let
//   the doors take the restrict path, and it is a later step.  The split of
//   a named ViewMut and of a const one do not compile.
//
// A const reference to a ViewMut is a shared borrow that keeps the brand of
// the frame.  It reads the run, and it does not write it.  A door of a kernel
// reads its inputs through such a reference, so the brands tell it that two
// runs do not overlap (the alias proof of Frame.h).
//
// The class states refers_unique (foundation/Transfer.h).  A ViewMut moves to
// another thread when its element can move.  The read first_of_ gives the
// first element for writing from a const reference too, so two threads share
// a const reference of a ViewMut only when they can write its element.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent = dynamic_extent>
    requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
class [[nodiscard]][[= ::foundation::transfer::refers_unique{}]] ViewMut final {
    static_assert(!detail::is_loan_key_type_(^^T),
                  "a ViewMut of LoanKeys would keep a borrow of the keys past the frame that lent them (THE "
                  "DISPATCH BRAND of foundation/core/Frame.h)");

    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, ::foundation::brand::IsBrand OtherBrand, std::size_t OtherExtent>
        requires ViewElement<U> && (!std::is_const_v<U>) && ViewExtent<U, OtherExtent>
    friend class ViewMut;

public:
    using element_type = T;
    using brand_type = Brand;
    static constexpr std::size_t extent = Extent;

    // The run of an owner that a frame door holds, with the key that only
    // FrameDoor builds.  The owner gave the View, so the View bounds the
    // count and holds a pointer that is not null for each count above zero.
    constexpr ViewMut(detail::FrameKey const& /*key*/, View<T, Extent> run) noexcept
        : first_{first_of_(detail::BaseLayerRead{}, run)} {
        if constexpr (Extent == dynamic_extent) count_.count = run.size();
    }

    // The ViewMut of the same run with a dynamic extent.  The source moves
    // in, so the run keeps one writer.
    template <std::size_t OtherExtent>
        requires(Extent == dynamic_extent && OtherExtent != dynamic_extent)
    constexpr ViewMut(ViewMut<T, Brand, OtherExtent>&& other) noexcept : first_{other.first_}, count_{OtherExtent} {}

    ViewMut(ViewMut const&) = delete("a ViewMut is the one writer of its run: a copy would be a second writer");
    ViewMut& operator=(ViewMut const&) = delete("a ViewMut is the one writer of its run: a copy would be a second "
                                                "writer");
    constexpr ViewMut(ViewMut&&) noexcept = default;
    constexpr ViewMut& operator=(ViewMut&&) noexcept = default;
    ~ViewMut() = default;

    // The pointer of the first element, for the free operations of the base
    // layer, such as copy() and write_each().  The tag keeps each caller
    // outside the base out, as for first_of_ of a View.
    [[nodiscard]] friend constexpr T* first_of_(detail::BaseLayerRead const& /*tag*/, ViewMut const& view) noexcept {
        return view.first_;
    }

    // The View of the run bounds the count, so the optimizer reads that
    // bound in each build, as size() of a View does.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = count_.value();
        [[assume(count <= max_view_count(sizeof(T)))]];
        return count;
    }

    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept {
        return ViewCursor<T const>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept {
        return ViewCursor<T const>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }

    // The element at position, as an Option<Ref<T const, Brand>> with the
    // brand of the frame.  The Ref reads, and get_mut below gives the writer
    // of an element.  A Ref copies, so a Ref that writes would be a second
    // writer of the element.  The brand keeps the Ref out of the result of the
    // frame (FrameResult of Choice.h).  Each frame has a brand of its own, so
    // each frame makes a ViewMut class of its own.  The member is a template,
    // so that class names no Option type and checks no constraint of one until
    // a body reads an element.  The pack refuses each explicit argument.
    template <class... Refused>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get(std::size_t position) const& noexcept {
        return RegionRefDoor::element_at_<Brand>(static_cast<T const*>(first_), size(), position);
    }

    // The element at position, as an Option<RefMut<T, Brand>> with the brand
    // of the frame.  A RefMut does not copy, so the element has one writer
    // for each call.  The brand keeps the RefMut out of the result of the
    // frame.  The member is a template with a deduced return type for the
    // reason of get(): the class of each frame names no Option type, and it
    // checks no constraint of one until a body writes an element.  A const
    // ViewMut writes nothing, so it gives no RefMut.
    template <class... Refused>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get_mut(std::size_t position) & noexcept {
        if (position >= size()) [[unlikely]] {
            return Option<RefMut<T, Brand>>{none};
        }
        return Option<RefMut<T, Brand>>::some(RefMut<T, Brand>{RegionFrameKeyDoor::key_(), first_[position]});
    }
    template <class... Refused>
    void get_mut(std::size_t) const& = delete("a const ViewMut writes nothing, so it gives no writer of an element");

    // The View of the run, for a read, with the extent of the ViewMut.  A View
    // copies, so a View that writes would be a second writer of the run, and
    // the View of a ViewMut holds const elements.  The doors that write take
    // the ViewMut, such as fill(), copy() and write_each().  The View has no
    // brand, so it proves no difference of two runs.
    [[nodiscard]] constexpr View<T const, Extent> view() const& noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T const>{first_, size()};
        } else {
            return View<T const, Extent>{first_};
        }
    }
};

// The range of the parts of a split into chunks (the split): next() gives the
// next part, from the first element, and no value after the last part.  Each
// call gives each part one time, so the range gives no second writer of a
// part.  The range does not copy, and it holds the borrow of the run that is
// left.
template <class T, ::foundation::brand::IsBrand Brand>
class [[nodiscard]] ViewMutChunks final {
    T* next_ = nullptr;
    std::size_t rest_ = 0;
    std::size_t width_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The chunks of width elements of run, with the key that only FrameDoor
    // builds.  The width is above zero.
    constexpr ViewMutChunks(detail::FrameKey const& /*key*/, View<T> run, std::size_t width) noexcept
        : next_{first_of_(detail::BaseLayerRead{}, run)}, rest_{run.size()}, width_{width} {}

    ViewMutChunks(ViewMutChunks const&) = delete("a copy of the range would give each part a second time");
    ViewMutChunks& operator=(ViewMutChunks const&) = delete("a copy of the range would give each part a second time");
    constexpr ViewMutChunks(ViewMutChunks&&) noexcept = default;
    constexpr ViewMutChunks& operator=(ViewMutChunks&&) noexcept = default;
    ~ViewMutChunks() = default;

    // The count of the parts that next() still gives.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return rest_ / width_ + (rest_ % width_ != 0 ? 1 : 0); }

    // The next part, of width elements, or of the rest when fewer are left,
    // or no value after the last part.  Complexity: O(1).
    [[nodiscard]] constexpr Option<ViewMut<T, Brand>> next() & noexcept {
        if (rest_ == 0) return Option<ViewMut<T, Brand>>{none};
        std::size_t const count = rest_ < width_ ? rest_ : width_;
        T* const first = next_;
        next_ += count;
        rest_ -= count;
        return Option<ViewMut<T, Brand>>::some(ViewMut<T, Brand>{RegionFrameKeyDoor::key_(), View<T>{first, count}});
    }
};

// The parts [0, offset) and [offset, size()) of the run of whole, by the
// split, or no value when the offset is past the count.  Each part has a
// dynamic extent and the brand of whole.  Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr Option<Record<ViewMut<T, Brand>, ViewMut<T, Brand>>> split_at(ViewMut<T, Brand, Extent>&& whole,
                                                                                      std::size_t offset) noexcept {
    using Parts = Record<ViewMut<T, Brand>, ViewMut<T, Brand>>;
    View<T> const run{detail::run_of_(whole)};
    std::size_t const count = run.size();
    if (offset > count) [[unlikely]] {
        return Option<Parts>{none};
    }
    // The test above puts each part inside the run, so the optimizer removes
    // the test of each window.
    return Option<Parts>::some(
        Parts{ViewMut<T, Brand>{RegionFrameKeyDoor::key_(), run.window(0, offset).expect("the offset is in the run")},
              ViewMut<T, Brand>{RegionFrameKeyDoor::key_(),
                                run.window(offset, count - offset).expect("the offset is in the run")}});
}

// The parts [0, Offset) and [Offset, Extent) of the run of a fixed extent of
// whole, by the split.  The type proves the offset inside the run, so the
// split has no check, and each part has a fixed extent and the brand of
// whole.  Complexity: O(1).
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires(Extent != dynamic_extent && Offset > 0 && Offset < Extent)
[[nodiscard]] constexpr Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>
split_at(ViewMut<T, Brand, Extent>&& whole) noexcept {
    View<T, Extent> const run = detail::run_of_(whole);
    return Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>{
        ViewMut<T, Brand, Offset>{RegionFrameKeyDoor::key_(), run.template prefix<Offset>()},
        ViewMut<T, Brand, Extent - Offset>{RegionFrameKeyDoor::key_(), run.template suffix<Extent - Offset>()}};
}

// The range of the parts of width elements of the run of whole, from the
// first element, and a last part of the size() % width elements when that
// count is not zero, by the split.  Each part has a dynamic extent and the
// brand of whole.  A width of zero ends the process.  Complexity: O(1), and
// O(1) for each part.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ViewMutChunks<T, Brand> split_chunks(ViewMut<T, Brand, Extent>&& whole,
                                                             std::size_t width) noexcept {
    if (width == 0) [[unlikely]] {
        fatal("a split of a ViewMut into chunks of zero elements");
    }
    return ViewMutChunks<T, Brand>{RegionFrameKeyDoor::key_(), View<T>{detail::run_of_(whole)}, width};
}

// The count elements of the run of whole from offset, as a ViewMut of a
// dynamic extent and the brand of whole, or no value when they do not fit, as
// window(offset, count) of a View.  The door takes whole by move, as a split
// does, so the window is the one writer of its elements, and no name of the
// caller writes the rest of the run.  A body that writes more of the run
// after the window gives the run to the window through reborrow.
// Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr Option<ViewMut<T, Brand>> take_window(ViewMut<T, Brand, Extent>&& whole, std::size_t offset,
                                                              std::size_t count) noexcept {
    Option<View<T>> part = View<T>{detail::run_of_(whole)}.window(offset, count);
    if (part.is_none()) [[unlikely]] {
        return Option<ViewMut<T, Brand>>{none};
    }
    return Option<ViewMut<T, Brand>>::some(
        ViewMut<T, Brand>{RegionFrameKeyDoor::key_(), std::move(part).expect("the test above")});
}

// The Count elements of the run of whole from offset, as a ViewMut of the
// fixed extent Count and the brand of whole, or no value when they do not
// fit, as window<Count>(offset) of a View.  The door takes whole by move, as
// the form above does.  Complexity: O(1).
template <std::size_t Count, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires ViewExtent<T, Count> && (Count != dynamic_extent)
[[nodiscard]] constexpr Option<ViewMut<T, Brand, Count>> take_window(ViewMut<T, Brand, Extent>&& whole,
                                                                     std::size_t offset) noexcept {
    Option<View<T, Count>> part = detail::run_of_(whole).template window<Count>(offset);
    if (part.is_none()) [[unlikely]] {
        return Option<ViewMut<T, Brand, Count>>{none};
    }
    return Option<ViewMut<T, Brand, Count>>::some(
        ViewMut<T, Brand, Count>{RegionFrameKeyDoor::key_(), std::move(part).expect("the test above")});
}

// Calls body with the ViewMut of each whole part of Width elements of the run
// of whole, from the first element, and gives the ViewMut of the last
// size() % Width elements.  Each part and the rest have the brand of whole.
// The door takes whole by move, as a split does, and body gets each part one
// time, so each part has one writer.  The loop is the loop of chunks<Width>()
// of a View, with no check.  Complexity: one call of body for each whole part.
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class Body>
    requires ViewExtent<T, Width> && (Width != dynamic_extent) && ChunkBody<Body, T, Brand, Width>
[[nodiscard]] constexpr ViewMut<T, Brand> for_each_chunk(ViewMut<T, Brand, Extent>&& whole, Body body) noexcept {
    View<T> const run{detail::run_of_(whole)};
    auto const parts = run.template chunks<Width>();
    for (View<T, Width> const part : parts) {
        body(ViewMut<T, Brand, Width>{RegionFrameKeyDoor::key_(), part});
    }
    return ViewMut<T, Brand>{RegionFrameKeyDoor::key_(), parts.rest()};
}

// The first count elements of the run of whole, or each element when the run
// holds fewer, as a ViewMut of a dynamic extent and the brand of whole: the
// total form of take_window(0, count), with no Option, as prefix_up_to of a
// View.  A door that writes data from outside into the run, such as a read
// of the kernel, takes whole by move and gives back the ViewMut of the part
// that it filled, so that part has one writer.  Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ViewMut<T, Brand> take_prefix_up_to(ViewMut<T, Brand, Extent>&& whole,
                                                            std::size_t count) noexcept {
    return ViewMut<T, Brand>{RegionFrameKeyDoor::key_(), View<T>{detail::run_of_(whole)}.prefix_up_to(count)};
}

// The erased writer of a run: the ViewMut of the same run and extent on
// DefaultBrand, the erased identity of foundation/Brand.h.  The door consumes
// view, and a ViewMut does not copy, so the erased view stays the one writer
// of the run.  A door that takes the runs of many frames erases each run at
// its entry, so its body is one instance for each element type and extent,
// and not one for each frame.
//
// The brand of a run keeps a view of one run out of a door that names the
// brand of another run, and the erased view loses that proof.  It proves no
// difference of two runs (the alias proof of foundation/core/Frame.h), and no
// conversion gives it a brand back.  So a door whose parameter names the brand
// of a run, such as a body of the frame of that run, refuses an erased view
// (the fixture neg_core_view_mut_erased_takes_no_brand).  A caller erases a
// run only after each check that reads its brand: the call of the door checks
// the brand, and the body of the door then erases it.  The constraint refuses
// a view that is on DefaultBrand.  The body of detail::erased_run_mut_ names
// the brand of the result, so the erased brand has one spelling in this door.
// Complexity: O(1).
namespace detail {

template <class T, class Brand, std::size_t Extent>
[[nodiscard]] constexpr auto erased_run_mut_(ViewMut<T, Brand, Extent>&& view) noexcept {
    return ViewMut<T, ::foundation::brand::DefaultBrand, Extent>{RegionFrameKeyDoor::key_(), run_of_(view)};
}

}  // namespace detail

template <class T, ::foundation::brand::IsFreshBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr auto erase_brand(ViewMut<T, Brand, Extent>&& view) noexcept {
    return detail::erased_run_mut_(std::move(view));
}

// The door consumes the one writer, so a named ViewMut that stays with its
// holder gives no writer to it.  A const ViewMut writes nothing, so it gives
// no writer to the door.
template <class T, ::foundation::brand::IsFreshBrand Brand, std::size_t Extent>
void erase_brand(ViewMut<T, Brand, Extent>& view) =
    delete("erase_brand consumes the one writer, and a named ViewMut stays with its holder: pass std::move(view)");
template <class T, ::foundation::brand::IsFreshBrand Brand, std::size_t Extent>
void erase_brand(ViewMut<T, Brand, Extent> const& view) =
    delete("a const ViewMut writes nothing, so it gives no writer to erase_brand");

// Calls body with the ViewMut of each whole part of Width elements of the run
// of first and the View of the part at the same position of second, from the
// first element, and gives the ViewMut of the last size() % Width elements of
// first and the View of the same elements of second.  Each part of first and
// the rest of first have the brand of first.  The door takes first by move,
// as a split does, and body gets each part one time, so each part has one
// writer.  The loop is the loop of detail::for_each_chunk_pair_, so two
// counts that differ end the process before the loop, and the loop has no
// check.  Complexity: one call of body for each whole part.
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class U, class Body>
    requires ViewExtent<T, Width> && ViewExtent<U, Width> && (Width != dynamic_extent)
          && ChunkPairMutBody<Body, T, Brand, U, Width>
[[nodiscard]] CRUCIBLE_INLINE Record<ViewMut<T, Brand>, View<U>>
for_each_chunk_pair(ViewMut<T, Brand, Extent>&& first, View<U> second, Body body) noexcept {
    detail::FrameKey const key = RegionFrameKeyDoor::key_();
    auto lend = [&key, &body](View<T, Width> part, View<U, Width> other) noexcept {
        body(ViewMut<T, Brand, Width>{key, part}, other);
    };
    auto const [first_rest, second_rest] =
        detail::for_each_chunk_pair_<Width>(View<T>{detail::run_of_(first)}, second, lend);
    return Record<ViewMut<T, Brand>, View<U>>{ViewMut<T, Brand>{key, first_rest}, second_rest};
}

// A named ViewMut stays with the caller, so the parts of its split would be
// second writers of its run.  A const ViewMut writes nothing, so it gives no
// writer of a part.  The same holds for a window.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent>&,
              std::size_t) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent> const&,
              std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent>&) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent> const&) =
    delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_chunks(ViewMut<T, Brand, Extent>&,
                  std::size_t) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_chunks(ViewMut<T, Brand, Extent> const&,
                  std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_window(ViewMut<T, Brand, Extent>&, std::size_t,
                 std::size_t) = delete("a window takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_window(ViewMut<T, Brand, Extent> const&, std::size_t,
                 std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a window");
template <std::size_t Count, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_window(ViewMut<T, Brand, Extent>&,
                 std::size_t) = delete("a window takes the ViewMut by move: pass std::move(view)");
template <std::size_t Count, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_window(ViewMut<T, Brand, Extent> const&,
                 std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a window");
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class Body>
void for_each_chunk(ViewMut<T, Brand, Extent>&,
                    Body) = delete("the parts take the ViewMut by move: pass std::move(view)");
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class Body>
void for_each_chunk(ViewMut<T, Brand, Extent> const&,
                    Body) = delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_prefix_up_to(ViewMut<T, Brand, Extent>&,
                       std::size_t) = delete("a prefix takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void take_prefix_up_to(ViewMut<T, Brand, Extent> const&,
                       std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a prefix");
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class U, class Body>
void for_each_chunk_pair(ViewMut<T, Brand, Extent>&, View<U>,
                         Body) = delete("the parts take the ViewMut by move: pass std::move(view)");
template <std::size_t Width, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent, class U, class Body>
void for_each_chunk_pair(ViewMut<T, Brand, Extent> const&, View<U>,
                         Body) = delete("a const ViewMut writes nothing, so it gives no writer of a part");

namespace detail {

// The body of each frame door of an owner of the Region family.  body gets
// the ViewMut of run with the brand of the frame and the extent of run, and
// the door gives held back: alone for a body that gives void, and after the
// result in a Record for each other body.  held lives in the frame of the
// door, and run is the run of held.  The body runs before the Record moves
// held, because a braced list runs its parts in order.  The door gives up
// held here, so the helper takes it as an rvalue.  The door gives the key of
// the frame doors from the door class of its header, so only a frame door
// calls the helper.
template <class Brand, class Owner, class T, std::size_t Extent, class Body>
[[nodiscard]] constexpr frame_return_t<body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Owner>
give_run_back_(FrameKey const& key, Owner&& held, View<T, Extent> run, Body&& body) noexcept {
    using Given = body_result_t<Body&&, ViewMut<T, Brand, Extent>>;
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(ViewMut<T, Brand, Extent>{key, run});
        return static_cast<Owner&&>(held);
    } else {
        return frame_return_t<Given, Owner>{static_cast<Body&&>(body)(ViewMut<T, Brand, Extent>{key, run}),
                                            static_cast<Owner&&>(held)};
    }
}

}  // namespace detail

// ── The reborrow ───────────────────────────────────────────────────────
//
// reborrow(std::move(view), body) gives body a ViewMut of the run of view,
// and gives view back after the body: alone for a body that gives void, and
// after the result in a Record for each other body.  The parent moves into
// the frame of the door, so the caller holds no name for it while the body
// runs, as a split takes its parent by move.  So the body holds the one
// writer of the run, and it can give it to a door that takes a ViewMut by
// value, such as fill, copy or write_each.  The result of the body leaves
// the frame under the rule of each frame door (FrameResult of Choice.h).
//
// The ViewMut of the body has the brand of the parent, and not a fresh
// brand.  A const reference to the parent can stay with the caller, and the
// move leaves the parent with its run.  A fresh brand would make the doors
// read the two as two runs, and a copy between them would take the memcpy
// on one run.  One brand takes the memmove, which is correct for each pair.

namespace detail {

// The run of view as a plain View of its extent.  Only the doors of the base
// use it.  A door that holds the ViewMut by move or by value makes the View a
// part of the one writer that it holds.  The shared view of atomic cells of
// foundation/core/AtomicCells.h reads the ViewMut through a const reference,
// because each write of an atomic cell is one atomic operation.  The ViewMut
// proves its run, so the View takes its parts with no check.
template <class T, class Brand, std::size_t Extent>
constexpr View<T, Extent> run_of_(ViewMut<T, Brand, Extent> const& view) noexcept {
    T* const first = first_of_(BaseLayerRead{}, view);
    if constexpr (Extent == dynamic_extent) {
        return View<T>{first, view.size()};
    } else {
        return View<T, Extent>{first};
    }
}

}  // namespace detail

template <class T, class Brand, std::size_t Extent, class Body>
    requires FrameBody<Body, T, Brand, Extent>
[[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>,
                                               ViewMut<T, Brand, Extent>>
reborrow(ViewMut<T, Brand, Extent>&& view, Body&& body) noexcept {
    View<T, Extent> const run = detail::run_of_(view);
    return detail::give_run_back_<Brand>(RegionFrameKeyDoor::key_(), std::move(view), run, static_cast<Body&&>(body));
}

template <class T, class Brand, std::size_t Extent, class Body>
    requires RefusedFrameBody<Body, T, Brand, Extent>
void reborrow(ViewMut<T, Brand, Extent>&&, Body&&) = delete(
    "the result of a reborrow leaves the frame: give a value with no reference, no address and no brand of the frame");

template <class T, class Brand, std::size_t Extent, class Body>
    requires LinearFrameBody<Body, T, Brand, Extent>
void reborrow(ViewMut<T, Brand, Extent>&&, Body&&) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A named ViewMut stays with the caller, so the ViewMut of the body would be
// a second writer of its run.  A const ViewMut writes nothing.
template <class T, class Brand, std::size_t Extent, class Body>
void reborrow(ViewMut<T, Brand, Extent>&,
              Body&&) = delete("a reborrow takes the ViewMut by move: pass std::move(view)");
template <class T, class Brand, std::size_t Extent, class Body>
void reborrow(ViewMut<T, Brand, Extent> const&,
              Body&&) = delete("a const ViewMut writes nothing, so it gives no writer to a body");

// ── The position of a run inside a run ─────────────────────────────────

// The two Views of offset_of: Views of one element type, with or without a
// const on each.
template <class Whole, class Part>
concept SameElement = std::same_as<std::remove_const_t<Whole>, std::remove_const_t<Part>>;

// The position of the first element of part inside whole, or no value when
// the elements of part do not all lie inside whole.  The position counts
// elements, not bytes, so whole.window(position, part.size()) is part.  A
// part that starts between two elements of whole gives no value.  An empty
// part at the end of whole gives the count of whole.  An empty View with no
// address, such as a default View, lies inside each other View with no
// address, at the position zero.
//
// At run time the function compares the two address ranges as integers, so
// two Views of different objects give no value and no undefined compare of
// pointers: O(1).  A constant evaluation reads no address as a word.  It
// compares the first address of part with the address of each position of
// whole: O(whole.size()).  There, an empty part of a different object can
// meet a compare with the address one past the end of an object.  Such a
// compare has no specified result, so such a call does not compile.
template <class Whole, std::size_t WholeExtent, class Part, std::size_t PartExtent>
    requires SameElement<Whole, Part>
[[nodiscard]] constexpr Option<std::size_t> offset_of(View<Whole, WholeExtent> whole,
                                                      View<Part, PartExtent> part) noexcept {
    Whole* const whole_first = first_of_(detail::BaseLayerRead{}, whole);
    Part* const part_first = first_of_(detail::BaseLayerRead{}, part);
    if consteval {
        // A part with an element starts at an element of whole, and only an
        // empty part can start at the end of whole.  The count of whole is
        // at most max_view_count, so the sum does not overflow.
        std::size_t const starts = part.size() == 0 ? whole.size() + 1 : whole.size();
        for (std::size_t position = 0; position < starts; ++position) {
            if (whole_first + position != part_first) continue;
            if (part.size() > whole.size() - position) return Option<std::size_t>{none};
            return Option<std::size_t>::some(position);
        }
        return Option<std::size_t>{none};
    } else {
        std::uintptr_t const whole_start = __builtin_bit_cast(std::uintptr_t, whole_first);
        std::uintptr_t const part_start = __builtin_bit_cast(std::uintptr_t, part_first);
        // The door of a View bounds its bytes by __PTRDIFF_MAX__, and an
        // object ends below the top of the address space, so no sum
        // overflows.
        std::uintptr_t const whole_end = whole_start + whole.size() * sizeof(Whole);
        std::uintptr_t const part_end = part_start + part.size() * sizeof(Part);
        if (part_start < whole_start || part_end > whole_end) return Option<std::size_t>{none};
        std::size_t const bytes = part_start - whole_start;
        if (bytes % sizeof(Whole) != 0) return Option<std::size_t>{none};
        return Option<std::size_t>::some(bytes / sizeof(Whole));
    }
}

// The position of element inside whole, or no value when element is no
// element of whole: the inverse of get(position).  The function gives the
// position of the run of one element at element, so it has the one body
// above, with its test of an address between two elements.  Complexity: O(1)
// at run time, and O(whole.size()) in a constant evaluation.
template <class Whole, std::size_t WholeExtent>
[[nodiscard]] constexpr Option<std::size_t> offset_of(View<Whole, WholeExtent> whole,
                                                      std::type_identity_t<Whole> const& element) noexcept {
    return offset_of(whole, detail::view_of_object_(element));
}

// A temporary, also the result of a conversion, is no element of a View.
template <class Whole, std::size_t WholeExtent>
void offset_of(View<Whole, WholeExtent>, std::type_identity_t<Whole> const&&) =
    delete("a temporary is no element of a View, so it has no position in one");

// How an input shares storage with an output: the answer of classify_overlap
// for two runs, and of the plan of the pointwise walk of
// foundation/core/Pointwise.h for two strided views.
enum class RunOverlap : std::uint8_t {
    // No element of the input is an element of the output, or a run is
    // empty.
    Disjoint = 0,
    // The input names the elements of the output at the same indexes.
    InPlace = 1,
    // The input can name an element of the output at a different index.
    Shared = 2,
};

// How the run of left shares storage with the run of right.  InPlace needs
// the same first element and the same count.  An empty run shares no byte,
// so it is Disjoint from each run.  A View has no brand, so two Views
// prove no difference of their runs (the alias proof of
// foundation/core/Frame.h), and a door that reads one run while a thread
// writes the other asks this function one time, before the threads start.
//
// At run time the function compares the two address ranges as integers, as
// offset_of does, so two Views of different objects give Disjoint and no
// undefined compare of pointers: O(1).  A constant evaluation reads no
// address as a word.  It compares the first address of each run with the
// address of each element of the other run: O(left.size() + right.size()).
template <class Left, std::size_t LeftExtent, class Right, std::size_t RightExtent>
    requires SameElement<Left, Right>
[[nodiscard]] constexpr RunOverlap classify_overlap(View<Left, LeftExtent> left,
                                                    View<Right, RightExtent> right) noexcept {
    if (left.size() == 0 || right.size() == 0) return RunOverlap::Disjoint;
    Left* const left_first = first_of_(detail::BaseLayerRead{}, left);
    Right* const right_first = first_of_(detail::BaseLayerRead{}, right);
    if consteval {
        // Each compared address is an element of its run, so no compare
        // meets the address one past the end of an object.
        if (left_first == right_first) return left.size() == right.size() ? RunOverlap::InPlace : RunOverlap::Shared;
        for (std::size_t position = 1; position < right.size(); ++position) {
            if (right_first + position == left_first) return RunOverlap::Shared;
        }
        for (std::size_t position = 1; position < left.size(); ++position) {
            if (left_first + position == right_first) return RunOverlap::Shared;
        }
        return RunOverlap::Disjoint;
    } else {
        std::uintptr_t const left_start = __builtin_bit_cast(std::uintptr_t, left_first);
        std::uintptr_t const right_start = __builtin_bit_cast(std::uintptr_t, right_first);
        // The door of a View bounds its bytes by __PTRDIFF_MAX__, and an
        // object ends below the top of the address space, so no sum
        // overflows.
        std::uintptr_t const left_end = left_start + left.size() * sizeof(Left);
        std::uintptr_t const right_end = right_start + right.size() * sizeof(Right);
        if (left_end <= right_start || right_end <= left_start) return RunOverlap::Disjoint;
        if (left_start == right_start && left.size() == right.size()) return RunOverlap::InPlace;
        return RunOverlap::Shared;
    }
}

// ── The doors of a foreign run ─────────────────────────────────────────

// The View of the count elements at first, which a foreign ABI gives.  The
// View only reads.  The mint ends the process for a count past
// max_view_count, for a null pointer with a count, and for a pointer that is
// not aligned for T.  It does not prove that the run lives: the caller keeps
// the foreign run alive while it uses the View.  So a call stands at the
// edge of a foreign ABI only, in an opt-out region of the class ABI.  The
// address of a run is no constant of the compile, so the mint runs only at
// run time.  The mint is the one door of View for a foreign run, and View
// names it as a friend.
template <class T>
    requires ForeignElement<T>
View<T const> mint_foreign_view(T const* first, std::size_t count) noexcept {
    if (count != 0 && (__builtin_bit_cast(std::uintptr_t, first) & (alignof(T) - 1)) != 0) [[unlikely]] {
        fatal("mint_foreign_view got a pointer that is not aligned for its element of {} bytes", alignof(T));
    }
    if (count > max_view_count(sizeof(T))) [[unlikely]] {
        fatal("mint_foreign_view got a count past the largest count of a View");
    }
    if (first == nullptr && count != 0) [[unlikely]] {
        fatal("mint_foreign_view got a null pointer with a count");
    }
    return View<T const>{first, count};
}

// The door of mint_foreign_text to the raw constructors of a TextView and a
// ForeignText (the doors of a text of foundation/core/Text.h).  Its members
// are private, and the two forms of mint_foreign_text are its friends.  The
// invariant: the mint of the parts checks the count and the null pointer
// before it calls the door, and the mint of the bytes of a View gives the
// first byte and the count of a View.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ForeignTextDoor final : ::foundation::NoObject<ForeignTextDoor> {
    friend ForeignText mint_foreign_text(char const* first, std::size_t count) noexcept;
    friend ForeignText mint_foreign_text(View<std::byte const> bytes) noexcept;

    [[nodiscard]] static constexpr ForeignText text_(char const* first, std::size_t size) noexcept {
        return ForeignText{TextView{first, size}};
    }

    // The characters of the bytes of a View: one character for each byte.
    // char may name the bytes of each object, and each bit pattern of a byte
    // is a character.
    [[nodiscard]] static ForeignText text_of_bytes_(View<std::byte const> bytes) noexcept {
        std::byte const* const first = first_of_(detail::BaseLayerRead{}, bytes);
        return text_(static_cast<char const*>(static_cast<void const*>(first)), bytes.size());
    }
};

// The ForeignText of the count characters at first, which a foreign ABI
// gives.  The text keeps its source in its type (foundation/core/Text.h).
// The mint ends the process for a count past __PTRDIFF_MAX__ and for a null
// pointer with a count.  As for mint_foreign_view, the caller keeps the run
// alive, and a call stands only in an opt-out region of the class ABI.
[[nodiscard]] inline ForeignText mint_foreign_text(char const* first, std::size_t count) noexcept {
    if (count > max_view_count(1)) [[unlikely]] {
        fatal("mint_foreign_text got a count past the largest count of a TextView");
    }
    if (first == nullptr && count != 0) [[unlikely]] {
        fatal("mint_foreign_text got a null pointer with a count");
    }
    return ForeignTextDoor::text_(first, count);
}

// The ForeignText of the bytes of a View of data from outside the program,
// such as the characters of a name in the image of a file, one character for
// each byte.  The View proves the count and the run, so the mint checks
// nothing, and the text borrows the run as the View does.  So a reader checks
// such characters in place, with no copy.  Complexity: constant.
[[nodiscard]] inline ForeignText mint_foreign_text(View<std::byte const> bytes) noexcept {
    return ForeignTextDoor::text_of_bytes_(bytes);
}

// The address of the first character of text, for a reader of a foreign ABI
// that reads up to the zero, such as a C caller that takes a char const*.
// The CText proves the zero after its last character and no zero before it,
// so the reader reads each character and the zero, and no other byte.  The
// address borrows the owner of the text, and the caller keeps the owner
// alive while the foreign side reads.  A call stands only at the edge of a
// foreign ABI, in an opt-out region of the class ABI.
[[nodiscard]] inline char const* foreign_c_str(CText text) noexcept { return detail::c_text_first_(text); }

// The address of the first element of a view, for a raw loop at the edge of
// the base: a reader of a foreign ABI, the measured loop of a bench, which
// indexes the run as the hardware sees it, or a probe of raw behavior.  The
// address borrows the owner of the run, and it carries no bound.  So the
// caller keeps the owner alive while it reads, reads only the count of the
// view, and stands in an opt-out region of the class ABI, MEASURE or PROBE.
// An empty view gives its address, which no reader reads.
template <class T, std::size_t Extent>
[[nodiscard]] constexpr T* foreign_first(View<T, Extent> view) noexcept {
    return first_of_(detail::BaseLayerRead{}, view);
}

// The address of the first element of the run of a frame, for the same raw
// loops and in the same opt-out regions as the form above, such as a foreign
// entry that writes the run.  The door reads the ViewMut through a const
// reference, so the frame keeps its one writer, and the address lives only
// while the frame lends the run.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr T* foreign_first(ViewMut<T, Brand, Extent> const& view) noexcept {
    return first_of_(detail::BaseLayerRead{}, view);
}

namespace detail {

// The root stages of the iteration verbs give the walks of the stages of
// foundation/core/Walk.h.  Each stage comes only from a View, so it holds a
// run that a View proves.  The cursors of the stages obey the rule of a
// ViewCursor: each cursor knows the end of its loop, a read or a step at the
// end ends the process in each build, and the optimizer removes the two
// checks in a loop that compares the cursor with the end first.  A cursor
// comes only from its stage, and the seal refuses std::bit_cast and a
// lifetime start over bytes.

// The position of a reverse loop: the place after the next element to read,
// and the first element of the View, which is the end of the loop.
template <class Element>
class ReverseCursor {
    Element* after_ = nullptr;
    Element* first_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ReverseStage<Element>;

    constexpr ReverseCursor(Element* after, Element* first) noexcept : after_{after}, first_{first} {}

public:
    [[nodiscard]] constexpr Element& operator*() const noexcept {
        if (after_ == first_) [[unlikely]] {
            fatal("a read through the end cursor of a reverse loop over a View");
        }
        return *(after_ - 1);
    }

    constexpr ReverseCursor& operator++() noexcept {
        if (after_ == first_) [[unlikely]] {
            fatal("a step past the end cursor of a reverse loop over a View");
        }
        --after_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ReverseCursor const& left, ReverseCursor const& right) noexcept {
        return left.after_ == right.after_;
    }
};

// The root stage of reverse(): the elements of a View, from the last
// element.  The loop is the backward loop of a pointer.
template <class Element>
class ReverseStage final {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

public:
    using element_reference = Element&;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ReverseStage(View<Element> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        for (Element* after = first + elements_.size(); after != first; --after) {
            sink(after[-1]);
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        for (Element* after = first + elements_.size(); after != first; --after) {
            if (sink(after[-1]) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return elements_.size(); }

    // The element at position from the last element, which the caller proves
    // below size_() (the stages of foundation/core/Walk.h).
    [[nodiscard]] constexpr Element& at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return first_()[elements_.size() - 1 - position];
    }

    // The last count elements, or each element when count is larger.
    [[nodiscard]] constexpr ReverseStage limited_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const kept = count < size ? count : size;
        return ReverseStage{View<Element>{first_() + (size - kept), kept}};
    }

    // The elements before the last count, or none when count is larger.
    [[nodiscard]] constexpr ReverseStage skipped_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const dropped = count < size ? count : size;
        return ReverseStage{View<Element>{first_(), size - dropped}};
    }

    [[nodiscard]] constexpr ReverseCursor<Element> begin_() const noexcept {
        Element* const first = first_();
        return ReverseCursor<Element>{first + elements_.size(), first};
    }
    [[nodiscard]] constexpr ReverseCursor<Element> end_() const noexcept {
        Element* const first = first_();
        return ReverseCursor<Element>{first, first};
    }
};

// The position of a loop over the windows of a View: the first element of
// the next window, and the place after the first element of the last
// window, which is the end of the loop.
template <class Element, std::size_t Width>
class WindowCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class WindowStage<Element, Width>;

    constexpr WindowCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the windows of a View");
        }
        return View<Element, Width>{at_};
    }

    constexpr WindowCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the windows of a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(WindowCursor const& left, WindowCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The root stage of windows<Width>(): each run of Width consecutive elements
// of a View, with a step of one.  A View of size n holds n - Width + 1 runs,
// and a View of fewer than Width elements holds no run.
template <class Element, std::size_t Width>
class WindowStage final {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

    [[nodiscard]] constexpr std::size_t runs_() const noexcept {
        std::size_t const size = elements_.size();
        return size < Width ? 0 : size - Width + 1;
    }

    // The place after the first element of the last run.  The select of two
    // places is the code of the raw loop, where a select of two counts and an
    // add gave the probe view_windows_sum one more instruction.
    [[nodiscard]] constexpr Element* end_of_runs_() const noexcept {
        Element* const first = first_();
        std::size_t const size = elements_.size();
        return size < Width ? first : first + (size - Width + 1);
    }

public:
    using element_reference = View<Element, Width>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit WindowStage(View<Element> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const end = end_of_runs_();
        for (Element* at = first_(); at != end; ++at) {
            sink(View<Element, Width>{at});
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const end = end_of_runs_();
        for (Element* at = first_(); at != end; ++at) {
            if (sink(View<Element, Width>{at}) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return runs_(); }

    // The run at position, which the caller proves below size_() (the stages
    // of foundation/core/Walk.h).
    [[nodiscard]] constexpr View<Element, Width> at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return View<Element, Width>{first_() + position};
    }

    // The first count runs, or each run when count is larger.
    [[nodiscard]] constexpr WindowStage limited_(std::size_t count) && noexcept {
        std::size_t const runs = runs_();
        std::size_t const kept = count < runs ? count : runs;
        return WindowStage{View<Element>{first_(), kept == 0 ? 0 : kept + Width - 1}};
    }

    // The runs after the first count, or none when count is larger.
    [[nodiscard]] constexpr WindowStage skipped_(std::size_t count) && noexcept {
        std::size_t const runs = runs_();
        std::size_t const dropped = count < runs ? count : runs;
        return WindowStage{View<Element>{first_() + dropped, elements_.size() - dropped}};
    }

    [[nodiscard]] constexpr WindowCursor<Element, Width> begin_() const noexcept {
        return WindowCursor<Element, Width>{first_(), end_of_runs_()};
    }
    [[nodiscard]] constexpr WindowCursor<Element, Width> end_() const noexcept {
        Element* const end = end_of_runs_();
        return WindowCursor<Element, Width>{end, end};
    }
};

// The position of a loop over the chunks of a View: the first element of
// the next chunk, and the first element after the last whole chunk, which
// is the end of the loop.
template <class Element, std::size_t Width>
class ChunkCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ChunkStage<Element, Width>;

    constexpr ChunkCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the chunks of a View");
        }
        return View<Element, Width>{at_};
    }

    constexpr ChunkCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the chunks of a View");
        }
        at_ += Width;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ChunkCursor const& left, ChunkCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The root stage of chunks<Width>(): each run of Width elements of a View,
// with a step of Width, and the rest: the elements after the last run of the
// walk.  The stage holds the elements from its first run to the end of the
// View, and the count of its runs.  A chunk walk of a View has size() /
// Width runs, so its rest is the last size() % Width elements.  A limit
// keeps the run of the View and gives fewer runs, so its rest grows.  A skip
// drops the first runs, and the rest stays.
template <class Element, std::size_t Width>
class ChunkStage final {
    View<Element> elements_;
    std::size_t runs_ = 0;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

    constexpr ChunkStage(View<Element> elements, std::size_t runs) noexcept : elements_{elements}, runs_{runs} {}

public:
    using element_reference = View<Element, Width>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ChunkStage(View<Element> elements) noexcept
        : elements_{elements}, runs_{elements.size() / Width} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        Element* const end = first + runs_ * Width;
        for (Element* at = first; at != end; at += Width) {
            sink(View<Element, Width>{at});
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        Element* const end = first + runs_ * Width;
        for (Element* at = first; at != end; at += Width) {
            if (sink(View<Element, Width>{at}) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return runs_; }

    // The run at position, which the caller proves below size_() (the stages
    // of foundation/core/Walk.h).
    [[nodiscard]] constexpr View<Element, Width> at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return View<Element, Width>{first_() + position * Width};
    }

    // The first count runs, or each run when count is larger.
    [[nodiscard]] constexpr ChunkStage limited_(std::size_t count) && noexcept {
        return ChunkStage{elements_, count < runs_ ? count : runs_};
    }

    // The runs after the first count, or none when count is larger.
    [[nodiscard]] constexpr ChunkStage skipped_(std::size_t count) && noexcept {
        std::size_t const dropped = count < runs_ ? count : runs_;
        std::size_t const offset = dropped * Width;
        return ChunkStage{View<Element>{first_() + offset, elements_.size() - offset}, runs_ - dropped};
    }

    // The View of the elements after the last run of the walk.
    [[nodiscard]] constexpr View<Element> rest_() const noexcept {
        std::size_t const offset = runs_ * Width;
        return View<Element>{first_() + offset, elements_.size() - offset};
    }

    [[nodiscard]] constexpr ChunkCursor<Element, Width> begin_() const noexcept {
        Element* const first = first_();
        return ChunkCursor<Element, Width>{first, first + runs_ * Width};
    }
    [[nodiscard]] constexpr ChunkCursor<Element, Width> end_() const noexcept {
        Element* const end = first_() + runs_ * Width;
        return ChunkCursor<Element, Width>{end, end};
    }
};

// The position of a loop over the elements of a View<Element, Count> with
// their indexes: the first element of the View, the position of the next
// element, and the position of the end of the loop, which is at most Count.
// The read compares the position with the end first, so the Index of each
// step is an index of indices(), and the type of the View proves the read
// below its count.  The position is a full word, also when the Index holds
// a 32-bit word, so the loop needs one loop variable: the position and the
// offset of the element at the same time.  The read makes the Index through
// its check after the compare with the end.  The constructor holds the end
// at most Count, so the optimizer knows the answer of the check and removes
// it.  Each Ref has the brand of the call of enumerate() that made the walk.
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor {
    Element* first_ = nullptr;
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class Other, std::size_t OtherCount, ::foundation::brand::IsBrand OtherBrand, std::size_t Extent>
    friend class EnumerateStage;

    // The cursor at the first element of run, which starts at the position
    // start of the View.  The loop ends after the last element of run.  The
    // stage holds the end of each run at most Count, and the bound in the
    // code shows that fact to the optimizer.
    constexpr EnumerateCursor(View<Element> run, std::size_t start) noexcept
        : first_{first_of_(BaseLayerRead{}, run) - start},
          at_{start},
          end_{start + run.size() < Count ? start + run.size() : Count} {}

public:
    [[nodiscard]] constexpr Record<Index<Count>, Ref<Element, Brand>> operator*() const noexcept {
        if (at_ >= end_) [[unlikely]] {
            fatal("a read through the end cursor of an enumerate loop over a View");
        }
        return {Index<Count>::check(at_).expect("the position of an enumerate cursor lies below its count"),
                RegionRefDoor::lend_<Brand>(first_[at_])};
    }

    constexpr EnumerateCursor& operator++() noexcept {
        if (at_ >= end_) [[unlikely]] {
            fatal("a step past the end cursor of an enumerate loop over a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(EnumerateCursor const& left, EnumerateCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The root stage of enumerate(): the elements of a View of the fixed extent
// Count, each with its index.  The stage holds the run that it walks and the
// position of its first element in the View, so a limit or a skip keeps the
// index of each element.  The root has the extent Count, and a limit or a
// skip gives a dynamic extent.  The run ends at or before the position Count,
// so each index of a pair is below Count.  The constructor from a whole View
// of Count elements is private, and RegionRefDoor builds the root stage only
// for the enumerate() verbs, with the fresh brand of their call.  So no
// caller names the brand of a Ref of the stage.  Only a limit or a skip of a
// stage builds a run from a later position.
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
class EnumerateStage final {
    View<Element, Extent> run_;
    std::size_t start_ = 0;

    template <class Other, std::size_t OtherCount, ::foundation::brand::IsBrand OtherBrand, std::size_t OtherExtent>
    friend class EnumerateStage;
    friend class ::foundation::core::RegionRefDoor;

    // Each element of a View of Count elements, from the position 0.
    constexpr explicit EnumerateStage(View<Element, Count> elements) noexcept
        requires(Extent == Count)
        : run_{elements} {}

    // The run from the position start of a View of Count elements.  The
    // caller proves start + run.size() at most Count.
    constexpr EnumerateStage(View<Element, Extent> run, std::size_t start) noexcept : run_{run}, start_{start} {}

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, run_); }

    // The position of the first element of the run in the View.  The root
    // stage, of the extent Count, starts at the position 0.
    [[nodiscard]] constexpr std::size_t start_of_() const noexcept { return Extent == Count ? 0 : start_; }

    // The position after the last element of the run.  The invariant of the
    // stage holds it at most Count, and the bound in the code shows that fact
    // to the optimizer, so the check of each Index below it goes away.
    [[nodiscard]] constexpr std::size_t end_of_() const noexcept {
        std::size_t const end = start_of_() + run_.size();
        return end < Count ? end : Count;
    }

    // The element at the position at of the View with its index.  origin is
    // the first element of the View, and the caller compares at with
    // end_of_() first.
    [[nodiscard]] static constexpr Record<Index<Count>, Ref<Element, Brand>> pair_(Element* origin,
                                                                                   std::size_t at) noexcept {
        return {Index<Count>::check(at).expect("an enumerate position lies below the count of its View"),
                RegionRefDoor::lend_<Brand>(origin[at])};
    }

public:
    using element_reference = Record<Index<Count>, Ref<Element, Brand>>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = Extent;
    static constexpr bool cursor_borrows_stage = false;

    // The loops run over the positions of the run in the View, so the one
    // loop variable is the index and the offset of the element.
    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        std::size_t const start = start_of_();
        Element* const origin = first_() - start;
        std::size_t const end = end_of_();
        for (std::size_t at = start; at < end; ++at) {
            sink(pair_(origin, at));
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        std::size_t const start = start_of_();
        Element* const origin = first_() - start;
        std::size_t const end = end_of_();
        for (std::size_t at = start; at < end; ++at) {
            if (sink(pair_(origin, at)) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return run_.size(); }

    // The element at position with its index, which the caller proves below
    // size_() (the stages of foundation/core/Walk.h).  A zip compares the
    // position with its own count, so the check of the Index stays for a run
    // that starts after the position 0.
    [[nodiscard]] constexpr element_reference at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        std::size_t const start = start_of_();
        return pair_(first_() - start, start + position);
    }

    // The first count elements, or each element when count is larger.
    [[nodiscard]] constexpr auto limited_(std::size_t count) && noexcept {
        std::size_t const size = run_.size();
        return EnumerateStage<Element, Count, Brand, dynamic_extent>{
            View<Element>{first_(), count < size ? count : size}, start_};
    }

    // The elements after the first count, or none when count is larger.
    [[nodiscard]] constexpr auto skipped_(std::size_t count) && noexcept {
        std::size_t const size = run_.size();
        std::size_t const dropped = count < size ? count : size;
        return EnumerateStage<Element, Count, Brand, dynamic_extent>{View<Element>{first_() + dropped, size - dropped},
                                                                     start_ + dropped};
    }

    // The cursor reads the View from its first element, at the positions of
    // this run.  The end cursor is the cursor of the empty run after it.
    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> begin_() const noexcept {
        return EnumerateCursor<Element, Count, Brand>{View<Element>{run_}, start_};
    }
    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> end_() const noexcept {
        std::size_t const size = run_.size();
        return EnumerateCursor<Element, Count, Brand>{View<Element>{first_() + size, 0}, start_ + size};
    }
};

// The largest copy of a fixed extent, in bytes, that copy() does with the
// words of copy_small_.  GCC expands a memmove builtin of a constant size
// inline only where it optimizes for speed.  It calls the library memmove
// in a block that it thinks cold, and under AddressSanitizer for most
// sizes.  The words of copy_small_ give no call in each block of each build.
inline constexpr std::size_t small_copy_bytes = 64;

// The word of Width bytes, for one load of copy_small_: an unsigned integer,
// or a vector of bytes for 16 bytes or more.
template <std::size_t Width>
struct CopyWord;
template <>
struct CopyWord<1> {
    using type = std::uint8_t;
};
template <>
struct CopyWord<2> {
    using type = std::uint16_t;
};
template <>
struct CopyWord<4> {
    using type = std::uint32_t;
};
template <>
struct CopyWord<8> {
    using type = std::uint64_t;
};
template <>
struct CopyWord<16> {
    using type [[gnu::vector_size(16)]] = std::uint8_t;
};
template <>
struct CopyWord<32> {
    using type [[gnu::vector_size(32)]] = std::uint8_t;
};
template <>
struct CopyWord<64> {
    using type [[gnu::vector_size(64)]] = std::uint8_t;
};

// The width of the tail word of a rest of bytes: the smallest power of two
// that is at least bytes.
[[nodiscard]] consteval std::size_t copy_tail_width_(std::size_t bytes) noexcept {
    std::size_t width = 1;
    while (width < bytes) {
        width *= 2;
    }
    return width;
}

// The words of copy_small_ follow the vector width of the target, so they
// are in the target namespace (the target namespace of foundation/Platform.h).
inline namespace CRUCIBLE_SIMD_TARGET {

// The width of the widest word of copy_small_: the width of a vector
// register of the target.
#if defined(__AVX512F__)
inline constexpr std::size_t widest_copy_word = 64;
#elif defined(__AVX__)
inline constexpr std::size_t widest_copy_word = 32;
#else
inline constexpr std::size_t widest_copy_word = 16;
#endif

// The width of the first word of a copy of bytes: the largest power of two
// that is at most bytes and at most widest_copy_word.
[[nodiscard]] consteval std::size_t copy_word_width_(std::size_t bytes) noexcept {
    std::size_t width = widest_copy_word;
    while (width > bytes) {
        width /= 2;
    }
    return width;
}

// The word of Width bytes at from.  The size of the builtin is the size of
// a local word, so GCC folds the builtin to one load in each block.
template <std::size_t Width>
[[nodiscard]] inline typename CopyWord<Width>::type load_copy_word_(unsigned char const* from) noexcept {
    typename CopyWord<Width>::type word{};
    __builtin_memcpy(&word, from, Width);
    return word;
}

// Writes the Width bytes of word at to.  GCC folds the builtin to one store.
template <std::size_t Width>
inline void store_copy_word_(unsigned char* to, typename CopyWord<Width>::type word) noexcept {
    __builtin_memcpy(to, &word, Width);
}

// Copies Bytes bytes from source to target with words.  The copy loads the
// first word.  When one more word of its width fits after it, the copy
// copies the rest with this function.  Else a tail word ends at the end of
// the run, and it can overlap the first word.  The copy stores the first
// word last, so each load comes before each store and the two runs can
// overlap.
//
// An overlap of two words writes some bytes two times, with the same value
// each time.  The words are the words of the inline memmove of GCC for the
// same size.
template <std::size_t Bytes>
    requires(Bytes > 0 && Bytes <= small_copy_bytes)
inline void copy_small_(unsigned char* target, unsigned char const* source) noexcept {
    constexpr std::size_t width = copy_word_width_(Bytes);
    auto const first = load_copy_word_<width>(source);
    if constexpr (Bytes >= 2 * width) {
        copy_small_<Bytes - width>(target + width, source + width);
    } else if constexpr (Bytes > width) {
        constexpr std::size_t tail_width = copy_tail_width_(Bytes - width);
        auto const tail = load_copy_word_<tail_width>(source + (Bytes - tail_width));
        store_copy_word_<tail_width>(target + (Bytes - tail_width), tail);
    }
    store_copy_word_<width>(target, first);
}

}  // namespace CRUCIBLE_SIMD_TARGET
}  // namespace detail

// The copy of a fixed extent calls copy_small_, so it is in the target
// namespace too.
inline namespace CRUCIBLE_SIMD_TARGET {

// Copies the elements of src into dst, two Views of one fixed extent.  The
// type gives the two the same count, so the copy cannot fail.  Two Views of
// one run can overlap, because a View<T> converts to a View<T const> of the
// same run.  So the copy gives dst the values that src held before the
// copy.  A copy of at most detail::small_copy_bytes takes the words of
// detail::copy_small_, so a copy of a record calls no library function in
// each build.  A larger copy is one memmove builtin.  On x86_64, glibc
// resolves memcpy and memmove to one function.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T> && (Extent != dynamic_extent)
void copy(View<T, Extent> dst, std::type_identity_t<View<T const, Extent>> src) noexcept {
    constexpr std::size_t bytes = Extent * sizeof(T);
    void* const target = first_of_(detail::BaseLayerRead{}, dst);
    void const* const source = first_of_(detail::BaseLayerRead{}, src);
    if constexpr (bytes <= detail::small_copy_bytes) {
        detail::copy_small_<bytes>(static_cast<unsigned char*>(target), static_cast<unsigned char const*>(source));
    } else {
        __builtin_memmove(target, source, bytes);
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

// Copies the elements of src into dst, two Views of a dynamic extent.  The
// counts can differ, so the copy gives a Result: LengthMismatch when they
// differ, and the copy then writes nothing.  A window of the count of src
// has that count, so the optimizer removes the test of a copy into it.
template <class T>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(View<T> dst, std::type_identity_t<View<T const>> src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    __builtin_memmove(first_of_(detail::BaseLayerRead{}, dst), first_of_(detail::BaseLayerRead{}, src),
                      dst.size() * sizeof(T));
    return Unit{};
}

// ── The doors of a kernel ──────────────────────────────────────────────
//
// A door of a kernel writes the run of a ViewMut and reads its inputs.  An
// input is a ViewMut through a const reference, which keeps the brand of its
// frame, or a View, which has no brand.  When the brand
// of an input differs from the brand of the output, the frames of the two
// hold two owners, and the two runs do not overlap (the alias proof of
// foundation/core/Frame.h).  When each input proves that, the door gives
// the compiler restrict pointers, so the loop needs no run-time alias check.
// Each other input takes the loop of plain pointers, which the compiler
// guards with an alias check, or the memmove of a copy.  The door takes the
// output by value, as the decision on the alias proof states.

// An input of a door of a kernel: a ViewMut or a View.  The door reads each
// input through a pointer to a const element.
template <class In>
concept KernelInput =
    ::foundation::reflect::IsInstanceOf<In, ^^ViewMut> || ::foundation::reflect::IsInstanceOf<In, ^^View>;

// An input whose run does not overlap the run of an output of the brand
// Brand: a ViewMut of a different brand (the alias proof of Frame.h).
template <class In, class Brand>
concept DisjointInput =
    ::foundation::reflect::IsInstanceOf<In, ^^ViewMut> && !std::same_as<typename In::brand_type, Brand>;

// The calls of write_each that take the restrict loop: each input is a
// ViewMut of a brand other than Brand, and no element of an input or of the
// output holds a borrow.  An element that holds a borrow, such as a View
// that the caller made before the frame of the output, could name the run of
// the output, and the op could read that run through it while the loop
// writes it.  So such an element takes the loop of plain pointers.
template <class T, class Brand, class... Inputs>
concept RestrictKernel = (DisjointInput<Inputs, Brand> && ...)
                      && HoldsNoBorrow<T> && (HoldsNoBorrow<std::remove_const_t<typename Inputs::element_type>> && ...);

// The op of write_each for an element T and the inputs: an object with no
// state, which takes one element of each input by const reference and gives
// a value that converts to T, with no throw.  An op with no state holds no
// capture, so it reaches no run of the call through a capture, and
// RestrictKernel keeps a borrow in an element out of the restrict loop.  The
// proof assumes that the op reaches no run through an object of static
// storage duration either.
template <class Op, class T, class... Inputs>
concept KernelOp = std::is_empty_v<Op> && std::is_nothrow_copy_constructible_v<Op> && (KernelInput<Inputs> && ...)
                && std::is_nothrow_invocable_r_v<T, Op const&, typename Inputs::element_type const&...>
                && ::foundation::linear::HoldsNoLinearValue<Op>;

namespace detail {

// The loop of write_each when no input overlaps the output.  Each pointer is
// a restrict parameter, and GCC keeps the restrict of a parameter of an
// inlined function, so the vectorized loop has no alias check.  Complexity:
// O(count).
template <class T, class Op, class... Elements>
[[gnu::always_inline]] inline void write_each_disjoint_(T* __restrict out, std::size_t count, Op const& op,
                                                        Elements const* __restrict... inputs) noexcept {
    for (std::size_t position = 0; position != count; ++position) {
        out[position] = op(inputs[position]...);
    }
}

// The loop of write_each when an input can overlap the output, such as an
// input of the brand of the output or a View.  The loop reads each input at
// a position before it writes the output at that position.  Complexity:
// O(count).
template <class T, class Op, class... Elements>
[[gnu::always_inline]] inline void write_each_aliased_(T* out, std::size_t count, Op const& op,
                                                       Elements const*... inputs) noexcept {
    for (std::size_t position = 0; position != count; ++position) {
        out[position] = op(inputs[position]...);
    }
}

}  // namespace detail

// Copies the elements of src into dst, a ViewMut and the ViewMut of a frame.
// Two brands prove that the runs do not overlap, so the copy is one memcpy.
// One brand proves no difference, so the copy is the memmove of
// copy(View, View).  The counts can differ, so the copy gives a Result, with
// LengthMismatch when they differ, and the copy then writes nothing.
template <class T, class Brand, std::size_t Extent, class SourceBrand, std::size_t SourceExtent>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(ViewMut<T, Brand, Extent> dst,
                                                ViewMut<T, SourceBrand, SourceExtent> const& src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    void* const target = first_of_(detail::BaseLayerRead{}, dst);
    void const* const source = first_of_(detail::BaseLayerRead{}, src);
    if constexpr (std::is_same_v<Brand, SourceBrand>) {
        __builtin_memmove(target, source, dst.size() * sizeof(T));
    } else {
        __builtin_memcpy(target, source, dst.size() * sizeof(T));
    }
    return Unit{};
}

// Copies the elements of a View into a ViewMut.  A View has no brand, so the
// copy is one memmove.
template <class T, class Brand, std::size_t Extent>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(ViewMut<T, Brand, Extent> dst,
                                                std::type_identity_t<View<T const>> src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    __builtin_memmove(first_of_(detail::BaseLayerRead{}, dst), first_of_(detail::BaseLayerRead{}, src),
                      dst.size() * sizeof(T));
    return Unit{};
}

// Copies the elements of a View into a ViewMut of the same fixed extent.  The
// type gives the two the same count, so the copy cannot fail, as the copy of
// two Views of one fixed extent.  A View has no brand, so the copy is the
// copy of two Views: the words of a small run, and one memmove for a larger
// run.  The source is a View of T or of T const.
template <class T, class Brand, std::size_t Extent, class Source>
    requires RegionCopyElement<T> && (Extent != dynamic_extent) && std::same_as<std::remove_const_t<Source>, T>
void copy(ViewMut<T, Brand, Extent> dst, View<Source, Extent> src) noexcept {
    View<T const, Extent> const source{src};
    ::foundation::core::copy(detail::run_of_(dst), source);
}

// Writes op(input[i]...) into each element out[i], in the order of i.  Each
// input has the count of out, or the door gives LengthMismatch and writes
// nothing.  When each input is a ViewMut of a brand other than the brand of
// out, and no element holds a borrow (RestrictKernel), the loop reads through
// restrict pointers.  Each other call takes the loop of plain pointers.  The
// two loops give the same values, because the
// loop of plain pointers reads each input at a position before it writes out
// at that position.  Complexity: O(out.size()).
template <class T, class Brand, std::size_t Extent, class Op, class... Inputs>
    requires KernelOp<Op, T, Inputs...>
[[nodiscard]] Result<Unit, LengthMismatch> write_each(ViewMut<T, Brand, Extent> out, Op op,
                                                      Inputs const&... inputs) noexcept {
    std::size_t const count = out.size();
    if (((inputs.size() != count) || ...)) return err(LengthMismatch{});
    T* const target = first_of_(detail::BaseLayerRead{}, out);
    if constexpr (RestrictKernel<T, Brand, Inputs...>) {
        detail::write_each_disjoint_(target, count, op, first_of_(detail::BaseLayerRead{}, inputs)...);
    } else {
        detail::write_each_aliased_(target, count, op, first_of_(detail::BaseLayerRead{}, inputs)...);
    }
    return Unit{};
}

// An op that holds a linear value by value binds to this overload, so the
// refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).  Such
// an op does not copy either, which KernelOp needs too.
template <class T, class Brand, std::size_t Extent, class Op, class... Inputs>
    requires(!::foundation::linear::HoldsNoLinearValue<Op>)
             && std::is_invocable_r_v<T, Op const&, typename Inputs::element_type const&...>
void write_each(ViewMut<T, Brand, Extent> out, Op op, Inputs const&... inputs) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

namespace detail {

// An element that fill() writes with one memset: an element of one byte
// that decode reads as one byte.  decode refuses a padding bit and a
// bit-field.  An empty class has one byte of padding and no value, so the
// guard also refuses it by name.
template <class T>
concept ByteFillElement = sizeof(T) == 1 && !std::is_empty_v<T> && DecodableFrom<std::uint8_t, T>;

}  // namespace detail

// Writes value into each element of dst.  For an element of one byte, the
// fill is one memset of the byte of value.  GCC gives the memset builtin
// the rule nonnull-if-nonzero, so an empty View with a null pointer needs
// no test.  A constant evaluation and each other element keep the loop.
// The element is trivially copyable, and its byte is a value of the
// element, so the write of the byte is an assignment.  -Wclass-memaccess
// refuses a memset through a pointer to a class with a member initializer,
// so the call gives the builtin a void*.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T>
constexpr void fill(View<T, Extent> dst, std::type_identity_t<T> const& value) noexcept {
    if constexpr (detail::ByteFillElement<T>) {
        if !consteval {
            std::uint8_t const byte = decode<std::uint8_t>(value).expect("decode gives a byte for each value");
            __builtin_memset(static_cast<void*>(first_of_(detail::BaseLayerRead{}, dst)), byte, dst.size());
            return;
        }
    }
    for (T& element : dst) {
        element = value;
    }
}

// Writes value into each element of the run of dst, as fill(View, value)
// does.  The door takes the ViewMut by value, as copy and write_each do, so
// a body that writes more after the fill gives it through reborrow.
template <class T, class Brand, std::size_t Extent>
    requires RegionCopyElement<T>
constexpr void fill(ViewMut<T, Brand, Extent> dst, std::type_identity_t<T> const& value) noexcept {
    ::foundation::core::fill(detail::run_of_(dst), value);
}

// The bytes of a run of unsigned chars, as a View of std::byte of the same
// count and extent.  A source of unsigned chars, such as the input of a fuzz
// entry or the storage of a tensor, then reaches a reader or a door of
// std::byte.  A std::byte may read and write each object, so the View reads
// and writes the bytes of the run.  A run of unsigned char const gives bytes
// of std::byte const, so the const stays.  The cast of the address is no
// constant expression, so the operation is not constexpr.  Complexity: O(1).
template <class T, std::size_t Extent>
    requires std::same_as<std::remove_const_t<T>, unsigned char>
[[nodiscard]] View<std::conditional_t<std::is_const_v<T>, std::byte const, std::byte>, Extent>
byte_view(View<T, Extent> run) noexcept {
    using Byte = std::conditional_t<std::is_const_v<T>, std::byte const, std::byte>;
    using Untyped = std::conditional_t<std::is_const_v<T>, void const*, void*>;
    Byte* const first = static_cast<Byte*>(static_cast<Untyped>(first_of_(detail::BaseLayerRead{}, run)));
    if constexpr (Extent == dynamic_extent) {
        return View<Byte>{first, run.size()};
    } else {
        return View<Byte, Extent>{first};
    }
}

// The bytes of the run of a frame of unsigned chars, as the ViewMut of
// std::byte of the same count, extent and brand.  The door takes the ViewMut
// by move, as a split does, so the bytes are the one writer of the run.  A
// named ViewMut and a const one are refused below.  Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires std::same_as<T, unsigned char>
[[nodiscard]] ViewMut<std::byte, Brand, Extent> byte_view(ViewMut<T, Brand, Extent>&& run) noexcept {
    return ViewMut<std::byte, Brand, Extent>{RegionFrameKeyDoor::key_(), byte_view(detail::run_of_(run))};
}

template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void byte_view(ViewMut<T, Brand, Extent>&) = delete("the bytes take the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void byte_view(ViewMut<T, Brand, Extent> const&) =
    delete("a const ViewMut writes nothing, so it gives no writer of its bytes: read its view() through byte_view");

}  // namespace foundation::core

namespace foundation::lifetime {

namespace detail {

// The start of count objects of type T over the storage, with no check: the
// one asm statement of each start.  It gives the address of the first object,
// and no View, so it makes no View of any run.  The caller proves the three
// facts that start_as_array of foundation/core/Array.h checks: the storage is
// aligned for T, the count is at most max_view_count(sizeof(T)), and the
// storage is not null when the count is above zero.
//
// The function gives the guarantee of std::start_lifetime_as_array
// ([obj.lifetime], P2590R2).  Each object that it starts has an
// implicit-lifetime type ([basic.types.general]), and the object
// representation of each object is the bytes that the storage holds before
// the call.  The function uses the empty asm statement that libstdc++ 16
// uses for that function (bits/stl_construct.h).  The statement reads and
// writes the bytes of the region, and it returns the pointer.  GCC then
// keeps each store to the bytes before the statement.  It keeps each access
// through the result after the statement.  The type of an earlier object in
// the bytes then does not apply to the new objects.  The statement writes no
// byte and emits no instruction.  The function always inlines (CRUCIBLE_INLINE),
// so each start costs only the checks of its caller.  An out-of-line clone of
// the start is a barrier of its own, because GCC keeps each access across a
// call.  Such a clone would hide a missing statement, and a test of the
// statement would see no start with no statement.
//
// The operand has the type of an array of unknown bound of unsigned char.
// GCC reads that type as an access of unknown length (the GCC manual,
// "Extended Asm"), so the operand needs no variable length array.  The bit
// cast removes a const of the storage only for the operand, as the
// const_cast of libstdc++ does.
template <ImplicitLifetimeThroughout T, typename Storage>
    requires(!std::is_volatile_v<Storage> && ::foundation::core::ViewElement<element_for_storage_t<Storage, T>>)
[[nodiscard]] CRUCIBLE_INLINE element_for_storage_t<Storage, T>* start_storage_(Storage* storage,
                                                                                std::size_t count) noexcept {
    using Element = element_for_storage_t<Storage, T>;
    using Untyped = std::conditional_t<std::is_const_v<Storage>, const void, void>;
    Element* first = static_cast<Element*>(static_cast<Untyped*>(storage));
    if (count != 0) {
        auto* const region = __builtin_bit_cast(unsigned char (*)[], static_cast<Untyped*>(storage));
        __asm__ __volatile__("" : "+r"(first), "=m"(*region) : "m"(*region));
    }
    return first;
}

// The body of start_as_array over a View of bytes: the checks of the start,
// the asm statement, and the View of the objects over the same run.  View
// names it as a friend, so it reaches the raw constructors.
template <ImplicitLifetimeThroughout T, StorageByte Byte, std::size_t Extent>
    requires ::foundation::core::ViewElement<element_for_storage_t<Byte, T>>
          && (Extent == ::foundation::core::dynamic_extent || Extent % sizeof(T) == 0)
CRUCIBLE_INLINE ::foundation::core::View<element_for_storage_t<Byte, T>, started_extent_v<T, Extent>>
objects_over_bytes_(::foundation::core::View<Byte, Extent> bytes) noexcept {
    using Element = element_for_storage_t<Byte, T>;
    Byte* const storage = first_of_(::foundation::core::detail::BaseLayerRead{}, bytes);
    CRUCIBLE_FATAL_INVARIANT((__builtin_bit_cast(std::uintptr_t, storage) & (alignof(Element) - 1)) == 0);
    if constexpr (Extent == ::foundation::core::dynamic_extent) {
        CRUCIBLE_FATAL_INVARIANT(bytes.size() % sizeof(T) == 0);
        std::size_t const count = bytes.size() / sizeof(T);
        return ::foundation::core::View<Element>{start_storage_<T>(storage, count), count};
    } else {
        return ::foundation::core::View<Element, Extent / sizeof(T)>{start_storage_<T>(storage, Extent / sizeof(T))};
    }
}

}  // namespace detail

// Starts the lifetime of the objects of type T that fill the bytes of a View,
// and returns them as a View.  The count of the objects is the count of the
// bytes divided by the size of T, so a caller that starts fewer objects takes
// a window of the bytes first.  The element of the result is const when the
// bytes are const.  The View of the bytes proves their storage, so the start
// makes the View of the objects over the same run, and no other door does.
// The function always inlines, as the asm statement of the start does.
//
// The bytes must be aligned for T, and the start ends the process for bytes
// that are not, in each build and under each contract semantic.  The count of
// a View of bytes is at most max_view_count(1), so the count of objects is at
// most max_view_count(sizeof(T)), and a View holds a pointer that is not null
// for each count above zero: the two other facts of a start need no check.
// Bytes of a fixed extent give a View of the fixed extent Extent / sizeof(T),
// and the type refuses an extent that holds no whole count of objects.  Bytes
// of a dynamic extent give a View of a dynamic extent, and a count of bytes
// that holds no whole count of objects ends the process, in each build and
// under each contract semantic.
template <ImplicitLifetimeThroughout T, StorageByte Byte, std::size_t Extent>
    requires ::foundation::core::ViewElement<element_for_storage_t<Byte, T>>
          && (Extent == ::foundation::core::dynamic_extent || Extent % sizeof(T) == 0)
[[nodiscard]]
CRUCIBLE_INLINE ::foundation::core::View<element_for_storage_t<Byte, T>, detail::started_extent_v<T, Extent>>
start_as_array(::foundation::core::View<Byte, Extent> bytes) noexcept {
    return detail::objects_over_bytes_<T>(bytes);
}

}  // namespace foundation::lifetime
