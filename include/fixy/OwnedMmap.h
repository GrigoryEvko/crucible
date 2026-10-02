#pragma once

// Exclusive ownership of one mmap'd region, unmapped on destruction.
//
// The empty sentinel is MAP_FAILED and not nullptr, because ::mmap
// returns MAP_FAILED on failure.  The stored length is exactly the length
// given to ::mmap, because ::munmap is called with it verbatim.
//
// Prot and Share are claims about the region, and the type makes them
// true.  The one door that maps, mint_region, calculates the PROT_* word
// from Prot and the MAP_* word from Share at compile time.  No caller
// gives it a bit.  So a region typed prot::ReadOnly holds a page that the
// kernel mapped read-only, and a region typed share::Anonymous holds a
// private anonymous range.  fixy::numa::mint_numa_placement and the W^X
// rule of prot::Exec each read those claims off the type.
//
// Tag gives each region its own type, so two unrelated mappings cannot be
// swapped at a call boundary.  Brand names one instance of the tag, and
// foundation/Brand.h states the rules of a brand.  mint_region takes the
// exclusive permission of the tag and the brand, so a mapping has the
// identity of that permission.  fixy::mmap::advise_release_aware, the one
// door that discards pages, admits only a permission of that identity.
//
// The constructor over an address is private, and mint_region is its only
// caller.  mint_region performs the ::mmap itself, so a region exists only
// over an address that the kernel returned.  Its gate asks for a context
// that owns IO and Block, because a mapping can park the caller.  The
// atom-pack mints of fixy/os/Mmap.h build their regions through this door.
//
// The address a mapping lands at is randomized by the kernel and is
// deliberately not reproducible.  No replay path may observe it.

#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Region.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <sys/mman.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

// A libc header older than the flag does not declare it.
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << 26)
#endif

namespace fixy::mmap {

// The PROT_* word of each protection tag and the MAP_* bits of each share
// tag.  A tag reaches mmap only through a row of these tables, and
// fixy/os/AtomPack.h says why a table is closed: a class template map
// takes a specialization for a class of the caller, and that class can
// then bring PROT_WRITE | PROT_EXEC to the door.  No row is PROT_NONE or a
// zero share word, so a tag with no row is refused rather than mapped as
// a page that nobody may touch.
inline constexpr ::fixy::atom_pack::tag_row<int> prot_table[] = {
    {^^prot::ReadOnly, PROT_READ},
    {^^prot::WriteCopy, PROT_READ | PROT_WRITE},
    {^^prot::ReadWrite, PROT_READ | PROT_WRITE},
    {^^prot::Exec, PROT_READ | PROT_EXEC},
};

inline constexpr ::fixy::atom_pack::tag_row<int> share_table[] = {
    {^^share::Private, MAP_PRIVATE},
    {^^share::Shared, MAP_SHARED},
    {^^share::Anonymous, MAP_PRIVATE | MAP_ANONYMOUS},
    {^^share::Locked, MAP_LOCKED},
    {^^share::Populate, MAP_POPULATE},
    {^^share::HugeTLB, MAP_HUGETLB | MAP_HUGE_2MB},
};

// A mapping has exactly one primary share mode.  The other share tags are
// flags that stack on a primary.
inline constexpr std::meta::info primary_share_tags[] = {^^share::Private, ^^share::Shared, ^^share::Anonymous};

// A tag is known when its table has a row.  The walk at the foot of
// fixy/os/Mmap.h reads fixy::mmap::prot and fixy::mmap::share and fails if
// a declared tag has no row.
template <typename Prot>
concept MappedProt = ::fixy::atom_pack::has_row(prot_table, ^^Prot);

template <typename Share>
concept MappedShare = ::fixy::atom_pack::has_row(share_table, ^^Share);

// The bits of a tag that has a row.  Each lookup is a function and not a
// variable template, because a caller can specialize a variable template
// for one tag and give it bits of its own.  A function over a reflection
// takes no specialization.  A call for a tag with no row is not a
// constant expression, so it fails the build.
[[nodiscard]] consteval int prot_bits_of(std::meta::info prot_tag) noexcept {
    return ::fixy::atom_pack::value_for(prot_table, prot_tag);
}

[[nodiscard]] consteval int share_flags_of(std::meta::info share_tag) noexcept {
    return ::fixy::atom_pack::value_for(share_table, share_tag);
}

template <typename Share>
concept PrimaryShare = MappedShare<Share> && ::fixy::atom_pack::names_tag(primary_share_tags, ^^Share);

// A modifier of a region is a share flag that stacks on the primary, or
// the trusted_jit atom, which licenses an executable page.
template <typename Modifier>
concept RegionModifier =
    (MappedShare<Modifier> && !PrimaryShare<Modifier>) || std::is_same_v<Modifier, ::fixy::atom::mmap::trusted_jit>;

namespace detail {

// The MAP_* bits of a modifier.  The licence adds no bit.
template <typename Modifier>
[[nodiscard]] consteval int modifier_flags() noexcept {
    if constexpr (std::is_same_v<Modifier, ::fixy::atom::mmap::trusted_jit>) {
        return 0;
    } else {
        return share_flags_of(^^Modifier);
    }
}

// The atom that states the modifier, so the row of the mapping is read
// off the atoms as the atom-pack mints read it.
template <typename Modifier>
struct modifier_atom {
    using type = ::fixy::atom::mmap::with_share<Modifier>;
};
template <>
struct modifier_atom<::fixy::atom::mmap::trusted_jit> {
    using type = ::fixy::atom::mmap::trusted_jit;
};

template <typename Prot, typename Share, typename... Modifiers>
using mapping_row_t =
    ::fixy::atom_pack::atoms_row_t<::fixy::atom::mmap::with_prot<Prot>, ::fixy::atom::mmap::with_share<Share>,
                                   typename modifier_atom<Modifiers>::type...>;

}  // namespace detail

}  // namespace fixy::mmap

namespace fixy {

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
template <typename Prot, typename Share>
struct owned_mmap;
}  // namespace row_discipline

namespace mmap {

// The gate of mint_region.  Each clause is its own concept, so a refusal
// names the clause that failed.
//
// A mapping can park the caller on page-cache pressure, on a NUMA-remote
// page fault and on write-back, so the atoms of the mapping lift to IO and
// Block, and the context must admit that row.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxAdmitsMapping = ::foundation::effects::IsExecCtx<Ctx>
                        && ::foundation::effects::CtxAdmits<Ctx, detail::mapping_row_t<Prot, Share, Modifiers...>>;

// W^X, read off the bits.  A protection that writes and executes is
// refused under every licence, and a protection that executes needs the
// trusted_jit atom, which states that the caller audited the bytes that
// will run.  The rule reads the PROT_* word and not the tag, so it holds
// for each row of the table.
template <typename Prot, typename... Modifiers>
concept ExecIsLicensed =
    MappedProt<Prot> && ((prot_bits_of(^^Prot) & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC))
    && ((prot_bits_of(^^Prot) & PROT_EXEC) == 0 || (std::is_same_v<Modifiers, ::fixy::atom::mmap::trusted_jit> || ...));

// Two copies of one modifier fold to the same word, and a repeat names
// nothing new, so the gate refuses it.
template <typename... Modifiers>
concept ModifiersAreDistinct = !(::fixy::atom_pack::OccursMoreThanOnce<Modifiers, Modifiers...> || ...);

template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsRegionMint =
    MappedProt<Prot> && PrimaryShare<Share> && (RegionModifier<Modifiers> && ...) && ModifiersAreDistinct<Modifiers...>
    && ExecIsLicensed<Prot, Modifiers...> && CtxAdmitsMapping<Ctx, Prot, Share, Modifiers...>;

// The gate of OwnedMmap::view.  The view starts the lifetime of its
// elements over the bytes of the mapping, so each subobject of an element
// is an implicit-lifetime type.  A page is aligned to at least 4096 bytes,
// so an element of a larger alignment is refused.  A view that writes
// needs a protection that writes.
template <typename Prot, typename T>
concept MappingViewElement = MappedProt<Prot> && ::foundation::core::ViewElement<T>
                          && ::foundation::lifetime::ImplicitLifetimeThroughout<std::remove_const_t<T>>
                          && (alignof(T) <= 4096) && (std::is_const_v<T> || (prot_bits_of(^^Prot) & PROT_WRITE) != 0);

}  // namespace mmap

template <typename Tag, typename Prot, typename Share, typename Brand = ::foundation::brand::DefaultBrand>
class [[nodiscard]] OwnedMmap {
    static_assert(::foundation::brand::IsBrand<Brand>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Brand must be an empty class type: the brand of the "
                  "permission the mapping was minted with, or DefaultBrand.");
    static_assert(mmap::MappedProt<Prot>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Prot must be a tag of fixy::mmap::prot.  The type is the "
                  "claim of the protection, so a tag with no PROT_* bits claims nothing.");
    static_assert(mmap::PrimaryShare<Share>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Share must be a primary share tag of fixy::mmap::share: "
                  "Private, Shared or Anonymous.  The type is the claim of the share mode.");

    void* addr_ = MAP_FAILED;
    std::size_t len_ = 0;

    // Private, and mint_region is its only caller.  A public one lets any
    // address be claimed, and the destructor unmaps whatever it was
    // claimed over.
    explicit OwnedMmap(void* address, std::size_t length) noexcept : addr_{address}, len_{length} {}

public:
    using tag_type = Tag;
    using prot_type = Prot;
    using share_type = Share;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::owned_mmap<Prot, Share>;
    using row_payload = ::foundation::diag::row_payloads<>;

    // The empty region owns nothing, releases nothing, and claims
    // nothing, so it stays public.
    OwnedMmap() noexcept = default;

    // The one door.  It maps with the bits of Prot, Share and the
    // modifiers, and it builds a region only from an address the kernel
    // returned.  On failure it returns the errno and builds no region.
    //
    // The permission is read and not consumed.  The caller keeps it, and
    // presents it again to fixy::mmap::advise_release_aware.
    //
    // §XXI carve-out: cx=alloc — mapping is a kernel side effect.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...>
    [[nodiscard]] static std::expected<OwnedMmap, std::error_code>
    mint_region(Ctx const&, ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/, int fd,
                std::size_t length, ::off_t offset) noexcept {
        constexpr int protection = mmap::prot_bits_of(^^Prot);
        constexpr int flags = (mmap::share_flags_of(^^Share) | ... | mmap::detail::modifier_flags<Modifiers>());
        void* const address =
            ::mmap(nullptr, length, protection, flags, fd,
                   offset);  // SYSCALL-CAP-OK: OwnedMmap::mint_region ctx-gate (CtxFitsRegionMint, IO+Block)
        if (address == MAP_FAILED) {
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        return OwnedMmap{address, length};
    }

    OwnedMmap(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");

    OwnedMmap(OwnedMmap&& other) noexcept
        : addr_{std::exchange(other.addr_, MAP_FAILED)}, len_{std::exchange(other.len_, 0)} {}

    OwnedMmap& operator=(OwnedMmap&& other) noexcept {
        if (this != &other) {
            release_();
            addr_ = std::exchange(other.addr_, MAP_FAILED);
            len_ = std::exchange(other.len_, 0);
        }
        return *this;
    }

    ~OwnedMmap() noexcept { release_(); }

    // A borrow.  Ownership stays here, so the caller must not unmap
    // the returned pointer.
    [[nodiscard]] void* data() const noexcept { return addr_; }
    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool is_mapped() const noexcept { return addr_ != MAP_FAILED && addr_ != nullptr; }

    // The bytes of the mapping as a View of T, with the count from the
    // length of the mapping.  The call starts the lifetime of the elements
    // over the bytes that the mapping holds, so each element has the value
    // that its bytes give.  The length is a whole number of elements.  An
    // empty region gives an empty View.
    //
    // The View borrows the mapping and must not outlive it.  The overloads
    // for an rvalue are deleted, so no View of a temporary mapping exists.
    // A const mapping gives only a View of const elements.
    template <typename T>
        requires mmap::MappingViewElement<Prot, T>
    [[nodiscard]] ::foundation::core::View<T> view() & noexcept {
        return view_of_<T>();
    }

    template <typename T>
        requires mmap::MappingViewElement<Prot, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::View<T> view() const& noexcept {
        return view_of_<T>();
    }

    template <typename T>
    void view() && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T>
    void view() const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");

    // Hands the region to something that will unmap it on its own
    // schedule, such as a subsystem the kernel takes over.  Almost
    // every transfer of ownership is a move instead.
    //
    // Two gates keep this from being reached by accident.  The witness
    // parameter has to be a leak atom, which rejects an unrelated
    // argument at overload resolution.  And the method binds only to an
    // rvalue, so releasing twice needs a second explicit move; together
    // with the sentinel swap on the way out, a double unmap cannot be
    // written.
    //
    // The witness is taken by value and is empty, so the rationale
    // lives in the type and costs nothing at run time.
    template <atom::IsLeakAtom LeakAtom>
    [[nodiscard]] std::pair<void*, std::size_t> release(LeakAtom) && noexcept {
        return {std::exchange(addr_, MAP_FAILED), std::exchange(len_, 0)};
    }

private:
    template <typename T>
    [[nodiscard]] ::foundation::core::View<T> view_of_() const noexcept {
        if (!is_mapped()) return ::foundation::core::View<T>{};
        CRUCIBLE_PRE(len_ % sizeof(T) == 0);
        std::size_t const count = len_ / sizeof(T);
        T* const first = ::foundation::lifetime::start_as_array<std::remove_const_t<T>>(addr_, count).data();
        return ::foundation::core::detail::view_over_<T>(first, count);
    }

    void release_() noexcept {
        if (is_mapped()) {
            ::munmap(addr_, len_);
            addr_ = MAP_FAILED;
            len_ = 0;
        }
    }
};

namespace detail {

// A sample mapping type.  The check file of this header and
// test/fixy/row_hash_census.h read it.
struct DummyTag {
    using permission_row = ::foundation::effects::Row<>;
};
using SmokeOwnedMmap = OwnedMmap<DummyTag, mmap::prot::ReadOnly, mmap::share::Private>;

}  // namespace detail

}  // namespace fixy
