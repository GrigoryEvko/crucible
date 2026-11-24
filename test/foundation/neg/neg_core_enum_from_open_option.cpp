// Negative-compile test. This file must fail to compile.
//
// Each value of the underlying type of an open enumeration is a value, so
// enum_from of it is total: it gives the value and no Option.  A test of an
// Option of it would be a branch that never runs, and it does not compile.
//
// Expected diagnostic: the result of enum_from<Code> is the enumeration
// Code, which has no member is_some.

#include <foundation/ByteSeal.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

enum class[[= ::foundation::core::open_values{}]] Code : std::uint16_t {
    one = 1
};

int main() { return ::foundation::core::enum_from<Code>(std::uint16_t{300}).is_some() ? 0 : 1; }
