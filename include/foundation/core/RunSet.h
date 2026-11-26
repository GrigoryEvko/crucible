#pragma once

// The RunSet family: some runs of elements, and a table of their counts.
//
// A RunSet<Count, RunOf<T, Slot>...> holds the first element of each run
// and a small table of counts.  The run at index I has the count at its
// slot, and two runs can share one slot.  So a record with three arrays of
// its inputs and two arrays of its outputs holds five pointers and two
// counts, and no View of 16 bytes for each array.
//
//   RunSet<Count, Runs...>              a set of runs of any owner
//   ArenaRunSet<Count, Brand, Runs...>  a set of runs of the arenas of the
//                                       brand (foundation/core/BumpArena.h)
//
//   count<Slot>()       the count at the slot
//   set_count<Slot>(n)  the count n at the slot.  Each run of the slot drops
//                       its elements, so no run keeps elements of a
//                       different count
//   set<I>(run)         the elements of run for the run at index I.  A RunSet
//                       takes a View, and an ArenaRunSet takes an ArenaRun of
//                       its brand.  A run whose count is not the count of the
//                       slot ends the process in each build
//   has_run<I>()        true when the run holds the count of its slot: the
//                       count is zero, or set<I> gave the run its elements
//   view<I>()           the run at index I as a View of const elements, so
//                       no door of a set writes an element.  A run with no
//                       elements at a count above zero gives the empty View
//
// A run with no elements at a count above zero is a valid state.  A record
// can keep the count of its inputs and hold no array of their metadata.
// has_run<I>() tells that state from a run of zero elements, and view<I>()
// never gives a View of the count over no elements.
//
// The set pairs each first element with its count itself, through the door
// of the base layer.  The only write of a first element takes a run of the
// count of its slot, and the only write of a count drops the runs of its
// slot.  So each View that view<I>() gives holds elements that a run held.
// The seal refuses std::bit_cast and a lifetime start over bytes, as for a
// View, so no bytes become a set with a pointer that no run gave.
//
// The layout: the pointer of each run in the order of the runs, then the
// counts.  A set owns nothing, so its runs dangle when their owner ends.
//
// A RunSet names no owner of its runs.  Each first element of an ArenaRunSet
// states the brand of its set, so a record of such a set holds only bytes of
// an arena of the brand, and its type says so.
//
// A copy of a set refers to the same runs, and a set holds no unique borrow of
// them, so it lends no writing View.  Two writing Views of one run would be two
// writers of one element.  The owner of a run writes its elements, such as an
// ArenaRun, before it gives the run to set<I>.
//
// The first element of each run states refers{} (foundation/Transfer.h), so
// the walk of the transfer facts reads a run of T as a reference to T.  The
// set keeps the address that set<I> took, so the walk does not read the const
// of view<I>() into the property: two threads can read a set of runs of T only
// when two threads can write a T.  A run of const elements gives only const
// elements, also through a copy, so two threads can read a set of such runs
// when two threads can read each element.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Index.h>
#include <foundation/core/Number.h>
#include <foundation/core/Record.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/TypeList.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

// A run of a set: elements of type T, with the count at index Slot of the
// table of counts.
template <class T, std::size_t Slot>
    requires ViewElement<T>
struct RunOf final {
    using element_type = T;
    static constexpr std::size_t slot = Slot;
};

namespace detail {

template <class T>
inline constexpr bool is_run_of_ = false;

template <class T, std::size_t Slot>
inline constexpr bool is_run_of_<RunOf<T, Slot>> = true;

// The brand of the runs of a RunSet, which names no owner of them.
struct RunsOfAnyOwner final {};

// The runs of a set: one run or more, and each a RunOf.
template <class... Runs>
concept RunsOfSet = sizeof...(Runs) > 0 && (is_run_of_<Runs> && ...);

template <UnsignedInteger Count, class Brand, class... Runs>
    requires RunsOfSet<Runs...>
class RunTable_;

}  // namespace detail

template <UnsignedInteger Count, class... Runs>
    requires detail::RunsOfSet<Runs...>
class RunSet;

template <UnsignedInteger Count, ::foundation::brand::IsFreshBrand Brand, class... Runs>
    requires detail::RunsOfSet<Runs...>
class ArenaRunSet;

namespace detail {

// The first element of the run at index I, or the null pointer when the set
// holds no elements for the run.  The index keeps two runs of one element
// type in two distinct bases.  The pointer refers to the elements of the run
// as a reference does, so the class states refers{} over its first argument.
// The elements are bytes of an owner of the brand, and only the table of a set
// writes the pointer, from a run of its own brand.  So the class names the
// owner (names_owner_second of foundation/core/TypeParts.h).
template <class T, class Brand, std::size_t I>
class[[= ::foundation::transfer::refers{^^T}]][[= names_owner_second{}]] RunFirst_ {
    template <UnsignedInteger Count, class TableBrand, class... Runs>
        requires RunsOfSet<Runs...>
    friend class RunTable_;

    T* first_ = nullptr;
};

template <class Brand, class Positions, class... Runs>
struct RunFirsts_;

// One base for each run, in the order of the runs.  So the pointers come
// first in the layout, and the counts after them.
template <class Brand, std::size_t... Positions, class... Runs>
struct RunFirsts_<Brand, std::index_sequence<Positions...>, Runs...>
    : RunFirst_<typename Runs::element_type, Brand, Positions>... {};

// The count of slots: one more than the largest slot of a run.
template <class... Runs>
[[nodiscard]] consteval std::size_t run_slot_count_() noexcept {
    std::size_t largest = 0;
    ((largest = Runs::slot > largest ? Runs::slot : largest), ...);
    return largest + 1;
}

}  // namespace detail

// The door of a set to the raw constructors of a View (the doors of the
// owners above this header of foundation/core/Region.h).  The set keeps the
// first element of each run beside one table of counts, so its layout holds
// one pointer for each run, and a View for each run would move it.  The
// invariant: set<I> takes the first element of a run whose count is the count
// of the slot of the run, and set_count drops each run of the slot.  So a run
// with a first element holds the count of its slot, and the View of the two
// borrows the elements that the run of set<I> borrowed.  The table of a set
// is the one friend, and a home owner row of utils/rule-table.txt keeps this
// definition in this header.
class RunSetLayoutDoor final : ::foundation::NoObject<RunSetLayoutDoor> {
    template <UnsignedInteger Count, class Brand, class... Runs>
        requires detail::RunsOfSet<Runs...>
    friend class detail::RunTable_;

    // The count elements from first, which set<I> took from a run.
    template <class Element>
    [[nodiscard]] static constexpr View<Element> held_run_(Element* first, std::size_t count) noexcept {
        return View<Element>{first, count};
    }
};

namespace detail {

// The table of a set: the first element of each run and the counts, and each
// door of the family other than set<I>.  keep_run_ is private, and the two sets
// are the friends.  So only set<I> of the two keeps a run.  Each other table
// is empty or a copy of the table of a set of the same brand.
template <UnsignedInteger Count, class Brand, class... Runs>
    requires RunsOfSet<Runs...>
class RunTable_ : private RunFirsts_<Brand, std::index_sequence_for<Runs...>, Runs...> {
    friend class RunSet<Count, Runs...>;
    friend class ArenaRunSet<Count, Brand, Runs...>;

public:
    // The count of slots in the table of counts.
    static constexpr std::size_t slot_count = run_slot_count_<Runs...>();

    // The count of runs.
    static constexpr std::size_t run_count = sizeof...(Runs);

    // The element type of the run at index I.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    using element_t = typename ::foundation::reflect::pack_at_t<I, ::foundation::reflect::Pack<Runs...>>::element_type;

    // The slot of the count of the run at index I.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    static constexpr std::size_t slot_of =
        ::foundation::reflect::pack_at_t<I, ::foundation::reflect::Pack<Runs...>>::slot;

    // What the set borrows, for the payload walk of
    // fixy/concurrent/PayloadRow.h: a function type with one pointer
    // parameter for the element of each run.  The walk reads the target of
    // each parameter, so it reads each element type of the set.
    using value_type = void(typename Runs::element_type*...);

private:
    template <std::size_t I>
    using First_ = RunFirst_<element_t<I>, Brand, I>;

    // Drops the elements of each run of the slot.
    template <std::size_t Slot, std::size_t... Positions>
    constexpr void drop_runs_of_(std::index_sequence<Positions...> /*positions*/) noexcept {
        ((slot_of<Positions> == Slot ? void(static_cast<First_<Positions>&>(*this).first_ = nullptr) : void()), ...);
    }

    // The View of the run at index I, from its first element.  A run with
    // no elements gives the empty View, also at a count above zero.
    template <std::size_t I, class Element>
    [[nodiscard]] constexpr View<Element> view_of_(Element* first) const noexcept {
        std::size_t const count = first == nullptr ? 0 : std::size_t{counts_.at(index<slot_of<I>>())};
        return RunSetLayoutDoor::held_run_(first, count);
    }

    FixedArray<Count, slot_count> counts_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // Keeps the elements of run for the run at index I.  The run holds the
    // count of its slot, or the process ends in each build.
    template <std::size_t I>
    constexpr void keep_run_(View<element_t<I>> run) noexcept {
        std::size_t const count = counts_.at(index<slot_of<I>>());
        if (run.size() != count) [[unlikely]] {
            fatal("RunSet::set got a run of {} elements for a slot of the count {}", run.size(), count);
        }
        static_cast<First_<I>&>(*this).first_ = first_of_(BaseLayerRead{}, run);
    }

public:
    // A table with each count zero and no elements for each run.
    constexpr RunTable_() noexcept = default;

    // The count at the slot.
    template <std::size_t Slot>
        requires(Slot < slot_count)
    [[nodiscard]] constexpr Count count() const noexcept {
        return counts_.at(index<Slot>());
    }

    // The count at the slot.  Each run of the slot drops its elements.
    template <std::size_t Slot>
        requires(Slot < slot_count)
    constexpr void set_count(Count count) noexcept {
        drop_runs_of_<Slot>(std::index_sequence_for<Runs...>{});
        counts_.set(index<Slot>(), count);
    }

    // True when the run at index I holds the count of its slot: the count
    // is zero, or set<I> gave the run its elements.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    [[nodiscard]] constexpr bool has_run() const noexcept {
        return static_cast<First_<I> const&>(*this).first_ != nullptr || counts_.at(index<slot_of<I>>()) == 0;
    }

    // The run at index I, as a View of const elements: a set lends no writing
    // View, because a copy of the set refers to the same run.  A run with no
    // elements gives the empty View.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    [[nodiscard]] constexpr View<element_t<I> const> view() const& noexcept {
        element_t<I> const* const first = static_cast<First_<I> const&>(*this).first_;
        return view_of_<I>(first);
    }
    template <std::size_t I>
    void view() && = delete("a View of a temporary RunSet dangles when the owner of its runs ends");
    template <std::size_t I>
    void view() const&& = delete("a View of a temporary RunSet dangles when the owner of its runs ends");
};

}  // namespace detail

// A set of runs that names no owner of them.
template <UnsignedInteger Count, class... Runs>
    requires detail::RunsOfSet<Runs...>
class RunSet final : public detail::RunTable_<Count, detail::RunsOfAnyOwner, Runs...> {
    using Table = detail::RunTable_<Count, detail::RunsOfAnyOwner, Runs...>;

public:
    // A set with each count zero and no elements for each run.
    constexpr RunSet() noexcept = default;

    // The elements of run for the run at index I.  The View holds the count
    // of the slot of the run, or the process ends in each build.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    constexpr void set(View<typename Table::template element_t<I>> run) noexcept {
        this->template keep_run_<I>(run);
    }
};

// A set of runs of the arenas of the brand.  Only an ArenaRun of the brand
// gives a run to the set.
template <UnsignedInteger Count, ::foundation::brand::IsFreshBrand Brand, class... Runs>
    requires detail::RunsOfSet<Runs...>
class ArenaRunSet final : public detail::RunTable_<Count, Brand, Runs...> {
    using Table = detail::RunTable_<Count, Brand, Runs...>;

public:
    using brand_type = Brand;

    // A set with each count zero and no elements for each run.
    constexpr ArenaRunSet() noexcept = default;

    // The elements of run for the run at index I.  The run holds the count of
    // the slot, or the process ends in each build.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    constexpr void set(ArenaRun<typename Table::template element_t<I>, Brand> run) noexcept {
        this->template keep_run_<I>(run.view());
    }

    // A View binds to this overload, so the refusal gives its reason.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    void set(View<typename Table::template element_t<I>>) =
        delete("a View names no arena, and a set of an arena keeps only runs of that arena: give the ArenaRun");
};

}  // namespace foundation::core
