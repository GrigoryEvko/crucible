// Tests of foundation/core/Ref.h: Box and mint_box.

#include <foundation/core/Ref.h>

#include <fixy/Ctx.h>
#include <fixy/ScopedView.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Box;
using ::foundation::core::mint_box;

// The allocation tag of foundation/effects passes the gate, and the other
// capability tags do not.
static_assert(::foundation::core::AllocationCapability<::foundation::effects::Alloc>);
static_assert(!::foundation::core::AllocationCapability<::foundation::effects::IO>);
static_assert(!::foundation::core::AllocationCapability<::foundation::effects::Block>);

// The count of live CountedObject objects.  A Box must destroy each object
// one time and leak none.
int live_objects = 0;

class CountedObject {
public:
    explicit CountedObject(int label) noexcept : label_{label} { ++live_objects; }
    CountedObject(CountedObject const&) = delete;
    CountedObject& operator=(CountedObject const&) = delete;
    ~CountedObject() { --live_objects; }

    [[nodiscard]] int label() const noexcept { return label_; }
    void relabel(int label) noexcept { label_ = label; }

private:
    int label_ = 0;
};

struct Pair {
    int first = 0;
    int second = 0;
};

void test_mint_and_access() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    Box<int> number = mint_box<int>(alloc, 41);
    CRUCIBLE_FATAL_INVARIANT(*number == 41);
    *number += 1;
    CRUCIBLE_FATAL_INVARIANT(*number == 42);

    Box<Pair> pair = mint_box<Pair>(alloc, 3, 4);
    CRUCIBLE_FATAL_INVARIANT(pair->first == 3 && pair->second == 4);
    pair->second = 5;
    Box<Pair> const& fixed_pair = pair;
    CRUCIBLE_FATAL_INVARIANT(fixed_pair->second == 5 && (*fixed_pair).first == 3);
    CRUCIBLE_FATAL_INVARIANT(fixed_pair.view()->second == 5);

    Box<int> zero = mint_box<int>(alloc);
    CRUCIBLE_FATAL_INVARIANT(*zero == 0);

    // A Ref from view() reaches the object of the Box, and a write through
    // it shows through the Box.  Each call of view() gives a brand of its own.
    auto const pair_ref = pair.view();
    static_assert(!std::is_same_v<decltype(pair.view()), decltype(pair.view())>);
    pair_ref->first = 8;
    CRUCIBLE_FATAL_INVARIANT(pair->first == 8);
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::same_object(pair_ref, fixed_pair.view()));
}

void test_destruction_counts() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        Box<CountedObject> first = mint_box<CountedObject>(alloc, 1);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        Box<CountedObject> second{std::move(first)};
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(second->label() == 1);

        Box<CountedObject> third = mint_box<CountedObject>(alloc, 3);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 2);
        // The assignment destroys the object that the target held.
        third = std::move(second);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(third->label() == 1);
        third->relabel(9);
        CRUCIBLE_FATAL_INVARIANT((*third).label() == 9);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

void test_self_move_assignment_keeps_the_object() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        Box<CountedObject> held = mint_box<CountedObject>(alloc, 7);
        Box<CountedObject>& alias = held;
        held = std::move(alias);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(held->label() == 7);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

// A payload that counts its live objects and can move.
int live_movables = 0;

class CountedMovable {
public:
    explicit CountedMovable(int label) noexcept : label_{label} { ++live_movables; }
    CountedMovable(CountedMovable&& other) noexcept : label_{other.label_} {
        other.label_ = 0;
        ++live_movables;
    }
    CountedMovable(CountedMovable const&) = delete;
    CountedMovable& operator=(CountedMovable const&) = delete;
    CountedMovable& operator=(CountedMovable&&) = delete;
    ~CountedMovable() { --live_movables; }

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

void test_consume() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        Box<CountedMovable> held = mint_box<CountedMovable>(alloc, 13);
        CRUCIBLE_FATAL_INVARIANT(live_movables == 1);
        CountedMovable const taken = std::move(held).consume();
        // The moved-from object in the storage is destroyed, so one object
        // lives: the one that consume() gave back.
        CRUCIBLE_FATAL_INVARIANT(live_movables == 1);
        CRUCIBLE_FATAL_INVARIANT(taken.label() == 13);
    }
    CRUCIBLE_FATAL_INVARIANT(live_movables == 0);
    CRUCIBLE_FATAL_INVARIANT(mint_box<int>(alloc, 17).consume() == 17);
}

// The death test consumes a moved-from Box on purpose: it holds no object,
// and the precondition ends the process.  The consume runs in a child through
// expect_dies of the Check family.
void test_consume_of_moved_from_aborts(::fixy::TestRunnerCtx const& test) {
    Box<int> source = mint_box<int>(::foundation::effects::testing::test().alloc, 1);
    Box<int> target{std::move(source)};
    ::fixy::check::expect_dies(
        test, [&source] noexcept { static_cast<void>(std::move(source).consume()); },
        ::foundation::core::TextView{
            "foundation: fatal: consume of a Box that holds no object: the Box was moved from or consumed"});
    CRUCIBLE_FATAL_INVARIANT(*target == 1);
}

// The death test borrows a moved-from Box on purpose: each build tests for
// the object, so the borrow ends the process and reads no null pointer.
void test_borrow_of_moved_from_aborts(::fixy::TestRunnerCtx const& test) {
    Box<int> source = mint_box<int>(::foundation::effects::testing::test().alloc, 2);
    Box<int> target{std::move(source)};
    ::fixy::check::expect_dies(
        test, [&source] noexcept { static_cast<void>(source.view()); },
        ::foundation::core::TextView{
            "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
    ::fixy::check::expect_dies(
        test, [&source] noexcept { static_cast<void>(*source); },
        ::foundation::core::TextView{
            "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
    Box<int> const& fixed_source = source;
    ::fixy::check::expect_dies(
        test, [&fixed_source] noexcept { static_cast<void>(fixed_source.view()); },
        ::foundation::core::TextView{
            "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
    ::fixy::check::expect_dies(
        test, [&fixed_source] noexcept { static_cast<void>(*fixed_source); },
        ::foundation::core::TextView{
            "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
    CRUCIBLE_FATAL_INVARIANT(*target == 2);
}

// The allocation door gives malloc storage up to an alignment of 16 and
// aligned_alloc storage above it.  Each alignment from 8 to 4096 gives a
// Box at that alignment, and its object holds its value.
template <std::size_t Alignment>
struct alignas(Alignment) AlignedValue {
    std::uint64_t value = 0;
};

template <std::size_t Alignment>
void test_box_at_alignment() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    for (std::uint64_t trial = 0; trial < 8; ++trial) {
        Box<AlignedValue<Alignment>> held = mint_box<AlignedValue<Alignment>>(alloc, AlignedValue<Alignment>{trial});
        CRUCIBLE_FATAL_INVARIANT(std::bit_cast<std::uintptr_t>(&*held) % Alignment == 0);
        CRUCIBLE_FATAL_INVARIANT(held->value == trial);
    }
}

void test_each_alignment() {
    test_box_at_alignment<8>();
    test_box_at_alignment<16>();
    test_box_at_alignment<32>();
    test_box_at_alignment<64>();
    test_box_at_alignment<128>();
    test_box_at_alignment<4096>();
}

// The property: boxes built from random values, moved along a random
// chain, give back the values they were built from.  A plain model holds
// the values.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::crucible::test::PhiloxStream stream{0x5EEDB0C5000000A1u};
    for (int trial = 0; trial < 512; ++trial) {
        std::uint64_t const model_value = stream.next_wide();
        Box<std::uint64_t> source = mint_box<std::uint64_t>(alloc, model_value);
        int const hops = static_cast<int>(stream.below(5));
        Box<std::uint64_t> current{std::move(source)};
        for (int hop = 0; hop < hops; ++hop) {
            Box<std::uint64_t> next{std::move(current)};
            current = std::move(next);
        }
        CRUCIBLE_FATAL_INVARIANT(*current == model_value);
    }
}

// An owner that holds a Box of a type that this point only declares.  The
// owner defines its constructor and its destructor below, where the type is
// complete, as an owner in a header and its source file do.
struct Engine;

class EngineOwner {
public:
    explicit EngineOwner(int level) noexcept;
    EngineOwner(EngineOwner const&) = delete;
    EngineOwner& operator=(EngineOwner const&) = delete;
    ~EngineOwner();

    [[nodiscard]] int power() const noexcept;

private:
    Box<Engine> engine_;
};

// A reflection walk of the owner reads each member of the Box.  The type of
// the object of the Box is not complete at this point.
static_assert(::fixy::no_scoped_view_field_check<EngineOwner>());

struct Engine {
    explicit Engine(int value) noexcept : power{value} { ++live_objects; }
    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;
    ~Engine() { --live_objects; }

    int power = 0;
};

EngineOwner::EngineOwner(int level) noexcept
    : engine_{mint_box<Engine>(::foundation::effects::testing::test().alloc, level)} {}

EngineOwner::~EngineOwner() = default;

int EngineOwner::power() const noexcept { return engine_->power; }

// The owner makes the object of the declared type and ends it one time.
void test_box_of_a_declared_type() {
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
    {
        EngineOwner const owner{7};
        CRUCIBLE_FATAL_INVARIANT(owner.power() == 7);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

// An owner that holds an Option of a Box of a type that this point only
// declares.  The Option reads the members of the Box here, where the type is
// not complete.  The owner defines its special members below, where the type
// is complete.
struct Gear;

class GearOwner {
public:
    GearOwner() noexcept;
    GearOwner(GearOwner const&) = delete;
    GearOwner& operator=(GearOwner const&) = delete;
    ~GearOwner();

    void fit(int teeth) noexcept;
    [[nodiscard]] bool has_gear() const noexcept;
    [[nodiscard]] int take_teeth() noexcept;

private:
    ::foundation::core::Option<Box<Gear>> gear_{};
};

struct Gear {
    explicit Gear(int count) noexcept : teeth{count} { ++live_movables; }
    Gear(Gear&& other) noexcept : teeth{other.teeth} { ++live_movables; }
    Gear(Gear const&) = delete;
    Gear& operator=(Gear const&) = delete;
    Gear& operator=(Gear&&) = delete;
    ~Gear() { --live_movables; }

    int teeth = 0;
};

GearOwner::GearOwner() noexcept = default;

GearOwner::~GearOwner() = default;

void GearOwner::fit(int teeth) noexcept {
    gear_ = ::foundation::core::some(mint_box<Gear>(::foundation::effects::testing::test().alloc, teeth));
}

bool GearOwner::has_gear() const noexcept { return gear_.is_some(); }

int GearOwner::take_teeth() noexcept {
    Gear const taken = std::move(gear_).expect("the owner holds a gear").consume();
    return taken.teeth;
}

// The Option of the owner starts empty.  The owner fills it with a Box and
// consumes the Box, then fills it again.  The destructor of the owner ends
// the last object.
void test_option_of_a_box_of_a_declared_type() {
    CRUCIBLE_FATAL_INVARIANT(live_movables == 0);
    {
        GearOwner owner{};
        CRUCIBLE_FATAL_INVARIANT(!owner.has_gear());
        owner.fit(12);
        CRUCIBLE_FATAL_INVARIANT(owner.has_gear() && live_movables == 1);
        CRUCIBLE_FATAL_INVARIANT(owner.take_teeth() == 12);
        CRUCIBLE_FATAL_INVARIANT(!owner.has_gear() && live_movables == 0);
        owner.fit(5);
        CRUCIBLE_FATAL_INVARIANT(owner.has_gear() && live_movables == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_movables == 0);
}

// The authority of the words of the tests below.  Its members build its key.
class WordCourier {
public:
    template <class T>
    [[nodiscard]] static ::foundation::core::BoxWord<T, WordCourier> send(Box<T>&& box) noexcept {
        return std::move(box).release_to_word(::foundation::core::box_word_key<WordCourier>{});
    }

    template <class T>
    [[nodiscard]] static Box<T> receive(::foundation::core::BoxWord<T, WordCourier> word) noexcept {
        return ::foundation::core::claim_box(word, ::foundation::core::box_word_key<WordCourier>{});
    }
};

// A Box gives its object to a word and holds none after.  The claim of the
// word gives a Box of the same object, and the object ends one time.
void test_word_round_trip(::fixy::TestRunnerCtx const& test) {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
    {
        Box<CountedObject> sent = mint_box<CountedObject>(alloc, 21);
        ::foundation::core::Address const address = ::foundation::core::address_of(sent.view());
        ::foundation::core::BoxWord<CountedObject, WordCourier> const word = WordCourier::send(std::move(sent));
        CRUCIBLE_FATAL_INVARIANT(!word.is_empty());
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        ::fixy::check::expect_dies(
            test, [&sent] { static_cast<void>(*sent); }, "a borrow of a Box that holds no object");

        Box<CountedObject> received = WordCourier::receive(word);
        CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(received.view()) == address);
        CRUCIBLE_FATAL_INVARIANT(received->label() == 21);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

// The claim of the empty word ends the process.  The release of an empty Box
// ends it too.
void test_word_of_no_object_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test, [] { static_cast<void>(WordCourier::receive(::foundation::core::BoxWord<int, WordCourier>{})); },
        "a claim of the empty box word");
    ::foundation::core::BoxWord<int, WordCourier> const empty_word{};
    CRUCIBLE_FATAL_INVARIANT(empty_word.is_empty());

    Box<int> source = mint_box<int>(::foundation::effects::testing::test().alloc, 3);
    Box<int> target{std::move(source)};
    ::fixy::check::expect_dies(
        test, [&source] { static_cast<void>(WordCourier::send(std::move(source))); },
        "a borrow of a Box that holds no object");
    CRUCIBLE_FATAL_INVARIANT(*target == 3);
}

}  // namespace

int main() {
    test_mint_and_access();
    test_destruction_counts();
    test_self_move_assignment_keeps_the_object();
    test_consume();
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_consume_of_moved_from_aborts(test);
    test_borrow_of_moved_from_aborts(test);
    test_each_alignment();
    test_agrees_with_plain_model();
    test_box_of_a_declared_type();
    test_option_of_a_box_of_a_declared_type();
    test_word_round_trip(test);
    test_word_of_no_object_aborts(test);
    return 0;
}
