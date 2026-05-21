// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-025 HS14 fixture #2.  PersistedSessionHandle's new
// templated `detach<Reason>()` overload carries
// `requires ::crucible::safety::proto::DetachReason<Reason>`.
// Passing a type that does NOT inherit from
// `detach_reason::tag_base` must fail constraint resolution —
// it must not silently fall through to the inherited deleted
// `[DetachReason_Required]` overload either; the *constraint*
// diagnostic is what pins the typed-reason discipline.
//
// Expected diagnostic family (one or more should match):
//   "DetachReason"  |  "requires"  |  "constraint"

#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>

// FIXY-V-031: Cipher::open() now takes Path<source::External>.
using CipherRoot = crucible::fixy::wrap::Path<
    crucible::fixy::tags::source::External>;

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

struct Resource {};

// Deliberately NOT inheriting from detach_reason::tag_base.
struct NotAReason {};

using P = proto::Send<int, proto::End>;

int main() {
    auto cipher = ::crucible::Cipher::open(CipherRoot{"/tmp/crucible_neg_psh_detach_untyped"});
    auto view   = cipher.mint_open_view();
    eff::TestRunnerCtx ctx{};
    auto h = proto::mint_persisted_session<P>(
        ctx,
        cipher,
        view,
        Resource{},
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    // NotAReason fails `requires DetachReason<Reason>` on PSH's
    // templated detach<Reason>().  Compile error pinned.
    std::move(h).detach(NotAReason{});
    return 0;
}
