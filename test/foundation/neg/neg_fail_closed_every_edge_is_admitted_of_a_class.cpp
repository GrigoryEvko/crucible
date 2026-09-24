// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation is read from the members of a namespace.  A class is not a
// namespace, and the static_assert in every_edge_is_admitted refuses it.
// The class holds no edge, so admits is never asked and cannot refuse it
// in its place.  Without that check, every_edge_is_admitted reads the
// class as a relation and answers.
//
// Expected diagnostic: the static_assert in every_edge_is_admitted that
// asks for the reflection of a namespace.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct NotARelation {
    static constexpr int width = 3;
};

[[maybe_unused]] constexpr auto answer = ffc::every_edge_is_admitted<^^NotARelation>();

}  // namespace

int main() { return 0; }
