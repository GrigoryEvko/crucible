// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-203 HS14 fixture #1 of 2 for fixy/spawn/JoinPolicy.h:
// IsJoinMechanismTag<T> rejects a type that lacks the
// `mechanism` member entirely.
//
// Mismatch axis: NO MECHANISM MEMBER at all.
//   * Fixture #1 (this file) — IsJoinMechanismTag<int> fires the
//     structural rejection: `int` has no member `mechanism` of any
//     kind, so even the first identity-allowlist clause
//     (`std::is_same_v<T, AutoJoin>`) fails immediately on a
//     fundamentally non-struct candidate.
//   * Fixture #2 (neg_fixy_v_203_join_imposter_struct.cpp) covers
//     the orthogonal axis: an IMPOSTER struct that DOES carry
//     `static constexpr JoinMechanism mechanism` (so it satisfies
//     the field-shape check) but is NOT one of the six declared
//     tag types — the `is_same_v` allowlist fires.
//
// Two distinct rejection paths through IsJoinMechanismTag ⇒ HS14
// floor satisfied.
//
// Expected diagnostic: IsJoinMechanismTag / constraints not satisfied
//                      / associated constraints / requires-clause.

#include <crucible/fixy/spawn/JoinPolicy.h>

namespace neg_fixy_v_203_concept_rejects_non_tag {

namespace join = ::crucible::fixy::spawn::join;

// Consumer template constrained on IsJoinMechanismTag.  Instantiating
// with `int` MUST red the requires-clause.
template <typename Mechanism>
    requires join::IsJoinMechanismTag<Mechanism>
[[nodiscard]] constexpr join::JoinMechanism mechanism_value() noexcept {
    return join::mechanism_of_v<Mechanism>;
}

// The bad instantiation — `int` is not one of the six declared tags
// (it isn't even a tag-shape candidate; no `mechanism` member at
// any spelling).
constexpr auto bad_dispatch = mechanism_value<int>();

}  // namespace neg_fixy_v_203_concept_rejects_non_tag

int main() {
    return 0;
}
