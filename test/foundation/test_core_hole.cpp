// Tests of the Hole of foundation/core/Ref.h: Hole and its two mints, and
// the mint of a Box in a Hole.
//
// The target links with the linker option --wrap for malloc, aligned_alloc,
// calloc and free (test/foundation/CMakeLists.txt).  Each call of the
// allocation door of foundation/core/Heap.h then goes through a wrapper of
// this file, which counts it.  The shared libraries of the process, such as
// libstdc++, call the allocator with no wrapper, so the counts hold only the
// calls of the code that this link holds.

#include <foundation/core/Ref.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The calls of the allocator of the C library that this link holds.  The
// test runs on one thread.
std::size_t allocator_takes = 0;
std::size_t allocator_gives = 0;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the linker wrap of the allocator of the C library counts the calls of the door")
extern "C" {
void* __real_malloc(std::size_t bytes);
void* __real_aligned_alloc(std::size_t alignment, std::size_t bytes);
void* __real_calloc(std::size_t count, std::size_t bytes);
void __real_free(void* storage);
void* __wrap_malloc(std::size_t bytes);
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t bytes);
void* __wrap_calloc(std::size_t count, std::size_t bytes);
void __wrap_free(void* storage);

void* __wrap_malloc(std::size_t bytes) {
    ++allocator_takes;
    return __real_malloc(bytes);
}
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t bytes) {
    ++allocator_takes;
    return __real_aligned_alloc(alignment, bytes);
}
void* __wrap_calloc(std::size_t count, std::size_t bytes) {
    ++allocator_takes;
    return __real_calloc(count, bytes);
}
void __wrap_free(void* storage) {
    ++allocator_gives;
    __real_free(storage);
}
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

using ::foundation::core::Box;
using ::foundation::core::Hole;
using ::foundation::core::mint_box;
using ::foundation::core::mint_hole;

// The count of live Counted objects.  A Box in a Hole must destroy each
// object one time and leak none.
int live_objects = 0;

class Counted {
public:
    explicit Counted(std::uint64_t label) noexcept : label_{label} { ++live_objects; }
    Counted(Counted const&) = delete;
    Counted& operator=(Counted const&) = delete;
    ~Counted() { --live_objects; }

    [[nodiscard]] std::uint64_t label() const noexcept { return label_; }

private:
    std::uint64_t label_ = 0;
};

using CountedHole = Hole<sizeof(Counted), alignof(Counted)>;

// A Hole from the allocation door takes one allocation, and its consume
// gives the storage back.
void test_mint_and_consume() {
    std::size_t const takes = allocator_takes;
    std::size_t const gives = allocator_gives;
    CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes + 1);
    std::move(hole).consume();
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 1);
}

// The property of the credit: N steps from a Box to a Hole and back call
// the allocation door one time, keep one storage, and end each object one
// time.
void test_steps_take_one_allocation() {
    constexpr std::uint64_t steps = 64;
    std::size_t const takes = allocator_takes;
    std::size_t const gives = allocator_gives;
    CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    for (std::uint64_t step = 0; step < steps; ++step) {
        Box<Counted> box = mint_box<Counted>(std::move(hole), step);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(box->label() == step);
        hole = mint_hole(std::move(box));
        CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
    }
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes + 1);
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives);
    std::move(hole).consume();
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 1);
}

// A Box in a Hole holds the storage of the Hole, and a Hole from that Box
// gives the same storage again.
void test_box_keeps_the_storage_of_the_hole() {
    CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    Box<Counted> first = mint_box<Counted>(std::move(hole), std::uint64_t{1});
    auto const first_address = ::foundation::core::address_of(first.view());
    CountedHole again = mint_hole(std::move(first));
    Box<Counted> second = mint_box<Counted>(std::move(again), std::uint64_t{2});
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(second.view()) == first_address);
    CRUCIBLE_FATAL_INVARIANT(second->label() == 2);
}

// The end of a Box built in a Hole gives the storage back with the size and
// the alignment of the Hole, as a Box from the allocation door does.
void test_end_of_box_in_hole_gives_the_storage_back() {
    std::size_t const gives = allocator_gives;
    {
        CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
        Box<Counted> box = mint_box<Counted>(std::move(hole), std::uint64_t{5});
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 1);
}

// A move leaves the source empty, and a move assignment into a spent Hole
// takes the storage of the source with no call of the door.  The drop door
// ends a Hole through its destructor, which gives the storage back.
void test_move_and_drop() {
    std::size_t const takes = allocator_takes;
    std::size_t const gives = allocator_gives;
    CountedHole source = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    CountedHole target = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    std::move(target).consume();
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 1);
    target = std::move(source);
    CRUCIBLE_FATAL_INVARIANT(allocator_takes == takes + 2);
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 1);
    CountedHole moved{std::move(target)};
    ::foundation::linear::drop(std::move(moved));
    CRUCIBLE_FATAL_INVARIANT(allocator_gives == gives + 2);
}

// Each alignment from 8 to 4096 gives a Hole at that alignment, and a Box
// built in it reads its value.
template <std::size_t Alignment>
struct alignas(Alignment) AlignedValue {
    std::uint64_t value = 0;
};

template <std::size_t Alignment>
void test_hole_at_alignment() {
    using Value = AlignedValue<Alignment>;
    auto hole = mint_hole<sizeof(Value), alignof(Value)>(::foundation::effects::Alloc{});
    Box<Value> box = mint_box<Value>(std::move(hole), Value{Alignment});
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::address_of(box.view()).template is_aligned<Alignment>());
    CRUCIBLE_FATAL_INVARIANT(box->value == Alignment);
}

void test_each_alignment() {
    test_hole_at_alignment<8>();
    test_hole_at_alignment<16>();
    test_hole_at_alignment<32>();
    test_hole_at_alignment<64>();
    test_hole_at_alignment<4096>();
}

// The two helpers spend a Hole two times on purpose.  The first spend gives
// the storage back, and the second spend finds no storage, so each build
// ends the process.  A Hole behind an lvalue reference is no object that
// the rule must-consume tracks, so the helpers hold the second spend.
void consume_two_times(CountedHole& hole) {
    CountedHole taken{std::move(hole)};
    std::move(taken).consume();
    std::move(hole).consume();
}

void build_in_spent_hole(CountedHole& hole) {
    CountedHole taken{std::move(hole)};
    std::move(taken).consume();
    static_cast<void>(mint_box<Counted>(std::move(hole), std::uint64_t{3}));
}

// The probe jumps out of the body with no destructor call, and the first
// spend gave the storage back, so the body owns no allocation.  The consume
// after each helper is the one spend of the Hole of the body.
void test_spend_of_moved_from_aborts() {
    bool const consume_aborts = ::crucible::test::aborts([] {
        CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
        consume_two_times(hole);
        std::move(hole).consume();
    });
    bool const build_aborts = ::crucible::test::aborts([] {
        CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
        build_in_spent_hole(hole);
        std::move(hole).consume();
    });
    CRUCIBLE_FATAL_INVARIANT(consume_aborts && build_aborts);
}

// The probe gives up a moved-from Box on purpose: it holds no object, and
// the mint of a Hole ends the process.
void test_hole_of_moved_from_box_aborts() {
    CountedHole hole = mint_hole<sizeof(Counted), alignof(Counted)>(::foundation::effects::Alloc{});
    Box<Counted> source = mint_box<Counted>(std::move(hole), std::uint64_t{4});
    Box<Counted> target{std::move(source)};
    bool const hole_aborts = ::crucible::test::aborts([&source] { mint_hole(std::move(source)).consume(); });
    CRUCIBLE_FATAL_INVARIANT(hole_aborts);
    CRUCIBLE_FATAL_INVARIANT(target->label() == 4);
}

}  // namespace

int main() {
    test_mint_and_consume();
    test_steps_take_one_allocation();
    test_box_keeps_the_storage_of_the_hole();
    test_end_of_box_in_hole_gives_the_storage_back();
    test_move_and_drop();
    test_each_alignment();
    test_spend_of_moved_from_aborts();
    test_hole_of_moved_from_box_aborts();
    return 0;
}
