#pragma once

// Exclusive ownership of one mmap'd region, unmapped on destruction.
//
// The empty sentinel is nullptr.  The map door gives a failed map as an
// error and no address, and a map with no fixed address is never at address
// zero.  The stored length is exactly the length given to the map, because
// the unmap is called with it verbatim.
//
// The door file src/fixy/os/Mmap.cpp holds the map, the unmap and the
// advice of a region.  It issues them with the raw primitive of
// foundation/os/Syscall.h, so this header includes no system header.
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
// The constructor over an address is private, and the map doors are its
// only callers.  Each door performs the ::mmap itself, so a region exists
// only over an address that the kernel returned.  Its gate asks for a
// context that owns IO and Block, because a mapping can park the caller.
// The atom-pack mints of fixy/os/Mmap.h build their regions through these
// doors.
//
// Source of a map
//   mint_region has two forms.  A region of the share Anonymous maps no
//   file and takes no descriptor.  A region of a file takes the borrow of
//   the Handle of the file, which holds the right map, and also read and
//   write for a shared map that writes (file_map_rights).  The open mode of
//   the file gives those rights, so a map of a file that the open cannot
//   read does not compile.  map_foreign_region maps a descriptor that a C
//   library opened, such as the map of a BPF object.  It takes the int of
//   the C library and refuses a negative one with ebadf.  Each call of the
//   product code is inside an opt-out region of the class ABI (the ABI
//   descriptor of fixy/os/Handle.h).
//
// The address a mapping lands at is randomized by the kernel and is
// deliberately not reproducible.  No replay path may observe it.

#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Handle.h>
#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// The owner of a mapping, below.  The shared words of fixy::mmap name it as
// the friend that builds them.
template <typename Tag, typename Prot, typename Share, typename Brand>
class OwnedMmap;

}  // namespace fixy

namespace fixy::mmap {

// The PROT_* word of each protection tag and the MAP_* bits of each share
// tag, with the values of foundation/os/MapWords.h.  A tag reaches mmap
// only through a row of these tables, and fixy/os/AtomPack.h says why a
// table is closed: a class template map takes a specialization for a class
// of the caller, and that class can then bring PROT_WRITE | PROT_EXEC to
// the door.  No row is PROT_NONE or a zero share word, so a tag with no row
// is refused rather than mapped as a page that nobody may touch.
inline constexpr ::fixy::atom_pack::tag_row<int> prot_table[] = {
    {^^prot::ReadOnly, ::foundation::os::kProtectRead},
    {^^prot::WriteCopy, ::foundation::os::kProtectRead | ::foundation::os::kProtectWrite},
    {^^prot::ReadWrite, ::foundation::os::kProtectRead | ::foundation::os::kProtectWrite},
    {^^prot::Exec, ::foundation::os::kProtectRead | ::foundation::os::kProtectExecute},
};

inline constexpr ::fixy::atom_pack::tag_row<int> share_table[] = {
    {^^share::Private, ::foundation::os::kMapPrivate},
    {^^share::Shared, ::foundation::os::kMapShared},
    {^^share::Anonymous, ::foundation::os::kMapPrivate | ::foundation::os::kMapAnonymous},
    {^^share::Locked, ::foundation::os::kMapLocked},
    {^^share::Populate, ::foundation::os::kMapPopulate},
    {^^share::HugeTLB, ::foundation::os::kMapHugeTlb | ::foundation::os::kMapHuge2Mb},
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

// The calls of the door file src/fixy/os/Mmap.cpp.  Each call gives the
// error of the kernel in its Result.

// Maps length bytes with the PROT_* word protection and the MAP_* word
// flags, from offset of the file of descriptor, or memory with no file for a
// descriptor of -1.  The kernel picks the address, and the result is the
// address of the map.
[[nodiscard]] ::foundation::core::Result<void*, ::foundation::os::Errno>
map_region_(std::size_t length, int protection, int flags, int descriptor, std::int64_t offset) noexcept;

// Unmaps the length bytes at address, which a call of map_region_ gave.  The
// kernel refuses an unmap only for an address or a length that no map
// gave, so the door reports no error.
void unmap_region_(void* address, std::size_t length) noexcept;

// Gives the kernel the advice, a MADV_* word, for the length bytes at
// address.  A null address, the address of a region that is not mapped, gives
// EINVAL and makes no system call.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
advise_region_(void* address, std::size_t length, int advice) noexcept;

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

namespace fixy::detail::owner {

// The fields of an OwnedMmap: the address and the length of the mapping, or
// null and zero when it holds no mapping.  The state is a member of the home
// of the owner states of fixy, which the roster of foundation/core/Owner.h
// names, so the probe of the owners holds it in its roster.  The unmap takes
// the length that the map took.
struct MappingState {
    using owner_state_mark = ::foundation::core::OwnerStateMark;

    void* address = nullptr;
    std::size_t length = 0;

    [[nodiscard]] static constexpr MappingState emptied(MappingState const& /*state*/) noexcept {
        return MappingState{};
    }

    // Unmaps the mapping.  The empty state unmaps nothing.
    static constexpr void release(MappingState const& state) noexcept {
        if (state.address != nullptr) {
            ::fixy::mmap::detail::unmap_region_(state.address, state.length);
        }
    }
};

}  // namespace fixy::detail::owner

namespace fixy {

// The home of the owner states of fixy, for an OwnerHomes list of the probe
// of the owners.
inline constexpr std::meta::info owner_state_home = ^^detail::owner;

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
concept ExecIsLicensed = MappedProt<Prot>
                      && ((prot_bits_of(^^Prot) & (::foundation::os::kProtectWrite | ::foundation::os::kProtectExecute))
                          != (::foundation::os::kProtectWrite | ::foundation::os::kProtectExecute))
                      && ((prot_bits_of(^^Prot) & ::foundation::os::kProtectExecute) == 0
                          || (std::is_same_v<Modifiers, ::fixy::atom::mmap::trusted_jit> || ...));

// Two copies of one modifier fold to the same word, and a repeat names
// nothing new, so the gate refuses it.
template <typename... Modifiers>
concept ModifiersAreDistinct = !(::fixy::atom_pack::OccursMoreThanOnce<Modifiers, Modifiers...> || ...);

template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsRegionMint =
    MappedProt<Prot> && PrimaryShare<Share> && (RegionModifier<Modifiers> && ...) && ModifiersAreDistinct<Modifiers...>
    && ExecIsLicensed<Prot, Modifiers...> && CtxAdmitsMapping<Ctx, Prot, Share, Modifiers...>;

// The rights that a map of a file needs from the handle of the file: map,
// and also read and write for a shared map that writes, because its stores
// reach the file (the source of a map).
template <typename Prot, typename Share>
    requires MappedProt<Prot>
[[nodiscard]] consteval ::fixy::os::RightSet file_map_rights() noexcept {
    bool const is_shared_write =
        std::is_same_v<Share, share::Shared> && (prot_bits_of(^^Prot) & ::foundation::os::kProtectWrite) != 0;
    return is_shared_write ? ::fixy::os::RightSet::map | ::fixy::os::RightSet::read | ::fixy::os::RightSet::write
                           : ::fixy::os::RightSet::map;
}

// The gate of the map of no file: a region of the share Anonymous.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsAnonRegionMint =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && std::is_same_v<Share, share::Anonymous>;

// The gate of the map of a file through a handle with the rights Held.
template <typename Ctx, typename Prot, typename Share, ::fixy::os::RightSet Held, typename... Modifiers>
concept CtxFitsFileRegionMint =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && !std::is_same_v<Share, share::Anonymous>
    && ::fixy::os::HoldsRights<Held, file_map_rights<Prot, Share>()>;

// The gate of the map of a descriptor that a C library opened.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsForeignRegionMap =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && !std::is_same_v<Share, share::Anonymous>;

// The gate of OwnedMmap::view.  The view starts the lifetime of its
// elements over the bytes of the mapping, so each subobject of an element
// is an implicit-lifetime type.  A page is aligned to at least 4096 bytes,
// so an element of a larger alignment is refused.  A view that writes
// needs a protection that writes.
template <typename Prot, typename T>
concept MappingViewElement =
    MappedProt<Prot> && ::foundation::core::ViewElement<T>
    && ::foundation::lifetime::ImplicitLifetimeThroughout<std::remove_const_t<T>> && (alignof(T) <= 4096)
    && (std::is_const_v<T> || (prot_bits_of(^^Prot) & ::foundation::os::kProtectWrite) != 0);

// A word that another writer of a shared mapping stores and a reader loads
// whole: an unsigned integer of at most eight bytes whose atomic load needs
// no lock.
template <typename T>
concept SharedWord = ::foundation::core::UnsignedInteger<T> && (sizeof(T) <= sizeof(std::uint64_t))
                  && __atomic_always_lock_free(sizeof(T), 0);

// The byte offset of the first word of a run in a mapping.
using MappingOffset = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

// Shared words
//   Count words of a shared mapping that another writer stores: the kernel,
//   a BPF program or another process.  Each read is an acquire load, so the
//   stores that the writer made before its store of a word are visible after
//   the load that reads that word.  A View of the words would read them with
//   plain loads, which the compiler can tear, merge or move.
//
//   The words borrow the mapping, as a View of view_at does.  The one door is
//   OwnedMmap::shared_words_at, which checks the fit and the alignment and
//   starts the lifetime of the words, so no pointer of a caller becomes a run
//   of shared words.
template <SharedWord T, std::size_t Count>
    requires(Count != ::foundation::core::dynamic_extent) && ::foundation::core::IndexCount<Count>
class SharedWords final {
    T const* first_ = nullptr;
    // The seal refuses a byte copy into a SharedWords, so no bytes become a
    // run of shared words.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <typename, typename, typename, typename>
    friend class ::fixy::OwnedMmap;

    constexpr explicit SharedWords(T const* first) noexcept : first_{first} {}

public:
    // The count of the words.
    [[nodiscard]] static constexpr std::size_t size() noexcept { return Count; }

    // Each index of a word, in order.
    [[nodiscard]] static constexpr ::foundation::core::IndexRange<Count> indices() noexcept {
        return ::foundation::core::IndexRange<Count>{};
    }

    // The word at index, with an acquire load.  The index proves the bound.
    [[nodiscard]] T load_acquire(::foundation::core::Index<Count> index) const noexcept {
        return __atomic_load_n(first_ + index.raw(), __ATOMIC_ACQUIRE);
    }
};

}  // namespace mmap

// The core of foundation/core/Owner.h gives the moves and the end of a
// mapping.  A move assignment reads the source and empties it before it
// unmaps the old mapping of the target, so a source inside that mapping is
// read before the unmap.
template <typename Tag, typename Prot, typename Share, typename Brand = ::foundation::brand::DefaultBrand>
class [[nodiscard]] OwnedMmap final
    : public ::foundation::core::OwnerCore<::fixy::detail::owner::MappingState, OwnedMmap<Tag, Prot, Share, Brand>> {
    using State = ::fixy::detail::owner::MappingState;
    using Core = ::foundation::core::OwnerCore<State, OwnedMmap>;

    static_assert(::foundation::brand::IsBrand<Brand>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Brand must be an empty class type: the brand of the "
                  "permission the mapping was minted with, or DefaultBrand.");
    static_assert(mmap::MappedProt<Prot>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Prot must be a tag of fixy::mmap::prot.  The type is the "
                  "claim of the protection, so a tag with no PROT_* bits claims nothing.");
    static_assert(mmap::PrimaryShare<Share>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Share must be a primary share tag of fixy::mmap::share: "
                  "Private, Shared or Anonymous.  The type is the claim of the share mode.");

    // Private, and mint_region is its only caller.  A public one lets any
    // address be claimed, and the destructor unmaps whatever it was
    // claimed over.
    explicit OwnedMmap(void* address, std::size_t length) noexcept : Core{State{address, length}} {}

    // The map of the doors below.  It maps with the bits of Prot, Share and
    // the modifiers, and it builds a region only from an address the kernel
    // returned.  On failure it returns the error of the kernel and builds no
    // region.  The map reads the file of descriptor from the byte at offset,
    // and an anonymous map takes the descriptor -1 and the offset 0.  The
    // body deduces the return type Result<OwnedMmap, Errno>, for the reason
    // that the doors below give.
    template <typename... Modifiers>
    [[nodiscard]] static auto map_(int descriptor, std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        using Mapped = ::foundation::core::Result<OwnedMmap, ::foundation::os::Errno>;
        constexpr int protection = mmap::prot_bits_of(^^Prot);
        constexpr int flags = (mmap::share_flags_of(^^Share) | ... | mmap::detail::modifier_flags<Modifiers>());
        ::foundation::core::Result<void*, ::foundation::os::Errno> mapped =
            mmap::detail::map_region_(length, protection, flags, descriptor, offset.raw());
        for (::foundation::os::Errno const error : mapped.err()) {
            return Mapped{::foundation::core::err(error)};
        }
        return Mapped{OwnedMmap{std::move(mapped).expect("the map door gave the address of the map"), length}};
    }

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

    // The map of no file: a region of the share Anonymous (the source of a
    // map).
    //
    // The permission is read and not consumed.  The caller keeps it, and
    // presents it again to fixy::mmap::advise_release_aware.
    //
    // The return type is Result<OwnedMmap, Errno>, and the body deduces it.
    // A declaration of a member is part of the class, so a Result of the
    // class in the declaration names a class that is not yet complete.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsAnonRegionMint<Ctx, Prot, Share, Modifiers...>
    [[nodiscard]] static auto mint_region(Ctx const&,
                                          ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                                          std::size_t length) noexcept {
        return map_<Modifiers...>(-1, length, ::fixy::fs::FileOffset{0});
    }

    // The map of the file of a handle from the byte at offset.  The handle
    // holds the rights that file_map_rights names (the source of a map).
    // The borrow of an empty handle gives ebadf.  The permission is read as
    // in the anonymous form.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx, ::fixy::os::RightSet Rights>
        requires mmap::CtxFitsFileRegionMint<Ctx, Prot, Share, Rights, Modifiers...>
    [[nodiscard]] static auto mint_region(Ctx const&,
                                          ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                                          ::fixy::os::HandleRef<::fixy::os::kind::File, Rights> file,
                                          std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        return map_<Modifiers...>(::fixy::os::detail::HandleDoor::descriptor_of(file), length, offset);
    }

    // The map of a descriptor that a C library opened, such as the map of a
    // BPF object, from the byte at offset (the source of a map).  The
    // descriptor is the int of the C library, and a negative descriptor
    // gives ebadf with no call to the kernel.  The permission is read as in
    // the anonymous form.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsForeignRegionMap<Ctx, Prot, Share, Modifiers...>
    [[nodiscard]] static auto
    map_foreign_region(Ctx const&, ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                       int abi_descriptor, std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        using Mapped = ::foundation::core::Result<OwnedMmap, ::foundation::os::Errno>;
        if (abi_descriptor < 0) {
            return Mapped{::foundation::core::err(::foundation::os::Errno::ebadf)};
        }
        return map_<Modifiers...>(abi_descriptor, length, offset);
    }

    OwnedMmap(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");

    // The assignment unmaps the old mapping of this owner after it takes the
    // mapping of other.
    OwnedMmap(OwnedMmap&&) noexcept = default;
    OwnedMmap& operator=(OwnedMmap&&) noexcept = default;

    // A borrow.  Ownership stays here, so the caller must not unmap
    // the returned pointer.
    [[nodiscard]] void* data() const noexcept { return this->state_.address; }
    [[nodiscard]] std::size_t size() const noexcept { return this->state_.length; }
    [[nodiscard]] bool is_mapped() const noexcept { return this->state_.address != nullptr; }

    // The bytes of the mapping as a View of T, with the count from the
    // length of the mapping, or no value when the length is not a whole
    // number of elements.  The call starts the lifetime of the elements
    // over the bytes that the mapping holds, so each element has the value
    // that its bytes give.  An empty region gives an empty View.
    //
    // The View borrows the mapping and must not outlive it.  The overloads
    // for an rvalue are deleted, so no View of a temporary mapping exists.
    // A const mapping gives only a View of const elements.
    template <typename T>
        requires mmap::MappingViewElement<Prot, T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view() & noexcept {
        return view_of_<T>();
    }

    template <typename T>
        requires mmap::MappingViewElement<Prot, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view() const& noexcept {
        return view_of_<T>();
    }

    template <typename T>
    void view() && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T>
    void view() const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");

    // The count elements of T from the byte offset of the mapping, as a
    // View, or no value when they do not fit in the mapping or when the
    // offset is not a multiple of the alignment of T.  A mapping starts on a
    // page, and the gate refuses an alignment above 4096, so an aligned
    // offset gives an aligned address.  The call starts the lifetime of the
    // count elements over their bytes, and of no other byte, so a mapping can
    // hold a header and then an array of a different type.  A count of zero
    // at an offset inside the mapping gives an empty View.
    //
    // The View borrows the mapping, as the View of view() does.  The checks
    // are two compares and one test of the low bits of the offset.
    template <typename T>
        requires mmap::MappingViewElement<Prot, T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view_at(std::size_t offset,
                                                                                  std::size_t count) & noexcept {
        return view_at_<T>(offset, count);
    }

    template <typename T>
        requires mmap::MappingViewElement<Prot, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view_at(std::size_t offset,
                                                                                  std::size_t count) const& noexcept {
        return view_at_<T>(offset, count);
    }

    // The Count elements of T from the byte offset, as a View of the fixed
    // extent Count, with the checks of view_at(offset, count).
    template <typename T, std::size_t Count>
        requires mmap::MappingViewElement<Prot, T> && (Count != ::foundation::core::dynamic_extent)
              && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T, Count>>
    view_at(std::size_t offset) & noexcept {
        return view_at_<T, Count>(offset);
    }

    template <typename T, std::size_t Count>
        requires mmap::MappingViewElement<Prot, T> && std::is_const_v<T>
              && (Count != ::foundation::core::dynamic_extent) && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T, Count>>
    view_at(std::size_t offset) const& noexcept {
        return view_at_<T, Count>(offset);
    }

    template <typename T>
    void view_at(std::size_t, std::size_t) && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T>
    void view_at(std::size_t,
                 std::size_t) const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T, std::size_t Count>
    void view_at(std::size_t) && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T, std::size_t Count>
    void view_at(std::size_t) const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");

    // The Count words of T from the byte offset of a shared mapping, which
    // another writer stores (the shared words of fixy::mmap), or no value
    // when they do not fit in the mapping or when the offset is not a
    // multiple of the alignment of T.  The checks are those of view_at, and
    // the call starts the lifetime of the Count words over their bytes.  The
    // words borrow the mapping, so the overloads of an rvalue are deleted.
    template <mmap::SharedWord T, std::size_t Count>
        requires std::is_same_v<Share, mmap::share::Shared> && (Count != ::foundation::core::dynamic_extent)
              && ::foundation::core::IndexCount<Count>
    [[nodiscard]] ::foundation::core::Option<mmap::SharedWords<T, Count>>
    shared_words_at(mmap::MappingOffset offset) const& noexcept {
        using Result = ::foundation::core::Option<mmap::SharedWords<T, Count>>;
        if (!fits_at_<T>(offset.raw(), Count)) return Result{::foundation::core::none};
        return Result::some(mmap::SharedWords<T, Count>{start_at_<T const>(offset.raw(), Count)});
    }

    template <typename T, std::size_t Count>
    void shared_words_at(mmap::MappingOffset) && = delete("shared words of a temporary mapping dangle when the "
                                                          "mapping unmaps");
    template <typename T, std::size_t Count>
    void shared_words_at(mmap::MappingOffset) const&& = delete("shared words of a temporary mapping dangle when the "
                                                               "mapping unmaps");

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
    // lives in the type and costs nothing at run time.  The owner is empty
    // when the call returns, and no unmap runs.
    template <atom::IsLeakAtom LeakAtom>
    [[nodiscard]] std::pair<void*, std::size_t> release(LeakAtom) && noexcept {
        State held = this->take_state_();
        return {held.address, held.length};
    }

private:
    template <typename T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view_of_() const noexcept {
        using Result = ::foundation::core::Option<::foundation::core::View<T>>;
        if (!is_mapped()) return Result::some(::foundation::core::View<T>{});
        if (this->state_.length % sizeof(T) != 0) return Result{::foundation::core::none};
        std::size_t const count = this->state_.length / sizeof(T);
        return Result::some(::foundation::core::View<T>{this->run_key_(), start_at_<T>(0, count), count});
    }

    // True when count elements of T from the byte offset lie inside the
    // mapping, and the offset is a multiple of the alignment of T.  The
    // compares take no sum, so no count overflows.
    template <typename T>
    [[nodiscard]] bool fits_at_(std::size_t offset, std::size_t count) const noexcept {
        std::size_t const length = this->state_.length;
        return offset <= length && count <= (length - offset) / sizeof(T) && (offset & (alignof(T) - 1)) == 0;
    }

    // Starts the lifetime of count elements of T at the byte offset, which
    // fits_at_ accepted, and gives the first of them.
    template <typename T>
    [[nodiscard]] T* start_at_(std::size_t offset, std::size_t count) const noexcept {
        unsigned char* const bytes = static_cast<unsigned char*>(this->state_.address) + offset;
        return ::foundation::lifetime::start_as_array<std::remove_const_t<T>>(bytes, count);
    }

    template <typename T>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T>> view_at_(std::size_t offset,
                                                                                   std::size_t count) const noexcept {
        using Result = ::foundation::core::Option<::foundation::core::View<T>>;
        if (!fits_at_<T>(offset, count)) return Result{::foundation::core::none};
        return Result::some(::foundation::core::View<T>{this->run_key_(), start_at_<T>(offset, count), count});
    }

    // A View of a fixed extent holds at least one element, so a fit gives
    // a mapping with an address that is not null.
    template <typename T, std::size_t Count>
    [[nodiscard]] ::foundation::core::Option<::foundation::core::View<T, Count>>
    view_at_(std::size_t offset) const noexcept {
        using Result = ::foundation::core::Option<::foundation::core::View<T, Count>>;
        if (!fits_at_<T>(offset, Count)) return Result{::foundation::core::none};
        return Result::some(::foundation::core::View<T, Count>{this->run_key_(), start_at_<T>(offset, Count)});
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
