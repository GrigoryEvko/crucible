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
#include <foundation/Brand.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
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

// ── The end of an owner ────────────────────────────────────────────────────

// The holder of an owner of each kind.  An element of the owner reaches the
// holder through a Ref.
struct ArrayTag final {};
struct BoxTag final {};

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
    ::fixy::check::expect_dies(kTest, [] noexcept { Holder<BoxTag> const holder{}; }, kEmptyBox);
    ::fixy::check::pass(kTest, "  test_the_end_of_an_owner_empties_it_first: PASSED\n");
}

}  // namespace

int main() {
    test_the_code_of_an_object_finds_an_empty_box();
    test_the_code_of_an_element_finds_an_empty_array_in_an_exchange();
    test_the_end_of_an_owner_empties_it_first();
    ::fixy::check::pass(kTest, "test_core_element_code: all tests passed\n");
    return 0;
}
