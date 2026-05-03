// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 3 for safety::reflected::bits_to_string (#1089).
//
// Premise: bits_to_string<E>(b, nullptr, cap) with cap > 0 MUST be
// rejected.  The function's contract is
//
//   pre (cap == 0 || out != nullptr)
//
// — passing nullptr while claiming the buffer is non-zero-sized is a
// boundary lie that, before this contract was added, segfaulted at
// the first character write (out[needed] = c).  The audit caught the
// gap on first read of the shipped header: the doc-comment claimed
// "Contract enforces" but no contract was present.  Adding the pre
// turned the doc-comment into a checkable invariant; this fixture
// proves the gate fires both at compile time (P2900R14 contract
// evaluation in constant-expression contexts) and at runtime under
// `enforce` semantic (verified manually via /tmp/probe_contract).
//
// The compile-time path is the one this fixture exercises: invoking
// `bits_to_string` from a `consteval` function with a deliberately
// contract-violating argument list forces the contract predicate to
// be evaluated at compile time, where a false predicate is reported
// as "contract predicate is false in constant expression".
//
// Without this rejection, every consumer that took the function's
// doc-comment at face value (e.g., a future Cipher debugger that
// passed nullptr to compute the required allocation size and then
// allocated a buffer of that size) would crash on the FIRST call,
// not the second — silently masking the API misuse for one full
// allocation cycle of testing.
//
// Expected diagnostic: "contract predicate is false in constant
// expression" / "false in constant expression" / "pre" / "predicate
// is false" pointing at the bits_to_string signature line where the
// `pre (cap == 0 || out != nullptr)` clause lives.

#include <crucible/safety/Reflected.h>

namespace ref = crucible::safety::reflected;

enum class TestFlags : unsigned char {
    Alpha = 0x01,
    Beta  = 0x02,
};

// Wrap the contract-violating call in a consteval helper so the
// contract pre evaluates at compile time.  Per P2900R14, contract
// violations during constant evaluation are diagnosed as compile
// errors regardless of -fcontract-evaluation-semantic.
consteval bool probe_violates_contract() {
    auto n = ref::bits_to_string<TestFlags>(
        crucible::safety::Bits<TestFlags>{TestFlags::Alpha},
        nullptr,    // out=nullptr
        100);       // cap=100 > 0 — boundary lie!
    return n > 0;
}

// Bridge fires: probe_violates_contract() is invoked at compile time
// and its body's bits_to_string call hits the contract pre, which is
// false in constant expression.  Compilation aborts.
static_assert(probe_violates_contract());

int main() { return 0; }
