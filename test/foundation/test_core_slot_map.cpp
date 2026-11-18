// Tests of the SlotMap of foundation/core/SlotMap.h: insert, get, remove,
// the refusal of a stale or a forged key, the stack of the free slots, the
// retirement of a slot at the wrap of its generation, the walk in the order of
// the slots, the end of each value, and a property test against a model of
// the slots.

#include <foundation/core/SlotMap.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/List.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::Index;
using ::foundation::core::mint_slot_map;
using ::foundation::core::Quantity;
using ::foundation::core::SlotKey;
using ::foundation::core::SlotMap;
namespace unit = ::foundation::core::unit;

struct HandleTag final {};

using Key = SlotKey<HandleTag>;
using Capacity = Quantity<unit::Element, std::uint32_t>;

// The map of count slots of the value T and the word Generation.
template <class T, class Generation = std::uint32_t>
[[nodiscard]] SlotMap<T, HandleTag, Generation> map_of(std::uint32_t count) noexcept {
    return mint_slot_map<T, HandleTag, Generation>(::foundation::effects::testing::test().alloc, Capacity{count});
}

// The index and the generation of a key.
[[nodiscard]] std::uint32_t index_of(Key key) noexcept { return static_cast<std::uint32_t>(key.raw()); }
[[nodiscard]] std::uint64_t generation_of(Key key) noexcept { return key.raw() >> 32; }

void test_insert_get_remove() {
    SlotMap<int, HandleTag> map = map_of<int>(4);
    CRUCIBLE_FATAL_INVARIANT(map.is_empty() && map.size() == 0 && map.capacity().raw() == 4);
    Key const first = map.insert(10).expect("the map has a free slot");
    Key const second = map.insert(20).expect("the map has a free slot");
    Key const third = map.insert(30).expect("the map has a free slot");
    CRUCIBLE_FATAL_INVARIANT(index_of(first) == 0 && index_of(second) == 1 && index_of(third) == 2);
    CRUCIBLE_FATAL_INVARIANT(generation_of(first) == 1 && map.size() == 3);
    CRUCIBLE_FATAL_INVARIANT(*map.get(second).expect("the key is live") == 20);
    *map.get(second).expect("the key is live") = 21;
    CRUCIBLE_FATAL_INVARIANT(map.remove(second).expect("the key is live") == 21);
    CRUCIBLE_FATAL_INVARIANT(map.get(second).is_none() && map.remove(second).is_none() && map.size() == 2);
    // The freed slot is on the top of the stack, so the next insert takes it
    // with a new generation, and the stale key stays refused.
    Key const reused = map.insert(40).expect("the map has a free slot");
    CRUCIBLE_FATAL_INVARIANT(index_of(reused) == 1 && generation_of(reused) == 3);
    CRUCIBLE_FATAL_INVARIANT(map.get(second).is_none() && *map.get(reused).expect("the key is live") == 40);
    SlotMap<int, HandleTag> const& view = map;
    CRUCIBLE_FATAL_INVARIANT(*view.get(third).expect("the key is live") == 30);
}

// An insert into a full map gives Full, and a linear value comes back in it.
void test_full_map() {
    SlotMap<int, HandleTag> map = map_of<int>(2);
    static_cast<void>(map.insert(1).expect("the map has a free slot"));
    static_cast<void>(map.insert(2).expect("the map has a free slot"));
    CRUCIBLE_FATAL_INVARIANT(map.insert(3).is_err() && map.size() == 2);
    SlotMap<int, HandleTag> empty{};
    CRUCIBLE_FATAL_INVARIANT(empty.insert(1).is_err() && empty.capacity().raw() == 0);
    SlotMap<int, HandleTag> none = map_of<int>(0);
    CRUCIBLE_FATAL_INVARIANT(none.insert(1).is_err() && none.is_empty());
}

// A linear value.  The seal makes its assignment user-provided, so it is not
// trivially copyable.
struct[[= ::foundation::linear::linear{}]] Token final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    explicit Token(int from) noexcept : id{from} {}
    Token(Token&& other) noexcept : id{other.id} {}
    Token& operator=(Token&& other) noexcept {
        id = other.id;
        return *this;
    }
    Token(Token const&) = delete;
};

void test_linear_values() {
    SlotMap<Token, HandleTag> map = map_of<Token>(1);
    Key const key = map.insert(Token{1}).expect("the map has a free slot");
    auto refused = map.insert(Token{2});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err());
    Token back = std::move(refused).err().expect("the map is full").take();
    CRUCIBLE_FATAL_INVARIANT(back.id == 2);
    ::foundation::linear::drop(std::move(back));
    Token first = map.remove(key).expect("the key is live");
    CRUCIBLE_FATAL_INVARIANT(first.id == 1 && map.is_empty());
    ::foundation::linear::drop(std::move(first));
    ::foundation::linear::drop(std::move(map));
}

// A key that a caller builds from a word is a claim: an index past the used
// slots, an even generation, a generation past the word, and the empty value
// of an Option are each refused.  A key that goes out as a word and comes back
// finds its value.
void test_forged_keys() {
    SlotMap<int, HandleTag, std::uint8_t> map = map_of<int, std::uint8_t>(8);
    Key const live = map.insert(7).expect("the map has a free slot");
    Key const removed = map.insert(8).expect("the map has a free slot");
    static_cast<void>(map.remove(removed).expect("the key is live"));
    CRUCIBLE_FATAL_INVARIANT(*map.get(Key{live.raw()}).expect("the word of a live key finds its value") == 7);
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{(std::uint64_t{1} << 32) | 5u}).is_none());
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{std::uint64_t{2} << 32}).is_none());
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{std::uint64_t{2} << 32 | 1u}).is_none());
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{live.raw() + (std::uint64_t{256} << 32)}).is_none());
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{~std::uint64_t{0}}).is_none() && map.remove(Key{0}).is_none());
    CRUCIBLE_FATAL_INVARIANT(map.size() == 1);
}

// With a generation of 8 bits a slot takes 128 values, and the remove of the
// last one retires the slot: no later insert takes it, and no key of it finds
// a value again.
void test_generation_wrap_retires_the_slot() {
    SlotMap<int, HandleTag, std::uint8_t> map = map_of<int, std::uint8_t>(2);
    FixedList<Key, 128> keys;
    for (int turn = 0; turn < 128; ++turn) {
        Key const key = map.insert(turn).expect("the map has a free slot");
        CRUCIBLE_FATAL_INVARIANT(index_of(key) == 0 && generation_of(key) == static_cast<std::uint64_t>(2 * turn + 1));
        static_cast<void>(keys.append(key).expect("the list holds 128 keys"));
        CRUCIBLE_FATAL_INVARIANT(map.remove(key).expect("the key is live") == turn);
    }
    Key const next = map.insert(500).expect("the second slot is free");
    CRUCIBLE_FATAL_INVARIANT(index_of(next) == 1 && generation_of(next) == 1);
    CRUCIBLE_FATAL_INVARIANT(map.insert(501).is_err() && map.size() == 1);
    for (Key const key : keys) {
        CRUCIBLE_FATAL_INVARIANT(map.get(key).is_none());
    }
    CRUCIBLE_FATAL_INVARIANT(map.get(Key{0}).is_none() && map.get(Key{std::uint64_t{1} << 32}).is_none());
    static_cast<void>(map.remove(next).expect("the key is live"));
    Key const again = map.insert(502).expect("the second slot is free again");
    CRUCIBLE_FATAL_INVARIANT(index_of(again) == 1 && generation_of(again) == 3);
}

// The walk gives each value, and keys() each key, in the order of the slots,
// and both pass the free slots.
void test_walk_in_slot_order() {
    SlotMap<int, HandleTag> map = map_of<int>(8);
    FixedArray<Key, 6> keys{Key{0}, Key{0}, Key{0}, Key{0}, Key{0}, Key{0}};
    for (Index<6> const position : keys.indices()) {
        keys.at(position) = map.insert(static_cast<int>(position.raw()) * 10).expect("the map has a free slot");
    }
    static_cast<void>(map.remove(keys.at(::foundation::core::index<0>())).expect("the key is live"));
    static_cast<void>(map.remove(keys.at(::foundation::core::index<3>())).expect("the key is live"));
    int expected_sum = 0;
    int last = -1;
    for (int const value : map) {
        CRUCIBLE_FATAL_INVARIANT(value > last);
        last = value;
        expected_sum += value;
    }
    CRUCIBLE_FATAL_INVARIANT(expected_sum == 10 + 20 + 40 + 50);
    std::uint32_t last_index = 0;
    std::size_t walked = 0;
    for (Key const key : map.keys()) {
        CRUCIBLE_FATAL_INVARIANT(walked == 0 || index_of(key) > last_index);
        last_index = index_of(key);
        CRUCIBLE_FATAL_INVARIANT(map.get(key).is_some());
        ++walked;
    }
    CRUCIBLE_FATAL_INVARIANT(walked == 4 && last_index == 5);
    for (int& value : map) {
        value += 1;
    }
    CRUCIBLE_FATAL_INVARIANT(*map.get(keys.at(::foundation::core::index<5>())).expect("the key is live") == 51);
    SlotMap<int, HandleTag> const empty{};
    for (int const value : empty) {
        static_cast<void>(value);
        ::foundation::core::fatal("an empty map gives no value to a walk");
    }
}

// The slots never move, so a Ref of get names its value across each later
// insert.  The ASan run finds a read of freed storage if an insert moved them.
void test_ref_survives_inserts() {
    SlotMap<std::uint64_t, HandleTag> map = map_of<std::uint64_t>(512);
    Key const first = map.insert(77u).expect("the map has a free slot");
    auto const held = map.get(first).expect("the key is live");
    for (std::uint64_t value = 0; value < 511; ++value) {
        static_cast<void>(map.insert(value).expect("the map has a free slot"));
    }
    CRUCIBLE_FATAL_INVARIANT(*held == 77u && map.size() == 512);
}

// A value of a line of the cache.  Its map takes storage from the allocator
// of an alignment past 16, which takes a count of bytes that is a multiple of
// the alignment, so the ASan run refuses a count that is not.
struct alignas(64) Line final {
    std::uint64_t value = 0;
};

// Each slot of a map of an over-aligned value lies at the alignment of the
// value, and the values keep their contents.
void test_over_aligned_values() {
    SlotMap<Line, HandleTag> map = map_of<Line>(3);
    Key const first = map.insert(Line{11}).expect("the map has a free slot");
    Key const second = map.insert(Line{22}).expect("the map has a free slot");
    Key const third = map.insert(Line{33}).expect("the map has a free slot");
    auto const held = map.get(second).expect("the key is live");
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(held).is_aligned<64>());
    CRUCIBLE_FATAL_INVARIANT((*held).value == 22 && map.remove(first).expect("the key is live").value == 11);
    CRUCIBLE_FATAL_INVARIANT((*map.get(third).expect("the key is live")).value == 33 && map.size() == 2);
}

// A value that writes its label into a log at its end.  The brand of the Ref
// of the log is a template argument, so no fresh brand leaves its scope.
template <class LogRef>
struct Logged final {
    LogRef log;
    int label = 0;
    bool is_live = true;
    Logged(LogRef target, int from) noexcept : log{target}, label{from} {}
    Logged(Logged&& other) noexcept : log{other.log}, label{other.label} { other.is_live = false; }
    Logged(Logged const&) = delete;
    Logged& operator=(Logged&&) = delete;
    Logged& operator=(Logged const&) = delete;
    ~Logged() {
        if (is_live) {
            static_cast<void>((*log).append(label).expect("the log has room"));
        }
    }
};

// Each value ends exactly one time: at its remove, or at the end of the map
// in the order of the slots.
void test_each_value_ends_once_in_slot_order() {
    FixedList<int, 16> log;
    auto const log_ref = ::foundation::core::to(log);
    using Value = Logged<std::remove_const_t<decltype(log_ref)>>;
    {
        SlotMap<Value, HandleTag> map = map_of<Value>(8);
        FixedArray<Key, 4> keys{Key{0}, Key{0}, Key{0}, Key{0}};
        for (Index<4> const position : keys.indices()) {
            keys.at(position) =
                map.insert(Value{log_ref, static_cast<int>(position.raw())}).expect("the map has a free slot");
        }
        {
            Value const taken = map.remove(keys.at(::foundation::core::index<1>())).expect("the key is live");
            CRUCIBLE_FATAL_INVARIANT(taken.label == 1 && log.size() == 0);
        }
        CRUCIBLE_FATAL_INVARIANT(log.size() == 1);
        static_cast<void>(map.insert(Value{log_ref, 9}).expect("the map has a free slot"));
    }
    FixedArray<int, 5> const expected{1, 0, 9, 2, 3};
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::equal(log.view(), expected.view()));
}

// A move gives the slots to the target and leaves the source empty.
void test_moves() {
    SlotMap<int, HandleTag> source = map_of<int>(4);
    Key const key = source.insert(5).expect("the map has a free slot");
    SlotMap<int, HandleTag> target{std::move(source)};
    CRUCIBLE_FATAL_INVARIANT(*target.get(key).expect("the key is live") == 5 && target.capacity().raw() == 4);
    SlotMap<int, HandleTag> other = map_of<int>(2);
    other = std::move(target);
    CRUCIBLE_FATAL_INVARIANT(*other.get(key).expect("the key is live") == 5 && other.size() == 1);
}

void test_refusals(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            SlotMap<int, HandleTag> map = map_of<int>(2);
            static_cast<void>(map.insert(1).expect("the map has a free slot"));
            static_cast<void>(*map.end());
        },
        ::foundation::core::TextView{
            "foundation: fatal: a read or a step through the end cursor of a walk of a SlotMap"});
}

// ── The property test ──────────────────────────────────────────────────────

// The model of one slot.  A key of the slot is live when the slot holds a
// value and the key has the last generation that the slot gave.  A slot
// retires when a remove takes the value of the largest generation.
struct SlotModel {
    std::uint64_t generation = 0;
    std::uint64_t value = 0;
    bool is_live = false;
    bool is_retired = false;
};

// The property: random inserts, removes and reads, with live keys, stale keys
// and forged keys, against a model of the slots that knows nothing of the
// stack of the free slots.  Each key that insert gives has a generation above
// each earlier generation of its slot, so no two keys are equal.  A read finds
// a value exactly when the model holds it, the count agrees, the walk gives
// the live values in the order of the slots, and an insert gives Full exactly
// when each slot holds a value or retired.  A probe is a forged key, the key
// of the last insert, or a recent key, so the stack reuses its top slot often
// and the slots of a narrow generation retire.  The result is the count of
// the retired slots.
template <class Generation, std::uint32_t Capacity>
[[nodiscard]] std::size_t expect_property(std::uint64_t seed, int steps) {
    ::crucible::test::PhiloxStream stream{seed};
    SlotMap<std::uint64_t, HandleTag, Generation> map = map_of<std::uint64_t, Generation>(Capacity);
    FixedArray<SlotModel, Capacity> model{};
    // The words of some recent keys, and at the start some forged words.
    FixedArray<std::uint64_t, 64> recent{};
    for (Index<64> const position : recent.indices()) {
        recent.at(position) = stream.next_wide();
    }
    std::size_t live = 0;
    std::size_t retired = 0;
    std::uint64_t const largest = ::foundation::core::max_of<Generation>;
    auto const model_holds = [&model](Key key) noexcept {
        auto slot = Index<Capacity>::check(index_of(key));
        if (slot.is_none()) return false;
        SlotModel const& entry = model.at(std::move(slot).expect("the index is below the capacity"));
        return entry.is_live && entry.generation == generation_of(key);
    };
    std::uint64_t last_inserted = 0;
    for (int step = 0; step < steps; ++step) {
        std::uint64_t const choice = stream.below(10);
        std::uint64_t const source = stream.below(16);
        Key const probe = source == 0 ? Key{stream.next_wide() & 0x000001FF0000007Full}
                        : source < 8  ? Key{last_inserted}
                                      : Key{recent.at(Index<64>::check(stream.below(64)).expect("below 64"))};
        if (choice < 5) {
            std::uint64_t const value = stream.next_wide();
            auto inserted = map.insert(value);
            CRUCIBLE_FATAL_INVARIANT(inserted.is_err() == (live + retired == Capacity));
            if (inserted.is_ok()) {
                Key const key = std::move(inserted).expect("the insert gave a key");
                SlotModel& entry =
                    model.at(Index<Capacity>::check(index_of(key)).expect("an index below the capacity"));
                CRUCIBLE_FATAL_INVARIANT(!entry.is_live && !entry.is_retired);
                CRUCIBLE_FATAL_INVARIANT(generation_of(key) > entry.generation && (generation_of(key) & 1u) == 1u);
                entry = SlotModel{generation_of(key), value, true, false};
                ++live;
                recent.at(Index<64>::check(stream.below(64)).expect("below 64")) = key.raw();
                last_inserted = key.raw();
            }
        } else if (choice < 8) {
            bool const is_held = model_holds(probe);
            auto removed = map.remove(probe);
            CRUCIBLE_FATAL_INVARIANT(removed.is_some() == is_held);
            if (is_held) {
                SlotModel& entry =
                    model.at(Index<Capacity>::check(index_of(probe)).expect("an index below the capacity"));
                CRUCIBLE_FATAL_INVARIANT(std::move(removed).expect("the key is live") == entry.value);
                entry.is_live = false;
                --live;
                if (entry.generation == largest) {
                    entry.is_retired = true;
                    ++retired;
                }
            }
        } else {
            bool const is_held = model_holds(probe);
            auto found = map.get(probe);
            CRUCIBLE_FATAL_INVARIANT(found.is_some() == is_held);
            if (is_held) {
                std::uint64_t const held = *std::move(found).expect("the key is live");
                CRUCIBLE_FATAL_INVARIANT(held
                                         == model.at(Index<Capacity>::check(index_of(probe)).expect("below")).value);
            }
        }
        CRUCIBLE_FATAL_INVARIANT(map.size() == live && map.is_empty() == (live == 0));
        std::uint32_t at = 0;
        std::size_t walked = 0;
        for (Key const key : map.keys()) {
            while (!model.at(Index<Capacity>::check(at).expect("below the capacity")).is_live) {
                ++at;
            }
            std::uint64_t const expected = model.at(Index<Capacity>::check(at).expect("below the capacity")).value;
            CRUCIBLE_FATAL_INVARIANT(index_of(key) == at && model_holds(key));
            CRUCIBLE_FATAL_INVARIANT(*map.get(key).expect("the key is live") == expected);
            ++at;
            ++walked;
        }
        CRUCIBLE_FATAL_INVARIANT(walked == live);
    }
    return retired;
}

void test_map_agrees_with_the_model() {
    std::size_t const narrow_few = expect_property<std::uint8_t, 8>(0x51A70001u, 40000);
    std::size_t const narrow_many = expect_property<std::uint8_t, 64>(0x51A70064u, 20000);
    std::size_t const wide = expect_property<std::uint32_t, 16>(0x51A73216u, 8192);
    CRUCIBLE_FATAL_INVARIANT(narrow_few != 0 && narrow_many != 0 && wide == 0);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_insert_get_remove();
    test_full_map();
    test_linear_values();
    test_forged_keys();
    test_generation_wrap_retires_the_slot();
    test_walk_in_slot_order();
    test_ref_survives_inserts();
    test_over_aligned_values();
    test_each_value_ends_once_in_slot_order();
    test_moves();
    test_refusals(test);
    test_map_agrees_with_the_model();
    return 0;
}
