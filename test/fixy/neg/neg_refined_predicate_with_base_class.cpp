// A call of a predicate can run a call operator of a base class, which the
// seal does not read, so the checked mint refuses a predicate with a base.
// This file derives a predicate from positive and mints a value under it.
// The seal gives false, and the mint has no candidate.

#include <fixy/Refined.h>

namespace {

struct PositiveTwice : fixy::IsPositive {};

inline constexpr PositiveTwice positive_twice{};

}  // namespace

constexpr auto refused = fixy::mint_refined<positive_twice>(3);

int main() { return refused.value(); }
