#pragma once

// The body of each unit of test_crucible_target_units.  Each unit includes
// this header, so each unit compiles these functions for its own target.  The
// functions are in an unnamed namespace, so each unit keeps its own copy, and
// the link cannot replace it with the copy of the other unit.  The probed
// names belong to the product headers, and the test reads which unit holds
// each of them.

#include "crucible_target_units.h"

#include <crucible/PhiloxSimd.h>
#include <crucible/SwissTable.h>
#include <crucible/cntp/Fec.h>
#include <crucible/cntp/Fountain.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Simd.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <utility>

namespace crucible::test {
namespace {

// Tells if Home, the target namespace of this unit in the namespace of the
// entity, holds the entity.
template <std::meta::info Home>
[[nodiscard]] consteval bool is_home_of(std::meta::info entity) noexcept {
    return std::meta::parent_of(entity) == Home;
}

// The home bits of the headers whose probed names this unit finds in its
// target namespace.
[[nodiscard]] consteval std::uint64_t homes_of_this_unit() noexcept {
    constexpr std::meta::info detail_home = ^^::crucible::detail::CRUCIBLE_SIMD_TARGET;
    constexpr std::meta::info codec_home = ^^::crucible::cntp::CRUCIBLE_SIMD_TARGET;
    constexpr std::meta::info kernel_home = ^^::crucible::cntp::detail::CRUCIBLE_SIMD_TARGET;
    std::uint64_t homes = 0;
    if (is_home_of<detail_home>(^^::crucible::detail::CtrlGroup)
        && is_home_of<detail_home>(^^::crucible::detail::BitMask)) {
        homes |= swiss_home_bit;
    }
    if (is_home_of<detail_home>(^^::crucible::detail::PhiloxBatch8)
        && is_home_of<detail_home>(^^::crucible::detail::philox_batch8)) {
        homes |= philox_home_bit;
    }
    if (is_home_of<kernel_home>(^^::crucible::cntp::detail::mul_xor)
        && is_home_of<codec_home>(^^::crucible::cntp::ReedSolomon)) {
        homes |= fec_home_bit;
    }
    if (is_home_of<codec_home>(^^::crucible::cntp::FountainEncoder)
        && is_home_of<codec_home>(^^::crucible::cntp::FountainDecoder)) {
        homes |= fountain_home_bit;
    }
    return homes;
}

// One step of the FNV-1a hash of 64 bits over a word.
[[nodiscard]] constexpr std::uint64_t fold_word(std::uint64_t digest, std::uint64_t word) noexcept {
    return (digest ^ word) * 0x100000001B3u;
}

// The bits of the kernels of the three headers on fixed inputs, by the body
// of this unit: the eight Philox streams, the parity of a Reed-Solomon block,
// and the matches of 64 control bytes in the groups of this target.  Each
// kernel gives the same bits on each target.
[[nodiscard]] std::uint64_t kernel_bits_of_this_unit() noexcept {
    namespace simd = ::foundation::simd;
    std::uint64_t digest = 0xCBF29CE484222325u;

    simd::u32x8 const counter(
        [](auto lane) noexcept -> std::uint32_t { return 0x9E3779B9u * (std::uint32_t{decltype(lane)::value} + 1u); });
    simd::u32x8 const key(
        [](auto lane) noexcept -> std::uint32_t { return 0x85EBCA6Bu ^ (std::uint32_t{decltype(lane)::value} << 8); });
    ::crucible::detail::PhiloxBatch8 const batch =
        ::crucible::detail::philox_batch8(counter, counter + key, key, counter ^ key, key, counter);
    for (int lane = 0; lane < 8; ++lane) {
        digest = fold_word(digest, batch.r0[lane]);
        digest = fold_word(digest, batch.r3[lane]);
    }

    auto const codec = ::crucible::cntp::mint_reed_solomon<4, 2>(::foundation::effects::testing::init());
    ::fixy::FixedArray<std::byte, 64> payload{};
    payload.with_mut([](auto run) noexcept {
        std::uint8_t next = 1;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(next);
            next = static_cast<std::uint8_t>(next * 29u + 7u);
        }
    });
    ::fixy::FixedArray<std::byte, 96> encoded{};
    bool const is_encoded = encoded.with_mut(
        [&codec, &payload](auto run) noexcept { return codec.encode(payload.view(), std::move(run)).is_ok(); });
    digest = fold_word(digest, is_encoded ? 1u : 0u);
    for (std::byte const byte : encoded) {
        digest = fold_word(digest, static_cast<std::uint8_t>(byte));
    }

    // The control bytes hold the tags 0x28 to 0x2F and some empty slots, so
    // the tag 0x2A matches at the positions 9, 17, 25, 33, 49 and 57.
    ::fixy::FixedArray<std::int8_t, 64> control{};
    control.with_mut([](auto run) noexcept {
        std::uint8_t tag = 3;
        for (std::int8_t& byte : run) {
            byte = tag % 5u == 0 ? ::crucible::detail::kEmpty : static_cast<std::int8_t>(0x28u | (tag & 0x07u));
            tag = static_cast<std::uint8_t>(tag * 13u + 11u);
        }
    });
    // The masks of the groups of one width join into one mask of the 64
    // positions, so each width folds the same two words.
    constexpr std::size_t width = ::crucible::detail::group_width();
    std::uint64_t matches = 0;
    std::uint64_t empties = 0;
    for (std::size_t first = 0; first < 64; first += width) {
        auto const group = ::crucible::detail::CtrlGroup::load(
            ::fixy::View<std::int8_t const>{control.view()}.template window<width>(first).expect("a group fits"));
        matches |= group.match(0x2A).raw() << first;
        empties |= group.match_empty().raw() << first;
    }
    digest = fold_word(digest, matches);
    return fold_word(digest, empties);
}

// The record of this unit, as a constant.  The address of mul_xor comes from
// the compile of this unit, so two units that share one body give one
// address.
[[nodiscard]] consteval CrucibleTargetUnit record_of_this_unit() noexcept {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body tells which unit of the link holds it")
    return CrucibleTargetUnit{
        .homes = homes_of_this_unit(),
        .mul_xor_body = &::crucible::cntp::detail::mul_xor<void, TargetUnitBrand>,
        .kernel_bits_body = &kernel_bits_of_this_unit,
    };
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

}  // namespace
}  // namespace crucible::test
