// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation is read from the members of a namespace.  A class is not a
// namespace, even when it holds a static member of type edge, and admits
// refuses it.  Without that check, admits reads the class as a relation
// and answers.
//
// Expected diagnostic: the call in admits to the function that names the
// fault, which has no constant definition.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

struct NotARelation {
    static constexpr ffc::edge<Raw, Checked> raw_to_checked{};
};

[[maybe_unused]] constexpr auto answer = ffc::admits(^^NotARelation, ^^Raw, ^^Checked);

}  // namespace

int main() { return 0; }
