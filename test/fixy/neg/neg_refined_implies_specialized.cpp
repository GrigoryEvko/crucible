// PredicateImplies reads the closure predicate_implies, and the session
// payload order reads it too.  This file tries to forge non_negative ⇒
// positive, which zero refutes: it writes an explicit specialization of
// predicate_implies.  The closure is a function at namespace scope that
// is not a template, so no specialization matches it.

#include <fixy/Refined.h>

#include <meta>

template <>
consteval bool fixy::refined::predicate_implies(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
