// Each check of the base that guards memory holds under the ignore semantic.
// The build gives this file the ignore semantic, where a CRUCIBLE_PRE is only
// an assumption for the optimizer.  A false assumption about an index is
// undefined behavior: the optimizer can remove a later test, and the access
// then reads or writes past its run with no report.  So each check of an
// index, a window or a count is a fatal invariant, and each violation below
// ends the process with the text of its check.
//
// Each death runs in a child process through expect_dies of the Check family,
// which requires SIGABRT and the text of the failed invariant.  The controls
// then use each door inside its bounds.  The legal uses of the chunk map need
// a reservation of a window, and test_alloc_chunk_map holds them.
//
// The doors: AppendOnly, the clock of HappensBeforeLattice, AffinityMask, the
// window record of the allocator, bits_to_string, ct::eq and static_run.

#include <fixy/ConstantTime.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/check/Check.h>
#include <fixy/session/Registry.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Enumerate.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;
namespace lattices = ::foundation::algebra::lattices;
namespace transition = ::fixy::session::transition;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;
using Clock = lattices::HappensBeforeLattice<2>;
using lattices::AffinityMask;

// The value comes back through a call that no optimization reads, so each
// index below is a value of the run time.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// A log of count values from 1.  The count is opaque, so the optimizer cannot
// see the log.
[[nodiscard]] ::fixy::AppendOnly<int> log_of(int count) {
    ::fixy::AppendOnly<int> log = ::fixy::mint_append_only<int>();
    for (int value = 1; value <= opaque(count); ++value) {
        log.append(::foundation::effects::testing::test().alloc, value);
    }
    return log;
}

void read_append_only_past_end() noexcept {
    ::fixy::AppendOnly<int> const log = log_of(3);
    (void)opaque(log[opaque(std::size_t{3})]);
}
void read_front_of_empty_append_only() noexcept {
    ::fixy::AppendOnly<int> const log = log_of(0);
    (void)opaque(log.front());
}

void read_clock_past_end() noexcept { (void)opaque(Clock::bottom()[opaque(std::size_t{2})]); }
void step_clock_past_end() noexcept { (void)opaque(Clock::successor_at(Clock::bottom(), opaque(std::size_t{2}))); }
void merge_clock_past_end() noexcept {
    (void)opaque(Clock::causal_merge(Clock::bottom(), Clock::bottom(), opaque(std::size_t{2})));
}

void make_mask_past_end() noexcept { (void)opaque(AffinityMask::single(opaque(std::uint16_t{1024}))); }
void read_mask_past_end() noexcept { (void)opaque(AffinityMask{}.contains(opaque(std::uint16_t{1024}))); }
void make_range_of_reversed_cores() noexcept {
    (void)opaque(AffinityMask::range(opaque(std::uint16_t{2000}), opaque(std::uint16_t{3})));
}
void make_range_past_end() noexcept {
    (void)opaque(AffinityMask::range(opaque(std::uint16_t{0}), opaque(std::uint16_t{1024})));
}

// The empty record of a window holds no address and no chunk.
void read_chunk_outside_window() noexcept {
    (void)opaque(alloc::WindowRecord{}.chunk_of(opaque(alloc::AddressWord{4096})));
}
void read_address_past_chunks() noexcept {
    (void)opaque(alloc::WindowRecord{}.address_of(opaque(alloc::ChunkIndex{0})));
}

enum class Signal : std::uint8_t {
    Alpha = 0x01,
};

void render_into_null_buffer() noexcept {
    (void)opaque(::foundation::reflect::bits_to_string<Signal>(std::uint8_t{0x01}, nullptr, opaque(std::size_t{100})));
}

constexpr ::fixy::FixedArray<std::byte, 2> two_bytes{std::byte{0x42}, std::byte{0x43}};

void compare_views_of_two_lengths() noexcept {
    (void)opaque(
        ::fixy::ct::eq(::fixy::View<std::byte const>{}, opaque(::fixy::View<std::byte const>{two_bytes.view()})));
}

constexpr ::fixy::FixedArray<int, 3> run_values{4, 5, 6};

consteval transition::static_run<int> run_of_three() {
    return transition::static_run<int>{&*run_values.get(0).expect("three values"), 3};
}

void read_static_run_past_end() noexcept {
    constexpr transition::static_run<int> run = run_of_three();
    (void)opaque(run[opaque(std::size_t{3})]);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    expect_dies(test, read_append_only_past_end, ::fixy::TextView{"fatal invariant failed: i < size()"});
    expect_dies(test, read_front_of_empty_append_only, ::fixy::TextView{"fatal invariant failed: !empty()"});
    expect_dies(test, read_clock_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::size_t>(p, 0, N - 1)"});
    expect_dies(test, step_clock_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::size_t>(p, 0, N - 1)"});
    expect_dies(test, merge_clock_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::size_t>(me, 0, N - 1)"});
    expect_dies(test, make_mask_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::uint16_t>(core, 0, "
                                 "kMaxCore)"});
    expect_dies(test, read_mask_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::uint16_t>(core, 0, "
                                 "kMaxCore)"});
    expect_dies(test, make_range_of_reversed_cores,
                ::fixy::TextView{"fatal invariant failed: first_core <= last_core"});
    expect_dies(test, make_range_past_end,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::in_range<std::uint16_t>(last_core, 0, "
                                 "kMaxCore)"});
    expect_dies(test, read_chunk_outside_window, ::fixy::TextView{"fatal invariant failed: is_in_window(address)"});
    expect_dies(test, read_address_past_chunks,
                ::fixy::TextView{"fatal invariant failed: chunk.raw() < chunk_count()"});
    expect_dies(test, render_into_null_buffer,
                ::fixy::TextView{"fatal invariant failed: ::foundation::decide::valid_span(cap, out)"});
    expect_dies(test, compare_views_of_two_lengths, ::fixy::TextView{"fatal invariant failed: a.size() == b.size()"});
    expect_dies(test, read_static_run_past_end, ::fixy::TextView{"fatal invariant failed: index < length"});

    // The controls: each door inside its bounds.
    ::fixy::AppendOnly<int> const log = log_of(3);
    CRUCIBLE_TEST_ASSERT(test, log[opaque(std::size_t{2})] == 3 && log.front() == 1);
    Clock::element_type const stepped = Clock::successor_at(Clock::bottom(), opaque(std::size_t{1}));
    CRUCIBLE_TEST_ASSERT(test, stepped[opaque(std::size_t{1})] == 1 && stepped[opaque(std::size_t{0})] == 0);
    CRUCIBLE_TEST_ASSERT(test, Clock::causal_merge(stepped, Clock::bottom(), opaque(std::size_t{0}))[0] == 1);
    CRUCIBLE_TEST_ASSERT(test, AffinityMask::single(opaque(std::uint16_t{1023})).contains(opaque(std::uint16_t{1023})));
    CRUCIBLE_TEST_ASSERT(test, AffinityMask::range(opaque(std::uint16_t{2}), opaque(std::uint16_t{5})).popcount()
                                   == std::uint16_t{4});
    CRUCIBLE_TEST_ASSERT(
        test, ::foundation::reflect::bits_to_string<Signal>(std::uint8_t{0x01}, nullptr, opaque(std::size_t{0})) == 5);
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::ct::eq(two_bytes.view(), opaque(::fixy::View<std::byte const>{two_bytes.view()})));
    constexpr transition::static_run<int> run = run_of_three();
    CRUCIBLE_TEST_ASSERT(test, run[opaque(std::size_t{2})] == 6);

    pass(test, "test_memory_checks_under_ignore: PASSED\n");
    return 0;
}
