// A receive whose join already holds the largest count in the receiver's
// slot cannot count the receive: the step would wrap.  causal_merge hands
// the join to successor_at, and the overflow guard there fires.  The clock
// has no top, so the received clock comes from a checked read of an image,
// as storage can give it.

#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/ImageRead.h>

#include <cstddef>

namespace fe = ::foundation::effects;
namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

// Byte `index` of the axis word, little-endian as an image holds it.
[[nodiscard]] constexpr std::byte axis_byte(unsigned index) noexcept {
    return static_cast<std::byte>((HB::image_axis() >> (8u * index)) & 0xFFu);
}

constexpr std::byte full{0xFF};

[[nodiscard]] constexpr HB::element_type saturated_clock() noexcept {
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const scope{fe::testing::test()};
    return fe::mint_from_image<HB>(
               scope, HB::image_type{axis_byte(0), axis_byte(1), axis_byte(2), axis_byte(3), axis_byte(4), axis_byte(5),
                                     axis_byte(6), axis_byte(7), full,         full,         full,         full,
                                     full,         full,         full,         full,         full,         full,
                                     full,         full,         full,         full,         full,         full})
        .expect("the image names the axis of its read");
}

constexpr HB::element_type local = HB::bottom();
constexpr HB::element_type received = saturated_clock();
static_assert(HB::causal_merge(local, received, 0)[1] == 3);

int main() { return 0; }
