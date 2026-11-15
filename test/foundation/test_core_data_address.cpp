// Tests of foundation/core/DataAddress.h: the two mints of a RunAddress, the
// pair DataAddress<Device>, is_aligned, ==, foreign_pointer, and the Options
// of the two types.

#include <foundation/core/DataAddress.h>

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include "philox_stream.h"

namespace {

using ::foundation::core::DataAddress;
using ::foundation::core::FixedArray;
using ::foundation::core::foreign_pointer;
using ::foundation::core::mint_foreign_run_address;
using ::foundation::core::mint_run_address;
using ::foundation::core::none;
using ::foundation::core::Option;
using ::foundation::core::RunAddress;
using ::foundation::core::View;

// A device of two bytes: a kind and an index, as a front end gives them.
struct TestDevice {
    unsigned char kind = 0;
    signed char index = -1;

    [[nodiscard]] friend constexpr bool operator==(TestDevice const&, TestDevice const&) noexcept = default;
};

constexpr TestDevice kHost{};
constexpr TestDevice kAccelerator{.kind = 1, .index = 0};

// A device that is an enumeration.
enum class TestKind : unsigned char {
    host = 0,
    accelerator = 1,
};

using HostData = DataAddress<TestDevice>;

struct alignas(64) LineBytes {
    FixedArray<unsigned char, 64> bytes{};
};

// The address of the byte at an offset, for each offset up to the count.
// An offset equal to the count gives the address one past the last byte.
void test_mint_from_bytes() {
    LineBytes line{};
    View<unsigned char> const bytes = line.bytes.view();
    Option<RunAddress> const first = mint_run_address(bytes, 0);
    Option<RunAddress> const middle = mint_run_address(bytes, 9);
    Option<RunAddress> const end = mint_run_address(bytes, 64);
    CRUCIBLE_FATAL_INVARIANT(first.is_some() && middle.is_some() && end.is_some());
    CRUCIBLE_FATAL_INVARIANT(first == mint_run_address(line.bytes.view(), 0));
    CRUCIBLE_FATAL_INVARIANT(!(first == middle) && !(middle == end));
}

// An empty run has no byte, so it gives no address.  An offset past the
// count gives no address either, and so does a null foreign pointer.
void test_mint_refuses() {
    View<unsigned char> const empty{};
    CRUCIBLE_FATAL_INVARIANT(mint_run_address(empty, 0).is_none());
    LineBytes line{};
    CRUCIBLE_FATAL_INVARIANT(mint_run_address(line.bytes.view(), 65).is_none());
    CRUCIBLE_FATAL_INVARIANT(mint_foreign_run_address(static_cast<unsigned char*>(nullptr)).is_none());
}

// A DataAddress pairs an address with its device.  Two data addresses are
// equal only when the address and the device agree.
void test_device_is_part_of_the_value() {
    LineBytes line{};
    RunAddress const address = mint_run_address(line.bytes.view(), 4).expect("the line holds 64 bytes");
    HostData const on_host{address, kHost};
    HostData const on_accelerator{address, kAccelerator};
    CRUCIBLE_FATAL_INVARIANT(!(on_host == on_accelerator));
    CRUCIBLE_FATAL_INVARIANT((on_host == HostData{address, kHost}));
    CRUCIBLE_FATAL_INVARIANT(on_accelerator.device() == kAccelerator && on_accelerator.address() == address);

    DataAddress<TestKind> const by_kind{address, TestKind::accelerator};
    CRUCIBLE_FATAL_INVARIANT(by_kind.device() == TestKind::accelerator && by_kind.address() == address);

    Option<HostData> const held = Option<HostData>::some(on_host);
    Option<HostData> const empty{none};
    CRUCIBLE_FATAL_INVARIANT(held.is_some() && empty.is_none() && !(held == empty));
}

// The foreign mint and the mint from bytes give the same address for the
// same byte, and foreign_pointer gives the pointer back.  A write through
// that pointer reaches the byte of the run.
void test_foreign_round_trip() {
    LineBytes line{};
    unsigned char& seventh = *line.bytes.view().get(7).expect("the line holds 64 bytes");
    Option<RunAddress> const foreign = mint_foreign_run_address(&seventh);
    CRUCIBLE_FATAL_INVARIANT(foreign == mint_run_address(line.bytes.view(), 7));
    for (RunAddress const address : foreign) {
        CRUCIBLE_FATAL_INVARIANT(foreign_pointer(address) == &seventh);
        *static_cast<unsigned char*>(foreign_pointer(address)) = 0x5A;
    }
    CRUCIBLE_FATAL_INVARIANT(seventh == 0x5A);
}

// The alignment of an address follows the alignment of its byte, alone and
// in a pair.
void test_alignment() {
    LineBytes line{};
    View<unsigned char> const bytes = line.bytes.view();
    for (RunAddress const address : mint_run_address(bytes, 0)) {
        CRUCIBLE_FATAL_INVARIANT(address.is_aligned<1>() && address.is_aligned<8>() && address.is_aligned<64>());
        CRUCIBLE_FATAL_INVARIANT((HostData{address, kHost}.is_aligned<64>()));
    }
    for (RunAddress const address : mint_run_address(bytes, 1)) {
        CRUCIBLE_FATAL_INVARIANT(address.is_aligned<1>() && !address.is_aligned<2>());
    }
    for (RunAddress const address : mint_run_address(bytes, 32)) {
        CRUCIBLE_FATAL_INVARIANT((address.is_aligned<32>() && !HostData{address, kHost}.is_aligned<64>()));
    }
}

// The property: for random offsets, a mint gives an address exactly when
// the offset is at most the count, and two pairs are equal exactly when
// their offsets and their devices are equal.
void test_agrees_with_plain_model() {
    LineBytes line{};
    View<unsigned char> const bytes = line.bytes.view();
    ::crucible::test::PhiloxStream stream{0x5EEDDA7A00000001u};
    for (int trial = 0; trial < 1024; ++trial) {
        auto const first_offset = stream.below(70);
        auto const second_offset = stream.below(70);
        TestDevice const first_device = stream.below(2) == 0 ? kHost : kAccelerator;
        TestDevice const second_device = stream.below(2) == 0 ? kHost : kAccelerator;
        Option<RunAddress> const first = mint_run_address(bytes, first_offset);
        Option<RunAddress> const second = mint_run_address(bytes, second_offset);
        CRUCIBLE_FATAL_INVARIANT(first.is_some() == (first_offset <= 64));
        CRUCIBLE_FATAL_INVARIANT(second.is_some() == (second_offset <= 64));
        for (RunAddress const first_address : first) {
            for (RunAddress const second_address : second) {
                bool const is_same = first_offset == second_offset && first_device == second_device;
                CRUCIBLE_FATAL_INVARIANT(
                    (HostData{first_address, first_device} == HostData{second_address, second_device}) == is_same);
                CRUCIBLE_FATAL_INVARIANT((first_address == second_address) == (first_offset == second_offset));
            }
        }
    }
}

}  // namespace

int main() {
    test_mint_from_bytes();
    test_mint_refuses();
    test_device_is_part_of_the_value();
    test_foreign_round_trip();
    test_alignment();
    test_agrees_with_plain_model();
    return 0;
}
