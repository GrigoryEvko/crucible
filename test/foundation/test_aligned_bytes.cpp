// Tests of the blocks of foundation/AlignedBytes.h.
//
// mint_aligned_bytes gives an UnwrittenBytes, which has no view.  fill and
// write give the AlignedBytes, and each byte then holds the value of its
// write.  mint_aligned_bytes_zeroed gives an AlignedBytes whose bytes each
// read zero.  The block keeps the alignment of the Layout that the mint got
// at run time, also the alignment of a huge page.  The empty block is the
// default constructor of each state, and a Layout has no zero bytes.  A move
// gives the block to the target.  The doors of a Layout refuse an alignment
// that is not a power of two, so no such request reaches a mint.

#include <foundation/AlignedBytes.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::AlignedBytes;
using ::foundation::mint_aligned_bytes;
using ::foundation::mint_aligned_bytes_zeroed;
using ::foundation::UnwrittenBytes;
using ::foundation::alloc::Alignment;
using ::foundation::alloc::Layout;
using ::foundation::core::ByteCount;

// The layout of bytes bytes at the alignment, which the test gives as a
// layout.
[[nodiscard]] Layout layout_of(ByteCount bytes, Alignment alignment) noexcept {
    return ::foundation::alloc::mint_layout(bytes, alignment).expect("the test gives a layout");
}

// The allocation tag passes the gate of the two mints, and a different tag
// does not.
static_assert(::foundation::CanMintAlignedBytes<::foundation::effects::Alloc>);
static_assert(!::foundation::CanMintAlignedBytes<::foundation::effects::IO>);

// The plain mint gives the write-only state, and the zeroed mint gives the
// readable state.
static_assert(std::is_same_v<decltype(mint_aligned_bytes(::foundation::effects::testing::test().alloc,
                                                         Layout::of<std::uint8_t>())),
                             UnwrittenBytes>);
static_assert(std::is_same_v<decltype(mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc,
                                                                Layout::of<std::uint8_t>())),
                             AlignedBytes>);

// True when the first byte of the block is on the alignment Align.
template <std::size_t Align>
[[nodiscard]] bool first_byte_is_aligned(AlignedBytes& block) noexcept {
    return ::foundation::core::address_of(block.view().get(0).expect("the block holds a byte"))
        .template is_aligned<Align>();
}

// The sum of the bytes of a block.  Complexity: O(size()).
[[nodiscard]] std::size_t sum_of(AlignedBytes const& block) noexcept {
    std::size_t total = 0;
    for (std::uint8_t const byte : block.view()) {
        total += byte;
    }
    return total;
}

// The empty block has no byte and no storage, and no Layout has zero bytes.
void test_empty_block() {
    UnwrittenBytes const unwritten{};
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(unwritten.alignment() == 0);

    AlignedBytes const unminted{};
    CRUCIBLE_FATAL_INVARIANT(unminted.size() == 0 && unminted.alignment() == 0);
    CRUCIBLE_FATAL_INVARIANT(unminted.view().size() == 0);

    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{0}, Alignment{256}).is_err());
}

// The block keeps each alignment of the run time, from below the alignment
// of malloc up to a huge page, from each mint.
void test_alignments() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes small = mint_aligned_bytes(alloc, layout_of(ByteCount{3}, Alignment{8})).fill(std::uint8_t{0});
    CRUCIBLE_FATAL_INVARIANT(small.size() == 3 && small.alignment() == 8);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<8>(small));

    AlignedBytes line = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{1000}, Alignment{256}));
    CRUCIBLE_FATAL_INVARIANT(line.size() == 1000 && line.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<256>(line));

    AlignedBytes huge = mint_aligned_bytes_zeroed(
        alloc, layout_of(ByteCount{::foundation::huge_page_bytes + 1}, Alignment{::foundation::huge_page_bytes}));
    CRUCIBLE_FATAL_INVARIANT(huge.size() == ::foundation::huge_page_bytes + 1);
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<::foundation::huge_page_bytes>(huge));

    AlignedBytes written = mint_aligned_bytes(alloc, layout_of(ByteCount{5}, Alignment{4096})).fill(std::uint8_t{1});
    CRUCIBLE_FATAL_INVARIANT(first_byte_is_aligned<4096>(written));
}

// The zeroed mint gives zero in each byte, at each alignment, also for a block
// that the allocator maps new and for a block of its free list that an
// earlier block wrote.
void test_zeroed_mint_reads_zero() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        AlignedBytes dirty =
            mint_aligned_bytes(alloc, layout_of(ByteCount{4096}, Alignment{64})).fill(std::uint8_t{0xEE});
        CRUCIBLE_FATAL_INVARIANT(sum_of(dirty) == 4096 * std::size_t{0xEE});
    }
    AlignedBytes const small = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{4096}, Alignment{64}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(small) == 0);
    AlignedBytes const odd = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{17}, Alignment{8}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(odd) == 0 && odd.view().size() == 17);
    AlignedBytes const large = mint_aligned_bytes_zeroed(
        alloc, layout_of(ByteCount{std::size_t{33} << 20}, Alignment{::foundation::huge_page_bytes}));
    CRUCIBLE_FATAL_INVARIANT(sum_of(large) == 0);
}

// fill writes its value into each byte, and the const View reads them.  A
// write through the View of the AlignedBytes reaches the byte.
void test_fill_reaches_each_byte() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block =
        mint_aligned_bytes(alloc, layout_of(ByteCount{4096 + 17}, Alignment{64})).fill(std::uint8_t{0xCD});
    *block.view().get(4096 + 16).expect("the last byte") = 7;
    AlignedBytes const& read_only = block;
    CRUCIBLE_FATAL_INVARIANT(sum_of(read_only) == (4096 + 16) * std::size_t{0xCD} + 7);
    CRUCIBLE_FATAL_INVARIANT(!read_only.view().get(4096 + 17).is_some());
}

// write lends a WriteLease that writes the bytes in order, and the block then
// holds them.
void test_write_in_order() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, layout_of(ByteCount{6}, Alignment{16}));
    AlignedBytes const block =
        std::move(unwritten).write([](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept {
            lease.put(std::uint8_t{1});
            lease.put(std::uint8_t{2});
            ::foundation::core::FixedArray<std::uint8_t, 4> const rest{3, 4, 5, 6};
            lease.put_run(rest.view());
        });
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(block.size() == 6 && sum_of(block) == 21);
    CRUCIBLE_FATAL_INVARIANT(*block.view().get(5).expect("the last byte") == 6);
}

// A move gives the block to the target and leaves the source empty.  A move
// assignment gives the old block of the target back first.
void test_move() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes first = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{512}, Alignment{256}));
    *first.view().get(0).expect("the first byte") = 42;
    AlignedBytes second{std::move(first)};
    CRUCIBLE_FATAL_INVARIANT(first.size() == 0 && first.alignment() == 0 && first.view().size() == 0);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 512 && second.alignment() == 256);
    CRUCIBLE_FATAL_INVARIANT(*second.view().get(0).expect("the first byte") == 42);

    AlignedBytes third = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{64}, Alignment{64}));
    third = std::move(second);
    CRUCIBLE_FATAL_INVARIANT(second.size() == 0);
    CRUCIBLE_FATAL_INVARIANT(third.size() == 512 && *third.view().get(0).expect("the first byte") == 42);
    third.reset();
    CRUCIBLE_FATAL_INVARIANT(third.size() == 0 && third.view().size() == 0);

    UnwrittenBytes unwritten = mint_aligned_bytes(alloc, layout_of(ByteCount{128}, Alignment{64}));
    UnwrittenBytes taken{std::move(unwritten)};
    CRUCIBLE_FATAL_INVARIANT(unwritten.size() == 0 && taken.size() == 128 && taken.alignment() == 64);
    taken = mint_aligned_bytes(alloc, layout_of(ByteCount{32}, Alignment{32}));
    CRUCIBLE_FATAL_INVARIANT(taken.size() == 32);
}

// The doors of a Layout refuse an alignment that is not a power of two, and
// a size whose round up to the alignment passes the largest object, with an
// error.  So no such request reaches a mint, and the process goes on.
void test_layouts_refuse_bad_requests() {
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{64}, Alignment{24}).is_err());
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{64}, Alignment{0}).is_err());
    CRUCIBLE_FATAL_INVARIANT(::foundation::alloc::mint_layout(ByteCount{~std::size_t{0}}, Alignment{64}).is_err());
}

using ::foundation::ByteWindow;
using ::foundation::WindowRefusal;
using ::foundation::with_mut_windows;
using ::foundation::core::FixedList;
using ::foundation::core::side_as;

using Start = ::foundation::core::side::WindowStart;
using Length = ::foundation::core::side::WindowLength;

// The list of the windows, in their order.
template <class... Windows>
    requires(std::is_same_v<Windows, ByteWindow> && ...)
[[nodiscard]] FixedList<ByteWindow, sizeof...(Windows)> make_windows(Windows... windows) noexcept {
    FixedList<ByteWindow, sizeof...(Windows)> list{};
    (static_cast<void>(list.append(windows).expect("the list holds each window")), ...);
    return list;
}

// The windows of the tests: the bytes [start, start + length) of a block.
constexpr ByteWindow kFirst16{side_as<Start>(ByteCount{0}), side_as<Length>(ByteCount{16})};
constexpr ByteWindow kAt8Of16{side_as<Start>(ByteCount{8}), side_as<Length>(ByteCount{16})};
constexpr ByteWindow kAt16Of16{side_as<Start>(ByteCount{16}), side_as<Length>(ByteCount{16})};
constexpr ByteWindow kAt32Of16{side_as<Start>(ByteCount{32}), side_as<Length>(ByteCount{16})};
constexpr ByteWindow kAt64Of16{side_as<Start>(ByteCount{64}), side_as<Length>(ByteCount{16})};
constexpr ByteWindow kAt64Of32{side_as<Start>(ByteCount{64}), side_as<Length>(ByteCount{32})};
constexpr ByteWindow kFirst36{side_as<Start>(ByteCount{0}), side_as<Length>(ByteCount{36})};
constexpr ByteWindow kAt240Of32{side_as<Start>(ByteCount{240}), side_as<Length>(ByteCount{32})};
constexpr ByteWindow kAt2Of4{side_as<Start>(ByteCount{2}), side_as<Length>(ByteCount{4})};
constexpr ByteWindow kFirst6{side_as<Start>(ByteCount{0}), side_as<Length>(ByteCount{6})};
constexpr ByteWindow kAt8Empty{side_as<Start>(ByteCount{8}), side_as<Length>(ByteCount{0})};

// Lends the windows of a block to body through the frame door, puts the block
// back in its name, and gives the Result.
template <class T, class Windows, class Body>
[[nodiscard]] auto lend_windows(AlignedBytes& block, Windows const& windows, Body&& body) noexcept {
    auto [result, back] = with_mut_windows<T>(std::move(block), windows, static_cast<Body&&>(body));
    block = std::move(back);
    return std::move(result);
}

// The door lends one ViewMut for each window, in the order of the windows,
// and each ViewMut writes only the bytes of its window.  A second call reads
// the values back.  A body that gives void gives Unit.
void test_windows_lend_disjoint_views() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{256}, Alignment{64}));
    FixedList<ByteWindow, 2> const windows = make_windows(kFirst16, kAt64Of32);
    auto const written = lend_windows<float>(block, windows, [](auto& views) noexcept {
        float value = 1.5f;
        std::size_t count = 0;
        std::move(views).drain([&value, &count](auto view) noexcept {
            for (float& element : view) {
                element = value;
                ++count;
            }
            value += 1.0f;
        });
        return count;
    });
    CRUCIBLE_FATAL_INVARIANT(written.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));
    auto const matched = lend_windows<float>(block, windows, [](auto& views) noexcept {
        using ::foundation::core::bit_cast;
        float expected = 1.5f;
        std::size_t count = 0;
        for (auto const& view : views) {
            for (float const& element : view) {
                count += bit_cast<std::uint32_t>(element) == bit_cast<std::uint32_t>(expected) ? 1u : 0u;
            }
            expected += 1.0f;
        }
        return count;
    });
    CRUCIBLE_FATAL_INVARIANT(matched.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));
    AlignedBytes const& reader = block;
    std::size_t nonzero_between = 0;
    for (std::uint8_t const byte : reader.view().window(16, 48).expect("the gap between the windows fits")) {
        nonzero_between += byte != 0 ? 1u : 0u;
    }
    CRUCIBLE_FATAL_INVARIANT(nonzero_between == 0);
    auto const empty = lend_windows<float>(block, windows, [](auto&) noexcept {});
    CRUCIBLE_FATAL_INVARIANT(empty.is_ok());
}

// A body that opens a second frame of the name of its block finds the block
// in the frame of the first door.  So the second door refuses each window that
// holds an element, and its body does not run.  The block comes back with its
// bytes, and a View of the bytes that the caller made before the frame reads
// them in the body.
void test_nested_windows_find_no_block() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{256}, Alignment{64}));
    FixedList<ByteWindow, 2> const windows = make_windows(kFirst16, kAt64Of32);
    ::foundation::core::View<std::uint8_t const> const before = block.view();
    int inner_runs = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the body reads the bytes of its block through a View from before the frame")
    auto [outer, back] = with_mut_windows<float>(
        std::move(block), windows, [&block, &windows, &inner_runs, before](auto& views) noexcept {
            auto [inner, inner_back] =
                with_mut_windows<float>(std::move(block), windows, [&inner_runs](auto&) noexcept { ++inner_runs; });
            block = std::move(inner_back);
            bool const is_refused = inner.match(
                [](::foundation::core::Unit const&) noexcept { return false; },
                [](WindowRefusal const& refusal) noexcept { return refusal == WindowRefusal::outside_block; });
            std::move(views).drain([](auto view) noexcept {
                for (float& element : view)
                    element = 1.5f;
            });
            bool const is_written =
                before.window<4>(0).expect("four bytes fit").at(::foundation::core::index<3>()) != 0;
            return is_refused && is_written;
        });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(outer.is_ok_and([](bool const& is_held) noexcept { return is_held; }));
    CRUCIBLE_FATAL_INVARIANT(inner_runs == 0 && block.size() == 0 && back.size() == 256);
}

// A keyed block lends its windows with its LoanKey.  The key moves into the
// door and comes back with the Result, also after a refused window, and the
// borrow form keeps the key with the caller.  The static form lends one
// ViewMut for each window as an argument.
void test_keyed_windows() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    auto [block, key] = ::foundation::core::mint_keyed_owner(
        ::foundation::keyed_bytes_owner, mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{256}, Alignment{64})));
    CRUCIBLE_FATAL_INVARIANT(block.size() == 256 && block.alignment() == 64);
    FixedList<ByteWindow, 2> const windows = make_windows(kFirst16, kAt64Of32);
    auto [written, back] = with_mut_windows<float>(block, std::move(key), windows, [](auto& views) noexcept {
        std::size_t count = 0;
        std::move(views).drain([&count](auto view) noexcept {
            for (float& element : view) {
                element = 2.0f;
                ++count;
            }
        });
        return count;
    });
    CRUCIBLE_FATAL_INVARIANT(written.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));
    auto [refused, again] =
        with_mut_windows<float>(block, std::move(back), make_windows(kFirst16, kAt8Of16), [](auto&) noexcept {});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err());
    auto const lent = with_mut_windows<float>(block, again, windows, [](auto& views) noexcept {
        std::size_t count = 0;
        for (auto const& view : views) {
            for (float const element : view) {
                count += ::foundation::core::bit_cast<std::uint32_t>(element)
                              == ::foundation::core::bit_cast<std::uint32_t>(2.0f)
                           ? 1u
                           : 0u;
            }
        }
        return count;
    });
    CRUCIBLE_FATAL_INVARIANT(lent.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));
    ::foundation::core::FixedArray<ByteWindow, 2> const fixed{kFirst16, kAt64Of32};
    auto const sizes = with_mut_windows<float>(
        block, again, fixed, [](auto first, auto second) noexcept { return first.size() + second.size(); });
    CRUCIBLE_FATAL_INVARIANT(sizes.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));
    std::size_t nonzero_between = 0;
    for (std::uint8_t const byte : block.view().window(16, 48).expect("the gap between the windows fits")) {
        nonzero_between += byte != 0 ? 1u : 0u;
    }
    CRUCIBLE_FATAL_INVARIANT(nonzero_between == 0);
}

// Each refused window gives its kind, and the body does not run.  A window
// of no byte shares no byte with a second window.
void test_windows_refusals() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{256}, Alignment{64}));
    int runs = 0;
    auto const refusal_of = [&block, &runs](FixedList<ByteWindow, 2> const& windows) noexcept {
        return lend_windows<float>(block, windows, [&runs](auto&) noexcept { ++runs; })
            .err()
            .expect("the door refuses the windows");
    };
    CRUCIBLE_FATAL_INVARIANT(refusal_of(make_windows(kFirst16, kAt240Of32)) == WindowRefusal::outside_block);
    CRUCIBLE_FATAL_INVARIANT(refusal_of(make_windows(kAt2Of4, kAt64Of16)) == WindowRefusal::misaligned);
    CRUCIBLE_FATAL_INVARIANT(refusal_of(make_windows(kFirst6, kAt64Of16)) == WindowRefusal::partial_element);
    CRUCIBLE_FATAL_INVARIANT(refusal_of(make_windows(kFirst16, kAt8Of16)) == WindowRefusal::overlap);
    CRUCIBLE_FATAL_INVARIANT(refusal_of(make_windows(kAt32Of16, kFirst36)) == WindowRefusal::overlap);
    CRUCIBLE_FATAL_INVARIANT(runs == 0);
    auto const touching =
        lend_windows<float>(block, make_windows(kFirst16, kAt16Of16), [&runs](auto&) noexcept { ++runs; });
    auto const nothing =
        lend_windows<float>(block, make_windows(kAt8Empty, kFirst16), [&runs](auto&) noexcept { ++runs; });
    CRUCIBLE_FATAL_INVARIANT(touching.is_ok() && nothing.is_ok() && runs == 2);
}

using ::foundation::core::FixedArray;

// The count of the elements of the views of a call, or 1000 plus the
// refusal of the call, so the results of the two doors compare as numbers.
template <class Given>
[[nodiscard]] std::size_t code_of(Given const& given) noexcept {
    return given.match(
        [](std::size_t const& count) noexcept { return count; },
        [](WindowRefusal const& refusal) noexcept { return std::size_t{1000} + static_cast<std::size_t>(refusal); });
}

// The door of a static count gives the count or the refusal that the door
// of a list gives for the same windows.  Its body gets one ViewMut for each
// window, in the order of the windows, and each writes only its window.
void test_static_windows_agree_with_the_list() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    AlignedBytes block = mint_aligned_bytes_zeroed(alloc, layout_of(ByteCount{256}, Alignment{64}));
    FixedArray<FixedArray<ByteWindow, 2>, 8> const pairs{
        FixedArray<ByteWindow, 2>{kFirst16, kAt64Of32}, FixedArray<ByteWindow, 2>{kFirst16, kAt240Of32},
        FixedArray<ByteWindow, 2>{kAt2Of4, kAt64Of16},  FixedArray<ByteWindow, 2>{kFirst6, kAt64Of16},
        FixedArray<ByteWindow, 2>{kFirst16, kAt8Of16},  FixedArray<ByteWindow, 2>{kAt32Of16, kFirst36},
        FixedArray<ByteWindow, 2>{kFirst16, kAt16Of16}, FixedArray<ByteWindow, 2>{kAt8Empty, kFirst16}};
    for (FixedArray<ByteWindow, 2> const& pair : pairs) {
        auto const listed = lend_windows<float>(
            block, make_windows(pair.at(::foundation::core::index<0>()), pair.at(::foundation::core::index<1>())),
            [](auto& views) noexcept {
                std::size_t count = 0;
                for (auto const& view : views)
                    count += view.size();
                return count;
            });
        auto const fixed = lend_windows<float>(
            block, pair, [](auto first, auto second) noexcept { return first.size() + second.size(); });
        CRUCIBLE_FATAL_INVARIANT(code_of(listed) == code_of(fixed));
    }

    auto const written = lend_windows<float>(block, FixedArray<ByteWindow, 2>{kFirst16, kAt64Of32},
                                             [](auto first, auto second) noexcept {
                                                 for (float& element : first)
                                                     element = 1.5f;
                                                 for (float& element : second)
                                                     element = 2.5f;
                                             });
    CRUCIBLE_FATAL_INVARIANT(written.is_ok());
    auto const matched = lend_windows<float>(block, make_windows(kFirst16, kAt64Of32), [](auto& views) noexcept {
        using ::foundation::core::bit_cast;
        float expected = 1.5f;
        std::size_t count = 0;
        for (auto const& view : views) {
            for (float const& element : view) {
                count += bit_cast<std::uint32_t>(element) == bit_cast<std::uint32_t>(expected) ? 1u : 0u;
            }
            expected += 1.0f;
        }
        return count;
    });
    CRUCIBLE_FATAL_INVARIANT(matched.is_ok_and([](std::size_t const& count) noexcept { return count == 12; }));

    auto const one = lend_windows<float>(block, FixedArray<ByteWindow, 1>{kAt32Of16},
                                         [](auto only) noexcept { return only.size(); });
    auto const three = lend_windows<float>(
        block, FixedArray<ByteWindow, 3>{kFirst16, kAt32Of16, kAt64Of32},
        [](auto first, auto second, auto third) noexcept { return first.size() + second.size() + third.size(); });
    auto const three_overlap = lend_windows<float>(block, FixedArray<ByteWindow, 3>{kFirst16, kAt64Of32, kAt8Of16},
                                                   [](auto, auto, auto) noexcept {});
    CRUCIBLE_FATAL_INVARIANT(code_of(one) == 4 && code_of(three) == 16);
    CRUCIBLE_FATAL_INVARIANT(
        three_overlap.match([](::foundation::core::Unit const&) noexcept { return false; },
                            [](WindowRefusal const& refusal) noexcept { return refusal == WindowRefusal::overlap; }));
}

}  // namespace

int main() {
    test_empty_block();
    test_alignments();
    test_zeroed_mint_reads_zero();
    test_fill_reaches_each_byte();
    test_write_in_order();
    test_move();
    test_layouts_refuse_bad_requests();
    test_windows_lend_disjoint_views();
    test_nested_windows_find_no_block();
    test_keyed_windows();
    test_windows_refusals();
    test_static_windows_agree_with_the_list();

    // A writer that leaves a byte with no write, or that gives a byte past the
    // last one, ends the process.  Each refused call runs in a child through
    // expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc,
                                     layout_of(ByteCount{4}, Alignment{4}))
                .write(
                    [](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept { lease.put(std::uint8_t{1}); });
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of an UnwrittenBytes left 3 of 4 bytes with no write, and gave 0 bytes past "
            "the last one"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            (void)mint_aligned_bytes(::foundation::effects::testing::test().alloc, Layout::of<std::uint8_t>())
                .write([](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept {
                    lease.put(std::uint8_t{1});
                    lease.put(std::uint8_t{2});
                });
        },
        ::foundation::core::TextView{
            "foundation: fatal: the writer of an UnwrittenBytes left 0 of 1 bytes with no write, and gave 1 bytes past "
            "the last one"});
    return 0;
}
