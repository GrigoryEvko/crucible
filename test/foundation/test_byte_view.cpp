// Tests of byte_view of foundation/core/Region.h.
//
// The View of std::byte reads and writes the bytes of a run of unsigned
// chars.  It keeps the count, the fixed extent and the const of the run.  The
// ViewMut of a frame gives its bytes by move, with its brand.

#include <foundation/core/Region.h>

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::byte_view;
using ::foundation::core::View;

// A run of unsigned char gives bytes, and a run of unsigned char const gives
// const bytes.  A fixed extent stays.
static_assert(std::is_same_v<decltype(byte_view(std::declval<View<std::uint8_t>>())), View<std::byte>>);
static_assert(std::is_same_v<decltype(byte_view(std::declval<View<std::uint8_t const>>())), View<std::byte const>>);
static_assert(std::is_same_v<decltype(byte_view(std::declval<View<std::uint8_t, 4>>())), View<std::byte, 4>>);

// A write through the bytes reaches each element of the run, and a read
// through the const bytes gives each element.
void test_reads_and_writes_the_run() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::HeapArray<std::uint8_t> run = ::foundation::core::mint_heap_array(alloc, 5, std::uint8_t{0});
    run = ::foundation::core::with_mut(std::move(run), [](auto cells) noexcept {
        using Cells = decltype(cells);
        auto bytes = byte_view(std::move(cells));
        static_assert(std::is_same_v<typename decltype(bytes)::brand_type, typename Cells::brand_type>);
        CRUCIBLE_FATAL_INVARIANT(bytes.size() == 5);
        std::uint8_t value = 1;
        for (std::byte& byte : bytes) {
            byte = static_cast<std::byte>(value);
            value = static_cast<std::uint8_t>(value * 3);
        }
    });

    std::uint8_t expected = 1;
    for (std::uint8_t const element : run) {
        CRUCIBLE_FATAL_INVARIANT(element == expected);
        expected = static_cast<std::uint8_t>(expected * 3);
    }

    ::foundation::core::HeapArray<std::uint8_t> const& read_only = run;
    std::size_t total = 0;
    for (std::byte const byte : byte_view(read_only.view())) {
        total += static_cast<std::uint8_t>(byte);
    }
    CRUCIBLE_FATAL_INVARIANT(total == 1 + 3 + 9 + 27 + 81);
}

// The bytes of a fixed run hold its extent and its elements.
void test_fixed_extent() {
    ::foundation::core::FixedArray<std::uint8_t, 4> const run =
        ::foundation::core::build_fixed_array<std::uint8_t, 4>([](auto cells) noexcept {
            auto bytes = byte_view(std::move(cells));
            static_assert(decltype(bytes)::extent == 4);
            auto last = bytes.get_mut(3).expect("four bytes");
            *last = std::byte{0x7F};
        });
    CRUCIBLE_FATAL_INVARIANT(run.view().at(::foundation::core::index<3>()) == 0x7F);
    CRUCIBLE_FATAL_INVARIANT(run.view().at(::foundation::core::index<0>()) == 0);
}

// The empty run gives the empty bytes.
void test_empty_run() {
    View<std::byte const> const empty = byte_view(View<std::uint8_t const>{});
    CRUCIBLE_FATAL_INVARIANT(empty.size() == 0);
}

}  // namespace

int main() {
    test_reads_and_writes_the_run();
    test_fixed_extent();
    test_empty_run();
    return 0;
}
