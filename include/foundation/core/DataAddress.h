#pragma once

// RunAddress and DataAddress<Device>: the address of the first byte of a run
// of data, such as the bytes of a tensor, alone and with its device.
//
// A run of data is not an object of the program.  It is a run of bytes in
// the memory of a device: the host or an accelerator.  An address of the
// memory of an accelerator has no meaning on the host, so each address has
// a device, and the two types give two places for it:
//
//   RunAddress          the address alone, in one word.  The holder keeps
//                       the device beside it, such as a column of devices
//                       beside a column of addresses, or the device fields
//                       of a record.  A hot path that only gives an address
//                       on reads one word
//   DataAddress<Device> a RunAddress and its device, in two words.  The
//                       device is a value of a class or an enumeration of
//                       fewer than eight bytes, such as a kind of device and
//                       an index, and the front end gives the meaning of
//                       each value
//
// A RunAddress is never null, so an Option<RunAddress> holds no address with
// the null pointer and takes one word, and an Option<DataAddress<Device>>
// takes two.  The values go in registers.
//
// is_aligned<N>(address) and == answer for each of the two types, and a
// DataAddress also answers address() and device().  Two data addresses are equal when they hold the
// same address on the same device.  An address changes from run to run, so
// neither type has a hash or an order.  Neither has arithmetic, because the
// replay of a region only gives an address on.
//
// Doors
//   mint_run_address(bytes, offset)   the address of the byte at offset of a
//                       run of bytes that the program owns, such as a pool.
//                       An offset equal to the count gives the address one
//                       past the last byte.  No value for an empty run and
//                       for an offset past the count
//   mint_foreign_run_address(first)   the address that a foreign ABI gives,
//                       such as the data pointer of a tensor of a front end.
//                       No value for the null pointer
//   DataAddress{address, device}   the pair of an address and its device.
//                       The address is never null, so the pair needs no check
//   foreign_pointer(address)   the pointer of an address, for a foreign
//                       reader such as a C caller that takes a void*
// The foreign mint cannot prove that the run lives, or that it is on the
// device that its holder states.  So a call of the foreign mint and of
// foreign_pointer stands only at the edge of a foreign ABI, in an opt-out
// region of the class ABI, or in a test that writes the run.

#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The device of a run of data: a class or an enumeration with equality, of
// fewer than eight bytes, whose copy is a copy of its bytes.  The device and
// a named pad fill the second word of a DataAddress.  An integer is no
// device, because a device id needs a type of its own.
template <class Device>
concept DeviceIdentity =
    (std::is_class_v<Device> || std::is_enum_v<Device>) && !std::is_const_v<Device> && !std::is_volatile_v<Device>
    && std::is_trivially_copyable_v<Device> && std::is_nothrow_default_constructible_v<Device>
    && std::equality_comparable<Device> && sizeof(Device) < sizeof(void*) && alignof(Device) <= alignof(void*);

// The gate of the mint from a run of bytes: a byte element with no const, so
// the address lends the right to write the run.
template <class Byte>
concept RunBytes = std::same_as<Byte, unsigned char> || std::same_as<Byte, std::byte>;

// The gate of the foreign mint: a pointer to data that the holder can
// write, with no const, no volatile and no function.  A void* is such a
// pointer.
template <class Pointer>
concept ForeignRunPointer =
    std::is_pointer_v<Pointer> && !std::is_function_v<std::remove_pointer_t<Pointer>>
    && !std::is_const_v<std::remove_pointer_t<Pointer>> && !std::is_volatile_v<std::remove_pointer_t<Pointer>>;

namespace detail {

// The door of the two mints, and the key of the constructor of a
// RunAddress.  RunAddressDoor is the one friend of the key.  Its key() is
// private, and the two mints of this header are its only friends, so each
// RunAddress comes from a mint that refuses the null pointer.  An explicit
// specialization of a mint is a declaration in this namespace, which the
// rule home refuses outside the base.  The door stands after the niche of
// a RunAddress, because its friends give an Option of a RunAddress.
class RunAddressDoor;

class [[nodiscard]] RunAddressKey final {
    friend class RunAddressDoor;

    constexpr RunAddressKey() noexcept {}
    constexpr RunAddressKey(RunAddressKey const&) noexcept {}

public:
    RunAddressKey& operator=(RunAddressKey const&) = delete("a key of a RunAddress lives for one construction");
};

}  // namespace detail

template <DeviceIdentity Device>
class DataAddress;

class RunAddress {
    void* address_ = nullptr;
    // The seal makes the assignment user-provided, so std::bit_cast cannot
    // build a RunAddress from the bytes of an integer.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The tag of the null value.  Only the two hidden friends below build a
    // RunAddress from it.
    struct NullTag {};
    constexpr explicit RunAddress(NullTag) noexcept {}

    // The null value of an Option of a RunAddress, and its test.  Each one
    // takes the key that only the storage of an Option builds.
    [[nodiscard]] friend constexpr RunAddress niche_null_(niche_key<RunAddress> const& /*key*/) noexcept {
        return RunAddress{NullTag{}};
    }
    [[nodiscard]] friend constexpr bool niche_is_null_(niche_key<RunAddress> const& /*key*/,
                                                       RunAddress const& address) noexcept {
        return address.address_ == nullptr;
    }

    // The one reader of the pointer.
    friend void* foreign_pointer(RunAddress address) noexcept;

    // A DataAddress builds and tests the null value for its own niche.
    template <DeviceIdentity Device>
    friend class DataAddress;

public:
    // An address that is not null, with the key that only the mints take.
    // The constructor has no [[assume]] that the address is not null, for
    // the reason that the constructor of a Ref gives.
    constexpr RunAddress(void* address, detail::RunAddressKey const& /*key*/) noexcept : address_{address} {}

    // True when the address is a multiple of Alignment, a power of two.  The
    // query is a friend and not a member function template, because a
    // reflection gives no const qualifier of a member function template, and
    // the niche statement of a RunAddress needs members that write nothing.
    template <std::size_t Alignment>
        requires(Alignment != 0 && (Alignment & (Alignment - 1)) == 0)
    [[nodiscard]] friend bool is_aligned(RunAddress const& address) noexcept {
        return (__builtin_bit_cast(std::uintptr_t, address.address_) & (Alignment - 1)) == 0;
    }

    [[nodiscard]] friend constexpr bool operator==(RunAddress const& left, RunAddress const& right) noexcept {
        return left.address_ == right.address_;
    }
};

// The empty value of an Option of a RunAddress: the null pointer, which no
// RunAddress holds.  No door builds the null pointer: each mint gives no
// value for it, and the seal refuses std::bit_cast.  The payload gate checks
// the statement (the check of the statement in foundation/core/Choice.h): the
// one constructor from a pointer takes the key of the mints, no public member
// writes a RunAddress, and the statement stands in a header of the base.  So
// a loop over a mutable Option of a RunAddress gets a cursor that writes the
// RunAddress, and the storage of a full Option tests no RunAddress for the
// null pointer.
template <>
struct niche<RunAddress> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr RunAddress empty(niche_key<RunAddress> const& key) noexcept {
        return niche_null_(key);
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<RunAddress> const& key, RunAddress const& address) noexcept {
        return niche_is_null_(key, address);
    }
};

template <DeviceIdentity Device>
class DataAddress {
    RunAddress address_;
    Device device_{};
    FixedArray<unsigned char, sizeof(void*) - sizeof(Device)> pad_{};

    // The null value of an Option of a DataAddress: the null address with
    // the device of value initialization.  Only the niche below reads it, and
    // it takes the key that only the storage of an Option builds, so it is a
    // niche door.  DataAddress is a friend of RunAddress, so the kind
    // niche-access of the rule proof-routes reads this body, and it admits
    // the null RunAddress only in a function that takes a niche key.
    [[nodiscard]] static constexpr DataAddress empty_(niche_key<DataAddress> const& /*key*/) noexcept {
        return DataAddress{RunAddress{RunAddress::NullTag{}}, Device{}};
    }
    [[nodiscard]] constexpr bool is_empty_() const noexcept { return address_.address_ == nullptr; }

    friend struct niche<DataAddress>;

public:
    // The address on device.  The address is never null, so the pair needs
    // no check.
    constexpr DataAddress(RunAddress address, Device device) noexcept : address_{address}, device_{device} {}

    // The address alone, for a holder that keeps the device beside it.
    [[nodiscard]] constexpr RunAddress address() const noexcept { return address_; }

    // The device that holds the run.
    [[nodiscard]] constexpr Device device() const noexcept { return device_; }

    // True when the address is a multiple of Alignment, a power of two.  The
    // query is a friend for the reason that RunAddress gives.
    template <std::size_t Alignment>
        requires(Alignment != 0 && (Alignment & (Alignment - 1)) == 0)
    [[nodiscard]] friend bool is_aligned(DataAddress const& data) noexcept {
        return is_aligned<Alignment>(data.address_);
    }

    // The same address on the same device.
    [[nodiscard]] friend constexpr bool operator==(DataAddress const& left, DataAddress const& right) noexcept {
        return left.address_ == right.address_ && left.device_ == right.device_;
    }
};

// The empty value of an Option of a DataAddress: the empty value of its
// RunAddress.  Each DataAddress holds a RunAddress, and no RunAddress holds
// the null pointer, so no DataAddress holds the empty value.
template <DeviceIdentity Device>
struct niche<DataAddress<Device>> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr DataAddress<Device> empty(niche_key<DataAddress<Device>> const& key) noexcept {
        return DataAddress<Device>::empty_(key);
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<DataAddress<Device>> const& /*key*/,
                                                 DataAddress<Device> const& data) noexcept {
        return data.is_empty_();
    }
};

template <class Byte, std::size_t Extent>
    requires RunBytes<Byte>
[[nodiscard]] constexpr Option<RunAddress> mint_run_address(View<Byte, Extent> bytes, std::size_t offset) noexcept;

template <class Pointer>
    requires ForeignRunPointer<Pointer>
[[nodiscard]] Option<RunAddress> mint_foreign_run_address(Pointer first) noexcept;

namespace detail {

class RunAddressDoor final : ::foundation::NoObject<RunAddressDoor> {
    template <class Byte, std::size_t Extent>
        requires RunBytes<Byte>
    friend constexpr Option<RunAddress> core::mint_run_address(View<Byte, Extent> bytes, std::size_t offset) noexcept;
    template <class Pointer>
        requires ForeignRunPointer<Pointer>
    friend Option<RunAddress> core::mint_foreign_run_address(Pointer first) noexcept;

    [[nodiscard]] static constexpr RunAddressKey key() noexcept { return RunAddressKey{}; }
};

}  // namespace detail

// The address of the byte at offset of bytes, or no value for an empty run
// and for an offset past the count.  An offset equal to the count gives the
// address one past the last byte, where a run of no bytes at the end of
// bytes starts.  The run can have a fixed extent or a dynamic one.
template <class Byte, std::size_t Extent>
    requires RunBytes<Byte>
[[nodiscard]] constexpr Option<RunAddress> mint_run_address(View<Byte, Extent> bytes, std::size_t offset) noexcept {
    if (bytes.size() == 0 || offset > bytes.size()) [[unlikely]] {
        return Option<RunAddress>{none};
    }
    Byte* const first = first_of_(detail::BaseLayerRead{}, bytes);
    return Option<RunAddress>::some(RunAddress{static_cast<void*>(first + offset), detail::RunAddressDoor::key()});
}

// The address that a foreign ABI gives, or no value for the null pointer.
// The mint cannot prove that the run lives, so a call stands only at the
// edge of a foreign ABI.
template <class Pointer>
    requires ForeignRunPointer<Pointer>
[[nodiscard]] Option<RunAddress> mint_foreign_run_address(Pointer first) noexcept {
    if (first == nullptr) [[unlikely]] {
        return Option<RunAddress>{none};
    }
    return Option<RunAddress>::some(RunAddress{static_cast<void*>(first), detail::RunAddressDoor::key()});
}

// The pointer of an address, for a foreign reader such as a C caller that
// takes a void*.  The pointer is not null.  The caller reads the run only on
// the device that the holder of the address states.
[[nodiscard]] inline void* foreign_pointer(RunAddress address) noexcept { return address.address_; }

}  // namespace foundation::core
