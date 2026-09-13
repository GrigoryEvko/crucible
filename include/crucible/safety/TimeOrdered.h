#pragma once

// A value paired with the vector clock recording the causal position
// at which it was produced.
//
// Copying is permitted because the grade records where in the causal
// order a value sits, which belongs to its identity rather than to its
// ownership. Two events with the same payload and the same clock are
// the same event, so a copy is a replay.
//
// There is deliberately no spaceship operator on the wrapper. The
// clock alone has one, returning a partial ordering, and lifting it
// here would contradict equality: two values with the same clock but
// different payloads are unequal, while their clocks compare as
// equivalent because neither happens before the other. The named
// methods below keep the two questions apart. A caller who wants the
// partial order takes the clock out and compares that.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/HappensBefore.h>
#include <crucible/safety/Decide.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Every use of the word clock in this file means a logical vector
// clock: N per-process counters, where slot p counts the events from
// process p observed so far. None of it is physical time. There is no
// wall clock here, no monotonic clock, no cycle counter.
//
// The order on these clocks is partial. Two clocks compare as one
// before the other, equal, or concurrent, and concurrent means the two
// events could have happened in either real-time order.
template <typename T, std::size_t N, typename Tag = void>
class [[nodiscard]] TimeOrdered {
    static_assert(N > 0, "TimeOrdered<T, 0> is forbidden — a zero-participant vector "
                         "clock has no algebraic content.  Use N >= 1; N=1 reduces to "
                         "a Lamport scalar clock.");

public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::HappensBeforeLattice<N, Tag>;
    using lattice_t = lattice_type;
    using vector_clock_t = typename lattice_type::element_type;
    using process_id_t = std::size_t;
    using tag_t = Tag;
    static constexpr std::size_t process_count = N;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

public:
    constexpr TimeOrdered() noexcept(std::is_nothrow_default_constructible_v<T>) : impl_{T{}, lattice_type::bottom()} {}

    constexpr TimeOrdered(T value, vector_clock_t vc) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), vc} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr TimeOrdered(std::in_place_t, vector_clock_t vc,
                          Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                   && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), vc} {}

    // The origin is the clock with no causal predecessors.
    [[nodiscard]] static constexpr TimeOrdered at_origin(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return TimeOrdered{std::move(value), lattice_type::bottom()};
    }

    constexpr TimeOrdered(const TimeOrdered&) = default;
    constexpr TimeOrdered(TimeOrdered&&) = default;
    constexpr TimeOrdered& operator=(const TimeOrdered&) = default;
    constexpr TimeOrdered& operator=(TimeOrdered&&) = default;
    ~TimeOrdered() = default;

    // Both the payload and the clock must match. This is composed by
    // hand rather than defaulted, because the substrate publishes no
    // equality of its own: whether comparing grades, values or both is
    // meant depends on the modality.
    [[nodiscard]] friend constexpr bool
    operator==(TimeOrdered const& a, TimeOrdered const& b) noexcept(noexcept(a.peek() == b.peek())
                                                                    && noexcept(a.vector_clock() == b.vector_clock())) {
        return a.peek() == b.peek() && a.vector_clock() == b.vector_clock();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    // This exchanges the clocks along with the payloads, which is
    // sound because it moves whole events between storage cells rather
    // than re-dating either one.
    constexpr void swap(TimeOrdered& other) noexcept(std::is_nothrow_swappable_v<T>
                                                     && std::is_nothrow_swappable_v<vector_clock_t>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(TimeOrdered& a,
                               TimeOrdered& b) noexcept(std::is_nothrow_swappable_v<T>
                                                        && std::is_nothrow_swappable_v<vector_clock_t>) {
        a.swap(b);
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr vector_clock_t vector_clock() const
        noexcept(std::is_nothrow_copy_constructible_v<vector_clock_t>) {
        return impl_.grade();
    }

    // Every slot-index precondition in this class is expressed against
    // an upper endpoint of N - 1. That endpoint cannot underflow,
    // because N is unsigned and the assertion above rules out zero.
    [[nodiscard]] constexpr std::uint64_t vector_clock_at(std::size_t p) const noexcept
        pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
        return impl_.grade()[p];
    }

    // Mutating the payload leaves the clock correct, because the clock
    // records when the value was produced and not what it holds.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    // Strict causal precedence: asymmetric, irreflexive and
    // transitive.
    [[nodiscard]] constexpr bool happens_before(TimeOrdered const& other) const noexcept {
        return lattice_type::happens_before(this->vector_clock(), other.vector_clock());
    }

    // Causal independence: neither event precedes the other, so they
    // could have happened in either order. With a single participant
    // the order is total, so this only ever holds for equal clocks.
    [[nodiscard]] constexpr bool is_concurrent(TimeOrdered const& other) const noexcept {
        return lattice_type::is_concurrent(this->vector_clock(), other.vector_clock());
    }

    // Ordered in one direction or the other.
    [[nodiscard]] constexpr bool comparable(TimeOrdered const& other) const noexcept {
        return lattice_type::comparable(this->vector_clock(), other.vector_clock());
    }

    // A clock is an immutable observation, so advancing yields a
    // successor event rather than mutating this one. The successor
    // always happens after its predecessor.
    [[nodiscard]] constexpr TimeOrdered
    advance_at(std::size_t p) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
        return TimeOrdered{this->peek(), lattice_type::successor_at(this->vector_clock(), p)};
    }

    [[nodiscard]] constexpr TimeOrdered advance_at(std::size_t p) && noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
        vector_clock_t advanced = lattice_type::successor_at(this->vector_clock(), p);
        return TimeOrdered{std::move(impl_).consume(), advanced};
    }

    // A synonym for advance_at with no semantic difference. Both names
    // exist so a call site can pick whichever reads better.
    [[nodiscard]] constexpr TimeOrdered tick(std::size_t p) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
        return advance_at(p);
    }

    [[nodiscard]] constexpr TimeOrdered tick(std::size_t p) && noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
        return std::move(*this).advance_at(p);
    }

    // Unlike advancing, ticking and merging, this enforces no relation
    // at all between the new clock and the current one. The new clock
    // may precede it. Whatever discipline applies is the caller's.
    [[nodiscard]] constexpr TimeOrdered
    with_vector_clock(vector_clock_t new_vector_clock) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return TimeOrdered{this->peek(), new_vector_clock};
    }

    [[nodiscard]] constexpr TimeOrdered
    with_vector_clock(vector_clock_t new_vector_clock) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return TimeOrdered{std::move(impl_).consume(), new_vector_clock};
    }

    // The receive step: the resulting clock is the pointwise maximum
    // of the two, with the receiver's own slot bumped. The payload is
    // whatever the caller passes, because how to combine two payloads
    // depends on what they mean.
    [[nodiscard]] constexpr TimeOrdered merge(T received_value, vector_clock_t received_vector_clock,
                                              std::size_t me) const noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(::crucible::decide::in_range<std::size_t>(me, 0, N - 1)) {
        vector_clock_t merged = lattice_type::causal_merge(this->vector_clock(), received_vector_clock, me);
        return TimeOrdered{std::move(received_value), merged};
    }
};

template <typename T, std::size_t N, typename Tag = void>
TimeOrdered(T, typename ::crucible::algebra::lattices::HappensBeforeLattice<N, Tag>::element_type)
    -> TimeOrdered<T, N, Tag>;

// The clock is carried per instance, so the wrapper costs the payload
// plus one counter per participant, plus padding.
namespace detail::time_ordered_layout {

using TO_int64_n4 = TimeOrdered<std::int64_t, 4>;
using TO_int64_n8 = TimeOrdered<std::int64_t, 8>;
using TO_int64_n16 = TimeOrdered<std::int64_t, 16>;

static_assert(sizeof(TO_int64_n4) <= sizeof(std::int64_t) + 4 * sizeof(std::uint64_t) + 8,
              "TimeOrdered<int64, 4> must fit the payload plus four counters "
              "plus one counter of padding. If this fires, either the clock's "
              "alignment grew or the wrapper gained a field.");
static_assert(sizeof(TO_int64_n8) <= sizeof(std::int64_t) + 8 * sizeof(std::uint64_t) + 8);
static_assert(sizeof(TO_int64_n16) <= sizeof(std::int64_t) + 16 * sizeof(std::uint64_t) + 8);

using TO_voidp_n4 = TimeOrdered<void*, 4>;
static_assert(sizeof(TO_voidp_n4) <= sizeof(void*) + 4 * sizeof(std::uint64_t) + 8);

}  // namespace detail::time_ordered_layout

namespace detail::time_ordered_self_test {

using TO4 = TimeOrdered<int, 4>;
using HB4 = ::crucible::algebra::lattices::HappensBeforeLattice<4>;

inline constexpr TO4 evt_a{10, HB4::element_type{{1, 0, 0, 0}}};
inline constexpr TO4 evt_b{20, HB4::element_type{{1, 1, 0, 0}}};
inline constexpr TO4 evt_c{30, HB4::element_type{{2, 2, 1, 0}}};

// Neither of the next two precedes the other.
inline constexpr TO4 evt_x{40, HB4::element_type{{2, 0, 1, 0}}};
inline constexpr TO4 evt_y{50, HB4::element_type{{0, 2, 0, 1}}};

inline constexpr TO4 evt_origin = TO4::at_origin(99);
static_assert(evt_origin.vector_clock() == HB4::element_type{{0, 0, 0, 0}});
static_assert(evt_origin.peek() == 99);

inline constexpr TO4 evt_default{};
static_assert(evt_default.vector_clock() == HB4::element_type{{0, 0, 0, 0}});
static_assert(evt_default.peek() == 0);

static_assert(evt_a.happens_before(evt_b));
static_assert(evt_b.happens_before(evt_c));
static_assert(evt_a.happens_before(evt_c));  // transitive
static_assert(!evt_b.happens_before(evt_a));  // asymmetric
static_assert(!evt_a.happens_before(evt_a));  // irreflexive

static_assert(evt_x.is_concurrent(evt_y));
static_assert(evt_y.is_concurrent(evt_x));  // symmetric
static_assert(!evt_x.happens_before(evt_y));
static_assert(!evt_y.happens_before(evt_x));
static_assert(!evt_x.comparable(evt_y));

static_assert(evt_a.comparable(evt_b));
static_assert(evt_a.comparable(evt_c));

static_assert(evt_a == TO4{10, HB4::element_type{{1, 0, 0, 0}}});

// Differing in either component is enough to be a different event,
// even where the clocks compare as equivalent in the lattice order.
static_assert(!(evt_a == TO4{99, HB4::element_type{{1, 0, 0, 0}}}));
static_assert(!(evt_a == TO4{10, HB4::element_type{{2, 0, 0, 0}}}));

static_assert(evt_a.vector_clock_at(0) == 1);
static_assert(evt_a.vector_clock_at(1) == 0);
static_assert(evt_b.vector_clock_at(1) == 1);
static_assert(evt_c.vector_clock_at(2) == 1);

inline constexpr TO4 evt_a_after = TO4{10, HB4::element_type{{1, 0, 0, 0}}}.advance_at(0);
static_assert(evt_a_after.vector_clock() == HB4::element_type{{2, 0, 0, 0}});
static_assert(evt_a_after.peek() == 10);

static_assert(evt_a.happens_before(evt_a_after));

inline constexpr TO4 evt_a_ticked = evt_a.tick(0);
static_assert(evt_a_ticked.vector_clock() == evt_a_after.vector_clock());
static_assert(evt_a_ticked.peek() == evt_a.peek());
static_assert(evt_a.happens_before(evt_a_ticked));

inline constexpr TO4 evt_a_relocated = evt_a.with_vector_clock(HB4::element_type{{5, 5, 5, 5}});
static_assert(evt_a_relocated.peek() == evt_a.peek());
static_assert(evt_a_relocated.vector_clock() == HB4::element_type{{5, 5, 5, 5}});

// The maximum of the two clocks is {1,2,0,1}, and bumping slot zero
// gives the expected {2,2,0,1}.
inline constexpr TO4 evt_merged = evt_a.merge(123, evt_y.vector_clock(), 0);
static_assert(evt_merged.vector_clock() == HB4::element_type{{2, 2, 0, 1}});
static_assert(evt_merged.peek() == 123);

// The merged event observes both of its inputs.
static_assert(evt_a.happens_before(evt_merged));
static_assert(evt_y.happens_before(evt_merged));

struct ReplayTag {};
struct KernelOrderTag {};
using TO_replay = TimeOrdered<int, 4, ReplayTag>;
using TO_kernel = TimeOrdered<int, 4, KernelOrderTag>;
static_assert(!std::is_same_v<TO_replay, TO_kernel>);
static_assert(!std::is_same_v<TO_replay::vector_clock_t, TO_kernel::vector_clock_t>);

static_assert(TO4::process_count == 4);
static_assert(TimeOrdered<int, 8>::process_count == 8);

// The reflected type name is compared by suffix, not by equality: the
// spelling depends on the including translation unit and can come back
// either qualified or unqualified.
static_assert(TO4::value_type_name().ends_with("int"));

// The participant count is not part of the lattice's name.
static_assert(TO4::lattice_name() == "HappensBeforeLattice");

[[nodiscard]] consteval bool swap_exchanges_both_components() noexcept {
    TO4 a{10, HB4::element_type{{1, 0, 0, 0}}};
    TO4 b{20, HB4::element_type{{2, 2, 1, 0}}};
    a.swap(b);
    return a.peek() == 20 && a.vector_clock() == HB4::element_type{{2, 2, 1, 0}} && b.peek() == 10
        && b.vector_clock() == HB4::element_type{{1, 0, 0, 0}};
}
static_assert(swap_exchanges_both_components());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    TO4 a{10, HB4::element_type{{1, 0, 0, 0}}};
    TO4 b{20, HB4::element_type{{2, 2, 1, 0}}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// The clock is a genuine per-instance field rather than a type-level
// singleton, so a divergence between its compile-time and run-time
// initialization would surface here and nowhere in the assertions
// above.
inline void runtime_smoke_test() {
    HB4::element_type c1{{1, 0, 0, 0}};
    HB4::element_type c2{{1, 1, 0, 0}};
    HB4::element_type cy{{0, 2, 0, 1}};

    TO4 a{10, c1};
    TO4 b{20, c2};
    TO4 y{50, cy};

    [[maybe_unused]] bool hb_ab = a.happens_before(b);
    [[maybe_unused]] bool conc = a.is_concurrent(y);
    [[maybe_unused]] bool comp = a.comparable(b);

    TO4 a_succ = a.advance_at(0);
    TO4 a_recv = a.merge(99, cy, 0);
    [[maybe_unused]] bool causal = a.happens_before(a_succ);

    TO4 a_moved = std::move(a).advance_at(0);
    [[maybe_unused]] auto v = a_moved.peek();

    TO4 d_evt{};
    TO4 o_evt = TO4::at_origin(7);
    [[maybe_unused]] auto d_clock = d_evt.vector_clock();
    [[maybe_unused]] auto o_clock = o_evt.vector_clock();

    [[maybe_unused]] auto slot0 = b.vector_clock_at(0);
    [[maybe_unused]] auto slot3 = b.vector_clock_at(3);

    a_recv.peek_mut() = 42;
}

}  // namespace detail::time_ordered_self_test

}  // namespace crucible::safety
