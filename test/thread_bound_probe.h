#pragma once

// The cross of an object of one owner thread to another thread, for a test
// that probes the run-time check of such an object.  The thread doors refuse
// a body that captures the object (foundation/Closure.h), and an atomic cell
// of a reference to it is not Sendable either (payload_cell{} of
// foundation/Transfer.h).  So the test gives the other thread the address of
// the object as a plain integer in an Atomic of an integer.  Only the two
// functions below turn the address into an integer and back, in a region
// that opts out of the base with the reason of the probe.  The run-time check
// of the object is the state under test.

#include <foundation/Quarantine.h>

#include <cstdint>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: probe of the run-time check of a thread-bound object")
namespace crucible::test {

// The address of the object as a plain integer.
template <class T>
[[nodiscard]] inline std::uint64_t address_of_thread_bound(T& object) noexcept {
    return __builtin_bit_cast(std::uint64_t, __builtin_addressof(object));
}

// The object at an address that address_of_thread_bound gave.  The object
// lives while the probe uses it.
template <class T>
[[nodiscard]] inline T& thread_bound_at(std::uint64_t address) noexcept {
    return *__builtin_bit_cast(T*, address);
}

}  // namespace crucible::test
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
