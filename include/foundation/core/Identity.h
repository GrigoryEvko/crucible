#pragma once

// The run-time identity of one brand instance.
//
// A brand is a type (foundation/Brand.h), and one mint site gives one brand
// type.  Two runs of one mint site, such as two turns of a loop or two calls
// of one helper, share that type.  An Identity tells such runs apart at run
// time: mint_identity() takes the next value of one counter of the process,
// so two mints never give equal identities.  A table that keeps a branded
// value across a unit keys it on the Identity, because no source file can
// name a fresh brand type.
//
// An Identity compares with ==, and it has no order and no hash.  Its value
// depends on the order of the mints, which two threads can change from run
// to run, so an order or a hash of it would break DetSafe.  The seal makes
// the assignment user-provided, so std::bit_cast builds no Identity from an
// integer.
//
// The counter is detail::next_identity_ of foundation/core/Atomic.h, one
// object for the process.  mint_sole_writer() of the same header takes its
// ids from that counter too, so no Identity is equal to the id of a writer.

#include <foundation/ByteSeal.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace foundation::core {

class Identity;

[[nodiscard]] Identity mint_identity() noexcept;

class Identity {
    std::uint64_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit Identity(std::uint64_t value) noexcept : value_{value} {}

    friend Identity mint_identity() noexcept;

public:
    [[nodiscard]] friend constexpr bool operator==(Identity const& left, Identity const& right) noexcept {
        return left.value_ == right.value_;
    }
};

// A fresh identity, which is equal to no identity that a different call gave.
// The cost is one atomic add.
[[nodiscard]] inline Identity mint_identity() noexcept { return Identity{detail::next_identity_.fetch_add_acq_rel(1)}; }

}  // namespace foundation::core
