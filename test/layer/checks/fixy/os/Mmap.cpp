// The compile-time checks of fixy/os/Mmap.h.

#include <fixy/os/Mmap.h>

namespace fixy::mmap::detail::mmap_surface_invariants {

static_assert(advice_value_of(^^advice::DontNeed) == MADV_DONTNEED);
static_assert(advice_value_of(^^advice::HugePage) == MADV_HUGEPAGE);
static_assert(advice_value_of(^^advice::Free) == MADV_FREE);

static_assert(DiscardsPages<advice::DontNeed>);
static_assert(DiscardsPages<advice::Free>, "Free lets the kernel drop the pages before the next write.");
static_assert(!DiscardsPages<advice::HugePage>);
static_assert(!DiscardsPages<advice::Sequential>);
static_assert(!DiscardsPages<advice::WipeOnFork>, "WipeOnFork zeros the pages of a child, not of this process.");

static_assert(PrimaryShare<share::Shared>);
static_assert(PrimaryShare<share::Anonymous>);
static_assert(!PrimaryShare<share::Locked>);
static_assert(!PrimaryShare<share::Populate>);
static_assert(!PrimaryShare<share::HugeTLB>);

using A_RO = ::fixy::atom::mmap::with_prot<prot::ReadOnly>;
using A_Shared = ::fixy::atom::mmap::with_share<share::Shared>;
using A_Private = ::fixy::atom::mmap::with_share<share::Private>;
using A_Anon = ::fixy::atom::mmap::with_share<share::Anonymous>;
using A_Locked = ::fixy::atom::mmap::with_share<share::Locked>;
using A_Exec = ::fixy::atom::mmap::with_prot<prot::Exec>;
using A_Jit = ::fixy::atom::mmap::trusted_jit;

static_assert(::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot, A_RO, A_Shared>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot, A_Shared>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot, A_RO, A_RO>);
static_assert(HasOnePrimaryShareAtom<A_RO, A_Shared>);
static_assert(HasOnePrimaryShareAtom<A_RO, A_Anon, A_Locked>);
static_assert(!HasOnePrimaryShareAtom<A_RO, A_Locked>);
static_assert(!HasOnePrimaryShareAtom<A_Shared, A_Private>);
static_assert(pack_has_anonymous_v<A_RO, A_Anon>);
static_assert(!pack_has_anonymous_v<A_RO, A_Shared>);

// The empty pack answers false rather than failing to compile, which is
// what lets the mint gate reject it instead of hard-erroring.
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot>);
static_assert(!HasOnePrimaryShareAtom<>);

static_assert(std::is_same_v<prot_of_t<A_RO, A_Shared>, prot::ReadOnly>);
static_assert(std::is_same_v<prot_of_t<A_Shared, A_Exec, A_Jit>, prot::Exec>);
static_assert(std::is_same_v<primary_share_of_t<A_RO, A_Shared>, share::Shared>);
static_assert(std::is_same_v<primary_share_of_t<A_RO, A_Anon, A_Locked>, share::Anonymous>);

// The prot atom and the primary share atom become the region type.  The
// modifiers are what is left of the pack for the region door.
static_assert(std::is_same_v<region_modifiers_t<A_RO, A_Shared>, region_modifiers<>>);
static_assert(std::is_same_v<region_modifiers_t<A_Shared, A_Exec, A_Locked, A_Jit>,
                             region_modifiers<share::Locked, ::fixy::atom::mmap::trusted_jit>>);

// The row derived from the pack is IO and Block.  This is the pin on
// that equality: it fails if an mmap atom
// stops lifting to IO and Block, which is a decision somebody has to
// make rather than discover.
using ExpectedMmapRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_RO, A_Shared>, ExpectedMmapRow>);
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Exec, A_Private, A_Jit>, ExpectedMmapRow>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using FgCtx = eff::ExecCtx<>;

static_assert(CtxFitsMmapMint<IoBlockCtx, A_RO, A_Shared>);
static_assert(!CtxFitsMmapMint<IoOnlyCtx, A_RO, A_Shared>,
              "a context without Block must not reach mmap: the call can park on page-cache pressure.");
static_assert(!CtxFitsMmapMint<FgCtx, A_RO, A_Shared>);

static_assert(CtxFitsAnonMmapMint<IoBlockCtx, A_RO, A_Anon>);
static_assert(!CtxFitsAnonMmapMint<IoBlockCtx, A_RO, A_Shared>);

static_assert(!CtxFitsMmapMint<IoBlockCtx, A_Exec, A_Private>, "an executable mapping needs the trusted_jit atom.");
static_assert(CtxFitsMmapMint<IoBlockCtx, A_Exec, A_Private, A_Jit>);

static_assert(!CtxFitsMmapMint<IoBlockCtx>, "an empty atom pack names no protection and no share mode.");

static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::HugePage>);
static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::Sequential>);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, advice::DontNeed>,
              "DontNeed zeroes the pages, so it must go through the release-aware door.");
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, advice::Free>,
              "Free lets the kernel zero the pages, so it must go through the release-aware door.");
static_assert(!CtxFitsSafeAdvise<IoOnlyCtx, advice::HugePage>);

// The release gate, read against one probe mapping and each proof a
// caller could hand it.  The brands are named types, so each claim reads
// without a mint.  The mapping spelled without a brand is on the erased
// identity.
struct ProbeRegion {};
struct ProbeOtherRegion final {};
struct ProbeDerivedRegion final : ProbeRegion {};
struct ProbeBrand final {};
struct ProbeOtherBrand final {};

using ProbeMapping = OwnedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous, ProbeBrand>;
using ErasedMapping = OwnedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous>;

using OwnProof = ::foundation::permissions::Permission<ProbeRegion, ProbeBrand>;
using OtherTagProof = ::foundation::permissions::Permission<ProbeOtherRegion, ProbeBrand>;
using DerivedTagProof = ::foundation::permissions::Permission<ProbeDerivedRegion, ProbeBrand>;
using OtherBrandProof = ::foundation::permissions::Permission<ProbeRegion, ProbeOtherBrand>;
using ShareProof = ::foundation::permissions::SharedPermission<ProbeRegion, ProbeBrand>;
// A class derived from the right permission.  It has the tag_type and
// the brand_type of that permission, which is why the tag clause asks
// for the Permission template exactly rather than for the two members.
struct LookAlikeProof : OwnProof {};

static_assert(CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::DontNeed, ProbeMapping, OwnProof>);
static_assert(CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::Free, ProbeMapping, OwnProof>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::HugePage, ProbeMapping, OwnProof>,
              "the release-aware door is for the advice that discards pages.  The rest go through advise.");
static_assert(!CtxFitsReleaseAwareAdvise<IoOnlyCtx, advice::DontNeed, ProbeMapping, OwnProof>);

static_assert(ProofNamesMappingTag<OwnProof, ProbeMapping> && MappingIsBranded<ProbeMapping>
              && ProofNamesMappingBrand<OwnProof, ProbeMapping>);
static_assert(!ProofNamesMappingTag<OtherTagProof, ProbeMapping>,
              "a permission of another region must not admit a discard of this mapping.");
static_assert(!ProofNamesMappingTag<DerivedTagProof, ProbeMapping>,
              "a tag derived from the mapping's tag is another tag.");
static_assert(!ProofNamesMappingTag<LookAlikeProof, ProbeMapping>, "a class derived from a permission is not one.");
static_assert(!ProofNamesMappingTag<ShareProof, ProbeMapping>, "a share is not the exclusive.");
static_assert(!ProofNamesMappingBrand<OtherBrandProof, ProbeMapping>,
              "a permission of another instance of the tag must not admit a discard of this mapping.");
static_assert(!MappingIsBranded<ErasedMapping>,
              "on the erased brand every mapping of the tag is one type, so no proof names one of them.");

// The same refusals through the function itself, where the tag and the
// brand are deduced from the mapping.
template <typename Mapping, typename Proof>
concept CanRelease = requires(IoBlockCtx const& ctx, Mapping& mapping, Proof const& proof) {
    advise_release_aware<advice::DontNeed>(ctx, mapping, proof);
};
static_assert(CanRelease<ProbeMapping, OwnProof>);
static_assert(!CanRelease<ProbeMapping, OtherTagProof>);
static_assert(!CanRelease<ProbeMapping, DerivedTagProof>);
static_assert(!CanRelease<ProbeMapping, LookAlikeProof>);
static_assert(!CanRelease<ProbeMapping, ShareProof>);
static_assert(!CanRelease<ProbeMapping, OtherBrandProof>);
static_assert(!CanRelease<ErasedMapping, OwnProof>);

// A tag with no row in a table has no bits, and a read of them is a
// compile error.  These cells are the witness: an atom over a tag with
// no row is refused at the gate, and it cannot fold into PROT_NONE.
struct NotAProt final {};
struct NotAShare final {};
using A_UnknownProt = ::fixy::atom::mmap::with_prot<NotAProt>;
using A_UnknownShare = ::fixy::atom::mmap::with_share<NotAShare>;

static_assert(!MappedProt<NotAProt>, "a prot tag with no row must have no bits, not PROT_NONE.");
static_assert(!MappedShare<NotAShare>, "a share tag with no row must have no bits, not zero.");
static_assert(!MappedProt<void> && !MappedShare<void>);
static_assert(MappedProt<prot::ReadOnly> && MappedShare<share::Private>);
static_assert(all_atom_tags_known_v<A_RO, A_Shared>);
static_assert(!all_atom_tags_known_v<A_UnknownProt, A_Shared>);
static_assert(!all_atom_tags_known_v<A_RO, A_Shared, A_UnknownShare>);
static_assert(!CtxFitsMmapMint<IoBlockCtx, A_UnknownProt, A_Shared>,
              "a prot tag with no PROT_* row must be refused at the gate, not mapped as PROT_NONE.");

// A type that is not an advice tag has no row, and the gate refuses it
// rather than instantiating madvise with a guessed value.
struct NotAnAdvice final {};
static_assert(!KnownAdvice<NotAnAdvice>);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, NotAnAdvice>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, NotAnAdvice, ProbeMapping, OwnProof>);

}  // namespace fixy::mmap::detail::mmap_surface_invariants
