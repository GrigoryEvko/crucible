// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-203 HS14 fixture #2 of 2 for fixy/spawn/JoinPolicy.h:
// IsJoinMechanismTag<T> rejects an IMPOSTER struct that carries
// `static constexpr JoinMechanism mechanism = ...` (so the field-
// shape clause would PASS structurally) but is NOT one of the six
// declared tag types.
//
// Mismatch axis: WELL-FORMED MECHANISM FIELD but WRONG TYPE IDENTITY.
//   * Distinct from fixture #1 — fixture #1 (`...rejects_non_tag.cpp`)
//     uses `int`, which has no `mechanism` member at any spelling.
//     This fixture instead uses a struct that DOES carry the right
//     field type and value, but is not one of the six declared tags.
//   * The load-bearing soundness check is the `is_same_v` allowlist
//     inside IsJoinMechanismTag — it forbids a downstream consumer
//     from inventing a private struct with `mechanism = AutoJoin`
//     and substituting it for the canonical fixy::spawn::join::AutoJoin
//     at a future spawn-mint call site.
//
// Two distinct rejection paths through IsJoinMechanismTag ⇒ HS14
// floor satisfied.
//
// Expected diagnostic: IsJoinMechanismTag / constraints not satisfied
//                      / associated constraints / requires-clause /
//                      `is_same_v` references the six declared tags.

#include <crucible/fixy/spawn/JoinPolicy.h>

namespace neg_fixy_v_203_join_imposter_struct {

namespace join = ::crucible::fixy::spawn::join;

// IMPOSTER struct that satisfies the field-shape clause but fails
// the identity allowlist.  Carries `static constexpr JoinMechanism
// mechanism` of the right type — the field SHAPE matches AutoJoin's
// — but the type identity is `BadImposterTag`, not any of
// {AutoJoin, ManualJoin, Detached, Cloned, Forked, PosixSpawn}.
struct BadImposterTag {
    static constexpr join::JoinMechanism mechanism =
        join::JoinMechanism::AutoJoin;
};

// Consumer template constrained on IsJoinMechanismTag.  Instantiating
// with the imposter MUST red the requires-clause via the identity
// allowlist (not the field-shape clause, which the imposter satisfies).
template <typename Mechanism>
    requires join::IsJoinMechanismTag<Mechanism>
[[nodiscard]] constexpr join::JoinMechanism extract_mechanism() noexcept {
    return Mechanism::mechanism;
}

// The bad instantiation — BadImposterTag carries the right field
// type and value but is NOT one of the six declared tag types.
constexpr auto bad_dispatch = extract_mechanism<BadImposterTag>();

}  // namespace neg_fixy_v_203_join_imposter_struct

int main() {
    return 0;
}
