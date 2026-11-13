#pragma once

// The RunSet family: some runs of elements, and a table of their counts.
//
// A RunSet<Count, RunOf<T, Slot>...> holds the first element of each run
// and a small table of counts.  The run at index I has the count at its
// slot, and two runs can share one slot.  So a record with three arrays of
// its inputs and two arrays of its outputs holds five pointers and two
// counts, and no View of 16 bytes for each array.
//
//   count<Slot>()       the count at the slot
//   set_count<Slot>(n)  the count n at the slot.  Each run of the slot drops
//                       its elements, so no run keeps elements of a
//                       different count
//   set<I>(run)         the elements of a View for the run at index I.  A
//                       View whose count is not the count of the slot ends
//                       the process in each build
//   has_run<I>()        true when the run holds the count of its slot: the
//                       count is zero, or set<I> gave the run its elements
//   view<I>()           the run at index I as a View.  A run with no
//                       elements at a count above zero gives the empty View
//
// A run with no elements at a count above zero is a valid state.  A record
// can keep the count of its inputs and hold no array of their metadata.
// has_run<I>() tells that state from a run of zero elements, and view<I>()
// never gives a View of the count over no elements.
//
// The set pairs each first element with its count itself, through the door
// of the base layer.  The only write of a first element takes a View of the
// count of its slot, and the only write of a count drops the runs of its
// slot.  So each View that view<I>() gives holds elements that a View held.
// The seal refuses std::bit_cast and a lifetime start over bytes, as for a
// View, so no bytes become a RunSet with a pointer that no View gave.
//
// The layout: the pointer of each run in the order of the runs, then the
// counts.  A set owns nothing, so its runs dangle when their owner ends.

#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Index.h>
#include <foundation/core/Number.h>
#include <foundation/core/Record.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

// A run of a RunSet: elements of type T, with the count at index Slot of
// the table of counts.
template <class T, std::size_t Slot>
    requires ViewElement<T> && (!std::is_const_v<T>)
struct RunOf final {
    using element_type = T;
    static constexpr std::size_t slot = Slot;
};

namespace detail {

template <class T>
inline constexpr bool is_run_of_ = false;

template <class T, std::size_t Slot>
inline constexpr bool is_run_of_<RunOf<T, Slot>> = true;

// The first element of the run at index I, or the null pointer when the set
// holds no elements for the run.  The index keeps two runs of one element
// type in two distinct bases.
template <std::size_t I, class T>
struct RunFirst_ {
    T* first_ = nullptr;
};

template <class Positions, class... Runs>
struct RunFirsts_;

// One base for each run, in the order of the runs.  So the pointers come
// first in the layout, and the counts after them.
template <std::size_t... Positions, class... Runs>
struct RunFirsts_<std::index_sequence<Positions...>, Runs...> : RunFirst_<Positions, typename Runs::element_type>... {};

// The count of slots: one more than the largest slot of a run.
template <class... Runs>
[[nodiscard]] consteval std::size_t run_slot_count_() noexcept {
    std::size_t largest = 0;
    ((largest = Runs::slot > largest ? Runs::slot : largest), ...);
    return largest + 1;
}

}  // namespace detail

template <UnsignedInteger Count, class... Runs>
    requires(sizeof...(Runs) > 0 && (detail::is_run_of_<Runs> && ...))
class RunSet final : private detail::RunFirsts_<std::index_sequence_for<Runs...>, Runs...> {
public:
    // The count of slots in the table of counts.
    static constexpr std::size_t slot_count = detail::run_slot_count_<Runs...>();

    // The count of runs.
    static constexpr std::size_t run_count = sizeof...(Runs);

    // The element type of the run at index I.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    using element_t = typename pack_at_t<I, Pack<Runs...>>::element_type;

    // The slot of the count of the run at index I.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    static constexpr std::size_t slot_of = pack_at_t<I, Pack<Runs...>>::slot;

    // What the set borrows, for the payload walk of
    // fixy/concurrent/PayloadRow.h: a function type with one pointer
    // parameter for the element of each run.  The walk reads the target of
    // each parameter, so it reads each element type of the set.
    using value_type = void(typename Runs::element_type*...);

private:
    template <std::size_t I>
    using First_ = detail::RunFirst_<I, element_t<I>>;

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
        return View<Element>{detail::ViewDoor::key(), first, count};
    }

    FixedArray<Count, slot_count> counts_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // A set with each count zero and no elements for each run.
    constexpr RunSet() noexcept = default;

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
        counts_.at(index<Slot>()) = count;
    }

    // The elements of run for the run at index I.  The View holds the count
    // of the slot of the run, or the process ends in each build.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    constexpr void set(View<element_t<I>> run) noexcept {
        std::size_t const count = counts_.at(index<slot_of<I>>());
        if (run.size() != count) [[unlikely]] {
            fatal("RunSet::set got a run of {} elements for a slot of the count {}", run.size(), count);
        }
        static_cast<First_<I>&>(*this).first_ = first_of_(detail::ViewDoor::key(), run);
    }

    // True when the run at index I holds the count of its slot: the count
    // is zero, or set<I> gave the run its elements.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    [[nodiscard]] constexpr bool has_run() const noexcept {
        return static_cast<First_<I> const&>(*this).first_ != nullptr || counts_.at(index<slot_of<I>>()) == 0;
    }

    // The run at index I.  A run with no elements gives the empty View.
    template <std::size_t I>
        requires(I < sizeof...(Runs))
    [[nodiscard]] constexpr View<element_t<I>> view() & noexcept {
        return view_of_<I>(static_cast<First_<I>&>(*this).first_);
    }
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

}  // namespace foundation::core
