// A rule of the relation is a function of type rule_signature.  A
// function of another type in admitted_implications is no rule, and the
// walk would skip it.  The natural slip is a rule that returns bool, and
// it would then compile and decide nothing.  The self-test at the foot of
// the header refuses it instead.
//
// The function is planted before the header, because the self-test reads
// the namespace as it stands at the foot of the header.

#include <meta>

namespace fixy::refined::admitted_implications {

consteval bool returns_bool(std::meta::info premise, std::meta::info conclusion) { return premise != conclusion; }

}  // namespace fixy::refined::admitted_implications

#include <fixy/Refined.h>

int main() { return 0; }
