#pragma once

// The annotation that refuses a lifetime start over bytes, and the member
// that closes the two byte routes to a proof.
//
// A class can refuse a lifetime start while it stays implicit-lifetime.
// A value that must come only from its own doors, such as a count or a
// version, keeps a trivial copy constructor so that the ABI passes it in a
// register, and that trivial constructor makes it implicit-lifetime.  Such a
// class carries the annotation no_start_over_bytes.  The checked lifetime
// start of foundation/Lifetime.h refuses the class wherever it sits: alone,
// in an array, as a base or as a member.
//
// This header holds only the two types, so that a class can carry the
// annotation without the <memory> that the checked lifetime start includes.

#include <foundation/Platform.h>

namespace foundation::lifetime {

// The annotation of a class whose lifetime must never start over bytes.
// Spell it on the class: struct [[=::foundation::lifetime::no_start_over_bytes{}]] X.
struct no_start_over_bytes {};

// A member that closes the two byte routes to the class that holds it, and
// keeps the call ABI of that class.
//
// std::bit_cast builds a trivially copyable class from bytes, and the
// checked lifetime start builds an implicit-lifetime class over bytes.
// Neither calls a constructor, so neither meets the door of a proof.  A
// proof that the ABI passes in a register keeps trivial copy and move
// constructors and a trivial destructor.  GCC also counts a class whose
// copies and moves are all deleted as trivially copyable.  Such a class
// holds one seal:
//
//     [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
//
// The assignments of the seal are user-provided, so the assignments of the
// class are not trivial, and the class is not trivially copyable.  The seal
// carries no_start_over_bytes, so the checked lifetime start refuses the
// class.  The copy and move constructors and the destructor of the seal are
// trivial, so the Itanium ABI still passes the class in registers.  The
// seal adds no byte, and a class that holds only a seal stays empty.  A
// class that holds a sealed member is sealed through that member, and a
// second seal in it can need a byte of its own.
struct[[= no_start_over_bytes{}]] byte_seal {
    constexpr byte_seal() noexcept = default;
    constexpr byte_seal(const byte_seal&) noexcept = default;
    constexpr byte_seal(byte_seal&&) noexcept = default;
    constexpr byte_seal& operator=(const byte_seal&) noexcept { return *this; }
    constexpr byte_seal& operator=(byte_seal&&) noexcept { return *this; }
    ~byte_seal() = default;
};

}  // namespace foundation::lifetime
