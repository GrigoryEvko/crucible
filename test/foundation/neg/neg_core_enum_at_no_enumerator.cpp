// Negative-compile test. This file must fail to compile.
//
// An enumeration with no enumerator has no value to pick: each value of its
// underlying type is a value, and none is an enumerator.  enum_at refuses
// such an enumeration, as enum_from does.
//
// Expected diagnostic: no enum_at of the enumeration Strong, and the
// constraint names its enumerators.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Strong : std::uint8_t {
};

int main() { return ::foundation::core::enum_at<Strong>(0).is_some() ? 0 : 1; }
