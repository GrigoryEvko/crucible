// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule I004 — SECRET pole of classified_v
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture already shipped:
//   * neg_collision_I004_classified_async_session_no_ct.cpp
//     — CLASSIFIED pole (SecLevel::Classified)
//
// I004_OK gates the conjunction:
//
//   concept I004_OK = !(classified_v<F> && has_async_v<F> &&
//                       session_protocol_v<F> && !has_ct_v<F>);
//
// `classified_v<F>` is a TWO-POLE value-fold:
//
//   classified_v = (security_v == SecLevel::Classified)
//               || (security_v == SecLevel::Secret);
//
// The shipped fixture exercises the Classified pole; THIS fixture
// exercises the Secret pole — a genuinely distinct mismatch class
// (Secret-classified async session traffic without CT), parallel to
// the I002 classified/secret value-fold duals.
//
// Why both poles are required per HS14:
//
//   * A refactor that drops the `== SecLevel::Classified` disjunct
//     breaks the Classified path → caught by the shipped fixture.
//   * A refactor that drops the `== SecLevel::Secret` disjunct
//     breaks the Secret path → caught by THIS fixture.
//   * Without both, classified_v can silently collapse to a single
//     pole and ship — Secret async session traffic would lose its
//     CT requirement (GAPS-012 reopened for the Secret tier).
//
// Mismatch class: Secret-classified async session, no CT.  Distinct
// from the Classified-pole fixture: the value-fold's SECOND disjunct
// drives the engagement here.
//
// Expected diagnostic substring: "I004:".

#include <crucible/safety/Fn.h>
#include <crucible/sessions/Session.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace proto = crucible::safety::proto;

namespace neg_collision_i004_secret {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Secret,
                   proto::Send<int, proto::End>>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_async<::neg_collision_i004_secret::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_i004_secret::Bad bad{};

int main() { return 0; }
