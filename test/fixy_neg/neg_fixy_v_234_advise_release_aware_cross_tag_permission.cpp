// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-234 fixture #3 — advise_release_aware Tag-vs-Permission mismatch.
//
// V-234's new `advise_release_aware<Advice, RegionTag>(ctx, region,
// Permission<RegionTag> const&)` signature ties the Permission tag to
// the RegionTag template parameter.  Passing a `Permission<RegionB>`
// when the call site names `RegionTag = RegionA` fires THIS fixture —
// the reference can't bind across distinct phantom-typed Permission
// instantiations (Permission<RegionA> and Permission<RegionB> are
// unrelated types).
//
// Why it matters: this is the laundering-attack closure.  Without the
// tag-pinning gate, a malicious or accidental call could pass any
// Permission they happened to hold and call DontNeed on a region they
// have no exclusive access to.  With the gate, the type system
// rejects laundering at template substitution time — long before any
// syscall is issued.
//
// Mismatch class: cross-tag-permission.  Distinct from V-234 fixture
// #2 (missing permission entirely) and from V-225 fixture #7 (wrong
// surface for the advice).  Together the three fixtures form the
// full distinct-mismatch-class set Agent 9 Bug 5 demands:
//
//   #2 missing-proof   — type system catches "no proof at all"
//   #3 wrong-tag-proof — type system catches "proof for wrong region"
//   #7 wrong-surface   — type system catches "wrong routing entirely"
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "cannot bind" / "no matching function" /
//   "invalid initialization" / "deduced conflicting".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/permissions/Permission.h>

struct RegionA {};
struct RegionB {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace advice = fwmm::advice;
    namespace prot   = fwmm::prot;
    namespace share  = fwmm::share;

    ::crucible::effects::TestRunnerCtx ctx{};

    fwmm::OwnedMmap<RegionA, prot::ReadOnly, share::Private> region_a{};

    // Mint a Permission for the WRONG region (RegionB).  Both RegionA
    // and RegionB have empty permission_row<> by default, so the
    // no-ctx root-mint is valid for both.
    auto perm_b = ::crucible::safety::mint_permission_root<RegionB>();

    // Should FAIL: passing Permission<RegionB> where the RegionTag
    // template parameter is RegionA.  The Permission<RegionA> const&
    // parameter can't bind to a Permission<RegionB> rvalue/lvalue —
    // they're unrelated phantom-typed types.
    [[maybe_unused]] auto r =
        fwmm::advise_release_aware<advice::DontNeed, RegionA>(ctx, region_a, perm_b);
    return 0;
}
