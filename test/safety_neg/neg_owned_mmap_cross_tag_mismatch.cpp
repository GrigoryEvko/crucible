// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-231 HS14 fixture #3: cross-Tag OwnedMmap mismatch rejected.
//
// Violation: passing `OwnedMmap<TagA, Prot, Share>` to a function
// whose signature demands `OwnedMmap<TagB, Prot, Share>`.  Despite
// identical Prot AND Share template parameters, the two OwnedMmap
// specializations are UNRELATED types — phantom Tag is the per-call-
// site identity discipline; the type system gives no implicit
// conversion across distinct tags.
//
// Discipline rationale (safety/OwnedMmap.h doc-block):
//   The Tag identifies WHICH mmap region the wrapper owns at the
//   type level.  An API that demands `OwnedMmap<TraceRingTag, ...>`
//   refuses `OwnedMmap<MetaLogTag, ...>` because the two regions
//   are semantically incompatible — they have different layouts,
//   different page-fault models, different durability tiers.
//   Mixing them would alias unrelated VA ranges.
//
//   This is the frame-rule-by-type-system: every OwnedMmap<Tag,...>
//   instantiation lives in its own type universe; mixing tags is a
//   compile error.  No runtime check is needed because the
//   incompatibility is structural.
//
// HS14 — paired with copy-rejected fixtures for THREE distinct
// mismatch classes:
//   * Class T-copy-ctor       (fixture #1): deleted-copy-ctor with
//     linearity-duplication reason — region uniqueness.
//   * Class T-copy-assign     (fixture #2): deleted-copy-assign with
//     the SAME reason — second half of the deleted-copy pair.
//   * Class T-cross-tag       (THIS file):  typed-overload rejection
//     across distinct Tag template parameters — per-call-site
//     identity discipline.
// Together the triple pins all three soundness layers of OwnedMmap:
//   (a) copy-as-duplicate is rejected at the type level (ctor + op=);
//   (b) phantom Tag is load-bearing for overload resolution (regions
//       for different syscall surfaces don't substitute for each
//       other).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "cannot convert" / "no matching function" / "deduced conflicting".

#include <crucible/safety/OwnedMmap.h>

#include <utility>

namespace {
    // Two distinct provenance tags — production-shape names like
    // mmap_region::TraceRing vs mmap_region::MetaLog would carry the
    // same semantic distinction.  Both are empty class types.
    struct RegionTagA {};
    struct RegionTagB {};

    // Shared Prot / Share — the only thing distinguishing the two
    // OwnedMmap instantiations is the Tag.  This isolates the
    // cross-Tag rejection from any Prot/Share mismatch noise.
    struct ProbeProt   {};
    struct ProbeShare  {};

    // API entry point demanding a specific tag.  In production this
    // would be a perf-hub install function whose signature names
    // exactly the ring it operates over.
    [[maybe_unused]] void install_region_a(
        ::crucible::safety::OwnedMmap<RegionTagA, ProbeProt, ProbeShare>&&)
    {
        // body irrelevant — call-site type-check is the test.
    }
}  // namespace

// Anchor: same-tag call compiles cleanly — RegionTagA accepted by
// the RegionTagA-typed parameter.
[[maybe_unused]] static void anchor_same_tag_call(
    ::crucible::safety::OwnedMmap<RegionTagA, ProbeProt, ProbeShare>&& region)
{
    install_region_a(std::move(region));
}

// VIOLATION: OwnedMmap<RegionTagA, ...> and OwnedMmap<RegionTagB, ...>
// are unrelated template instantiations — even though Prot and Share
// match.  C++ overload resolution finds no implicit conversion across
// distinct Tag types; GCC rejects with "cannot convert".
[[maybe_unused]] static void offending_cross_tag_call(
    ::crucible::safety::OwnedMmap<RegionTagB, ProbeProt, ProbeShare>&& region)
{
    install_region_a(std::move(region));   // ERROR: TagB ≠ TagA
}

int main() { return 0; }
