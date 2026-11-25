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
// The counter starts at 1 and does not wrap, so no mint gives the value 0.
//
// An Identity copies, so it proves which instance a value belongs to, and not
// who holds a right.  A right that one holder has, such as the token of one
// side of a split, holds a UniqueIdentity.  It does not copy, and a move
// leaves none in the source.

#include <foundation/ByteSeal.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace foundation::core {

class Identity;
class UniqueIdentity;

[[nodiscard]] Identity mint_identity() noexcept;

class Identity {
    std::uint64_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit Identity(std::uint64_t value) noexcept : value_{value} {}

    friend Identity mint_identity() noexcept;
    friend class UniqueIdentity;

public:
    [[nodiscard]] friend constexpr bool operator==(Identity const& left, Identity const& right) noexcept {
        return left.value_ == right.value_;
    }
};

// A fresh identity, which is equal to no identity that a different call gave.
// The cost is one atomic add.
[[nodiscard]] inline Identity mint_identity() noexcept { return Identity{detail::next_identity_.fetch_add_acq_rel(1)}; }

// The Identity of a right that one holder has.  A move gives the Identity to
// the target and leaves none in the source.  So a second name of a right that
// moved, such as a second reference parameter that names one object, names no
// Identity, and the door that asks for the Identity refuses it.  The rule
// use-after-move does not see each such name.
//
// The word of a source with none is 0, and no mint gives 0.  The move
// constructor is user-provided, so the ABI passes a right by value in memory.
// The class is not trivially copyable, and it has no trivial constructor, so
// no byte route builds one.
class [[nodiscard]] UniqueIdentity {
    std::uint64_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    constexpr explicit UniqueIdentity(Identity const& identity) noexcept : value_{identity.value_} {}

    UniqueIdentity(UniqueIdentity const&) = delete("a right has one holder: a copy would give it a second one");
    UniqueIdentity& operator=(UniqueIdentity const&) = delete("a right has one holder: a copy would give it a second "
                                                              "one");
    constexpr UniqueIdentity(UniqueIdentity&& other) noexcept : value_{other.value_} { other.value_ = 0; }

    // The source gives its Identity up before the target takes it, so a self
    // move keeps the Identity.
    constexpr UniqueIdentity& operator=(UniqueIdentity&& other) noexcept {
        std::uint64_t const taken = other.value_;
        other.value_ = 0;
        value_ = taken;
        return *this;
    }
    ~UniqueIdentity() = default;

    // True when the right holds the Identity.  False for a right that a move
    // emptied, because no Identity has its word.  Complexity: one compare.
    [[nodiscard]] constexpr bool names(Identity const& identity) const noexcept { return value_ == identity.value_; }
};

}  // namespace foundation::core
