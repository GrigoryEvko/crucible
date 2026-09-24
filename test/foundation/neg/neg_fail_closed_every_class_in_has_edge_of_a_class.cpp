// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation is read from the members of a namespace.  A class is not a
// namespace, even when it holds a static member of type edge, and the
// static_assert in every_class_in_has_edge refuses it.  Without that
// check, every_class_in_has_edge reads the class as a relation and
// answers.
//
// Expected diagnostic: the static_assert in every_class_in_has_edge that
// asks for the reflections of two namespaces.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

struct NotARelation {
    static constexpr ffc::edge<Raw, Checked> raw_to_checked{};
};

namespace vacant {}

[[maybe_unused]] constexpr auto answer =
    ffc::every_class_in_has_edge<^^NotARelation, ^^vacant, ffc::EdgeEnd::Either>();

}  // namespace

int main() { return 0; }
