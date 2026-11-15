// The device and a named pad fill the second word of a DataAddress, so a
// device of eight bytes leaves no room for the pad.  DataAddress refuses it.

#include <foundation/core/DataAddress.h>

struct WideDevice {
    unsigned long long id = 0;

    friend constexpr bool operator==(WideDevice const&, WideDevice const&) noexcept = default;
};

int main() { return static_cast<int>(sizeof(::foundation::core::DataAddress<WideDevice>)); }
