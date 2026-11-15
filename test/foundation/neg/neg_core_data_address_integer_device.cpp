// A device needs a type of its own.  An integer can mean a count, an index
// or a kind of device, so DataAddress refuses it as its device.

#include <foundation/core/DataAddress.h>

int main() { return static_cast<int>(sizeof(::foundation::core::DataAddress<int>)); }
