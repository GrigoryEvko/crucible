// Sentinel TU for fixy/CyclicBuffer.h.  The ring keeps the last N elements,
// reads the most recent first, and its fill count saturates at N while the
// cursor keeps counting every claim.  A ring of Unkeyed writes through its
// value doors, and a keyed ring lends an element that a body writes only with
// its key.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using Ring = ::fixy::CyclicBuffer<std::uint64_t, 4>;

// The value of the element back places before the newest.
template <class RingType>
[[nodiscard]] std::uint64_t value_back(RingType const& ring, ::fixy::Index<4> back) noexcept {
    return ring.with_slot(ring.slot_back(back), [](std::uint64_t const& value) noexcept { return value; });
}

// The value of the element back places before the newest of a keyed ring, which
// takes the key of the ring.
template <class RingType, class Key>
[[nodiscard]] std::uint64_t keyed_value_back(RingType const& ring, Key const& key, ::fixy::Index<4> back) noexcept {
    return ring.with_slot(key, ring.slot_back(back), [](std::uint64_t const& value) noexcept { return value; });
}

[[nodiscard]] int starts_empty() {
    const Ring ring{};
    if (!ring.empty() || ring.full() || ring.size() != 0 || Ring::capacity != 4) return 1;
    return 0;
}

// Each push lands at the front, and the ring reads back in reverse order.
[[nodiscard]] int reads_most_recent_first() {
    Ring ring{};
    ring.push(std::uint64_t{10});
    ring.push(std::uint64_t{20});
    ring.push(std::uint64_t{30});
    if (ring.size() != 3 || ring.full()) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 30 || value_back(ring, ::fixy::index<1>()) != 20
        || value_back(ring, ::fixy::index<2>()) != 10) {
        return 2;
    }
    return 0;
}

// Past the capacity the oldest element goes, the size stays at N, and the
// cursor keeps the count of every claim.
[[nodiscard]] int evicts_the_oldest_past_capacity() {
    Ring ring{};
    for (std::uint64_t value = 0; value < 11; ++value)
        ring.push(value);
    if (ring.size() != 4 || !ring.full()) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 10 || value_back(ring, ::fixy::index<3>()) != 7) return 2;
    if (ring.cursor().raw() != 11 || ring.cursor().index() != 3) return 3;
    if (ring.next_slot() != ::fixy::Index<4>{::fixy::index<3>()}) return 4;
    return 0;
}

// An update changes the element of its slot alone, gives the result of its
// body, and claims no slot.  A pass claims a slot and keeps its element.
[[nodiscard]] int update_changes_one_slot() {
    Ring ring{};
    ring.push(std::uint64_t{99});
    ring.push(std::uint64_t{7});
    std::uint64_t const held =
        ring.update(ring.slot_back(::fixy::index<1>()), [](std::uint64_t& slot) noexcept { return slot++; });
    if (held != 99 || ring.size() != 2) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 7 || value_back(ring, ::fixy::index<1>()) != 100) return 2;
    ring.pass_slot();
    if (ring.size() != 3 || value_back(ring, ::fixy::index<0>()) != 0) return 3;
    return 0;
}

// A helper that updates a slot of a ring with no key.
void add_ten(Ring& ring, ::fixy::Index<4> slot) noexcept {
    ring.update(slot, [](std::uint64_t& element) noexcept { element += 10; });
}

// A helper that claims a slot while a body of update runs finds whole elements.
// An update of the same slot loses its step, and a build with the test oracles
// ends the process there.
[[nodiscard]] int update_loses_a_nested_step() {
    Ring ring{};
    ring.push(std::uint64_t{1});
    ring.push(std::uint64_t{2});
    ::fixy::Index<4> const newest = ring.slot_back(::fixy::index<0>());
    ::fixy::Index<4> const older = ring.slot_back(::fixy::index<1>());
    ring.update(newest, [&ring, older](std::uint64_t& element) noexcept {
        add_ten(ring, older);
        element += 5;
    });
    if (value_back(ring, ::fixy::index<0>()) != 7 || value_back(ring, ::fixy::index<1>()) != 11) return 1;

    auto const lose_a_step = [] noexcept {
        Ring lost{};
        lost.push(std::uint64_t{1});
        ::fixy::Index<4> const slot = lost.slot_back(::fixy::index<0>());
        lost.update(slot, [&lost, slot](std::uint64_t& element) noexcept {
            add_ten(lost, slot);
            element += 5;
        });
        return value_back(lost, ::fixy::index<0>());
    };
    if constexpr (::foundation::has_test_oracles) {
        ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
        ::fixy::check::expect_dies(
            test, [&lose_a_step] noexcept { static_cast<void>(lose_a_step()); },
            ::fixy::TextView{"a body of update wrote the element that the door then puts back"});
        return 0;
    } else {
        return lose_a_step() == 6 ? 0 : 2;
    }
}

// A const ring reads the same slots.
[[nodiscard]] int const_ring_reads_the_same_slots() {
    Ring ring{};
    ring.push(std::uint64_t{5});
    const Ring& view = ring;
    return value_back(view, ::fixy::index<0>()) == 5 ? 0 : 1;
}

// The count of the ends of each Step.  A test reads it around a claim.
int g_step_ends = 0;

// An element whose value only goes up.  The assignment is deleted, as the
// assignment of a counter that refuses a write backward is, so only a claim
// that ends the old element gives its slot a new element.
struct Step {
    std::uint64_t value = 7;

    Step() noexcept = default;
    Step(const Step&) = default;
    Step& operator=(const Step&) = delete("a step only goes up");
    ~Step() { ++g_step_ends; }
};

[[nodiscard]] std::uint64_t value_of(Step const& step) noexcept { return step.value; }

// An element with no assignment whose move and end run no code, as an element
// with a counter that only goes up.
struct Level {
    std::uint64_t value = 0;

    Level() noexcept = default;
    explicit Level(std::uint64_t from) noexcept : value{from} {}
    Level(const Level&) = default;
    Level& operator=(const Level&) = delete("a level only goes up");
};

// An element with a nothrow assignment whose end is code.  It counts its ends,
// and its assignments.
int g_gauge_ends = 0;
int g_gauge_writes = 0;

struct Gauge {
    std::uint64_t value = 0;

    Gauge() noexcept = default;
    explicit Gauge(std::uint64_t from) noexcept : value{from} {}
    Gauge(const Gauge&) = default;
    Gauge& operator=(const Gauge& other) noexcept {
        value = other.value;
        ++g_gauge_writes;
        return *this;
    }
    ~Gauge() { ++g_gauge_ends; }
};

// A push_new of a full ring puts its value in the place of the oldest element,
// in the slot that the slot of the newest names.  An element with an assignment
// takes it, so no element of the ring ends.
template <class Element>
[[nodiscard]] int push_new_replaces_the_oldest_element() {
    ::fixy::CyclicBuffer<Element, 2> ring{};
    Element first{11};
    Element second{12};
    Element third{13};
    ring.push_new(std::move(first));
    ring.push_new(std::move(second));
    const int ends_before = g_gauge_ends;
    ring.push_new(std::move(third));
    if (g_gauge_ends != ends_before) return 1;
    const auto read = [](Element const& element) noexcept { return element.value; };
    if (ring.with_slot(ring.slot_back(::fixy::index<0>()), read) != 13) return 2;
    if (ring.with_slot(ring.slot_back(::fixy::index<1>()), read) != 12 || ring.size() != 2) return 3;
    return 0;
}

[[nodiscard]] int push_new_of_each_kind_of_element() {
    if (const int failed = push_new_replaces_the_oldest_element<Level>(); failed != 0) return failed;
    if (const int failed = push_new_replaces_the_oldest_element<Gauge>(); failed != 0) return 3 + failed;
    return g_gauge_writes == 3 ? 0 : 7;
}

// An update of an element with no assignment puts its copy in the place of the
// element, and it changes no other slot.
[[nodiscard]] int update_of_an_element_with_no_assignment() {
    ::fixy::CyclicBuffer<Level, 2> ring{};
    ring.push_new(Level{});
    ring.push_new(Level{});
    const auto level_of = [](Level const& level) noexcept { return level.value; };
    ring.update(ring.slot_back(::fixy::index<1>()), [](Level& level) noexcept { level.value += 100; });
    if (ring.with_slot(ring.slot_back(::fixy::index<1>()), level_of) != 100) return 1;
    if (ring.with_slot(ring.slot_back(::fixy::index<0>()), level_of) != 0 || ring.size() != 2) return 2;
    return 0;
}

// A fresh claim of a keyed ring that is full ends the oldest element one time
// and starts a value-initialized element in its slot.
[[nodiscard]] int fresh_claim_starts_a_new_element_in_the_slot() {
    auto [ring, key] = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<Step, 2>);
    ring.with_claim(key, [](Step& step) noexcept { step.value = 11; });
    ring.with_claim(key, [](Step& step) noexcept { step.value = 12; });
    const int ends_before = g_step_ends;
    const std::uint64_t fresh = ring.with_fresh_claim(key, [](Step& step) noexcept {
        std::uint64_t const started = step.value;
        step.value = 13;
        return started;
    });
    if (g_step_ends != ends_before + 1) return 1;
    if (fresh != 7) return 2;
    auto const& read = ring;
    if (read.with_slot(key, ring.slot_back(::fixy::index<0>()), value_of) != 13) return 3;
    if (read.with_slot(key, ring.slot_back(::fixy::index<1>()), value_of) != 12 || ring.size() != 2) return 4;
    return 0;
}

// A keyed ring takes its key at each door that writes.  The move form gives
// the key back, and the borrow form keeps it with the caller.
[[nodiscard]] int keyed_ring_writes_with_its_key() {
    auto [ring, key] = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<std::uint64_t, 4>);
    auto back = ring.with_claim(std::move(key), [](std::uint64_t& slot) noexcept { slot = 41; });
    ring.with_claim(back, [](std::uint64_t& slot) noexcept { slot = 42; });
    ring.push(back, std::uint64_t{43});
    auto swapped =
        ring.with_slot(std::move(back), ring.slot_back(::fixy::index<0>()), [](std::uint64_t& slot) noexcept {
            std::uint64_t const held = slot;
            slot = 44;
            return held;
        });
    std::uint64_t const old_value = swapped.at(::fixy::index<0>());
    if (old_value != 43) return 1;
    auto& held_key = swapped.at(::fixy::index<1>());
    if (keyed_value_back(ring, held_key, ::fixy::index<0>()) != 44) return 2;
    if (keyed_value_back(ring, held_key, ::fixy::index<2>()) != 41) return 3;
    // The pass of a keyed ring takes the key too, and it keeps the element of
    // the slot that it claims.
    ring.pass_slot(held_key);
    if (ring.size() != 4 || keyed_value_back(ring, held_key, ::fixy::index<0>()) != 0) return 4;
    if (keyed_value_back(ring, held_key, ::fixy::index<1>()) != 44) return 5;
    return 0;
}

}  // namespace

int main() {
    if (const int failed = starts_empty(); failed != 0) return 10 + failed;
    if (const int failed = reads_most_recent_first(); failed != 0) return 20 + failed;
    if (const int failed = evicts_the_oldest_past_capacity(); failed != 0) return 30 + failed;
    if (const int failed = update_changes_one_slot(); failed != 0) return 40 + failed;
    if (const int failed = const_ring_reads_the_same_slots(); failed != 0) return 50 + failed;
    if (const int failed = fresh_claim_starts_a_new_element_in_the_slot(); failed != 0) return 60 + failed;
    if (const int failed = keyed_ring_writes_with_its_key(); failed != 0) return 70 + failed;
    if (const int failed = push_new_of_each_kind_of_element(); failed != 0) return 80 + failed;
    if (const int failed = update_loses_a_nested_step(); failed != 0) return 90 + failed;
    if (const int failed = update_of_an_element_with_no_assignment(); failed != 0) return 100 + failed;
    return 0;
}
