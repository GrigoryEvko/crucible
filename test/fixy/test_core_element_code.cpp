// Tests of the code of an element that reaches the owner of the element: the
// end or the move of an element reads the owner, or it changes the owner.
//
// An owner holds a live element in each slot that it counts while such code
// runs, so a read finds no ended element.  A change by such code ends the
// process before it works on the storage of the door that runs the code.
//
// Each death runs in a child process through expect_dies of the Check family,
// which requires SIGABRT and the text of the refusal.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/handle/Once.h>
#include <foundation/Brand.h>
#include <foundation/core/Array.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

[[nodiscard]] ::foundation::effects::Alloc allocation() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The reads of an owner by the code of an element, and what they found.
constinit std::uint32_t reads_of_an_owner = 0;
constinit bool is_ended_element_seen = false;

// Reads each element of an owner.
template <class Element>
void read_each(core::View<Element const> view) noexcept {
    ++reads_of_an_owner;
    for (Element const& element : view)
        is_ended_element_seen = is_ended_element_seen || !element.is_live();
}

// Tests that the code of the elements read their owner count times and found
// live elements only, and starts the next case.
void check_whole_reads(std::uint32_t count) {
    CRUCIBLE_TEST_ASSERT(kTest, reads_of_an_owner == count && !is_ended_element_seen);
    reads_of_an_owner = 0;
}

// ── A box and an array ─────────────────────────────────────────────────────

// What the code of an object does to the box and to the array.
enum class Turn : std::uint8_t {
    Nothing,
    ReadBox,
    ReplaceBox,
    DropArray,
    ReplaceArray,
};

constinit Turn turn_at_end = Turn::Nothing;
constinit Turn turn_at_move = Turn::Nothing;

void take_turn(Turn& turn) noexcept;

// An object of the box and an element of the array.
class Turning final {
public:
    explicit Turning(std::uint32_t label) noexcept : label_{label} {}
    Turning(Turning&& other) noexcept : label_{other.label_} { take_turn(turn_at_move); }
    Turning& operator=(Turning&& other) noexcept {
        label_ = other.label_;
        return *this;
    }
    Turning(Turning const&) = delete("an object of the test has one identity");
    Turning& operator=(Turning const&) = delete("an object of the test has one identity");
    ~Turning() { take_turn(turn_at_end); }

    [[nodiscard]] std::uint32_t label() const noexcept { return label_; }

private:
    std::uint32_t label_ = 0;
};

[[nodiscard]] core::Box<Turning> make_box(std::uint32_t label) noexcept {
    return core::mint_box<Turning>(allocation(), label);
}
[[nodiscard]] core::HeapArray<Turning> make_array(std::size_t count) noexcept {
    return core::mint_heap_array_with<Turning>(
        allocation(), count, [](std::size_t at) noexcept { return Turning{static_cast<std::uint32_t>(at)}; });
}

[[nodiscard]] core::Box<Turning>& reached_box() noexcept {
    static core::Box<Turning> box = make_box(1);
    return box;
}
[[nodiscard]] core::HeapArray<Turning>& reached_array() noexcept {
    static core::HeapArray<Turning> array = make_array(4);
    return array;
}

// The code of the object, one time.
void take_turn(Turn& turn) noexcept {
    Turn const taken = turn;
    turn = Turn::Nothing;
    switch (taken) {
        case Turn::Nothing:
        default:
            return;
        case Turn::ReadBox:
            static_cast<void>(reached_box()->label());
            return;
        case Turn::ReplaceBox:
            reached_box() = make_box(2);
            return;
        case Turn::DropArray:
            reached_array() = core::HeapArray<Turning>{};
            return;
        case Turn::ReplaceArray:
            reached_array() = make_array(2);
            return;
    }
}

constexpr ::fixy::TextView kEmptyBox{"a borrow of a Box that holds no object: the Box was moved from or consumed"};

// A consume empties the Box before the object moves, so a new object that the
// move puts into the Box stays.  A Hole takes the storage of a Box that holds no
// object before the old object ends.
void test_the_code_of_an_object_finds_an_empty_box() {
    reached_box() = make_box(1);
    turn_at_move = Turn::ReplaceBox;
    Turning const taken = std::move(reached_box()).consume();
    CRUCIBLE_TEST_ASSERT(kTest, taken.label() == 1 && reached_box()->label() == 2);

    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            reached_box() = make_box(1);
            turn_at_end = Turn::ReadBox;
            core::mint_hole(std::move(reached_box())).consume();
        },
        kEmptyBox);
    ::fixy::check::pass(kTest, "  test_the_code_of_an_object_finds_an_empty_box: PASSED\n");
}

// An exchange holds the storage of the array while the code of an element runs.
// So an element that drops the array drops nothing, and an element that puts a
// new array there ends the process.
void test_the_code_of_an_element_finds_an_empty_array_in_an_exchange() {
    reached_array() = make_array(4);
    turn_at_move = Turn::DropArray;
    Turning const held = reached_array().exchange(1, Turning{7});
    CRUCIBLE_TEST_ASSERT(kTest, turn_at_move == Turn::Nothing);
    CRUCIBLE_TEST_ASSERT(kTest, held.label() == 1 && reached_array().size() == 4);
    CRUCIBLE_TEST_ASSERT(
        kTest, reached_array().get(1).is_some_and([](auto const& element) noexcept { return element->label() == 7; }));

    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            reached_array() = make_array(4);
            turn_at_move = Turn::ReplaceArray;
            static_cast<void>(reached_array().exchange(1, Turning{7}).label());
        },
        ::fixy::TextView{"the code of an element changed a HeapArray while an exchange moved an element"});
    ::fixy::check::pass(kTest, "  test_the_code_of_an_element_finds_an_empty_array_in_an_exchange: PASSED\n");
}

// ── A slot map ─────────────────────────────────────────────────────────────

// What the code of a value does to the map.
enum class Step : std::uint8_t {
    Nothing,
    Walk,
    Get,
    Remove,
    Insert,
    DropMap,
};

constinit Step step_at_end = Step::Nothing;
constinit Step step_at_move = Step::Nothing;
constinit std::uint64_t reached_key_word = 0;
constinit std::uint32_t values_taken_by_the_code = 0;

void take_step(Step& step) noexcept;

// A value of the map.  It is live from the end of its constructor to its move or
// its end.
class Mapped final {
public:
    Mapped() noexcept = default;
    Mapped(Mapped&& other) noexcept : is_live_{false} {
        other.is_live_ = false;
        take_step(step_at_move);
        is_live_ = true;
    }
    Mapped(Mapped const&) = delete("a value of the test has one identity");
    Mapped& operator=(Mapped const&) = delete("a value of the test has one identity");
    ~Mapped() {
        is_live_ = false;
        take_step(step_at_end);
    }

    [[nodiscard]] bool is_live() const noexcept { return is_live_; }

private:
    bool is_live_ = true;
};

struct KeyTag final {};

[[nodiscard]] auto make_map() noexcept {
    return core::mint_slot_map<Mapped, KeyTag>(allocation(), core::Quantity<core::unit::Element, std::uint32_t>{8});
}
using Map = decltype(make_map());

[[nodiscard]] Map& reached_map() noexcept {
    static Map map = make_map();
    return map;
}

[[nodiscard]] Map::key_type insert_value() noexcept {
    return reached_map().insert(Mapped{}).expect("the map has a free slot");
}

// The code of the value, one time.
void take_step(Step& step) noexcept {
    Step const taken = step;
    step = Step::Nothing;
    switch (taken) {
        case Step::Nothing:
        default:
            return;
        case Step::Walk:
            ++reads_of_an_owner;
            for (Mapped const& value : ::fixy::as_const(reached_map()))
                is_ended_element_seen = is_ended_element_seen || !value.is_live();
            return;
        case Step::Get:
            ++reads_of_an_owner;
            is_ended_element_seen =
                is_ended_element_seen || ::fixy::as_const(reached_map()).get(Map::key_type{reached_key_word}).is_some();
            return;
        case Step::Remove:
            ++reads_of_an_owner;
            if (reached_map().remove(Map::key_type{reached_key_word}).is_some()) ++values_taken_by_the_code;
            return;
        case Step::Insert:
            ++reads_of_an_owner;
            static_cast<void>(insert_value());
            return;
        case Step::DropMap:
            reached_map() = Map{};
            return;
    }
}

// Runs a remove or an insert of a map of two values, and gives a step to its next
// end and to its next move.  Then each slot holds at most one value: two more
// inserts give two keys that no value of the map has.
void run_map_door(bool is_insert, Step at_end, Step at_move) {
    reached_map() = make_map();
    static_cast<void>(insert_value());
    Map::key_type const key = insert_value();
    reached_key_word = key.raw();
    step_at_end = at_end;
    step_at_move = at_move;
    if (is_insert) {
        static_cast<void>(insert_value());
    } else {
        CRUCIBLE_TEST_ASSERT(kTest, reached_map().remove(key).is_some());
    }
    CRUCIBLE_TEST_ASSERT(kTest, step_at_end == Step::Nothing && step_at_move == Step::Nothing);
    CRUCIBLE_TEST_ASSERT(kTest, values_taken_by_the_code == 0);

    std::size_t const size = reached_map().size();
    Map::key_type const first = insert_value();
    Map::key_type const second = insert_value();
    CRUCIBLE_TEST_ASSERT(kTest, first != second && reached_map().size() == size + 2);
    std::size_t walked = 0;
    for (Mapped const& value : ::fixy::as_const(reached_map())) {
        CRUCIBLE_TEST_ASSERT(kTest, value.is_live());
        ++walked;
    }
    CRUCIBLE_TEST_ASSERT(kTest, walked == size + 2);
    check_whole_reads(1);
}

// A slot holds a value only while no code of a value works on it.  So the code
// finds no value behind the key of a remove, and no value that an insert moves.
void test_the_code_of_a_value_finds_live_values_only() {
    run_map_door(false, Step::Walk, Step::Nothing);
    run_map_door(false, Step::Get, Step::Nothing);
    run_map_door(false, Step::Remove, Step::Nothing);
    run_map_door(false, Step::Insert, Step::Nothing);
    run_map_door(false, Step::Nothing, Step::Walk);
    run_map_door(false, Step::Nothing, Step::Get);
    run_map_door(false, Step::Nothing, Step::Remove);
    run_map_door(false, Step::Nothing, Step::Insert);
    run_map_door(true, Step::Nothing, Step::Walk);
    run_map_door(true, Step::Nothing, Step::Insert);
    ::fixy::check::pass(kTest, "  test_the_code_of_a_value_finds_live_values_only: PASSED\n");
}

// The outer door works on the storage of the map, so a move or the end of the map
// by the code of a value ends the process.
void test_the_end_of_a_map_by_the_code_of_a_value_ends_the_process() {
    constexpr ::fixy::TextView refusal{
        "a move or the end of a SlotMap runs in the code of its value, while a door of the map runs that code"};
    ::fixy::check::expect_dies(kTest, [] noexcept { run_map_door(false, Step::DropMap, Step::Nothing); }, refusal);
    ::fixy::check::expect_dies(kTest, [] noexcept { run_map_door(false, Step::Nothing, Step::DropMap); }, refusal);
    ::fixy::check::expect_dies(kTest, [] noexcept { run_map_door(true, Step::Nothing, Step::DropMap); }, refusal);
    ::fixy::check::pass(kTest, "  test_the_end_of_a_map_by_the_code_of_a_value_ends_the_process: PASSED\n");
}

// ── An Option ──────────────────────────────────────────────────────────────

// What the code of a payload does to the Option.
enum class Look : std::uint8_t {
    Nothing,
    Read,
    Take,
    Fill,
    Assign,
    Clear,
};

constinit Look look_at_end = Look::Nothing;
constinit Look look_at_move = Look::Nothing;
constinit std::uint32_t payloads_taken_by_the_code = 0;

void look_around(Look& look) noexcept;

// A payload of the Option.  It is live from the end of its constructor to its move
// or its end.
class Payload final {
public:
    Payload() noexcept = default;
    Payload(Payload&& other) noexcept : is_live_{false} {
        other.is_live_ = false;
        look_around(look_at_move);
        is_live_ = true;
    }
    Payload(Payload const&) = delete("a payload of the test has one identity");
    Payload& operator=(Payload const&) = delete("a payload of the test has one identity");
    ~Payload() {
        is_live_ = false;
        look_around(look_at_end);
    }

    [[nodiscard]] bool is_live() const noexcept { return is_live_; }

private:
    bool is_live_ = true;
};

[[nodiscard]] core::Option<Payload>& reached_option() noexcept {
    static core::Option<Payload> option{core::none};
    return option;
}

// The code of the payload, one time.
void look_around(Look& look) noexcept {
    Look const taken = look;
    look = Look::Nothing;
    switch (taken) {
        case Look::Nothing:
        default:
            return;
        case Look::Read:
            ++reads_of_an_owner;
            ::fixy::as_const(reached_option())
                .match([](Payload const& payload) noexcept { is_ended_element_seen |= !payload.is_live(); },
                       []() noexcept {});
            return;
        case Look::Take:
            ++reads_of_an_owner;
            if (reached_option().take().is_some()) ++payloads_taken_by_the_code;
            return;
        case Look::Fill:
            reached_option().fill(Payload{});
            return;
        case Look::Assign:
            reached_option() = core::Option<Payload>::some(Payload{});
            return;
        case Look::Clear:
            reached_option() = core::Option<Payload>{core::none};
            return;
    }
}

// The doors of an Option that end its payload or move it out, or that build one.
enum class Change : std::uint8_t {
    Take,
    Clear,
    Assign,
    Fill,
};

// Runs a door of the Option, which is full, or empty for a fill, and gives a look to
// the next end and to the next move of a payload.
void run_option_door(Change change, Look at_end, Look at_move) {
    reached_option() = core::Option<Payload>{core::none};
    if (change != Change::Fill) reached_option().fill(Payload{});
    core::Option<Payload> next = core::Option<Payload>::some(Payload{});
    Payload value{};
    look_at_end = at_end;
    look_at_move = at_move;
    switch (change) {
        case Change::Take:
            CRUCIBLE_TEST_ASSERT(kTest, reached_option().take().is_some());
            break;
        case Change::Clear:
            reached_option() = core::Option<Payload>{core::none};
            break;
        case Change::Assign:
            reached_option() = std::move(next);
            break;
        case Change::Fill:
        default:
            reached_option().fill(std::move(value));
            break;
    }
    CRUCIBLE_TEST_ASSERT(kTest, look_at_end == Look::Nothing && look_at_move == Look::Nothing);
    CRUCIBLE_TEST_ASSERT(kTest, payloads_taken_by_the_code == 0);
    bool const is_full = change == Change::Assign || change == Change::Fill;
    CRUCIBLE_TEST_ASSERT(kTest, reached_option().is_some() == is_full);
    auto const is_live = [](Payload const& payload) noexcept { return payload.is_live(); };
    CRUCIBLE_TEST_ASSERT(kTest, !is_full || reached_option().is_some_and(is_live));
}

// An Option is empty while the code of its payload runs, so a read by that code
// finds no payload that moves or ends, and a take finds none.
void test_the_code_of_a_payload_finds_an_empty_option() {
    constexpr ::fixy::FixedArray<Look, 2> looks{Look::Read, Look::Take};
    for (Look const look : looks) {
        run_option_door(Change::Take, look, Look::Nothing);
        check_whole_reads(1);
        run_option_door(Change::Take, Look::Nothing, look);
        check_whole_reads(1);
        run_option_door(Change::Clear, look, Look::Nothing);
        check_whole_reads(1);
        run_option_door(Change::Fill, Look::Nothing, look);
        check_whole_reads(1);
    }
    // The first end of an assignment ends the moved-from payload of its source.
    run_option_door(Change::Assign, Look::Read, Look::Nothing);
    check_whole_reads(1);
    ::fixy::check::pass(kTest, "  test_the_code_of_a_payload_finds_an_empty_option: PASSED\n");
}

// The outer door works on the storage of the Option, so a change by the code of the
// payload ends the process.
void test_a_change_by_the_code_of_a_payload_ends_the_process() {
    constexpr ::fixy::TextView refusal{"or while the code of the payload runs"};
    constexpr ::fixy::FixedArray<Look, 3> changes{Look::Fill, Look::Assign, Look::Clear};
    for (Look const change : changes) {
        ::fixy::check::expect_dies(
            kTest, [change] noexcept { run_option_door(Change::Take, change, Look::Nothing); }, refusal);
        ::fixy::check::expect_dies(
            kTest, [change] noexcept { run_option_door(Change::Take, Look::Nothing, change); }, refusal);
        ::fixy::check::expect_dies(
            kTest, [change] noexcept { run_option_door(Change::Clear, change, Look::Nothing); }, refusal);
        ::fixy::check::expect_dies(
            kTest, [change] noexcept { run_option_door(Change::Fill, Look::Nothing, change); }, refusal);
    }
    ::fixy::check::pass(kTest, "  test_a_change_by_the_code_of_a_payload_ends_the_process: PASSED\n");
}

// ── The end of an owner ────────────────────────────────────────────────────

// The holder of an owner of each kind.  An element of the owner reaches the
// holder through a Ref.
struct ArrayTag final {};
struct BoxTag final {};
struct MapTag final {};
struct LazyTag final {};
struct LazyCallTag final {};

template <class Tag>
struct Holder;
template <class Tag>
struct HolderBrand final {};
template <class Tag>
using HolderRef = core::Ref<Holder<Tag>, HolderBrand<Tag>>;

// An element whose end reads the owner that its holder holds.
template <class Tag>
class Ending final {
public:
    explicit Ending(HolderRef<Tag> holder) noexcept : holder_{holder} {}
    Ending(Ending&&) noexcept = default;
    Ending(Ending const&) = delete("an element of the test has one identity");
    Ending& operator=(Ending const&) = delete("an element of the test has one identity");
    ~Ending() {
        is_live_ = false;
        holder_->read();
    }

    [[nodiscard]] bool is_live() const noexcept { return is_live_; }

private:
    HolderRef<Tag> holder_;
    bool is_live_ = true;
};

template <>
struct Holder<ArrayTag> final {
    using brand_type = HolderBrand<ArrayTag>;
    core::HeapArray<Ending<ArrayTag>> held{};

    void read() const noexcept { read_each(held.view()); }
};

template <>
struct Holder<MapTag> final {
    using brand_type = HolderBrand<MapTag>;
    struct[[= ::foundation::brand::arena_member_brand{}]] SlotTag final {};
    core::SlotMap<Ending<MapTag>, KeyTag, SlotTag> held = core::mint_member_slot_map<Ending<MapTag>, KeyTag, SlotTag>(
        allocation(), core::Quantity<core::unit::Element, std::uint32_t>{4});

    void read() const noexcept {
        ++reads_of_an_owner;
        for (Ending<MapTag> const& value : held)
            is_ended_element_seen = is_ended_element_seen || !value.is_live();
    }
};

template <>
struct Holder<LazyTag> final {
    using brand_type = HolderBrand<LazyTag>;
    ::fixy::handle::Lazy<Ending<LazyTag>> held{};

    void read() const noexcept {
        ++reads_of_an_owner;
        is_ended_element_seen = is_ended_element_seen || held.initialized() || held.get().is_some();
    }
};

template <>
struct Holder<LazyCallTag> final {
    using brand_type = HolderBrand<LazyCallTag>;
    ::fixy::handle::Lazy<Ending<LazyCallTag>> held{};

    void read() noexcept {
        static_cast<void>(
            held.get_or_init(kTest, [this]() noexcept { return Ending<LazyCallTag>{core::to(*this)}; })->is_live());
    }
};

template <>
struct Holder<BoxTag> final {
    using brand_type = HolderBrand<BoxTag>;
    core::Box<Ending<BoxTag>> held;

    Holder() noexcept : held{core::mint_box<Ending<BoxTag>>(allocation(), core::to(*this))} {}
    Holder(Holder const&) = delete("an element of the box names its holder");
    Holder& operator=(Holder const&) = delete("an element of the box names its holder");

    void read() const noexcept { static_cast<void>(held->is_live()); }
};

// The end of an owner empties it before it ends its elements, so the end of each
// element finds an empty owner.
void test_the_end_of_an_owner_empties_it_first() {
    {
        Holder<ArrayTag> holder{};
        holder.held = core::mint_heap_array_with<Ending<ArrayTag>>(
            allocation(), 3, [&holder](std::size_t) noexcept { return Ending<ArrayTag>{core::to(holder)}; });
    }
    check_whole_reads(3);
    {
        Holder<MapTag> holder{};
        for (std::uint32_t at = 0; at != 3; ++at)
            static_cast<void>(holder.held.insert(Ending<MapTag>{core::to(holder)}).expect("the map has a free slot"));
        // The end of each moved-from value read the map before this point.
        reads_of_an_owner = 0;
    }
    check_whole_reads(3);
    {
        Holder<LazyTag> holder{};
        static_cast<void>(
            holder.held.get_or_init(kTest, [&holder]() noexcept { return Ending<LazyTag>{core::to(holder)}; })
                ->is_live());
    }
    check_whole_reads(1);
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            Holder<LazyCallTag> holder{};
            holder.read();
        },
        ::fixy::TextView{"get_or_init of a Lazy runs in the code of its value, while the Lazy ends that value"});
    ::fixy::check::expect_dies(kTest, [] noexcept { Holder<BoxTag> const holder{}; }, kEmptyBox);
    ::fixy::check::pass(kTest, "  test_the_end_of_an_owner_empties_it_first: PASSED\n");
}

}  // namespace

int main() {
    test_the_code_of_an_object_finds_an_empty_box();
    test_the_code_of_an_element_finds_an_empty_array_in_an_exchange();
    test_the_code_of_a_value_finds_live_values_only();
    test_the_end_of_a_map_by_the_code_of_a_value_ends_the_process();
    test_the_code_of_a_payload_finds_an_empty_option();
    test_a_change_by_the_code_of_a_payload_ends_the_process();
    test_the_end_of_an_owner_empties_it_first();
    ::fixy::check::pass(kTest, "test_core_element_code: all tests passed\n");
    return 0;
}
