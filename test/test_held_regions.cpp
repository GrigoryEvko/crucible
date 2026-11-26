// The cells of the regions that a foreground holds (crucible/HeldRegions.h).
//
// A read takes the regions of one finished write, and it gives none while a
// write stands open.  It waits for nothing, so the test reads inside an open
// write on the thread of the writer.  The last case runs a writer and a reader
// on two threads, and the thread sanitizer reports no race on the cells.

#include <crucible/HeldRegions.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using crucible::ConstRegionLink;

// The foreground of the test.  It is the Owner of the cells, so only it opens a
// write.
class TestForeground final {
public:
    using Holders = crucible::HeldRegions<TestForeground>;

    template <class Body>
    void write(Body&& body) noexcept {
        held_.with_write_(std::forward<Body>(body));
    }

    // Writes one region into each cell.
    void write_all(ConstRegionLink region) noexcept {
        write([region](auto store_cell) noexcept {
            for (const Holders::Position position : ::fixy::IndexRange<Holders::CAP>{})
                store_cell(position, region);
        });
    }

    [[nodiscard]] Holders const& held() const& noexcept { return held_; }
    void held() const&& = delete("the cells of a temporary foreground end with the full expression");

private:
    Holders held_{};
};

using Holders = TestForeground::Holders;

static_assert(!std::is_copy_constructible_v<Holders> && !std::is_move_constructible_v<Holders>);
static_assert(!std::is_copy_assignable_v<Holders> && !std::is_move_assignable_v<Holders>);

// Only the Owner opens a write.
template <class Cells>
concept OpensWrite = requires(Cells& cells) { cells.with_write_([](auto) noexcept {}); };
static_assert(!OpensWrite<Holders>);

// An arena of nodes of regions, which outlives each link to one of them.
class Regions final {
public:
    [[nodiscard]] ConstRegionLink make() noexcept {
        return ConstRegionLink::some(::foundation::core::with_arena_frame(arena_, [](auto& frame) noexcept {
            return crucible::mint_dag_node<crucible::RegionNode>(::foundation::effects::testing::test().alloc, frame)
                .weaken();
        }));
    }

private:
    ::fixy::BumpArena<crucible::DagArena> arena_ = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 14});
};

// The generation of the last write, or none while a write stands open.
[[nodiscard]] ::fixy::Option<uint64_t> read_generation(Holders const& held) noexcept {
    return held.try_read().map([](Holders::Read read) noexcept { return read.generation; });
}

void test_read_takes_the_last_write() {
    Regions regions{};
    TestForeground foreground{};
    const ConstRegionLink first = regions.make();
    const ConstRegionLink second = regions.make();

    CRUCIBLE_TEST_ASSERT(kTest, read_generation(foreground.held()) == ::fixy::some(uint64_t{0}));
    CRUCIBLE_TEST_ASSERT(kTest, !foreground.held().holds(first));

    foreground.write([&first, &second](auto store_cell) noexcept {
        store_cell(::fixy::index<0>(), first);
        store_cell(Holders::Position::check(Holders::CAP - 1).expect("the last cell"), second);
    });
    CRUCIBLE_TEST_ASSERT(kTest, read_generation(foreground.held()) == ::fixy::some(uint64_t{1}));
    CRUCIBLE_TEST_ASSERT(kTest, foreground.held().holds(first) && foreground.held().holds(second));
    uint32_t named = 0;
    for (const Holders::Read& read : foreground.held().try_read()) {
        for (const Holders::Position position : read.regions.indices()) {
            const ConstRegionLink region = read.regions.at(position);
            if (region.is_none()) continue;
            ++named;
            const bool is_first = position.raw() == 0 && ::fixy::same_target(region, first);
            const bool is_second = position.raw() == Holders::CAP - 1 && ::fixy::same_target(region, second);
            CRUCIBLE_TEST_ASSERT(kTest, is_first || is_second);
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, named == 2);

    foreground.write([](auto store_cell) noexcept { store_cell(::fixy::index<0>(), ::fixy::none); });
    CRUCIBLE_TEST_ASSERT(kTest, read_generation(foreground.held()) == ::fixy::some(uint64_t{2}));
    CRUCIBLE_TEST_ASSERT(kTest, !foreground.held().holds(first) && foreground.held().holds(second));
    ::fixy::check::pass(kTest, "  read_takes_the_last_write: PASSED\n");
}

// A read inside an open write gives none, at each place of the write, and the
// read after it gives the regions of that write.
void test_read_gives_none_while_a_write_stands_open() {
    Regions regions{};
    TestForeground foreground{};
    const ConstRegionLink region = regions.make();
    uint32_t refused = 0;
    foreground.write([&foreground, &region, &refused](auto store_cell) noexcept {
        refused += foreground.held().try_read().is_none() ? 1u : 0u;
        for (const Holders::Position position : ::fixy::IndexRange<Holders::CAP>{}) {
            store_cell(position, region);
            refused += foreground.held().try_read().is_none() ? 1u : 0u;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, refused == Holders::CAP + 1);
    CRUCIBLE_TEST_ASSERT(kTest, read_generation(foreground.held()) == ::fixy::some(uint64_t{1}));
    ::fixy::check::pass(kTest, "  read_gives_none_while_a_write_stands_open: PASSED\n");
}

// Two threads: the writer stores one region into each cell of a write, and the
// region follows from the generation.  So a read that mixes the cells of two
// writes shows two regions, or a region of another generation.  The reader
// ends when it reads the last write.
void test_a_read_mixes_no_two_writes() {
    constexpr uint64_t WRITES = 400000;
    Regions regions{};
    TestForeground foreground{};
    const ::fixy::FixedArray<ConstRegionLink, 2> links{regions.make(), regions.make()};
    foreground.write_all(links.at(::fixy::index<0>()));

    auto write_each = [&foreground, &links] noexcept {
        for (uint64_t generation = 2; generation <= WRITES; ++generation) {
            foreground.write_all(generation % 2 == 1 ? links.at(::fixy::index<0>()) : links.at(::fixy::index<1>()));
        }
        return uint64_t{0};
    };
    auto read_each = [&foreground, &links] noexcept {
        uint64_t mixed = 0;
        uint64_t last = 0;
        while (last != WRITES) {
            for (const Holders::Read& read : foreground.held().try_read()) {
                const ConstRegionLink expected =
                    read.generation % 2 == 1 ? links.at(::fixy::index<0>()) : links.at(::fixy::index<1>());
                for (const ConstRegionLink& region : read.regions)
                    mixed += ::fixy::same_target(region, expected) ? 0u : 1u;
                mixed += read.generation < last ? 1u : 0u;
                last = read.generation;
            }
            CRUCIBLE_SPIN_PAUSE;
        }
        return mixed;
    };
    auto const counts = ::fixy::thread::run_joined_raw_probe(
                            ::fixy::BgLoadCtx{::foundation::effects::testing::bg()}, kTest,
                            "one writer and one reader of the cells of a foreground", ::fixy::thread::ThreadOptions{},
                            std::move(write_each), std::move(read_each))
                            .expect("the probe starts two threads");
    CRUCIBLE_TEST_ASSERT(kTest, counts.at(::fixy::index<1>()) == 0 && "a read took the cells of two writes");
    CRUCIBLE_TEST_ASSERT(kTest, read_generation(foreground.held()) == ::fixy::some(WRITES));
    ::fixy::check::pass(kTest, "  a_read_mixes_no_two_writes: PASSED\n");
}

}  // namespace

int main() {
    test_read_takes_the_last_write();
    test_read_gives_none_while_a_write_stands_open();
    test_a_read_mixes_no_two_writes();
    ::fixy::check::pass(kTest, "test_held_regions: all tests PASSED\n");
    return 0;
}
