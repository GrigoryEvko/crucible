// The compile-time checks of foundation/core/DataAddress.h.
#include <foundation/core/DataAddress.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {
namespace detail::data_address_checks {

struct TwoBytes {
    unsigned char kind = 0;
    signed char index = -1;

    [[nodiscard]] friend constexpr bool operator==(TwoBytes const&, TwoBytes const&) noexcept = default;
};

struct FourBytes {
    std::uint32_t id = 0;

    [[nodiscard]] friend constexpr bool operator==(FourBytes const&, FourBytes const&) noexcept = default;
};

struct EightBytes {
    std::uint64_t id = 0;

    [[nodiscard]] friend constexpr bool operator==(EightBytes const&, EightBytes const&) noexcept = default;
};

struct NoEquality {
    unsigned char kind = 0;
};

enum class Kind : unsigned char {
    host = 0,
    accelerator = 1,
};

// A device is a class or an enumeration with equality, of fewer than eight
// bytes, whose copy is a copy of its bytes.  An integer and a pointer are no
// device.
static_assert(DeviceIdentity<TwoBytes> && DeviceIdentity<FourBytes> && DeviceIdentity<Kind>
              && DeviceIdentity<std::byte>);
static_assert(!DeviceIdentity<EightBytes> && !DeviceIdentity<NoEquality> && !DeviceIdentity<int>
              && !DeviceIdentity<int*> && !DeviceIdentity<TwoBytes const>);

// A RunAddress is one word, and an Option of it holds no address with the
// null pointer, so it is one word too.  A DataAddress is two words: the
// address, and the device with a named pad.
static_assert(sizeof(RunAddress) == sizeof(void*) && alignof(RunAddress) == alignof(void*));
static_assert(sizeof(Option<RunAddress>) == sizeof(void*));
static_assert(sizeof(DataAddress<TwoBytes>) == 16 && alignof(DataAddress<TwoBytes>) == 8);
static_assert(sizeof(DataAddress<FourBytes>) == 16 && sizeof(DataAddress<Kind>) == 16);
static_assert(sizeof(Option<DataAddress<TwoBytes>>) == 16 && sizeof(Option<DataAddress<Kind>>) == 16);
static_assert(niche<RunAddress>::is_empty_never_built && niche<DataAddress<TwoBytes>>::is_empty_never_built);

// The copy and the end are those of the bytes, so the ABI gives the words in
// registers.  The seal makes the assignment user-provided, so the types are
// not trivially copyable, and std::bit_cast builds none.  No default
// constructor: each address comes through a mint.
static_assert(std::is_trivially_copy_constructible_v<RunAddress> && std::is_trivially_destructible_v<RunAddress>);
static_assert(std::is_trivially_copy_constructible_v<DataAddress<TwoBytes>>
              && std::is_trivially_destructible_v<DataAddress<TwoBytes>>);
static_assert(std::is_nothrow_copy_assignable_v<RunAddress>
              && std::is_nothrow_copy_assignable_v<DataAddress<TwoBytes>>);
static_assert(!std::is_trivially_copyable_v<RunAddress> && !std::is_trivially_copyable_v<DataAddress<TwoBytes>>);
static_assert(!std::is_default_constructible_v<RunAddress> && !std::is_default_constructible_v<DataAddress<TwoBytes>>);

// A pair comes from an address and a device, and from nothing else.
static_assert(std::is_nothrow_constructible_v<DataAddress<TwoBytes>, RunAddress, TwoBytes>);
static_assert(!std::is_constructible_v<DataAddress<TwoBytes>, void*, TwoBytes>);
static_assert(!std::is_constructible_v<RunAddress, void*>);

// Equality and no order: an address changes from run to run.  No
// arithmetic: the replay only gives an address on.
template <class T>
concept Ordered = requires(T const& left, T const& right) { left < right; };
template <class T>
concept Offsets = requires(T const& data) { data + 1; };
static_assert(std::equality_comparable<RunAddress> && std::equality_comparable<DataAddress<TwoBytes>>);
static_assert(!Ordered<RunAddress> && !Offsets<RunAddress>);
static_assert(!Ordered<DataAddress<TwoBytes>> && !Offsets<DataAddress<TwoBytes>>);

// The mint from bytes takes a run of bytes that it can write.  The foreign
// mint takes a pointer to data that it can write.
static_assert(RunBytes<unsigned char> && RunBytes<std::byte>);
static_assert(!RunBytes<unsigned char const> && !RunBytes<char> && !RunBytes<int>);
static_assert(ForeignRunPointer<void*> && ForeignRunPointer<float*> && ForeignRunPointer<unsigned char*>);
static_assert(!ForeignRunPointer<void const*> && !ForeignRunPointer<float const*> && !ForeignRunPointer<void (*)()>
              && !ForeignRunPointer<std::uintptr_t>);

// An empty run gives no address, also in a constant evaluation.
static_assert(mint_run_address(View<unsigned char>{}, 0).is_none());

}  // namespace detail::data_address_checks
}  // namespace foundation::core
