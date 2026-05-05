// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-006 / I002: classified data may not flow through Fail(E) with
// a non-secret error payload.  The Phase-0 catalog uses source-visible
// marker traits for body/effect facts that the C++ substrate cannot
// infer from a Fixy IR yet.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_i002 {
struct Payload {
    int value = 0;
};

using Bad = fn::Fn<Payload>;  // default security is Classified.
}

namespace crucible::safety::fn::collision {
template <>
struct marks_fail<::neg_collision_i002::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_i002::Bad bad{};

int main() { return 0; }
