// The relation admits a pair through an edge or a rule, and each rule is
// a function at namespace scope that is not a template.  This file tries
// to forge bounded_above<30> ⇒ bounded_above<20>, which thirty refutes:
// it writes an explicit specialization of the rule for ceilings.  No
// specialization matches a function that is not a template.

#include <fixy/Refined.h>

#include <meta>

template <>
consteval fixy::refined::rule_verdict fixy::refined::admitted_implications::bounded_above_weakens(std::meta::info,
                                                                                                  std::meta::info) {
    return {.admits = true};
}

int main() { return 0; }
