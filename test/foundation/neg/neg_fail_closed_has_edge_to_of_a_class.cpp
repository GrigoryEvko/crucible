// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation is read from the members of a namespace.  A class is not a
// namespace, even when it holds a static member of type edge, and the
// static_assert in has_edge_to refuses it.  Without that check,
// has_edge_to reads the class as a relation and answers.
//
// Expected diagnostic: the static_assert in has_edge_to that asks for
// the reflection of a namespace.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

struct NotARelation {
    static constexpr ffc::edge<Raw, Checked> raw_to_checked{};
};

[[maybe_unused]] constexpr auto answer = ffc::has_edge_to<^^NotARelation, Checked>();

}  // namespace

int main() { return 0; }
