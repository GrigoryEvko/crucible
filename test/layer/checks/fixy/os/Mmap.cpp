// The compile-time checks of fixy/os/Mmap.h.

#include <fixy/os/Mmap.h>

namespace fixy::mmap::detail::mmap_surface_invariants {

// The words of foundation/os/MapWords.h, which src/foundation/os/Syscall.cpp
// compares with the MADV_* words of <linux/mman.h>.
static_assert(advice_value_of(^^advice::DontNeed) == ::foundation::os::kAdviseDontNeed);
static_assert(advice_value_of(^^advice::HugePage) == ::foundation::os::kAdviseHugePage);
static_assert(advice_value_of(^^advice::Free) == ::foundation::os::kAdviseFree);

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
using A_Faults = ::fixy::atom::mmap::faults_on_truncate;
using A_Unchanged = ::fixy::atom::mmap::unchanged_file;

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
static_assert(std::is_same_v<region_modifiers_t<A_RO, A_Faults, A_Shared>,
                             region_modifiers<::fixy::atom::mmap::faults_on_truncate>>);

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

// The end of a shared file, read over a pack.
using FileKind = ::fixy::os::kind::File;
using SealedKind = ::fixy::os::kind::SealedMemory;
inline constexpr ::fixy::os::RightSet kMapRight = ::fixy::os::RightSet::map;
static_assert(CtxFitsFileMmapMint<IoBlockCtx, FileKind, kMapRight, A_RO, A_Shared, A_Faults>);
static_assert(!CtxFitsFileMmapMint<IoBlockCtx, FileKind, kMapRight, A_RO, A_Shared>,
              "a shared map of a regular file needs the atom faults_on_truncate.");
static_assert(CtxFitsFileMmapMint<IoBlockCtx, FileKind, kMapRight, A_RO, A_Private, A_Unchanged>);
static_assert(!CtxFitsFileMmapMint<IoBlockCtx, FileKind, kMapRight, A_RO, A_Private>,
              "a private map of a regular file needs the atom unchanged_file.");
static_assert(!CtxFitsFileMmapMint<IoBlockCtx, FileKind, kMapRight, A_RO, A_Shared, A_Unchanged>,
              "the atom of a private map is no licence of a shared one.");
static_assert(CtxFitsFileMmapMint<IoBlockCtx, SealedKind, kMapRight, A_RO, A_Shared>);
static_assert(!CtxFitsFileMmapMint<IoBlockCtx, SealedKind, kMapRight, A_RO, A_Private>,
              "sealed memory has only shared maps.");

static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::HugePage>);
static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::Sequential>);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, advice::DontNeed>,
              "DontNeed zeroes the pages, so it must go through the release-aware door.");
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, advice::Free>,
              "Free lets the kernel zero the pages, so it must go through the release-aware door.");
static_assert(!CtxFitsSafeAdvise<IoOnlyCtx, advice::HugePage>);

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

}  // namespace fixy::mmap::detail::mmap_surface_invariants
