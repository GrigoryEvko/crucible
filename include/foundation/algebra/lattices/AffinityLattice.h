#pragma once

// Boolean lattice over a CPU affinity bitmask: the powerset of the core
// ids 0 through kMaxCore.  leq is set inclusion, join is union and meet
// is intersection.  bottom admits no core and top admits every core, so
// a larger set sits higher and is the more permissive claim.
//
// The mask holds every CPU that a cpu_set_t can name.  kWords fixes the
// width at 1024 cores, which is CPU_SETSIZE of glibc, and
// fixy/os/CpuPinned.h asserts that the two widths agree.  So a runtime pin
// to one CPU goes through a mask with no loss.
//
// The backing store is a plain array of words rather than a
// std::bitset.  A bitset is functionally equivalent, but its
// implementation is not guaranteed trivially relocatable, and this type
// is the grade of a layout-critical carrier.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

struct AffinityMask {
    static constexpr std::size_t kWords = 16;
    static constexpr std::size_t kBits = kWords * 64;
    static constexpr std::uint16_t kMaxCore = static_cast<std::uint16_t>(kBits - 1);

    std::array<std::uint64_t, kWords> words{};

    [[nodiscard]] constexpr bool operator==(AffinityMask const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(AffinityMask const&) const noexcept = default;

    // A core index past the mask width would index outside the word
    // array.  The preconditions on this factory, on contains and on
    // range fence that.  CRUCIBLE_PRE also stops a constant evaluation,
    // so every static assertion in the check file of this header is a
    // real test of the bound.
    [[nodiscard]] static constexpr AffinityMask single(std::uint16_t core) noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<std::uint16_t>(core, 0, kMaxCore));
        AffinityMask m{};
        m.words[core / 64] = std::uint64_t{1} << (core % 64);
        CRUCIBLE_POST(m, m.popcount() == 1 && m.contains(core));
        return m;
    }

    [[nodiscard]] constexpr bool contains(std::uint16_t core) const noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<std::uint16_t>(core, 0, kMaxCore));
        return (words[core / 64] & (std::uint64_t{1} << (core % 64))) != 0;
    }

    [[nodiscard]] constexpr std::uint16_t popcount() const noexcept {
        std::uint16_t count = 0;
        for (auto w : words) {
            count = static_cast<std::uint16_t>(count + __builtin_popcountll(w));
        }
        return count;
    }

    [[nodiscard]] static constexpr AffinityMask range(std::uint16_t first_core, std::uint16_t last_core) noexcept {
        CRUCIBLE_PRE(first_core <= last_core);
        CRUCIBLE_PRE(::foundation::decide::in_range<std::uint16_t>(last_core, 0, kMaxCore));
        AffinityMask m{};
        for (std::uint16_t c = first_core; c <= last_core; ++c) {
            m.words[c / 64] |= std::uint64_t{1} << (c % 64);
        }
        CRUCIBLE_POST(m, m.popcount() == last_core - first_core + 1);
        return m;
    }
};

struct AffinityLattice {
    using element_type = AffinityMask;

    // A larger mask says less about where the thread runs, and it is the
    // weaker claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type m{};
        for (auto& w : m.words)
            w = std::numeric_limits<std::uint64_t>::max();
        return m;
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            if ((a.words[i] & b.words[i]) != a.words[i]) return false;
        }
        return true;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] | b.words[i];
        }
        return r;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] & b.words[i];
        }
        return r;
    }

    // A timestamp-counter read is sound only when the thread cannot
    // migrate, so a pin proof tests the mask for exactly one core.
    [[nodiscard]] static constexpr bool is_singleton(element_type m) noexcept { return m.popcount() == 1; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "AffinityLattice"; }
};

}  // namespace foundation::algebra::lattices
