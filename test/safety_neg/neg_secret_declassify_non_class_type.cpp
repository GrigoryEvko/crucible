// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-24 fixture #2: `Secret<T>::declassify<Policy>()` rejects a
// Policy that is NOT a class type — primitive types, enum types,
// references, pointers, etc.
//
// This is the orthogonal failure mode to fixture #1: where #1 covers
// "class but wrong inheritance", #2 covers "wrong kind of type
// entirely".  Together they witness BOTH clauses of the tightened
// concept:
//
//     concept DeclassificationPolicy =
//         std::is_class_v<Policy> &&                                // ← THIS
//         std::derived_from<Policy, secret_policy_base>;            // (other)
//
// `int` fails `is_class_v` (the first clause), so the concept fails
// before `std::derived_from` is even checked.  GCC's diagnostic cites
// either "DeclassificationPolicy" / "constraints not satisfied" or
// the named [SecretPolicy_NotInBase] static_assert depending on which
// substitution path the compiler took.
//
// Expected diagnostic: "DeclassificationPolicy" or "SecretPolicy_NotInBase"
// or "constraints not satisfied".

#include <crucible/safety/Secret.h>

#include <cstdint>

using crucible::safety::Secret;

int main() {
    Secret<std::uint64_t> s{0xDEADBEEFCAFEBABEULL};

    // BAD: int is a primitive type, not a class — the concept's
    // `std::is_class_v<Policy>` clause rejects it.
    auto bad = std::move(s).declassify<int>();
    (void)bad;
    return 0;
}
