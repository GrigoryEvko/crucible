#pragma once

// The image of a count or of a clock that crossed a wire or came back from
// storage (foundation/effects/ImageRead.h).  mint_from_image reads the first
// bytes of the input as the image of each count of StrongCounterLattice.h and
// of a clock of four processes of HappensBefore.h.  A short input gets zero
// bytes after its end.  A read either refuses an image of another axis, or it
// gives a count or a clock whose image is the same bytes.
//
// The interface holds only types of the base (entry.h).

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/ImageRead.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

namespace counter_clock_image_detail {

namespace lattices = ::foundation::algebra::lattices;

// The clock of the harness: four processes with the default protocol.
using ClockLattice = lattices::HappensBeforeLattice<4>;

// The bytes of the largest image, the image of the clock.
inline constexpr std::size_t kSeedBytes = sizeof(ClockLattice::image_type);

using Seed = ::fixy::FixedArray<std::byte, kSeedBytes>;

// The seeds of the harness: one image of each axis of a count, and two images
// of the clock.
using Seeds = ::fixy::FixedList<Seed, 6>;

// The bytes of an image at the start of a seed, and zero bytes after them.
template <class Image>
[[nodiscard]] Seed seed_of(Image const& image) noexcept {
    Seed const seed = ::fixy::build_fixed_array<std::byte, Seed::size()>([&image](auto run) noexcept {
        std::size_t position = 0;
        for (std::byte const byte : image) {
            auto element = run.get_mut(position).expect("an image fits a seed");
            *element = byte;
            ++position;
        }
    });
    return seed;
}

// Reads the first bytes of the input as an image of Lattice, and checks the
// read: a refusal names another axis, and a value writes back to the image.
template <class Lattice>
void check_image_read(::fixy::View<std::byte const> input) noexcept {
    typename Lattice::image_type image{};
    std::size_t position = 0;
    for (std::byte& byte : image) {
        for (auto const& given : input.get(position)) {
            byte = *given;
        }
        ++position;
    }
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto const read = ::foundation::effects::mint_from_image<Lattice>(ctx, image);
    read.match([&image](auto const& value) noexcept { CRUCIBLE_FATAL_INVARIANT(Lattice::image_of(value) == image); },
               [&image](lattices::CountImageError error) noexcept {
                   // A refused image does not start with the axis of the
                   // lattice: the little-endian word of its first eight
                   // bytes, which each image of the lattice starts with.
                   CRUCIBLE_FATAL_INVARIANT(error == lattices::CountImageError::OtherAxis);
                   std::uint64_t axis = 0;
                   unsigned shift = 0;
                   for (std::byte const byte : image) {
                       if (shift == 64) break;
                       axis |= static_cast<std::uint64_t>(byte) << shift;
                       shift += 8;
                   }
                   CRUCIBLE_FATAL_INVARIANT(axis != Lattice::image_axis());
               });
}

}  // namespace counter_clock_image_detail

[[nodiscard]] inline counter_clock_image_detail::Seeds seeds_counter_clock_image() noexcept {
    namespace detail = counter_clock_image_detail;
    namespace lattices = detail::lattices;
    detail::Seeds seeds{};
    bool const is_built =
        seeds.append(detail::seed_of(lattices::EpochLattice::image_of(lattices::EpochLattice::bottom()))).is_ok()
        && seeds
               .append(detail::seed_of(lattices::GenerationLattice::image_of(
                   lattices::GenerationLattice::successor(lattices::GenerationLattice::bottom()))))
               .is_ok()
        && seeds.append(detail::seed_of(lattices::PeakBytesLattice::image_of(lattices::PeakBytesLattice::bottom())))
               .is_ok()
        && seeds.append(detail::seed_of(lattices::BitsBudgetLattice::image_of(lattices::BitsBudgetLattice::bottom())))
               .is_ok()
        && seeds.append(detail::seed_of(detail::ClockLattice::image_of(detail::ClockLattice::bottom()))).is_ok()
        && seeds
               .append(detail::seed_of(detail::ClockLattice::image_of(
                   detail::ClockLattice::successor_at(detail::ClockLattice::bottom(), 2))))
               .is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_built);
    return seeds;
}

inline void run_counter_clock_image(::fixy::View<std::byte const> input) noexcept {
    namespace detail = counter_clock_image_detail;
    namespace lattices = detail::lattices;
    detail::check_image_read<lattices::EpochLattice>(input);
    detail::check_image_read<lattices::GenerationLattice>(input);
    detail::check_image_read<lattices::PeakBytesLattice>(input);
    detail::check_image_read<lattices::BitsBudgetLattice>(input);
    detail::check_image_read<detail::ClockLattice>(input);
}

}  // namespace crucible::fuzz::boundary
