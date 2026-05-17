// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #2: Refined<positive, int> rejects predicate
// violation at consteval construction.
//
// Violation: Refined construction contract-checks the predicate.
// Constructing `Refined<positive, int>{-3}` at consteval fires the
// pre-condition `pre(Pred(v))` which surfaces as a non-constant
// expression (consteval / static_assert context).
//
// Expected diagnostic: substring naming the contract violation or
// "not a constant expression" / "contract".

#include <crucible/fixy/Wrap.h>

namespace fw  = crucible::fixy::wrap;
namespace saf = crucible::safety;

struct TypeFixyWrapRefinedNeg {};

// Trigger consteval failure: Refined<positive, int>{-3} construction
// should fire pre(Pred(v)) and reject as non-constant.
consteval int probe() {
    fw::Refined<saf::positive, int> r{-3};  // predicate `-3 > 0` false
    return r.value();
}

constexpr int kProbed = probe();

int main() {
    (void)kProbed;
    return 0;
}
