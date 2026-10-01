// The compile-time checks of fixy/OwnedMmap.h.

#include <fixy/OwnedMmap.h>

namespace fixy {

namespace detail::owned_mmap_self_test {

static_assert(!std::is_copy_constructible_v<SmokeOwnedMmap>, "OwnedMmap must be move-only — copy would double-unmap");
static_assert(!std::is_copy_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_default_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_destructible_v<SmokeOwnedMmap>);
static_assert(sizeof(SmokeOwnedMmap) == sizeof(void*) + sizeof(std::size_t),
              "OwnedMmap is exactly {addr, len} — no hidden padding");

// The construction door, checked from a scope the class does not
// befriend.  A public constructor over an address lets a caller claim a
// mapping it never made, and the destructor unmaps whatever it holds, so
// a stack address handed in here is a stack address unmapped on scope
// exit.  mint_region is the only way to a non-empty region, and it builds
// one only from what ::mmap returned.
static_assert(!std::is_constructible_v<SmokeOwnedMmap, void*, std::size_t>,
              "The constructor that claims an address must not be public.  A caller could hand it a stack or heap "
              "address and the destructor would munmap it.  Take a region from mint_region, or from "
              "fixy::mmap::mint_mmap.");
static_assert(!std::is_constructible_v<SmokeOwnedMmap, void*>,
              "There is no one-argument form either: a region without its exact length cannot be unmapped.");
static_assert(std::is_default_constructible_v<SmokeOwnedMmap>,
              "The empty region claims nothing, so it stays reachable.");

// The brand is a type and costs no byte.  Two brands of one tag are two
// types, and neither converts to the other, so a mapping of one instance
// cannot be handed where a mapping of another is asked for.
struct DummyBrand {};
struct OtherDummyBrand {};
using BrandedSmoke = OwnedMmap<DummyTag, mmap::prot::ReadOnly, mmap::share::Private, DummyBrand>;
using OtherBrandedSmoke = OwnedMmap<DummyTag, mmap::prot::ReadOnly, mmap::share::Private, OtherDummyBrand>;
static_assert(sizeof(BrandedSmoke) == sizeof(SmokeOwnedMmap), "a brand adds no byte to a mapping");
static_assert(std::is_same_v<SmokeOwnedMmap::brand_type, ::foundation::brand::DefaultBrand>);
static_assert(std::is_same_v<BrandedSmoke::brand_type, DummyBrand>);
static_assert(!std::is_constructible_v<BrandedSmoke, OtherBrandedSmoke&&>,
              "a mapping of one brand must not become a mapping of another");
static_assert(!std::is_constructible_v<BrandedSmoke, SmokeOwnedMmap&&>, "an erased mapping must not acquire a brand");

// The leak witness admits the atom and nothing else.  A tag type, a
// pointer and an unrelated empty struct each fail the concept, which is
// what keeps release() from being reachable by accident.
struct NotAnAtom final {};
using SampleLeak = atom::leak::resource<atom::detail::leak_sample_rationale>;

template <typename Witness>
concept can_release = requires(SmokeOwnedMmap m, Witness w) { std::move(m).release(w); };

static_assert(can_release<SampleLeak>);
static_assert(!can_release<NotAnAtom>);
static_assert(!can_release<DummyTag>);
static_assert(!can_release<void*>);
static_assert(!can_release<int>);

// The bits of each tag, pinned.
static_assert(mmap::prot_bits_of(^^mmap::prot::ReadOnly) == PROT_READ);
static_assert(mmap::prot_bits_of(^^mmap::prot::WriteCopy) == (PROT_READ | PROT_WRITE));
static_assert(mmap::prot_bits_of(^^mmap::prot::ReadWrite) == (PROT_READ | PROT_WRITE));
static_assert(mmap::prot_bits_of(^^mmap::prot::Exec) == (PROT_READ | PROT_EXEC));
static_assert((mmap::prot_bits_of(^^mmap::prot::Exec) & PROT_WRITE) == 0,
              "W^X: prot::Exec must NOT include PROT_WRITE");
static_assert(mmap::share_flags_of(^^mmap::share::Private) == MAP_PRIVATE);
static_assert(mmap::share_flags_of(^^mmap::share::Shared) == MAP_SHARED);
static_assert(mmap::share_flags_of(^^mmap::share::Anonymous) == (MAP_PRIVATE | MAP_ANONYMOUS),
              "an anonymous region is private: the kernel keeps a NUMA binding only for a private range");

// The gate, one clause at a time.
using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>>;
using IoOnlyCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::IO>>;
using Jit = ::fixy::atom::mmap::trusted_jit;

static_assert(mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::ReadOnly, mmap::share::Private>);
static_assert(mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::WriteCopy, mmap::share::Anonymous, mmap::share::Locked,
                                      mmap::share::Populate>);
static_assert(!mmap::CtxFitsRegionMint<IoOnlyCtx, mmap::prot::ReadOnly, mmap::share::Private>,
              "a context without Block must not map: the call can park on page-cache pressure.");
static_assert(!mmap::CtxFitsRegionMint<::foundation::effects::ExecCtx<>, mmap::prot::ReadOnly, mmap::share::Private>);
static_assert(!mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::Exec, mmap::share::Private>,
              "an executable page needs the trusted_jit atom.");
static_assert(mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::Exec, mmap::share::Private, Jit>);
static_assert(!mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::ReadOnly, mmap::share::Anonymous, mmap::share::Shared>,
              "a primary share mode is part of the type and is not a modifier: a shared range must not pass for an "
              "anonymous private one.");
static_assert(!mmap::CtxFitsRegionMint<IoBlockCtx, mmap::prot::ReadOnly, mmap::share::Private, mmap::share::Locked,
                                       mmap::share::Locked>,
              "a repeated modifier names nothing new.");
static_assert(!mmap::CtxFitsRegionMint<IoBlockCtx, int, mmap::share::Private>, "a prot tag with no bits is refused.");

// A tag with no row is refused, and so is a tag with a cv-qualifier,
// which is another type.  An alias of a tag is the tag.
struct NotAProt final {};
using ReadOnlyAlias = mmap::prot::ReadOnly;
static_assert(!mmap::MappedProt<NotAProt> && !mmap::MappedProt<const mmap::prot::ReadOnly>);
static_assert(mmap::MappedProt<ReadOnlyAlias>);
static_assert(mmap::PrimaryShare<mmap::share::Anonymous> && !mmap::PrimaryShare<mmap::share::Locked>);
static_assert(mmap::RegionModifier<mmap::share::Locked> && !mmap::RegionModifier<mmap::share::Private>);

// The W^X rule reads the bits of each row: no row writes and executes,
// and each row that executes asks for the licence.
static_assert(mmap::ExecIsLicensed<mmap::prot::ReadWrite> && !mmap::ExecIsLicensed<mmap::prot::Exec>);
static_assert(mmap::ExecIsLicensed<mmap::prot::Exec, Jit> && !mmap::ExecIsLicensed<NotAProt, Jit>);

}  // namespace detail::owned_mmap_self_test

}  // namespace fixy
