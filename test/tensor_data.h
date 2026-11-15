#pragma once

// The doors between a data address of the replay and the host pointer of a
// test.  A test computes on memory of the host.  The replay gives the
// address of a slot alone, and the pool keeps its device, so the door of an
// address alone trusts the test.  The door of an address with its device
// checks that the device is a CPU.

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/core/DataAddress.h>

namespace crucible::test {

// The host pointer of an address, as a pointer to T, or null for no
// address.
template <class T = void>
[[nodiscard]] T* host_ptr(::fixy::Option<::fixy::RunAddress> address) noexcept {
    return address.match(
        [](::fixy::RunAddress found) noexcept -> T* {
            return static_cast<T*>(::foundation::core::foreign_pointer(found));
        },
        [] noexcept -> T* { return nullptr; });
}

// The host pointer of an address with its device, as a pointer to T, or
// null for no address.  An address on a device that is not a CPU ends the
// process.  A front end can give a CPU tensor the index -1 or 0, so the door
// reads the type of the device only.
template <class T = void>
[[nodiscard]] T* host_ptr(::fixy::Option<TensorData> data) noexcept {
    return data.match(
        [](TensorData found) noexcept -> T* {
            CRUCIBLE_FATAL_INVARIANT(found.device().type == DeviceType::CPU);
            return static_cast<T*>(::foundation::core::foreign_pointer(found.address()));
        },
        [] noexcept -> T* { return nullptr; });
}

// The data address on the host of a buffer that a test owns.  The buffer
// is not null.
[[nodiscard]] inline TensorData host_data(void* buffer) noexcept {
    return TensorData{::fixy::mint_foreign_run_address(buffer).expect("a buffer of a test is not null"), kHostDevice};
}

}  // namespace crucible::test
