#pragma once

// A mapping and the lock of its readers in one object:
// LockedMmap<Tag, Prot, Share, Brand>.
//
// An advice that discards pages makes each read of them give zeros, so it
// must not run while a reader reads the region.  The proof that no reader
// reads is the exclusive of the pool of the region, and a proof of another
// mapping proves nothing.  A brand names a mint site and not one object (fact
// 2 of foundation/Brand.h), so no type tells two mappings of one site apart.
// This object holds the mapping and its pool together, so the advice takes no
// proof from outside: it asks its own pool.
//
//   locked->with_read(ctx, body)    a share of the pool for the time of body,
//                                   which gets the region as const
//   locked->with_write(ctx, body)   the lease of the exclusive for the time of
//                                   body, which gets the region
//   locked->advise_release_aware<Advice>(ctx)
//                                   takes the exclusive, discards the pages,
//                                   and gives the exclusive back.  A share or
//                                   a lease that is out gives ReadersLive
//
// with_read and with_write are the doors of SharedLocked
// (fixy/os/SharedLocked.h), with their gates: the result of a body holds no
// borrow, and a door never waits.  A body gets a LockedRegion, which gives the
// views of the pages and the advices that discard no page.
//
// A mint maps the pages into the object, so no code has the mapping before
// the lock.  A LockedRegion does not move and takes no assignment, so no body
// moves the mapping out, and no body puts a mapping that it already has, with
// the Views that it took of it, into the lock.  So no View of the pages is
// older than the lock.  A body can store a View in an object outside through a
// capture in a type that names no brand, as with each body of SharedLocked.
//
// The mapping stays in the object until its end.  So a door that takes the
// mapping by move has no form here: the NUMA placement
// (fixy/os/NumaPlacement.h) and the release of OwnedMmap.  The two mints map a
// file of a Handle or no file, and not a descriptor of a C library.
//
// The object is pinned, and a map can fail, so a mint gives a Box of the
// object, which is one allocation on a cold door.  A map that fails gives the
// exclusive back (MapRefusal).

#include <fixy/os/Fd.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Handle.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/os/SharedLocked.h>
#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename Tag, typename Prot, typename Share, ::foundation::brand::IsFreshBrand Brand>
class LockedMmap;

}  // namespace fixy

namespace fixy::mmap {

// A share of the pool or the lease of its exclusive is out, so a thread can
// read the pages.
struct ReadersLive final {
    [[nodiscard]] friend constexpr bool operator==(ReadersLive, ReadersLive) noexcept = default;
};

// Why a release-aware advice discarded no page: a reader or a writer is in
// the region, or the kernel refused the advice.
using ReleaseRefusal = ::foundation::core::OneOf<ReadersLive, ::foundation::os::Errno>;

// A map that failed: the error, and the exclusive that the mint took.  The
// caller takes the exclusive back, so a failed map loses no token.
template <typename Tag, typename Brand>
class [[nodiscard]] MapRefusal final {
    ::foundation::os::Errno error_;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag, Brand> exclusive_;

public:
    MapRefusal(::foundation::os::Errno error, ::foundation::permissions::Permission<Tag, Brand>&& exclusive) noexcept
        : error_{error}, exclusive_{std::move(exclusive)} {}

    [[nodiscard]] ::foundation::os::Errno error() const noexcept { return error_; }

    [[nodiscard]] ::foundation::permissions::Permission<Tag, Brand> take_exclusive() && noexcept {
        return std::move(exclusive_);
    }
};

// The gate of the advice that discards pages.  The advice gives the rows of
// the system call, and the pool gives the row of the upgrade (PoolCtx).
template <typename Ctx, typename Advice, typename Tag>
concept CtxFitsReleaseAwareAdvise =
    CtxAdmitsAdvise<Ctx> && DiscardsPages<Advice> && ::foundation::permissions::PoolCtx<Tag, Ctx>;

class LockedMmapDoor;

// The key of the constructor of a LockedMmap and of its region.  The door of
// the two mints is its one friend, so each LockedMmap holds a mapping that its
// mint made.
class [[nodiscard]] LockedMmapKey final {
    friend class LockedMmapDoor;

    constexpr LockedMmapKey() noexcept {}
    constexpr LockedMmapKey(LockedMmapKey const&) noexcept {}

public:
    LockedMmapKey& operator=(LockedMmapKey const&) = delete("a key of a LockedMmap lives for one construction");
};

// The mapping inside a LockedMmap.  It does not move and takes no assignment,
// and it gives no reference to the mapping, so the mapping of a LockedMmap is
// the one that its mint made.  The views have the gates and the checks of the
// views of OwnedMmap: a const region gives only a View of const elements, and a
// mapping of share::Shared gives a SharedView.  Each View borrows the pages and
// lives for the body that gets the region.  The offset of a view is in bytes
// from the start of the mapping.
template <typename Tag, typename Prot, typename Share, ::foundation::brand::IsFreshBrand Brand>
class [[nodiscard]] LockedRegion final : public ::foundation::Pinned<LockedRegion<Tag, Prot, Share, Brand>> {
    using Mapping = OwnedMmap<Tag, Prot, Share, Brand>;

    Mapping mapping_;

    friend class ::fixy::LockedMmap<Tag, Prot, Share, Brand>;

public:
    // Only the mints hold the key.
    explicit LockedRegion(LockedMmapKey const& /*key*/, Mapping&& mapping) noexcept : mapping_{std::move(mapping)} {}

    [[nodiscard]] ::foundation::core::ByteCount size() const noexcept {
        return ::foundation::core::ByteCount{mapping_.size()};
    }

    // An advice that discards no page (fixy::mmap::advise).
    template <typename Advice, ::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsSafeAdvise<Ctx, Advice>
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    advise(Ctx const& ctx) const noexcept {
        return ::fixy::mmap::advise<Advice>(ctx, mapping_);
    }

    template <typename T>
        requires MappingViewOf<Prot, Share, T>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T>> view() & noexcept {
        return mapping_.template view<T>();
    }

    template <typename T>
        requires MappingViewOf<Prot, Share, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T>> view() const& noexcept {
        return mapping_.template view<T>();
    }

    template <typename T>
        requires MappingViewOf<Prot, Share, T>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T>>
    view_at(::foundation::core::ByteCount offset, ::foundation::core::ElementCount count) & noexcept {
        return mapping_.template view_at<T>(offset.raw(), count.raw());
    }

    template <typename T>
        requires MappingViewOf<Prot, Share, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T>>
    view_at(::foundation::core::ByteCount offset, ::foundation::core::ElementCount count) const& noexcept {
        return mapping_.template view_at<T>(offset.raw(), count.raw());
    }

    template <typename T, std::size_t Count>
        requires MappingViewOf<Prot, Share, T> && (Count != ::foundation::core::dynamic_extent)
              && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T, Count>>
    view_at(::foundation::core::ByteCount offset) & noexcept {
        return mapping_.template view_at<T, Count>(offset.raw());
    }

    template <typename T, std::size_t Count>
        requires MappingViewOf<Prot, Share, T> && std::is_const_v<T> && (Count != ::foundation::core::dynamic_extent)
              && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<mapping_view_t<Share, T, Count>>
    view_at(::foundation::core::ByteCount offset) const& noexcept {
        return mapping_.template view_at<T, Count>(offset.raw());
    }
};

}  // namespace fixy::mmap

namespace fixy {

template <typename Tag, typename Prot, typename Share, ::foundation::brand::IsFreshBrand Brand>
class [[nodiscard]] LockedMmap final : private SharedLocked<mmap::LockedRegion<Tag, Prot, Share, Brand>, Tag, Brand> {
    using Lock = SharedLocked<mmap::LockedRegion<Tag, Prot, Share, Brand>, Tag, Brand>;

public:
    using mapping_type = OwnedMmap<Tag, Prot, Share, Brand>;
    using region_type = mmap::LockedRegion<Tag, Prot, Share, Brand>;
    using tag_type = Tag;
    using prot_type = Prot;
    using share_type = Share;
    using brand_type = Brand;

    // Only the mints hold the key.
    LockedMmap(mmap::LockedMmapKey const& key, ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
               mapping_type&& mapping) noexcept
        : Lock{std::move(exclusive), key, std::move(mapping)} {}

    using Lock::with_read;
    using Lock::with_write;

    // Discards the pages of the mapping under the exclusive of its own pool,
    // and gives the exclusive back.  ReadersLive for a share or a lease that
    // is out, and then no call of the kernel.  The Errno of the kernel for an
    // advice that it refused.  Complexity: one compare-exchange and one system
    // call.
    template <typename Advice, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsReleaseAwareAdvise<Ctx, Advice, Tag>
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, mmap::ReleaseRefusal>
    advise_release_aware(Ctx const& ctx) noexcept {
        using Discarded = ::foundation::core::Result<::foundation::core::Unit, mmap::ReleaseRefusal>;
        auto advised = this->with_write(ctx, [](region_type& region) noexcept {
            return mmap::detail::advise_region_(region.mapping_.data(), region.mapping_.size(),
                                                mmap::advice_value_of(^^Advice));
        });
        return std::move(advised).match(
            [](::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> result) noexcept
                -> Discarded {
                return std::move(result).map_err(
                    [](::foundation::os::Errno error) noexcept { return mmap::ReleaseRefusal{error}; });
            },
            []() noexcept -> Discarded { return ::foundation::core::err(mmap::ReleaseRefusal{mmap::ReadersLive{}}); });
    }
};

namespace mmap {

// The object of a mint over the pack of its atoms.
template <typename Tag, typename Brand, typename... Atoms>
using locked_mmap_of_t = LockedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>;

// The result of a mint: the Box of the object, or the refusal of the map.
template <typename Tag, typename Brand, typename... Atoms>
using LockedMint =
    ::foundation::core::Result<::foundation::core::Box<locked_mmap_of_t<Tag, Brand, Atoms...>>, MapRefusal<Tag, Brand>>;

// The gates of the two mints: the gate of the map, and the allocation
// capability of the Box.
template <typename Ctx, typename Marker, typename Kind, ::fixy::os::RightSet Held, typename... Atoms>
concept CtxFitsLockedFileMint =
    CtxFitsFileMmapMint<Ctx, Kind, Held, Atoms...> && ::foundation::core::AllocationCapability<Marker>;

template <typename Ctx, typename Marker, typename... Atoms>
concept CtxFitsLockedAnonMint = CtxFitsAnonMmapMint<Ctx, Atoms...> && ::foundation::core::AllocationCapability<Marker>;

template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Marker, typename Tag,
          ::foundation::brand::IsFreshBrand Brand, class Kind, ::fixy::os::RightSet Rights>
    requires CtxFitsLockedFileMint<Ctx, Marker, Kind, Rights, Atoms...>
[[nodiscard]] auto mint_locked_mmap(Ctx const& ctx, Marker allocation,
                                    ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
                                    ::fixy::os::HandleRef<Kind, Rights> file, ::foundation::core::ByteCount length,
                                    ::fixy::fs::FileOffset offset = ::fixy::fs::FileOffset{0}) noexcept
    -> LockedMint<Tag, Brand, Atoms...>;

template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Marker, typename Tag,
          ::foundation::brand::IsFreshBrand Brand>
    requires CtxFitsLockedAnonMint<Ctx, Marker, Atoms...>
[[nodiscard]] auto mint_locked_mmap_anon(Ctx const& ctx, Marker allocation,
                                         ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
                                         ::foundation::core::ByteCount length) noexcept
    -> LockedMint<Tag, Brand, Atoms...>;

// The door of the two mints to the key of a LockedMmap.  It puts the mapping
// of a map and its exclusive in the object of a Box, or gives the exclusive
// back beside the error of the map.
class LockedMmapDoor final : ::foundation::NoObject<LockedMmapDoor> {
    template <typename Locked, typename Marker, typename Tag, typename Brand>
    [[nodiscard]] static ::foundation::core::Result<::foundation::core::Box<Locked>, MapRefusal<Tag, Brand>>
    lock_(Marker allocation, ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
          ::foundation::core::Result<typename Locked::mapping_type, ::foundation::os::Errno> mapped) noexcept {
        for (::foundation::os::Errno const error : mapped.err())
            return ::foundation::core::err(MapRefusal<Tag, Brand>{error, std::move(exclusive)});
        auto mapping = std::move(mapped).expect("a map with no error gives a mapping");
        return ::foundation::core::mint_box<Locked>(allocation, LockedMmapKey{}, std::move(exclusive),
                                                    std::move(mapping));
    }

    template <typename... FriendAtoms, ::foundation::effects::IsExecCtx FriendCtx, typename FriendMarker,
              typename FriendTag, ::foundation::brand::IsFreshBrand FriendBrand, class FriendKind,
              ::fixy::os::RightSet FriendRights>
        requires CtxFitsLockedFileMint<FriendCtx, FriendMarker, FriendKind, FriendRights, FriendAtoms...>
    friend auto mint_locked_mmap(FriendCtx const&, FriendMarker,
                                 ::foundation::permissions::Permission<FriendTag, FriendBrand>&&,
                                 ::fixy::os::HandleRef<FriendKind, FriendRights>, ::foundation::core::ByteCount,
                                 ::fixy::fs::FileOffset) noexcept -> LockedMint<FriendTag, FriendBrand, FriendAtoms...>;

    template <typename... FriendAtoms, ::foundation::effects::IsExecCtx FriendCtx, typename FriendMarker,
              typename FriendTag, ::foundation::brand::IsFreshBrand FriendBrand>
        requires CtxFitsLockedAnonMint<FriendCtx, FriendMarker, FriendAtoms...>
    friend auto mint_locked_mmap_anon(FriendCtx const&, FriendMarker,
                                      ::foundation::permissions::Permission<FriendTag, FriendBrand>&&,
                                      ::foundation::core::ByteCount) noexcept
        -> LockedMint<FriendTag, FriendBrand, FriendAtoms...>;
};

// The map of a file in a LockedMmap, from the byte at offset.  The pack of
// atoms, the context, the kind of the file and the rights are those of
// mint_mmap (fixy/os/Mmap.h).  The mint reads the exclusive for the map, and
// the pool of the object then holds it.  The object takes one allocation.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Marker, typename Tag,
          ::foundation::brand::IsFreshBrand Brand, class Kind, ::fixy::os::RightSet Rights>
    requires CtxFitsLockedFileMint<Ctx, Marker, Kind, Rights, Atoms...>
[[nodiscard]] auto mint_locked_mmap(Ctx const& ctx, Marker allocation,
                                    ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
                                    ::fixy::os::HandleRef<Kind, Rights> file, ::foundation::core::ByteCount length,
                                    ::fixy::fs::FileOffset offset) noexcept -> LockedMint<Tag, Brand, Atoms...> {
    using Locked = locked_mmap_of_t<Tag, Brand, Atoms...>;
    auto mapped = detail::mint_region_with_<typename Locked::mapping_type>(detail::region_modifiers_t<Atoms...>{}, ctx,
                                                                           exclusive, file, length.raw(), offset);
    return LockedMmapDoor::lock_<Locked>(allocation, std::move(exclusive), std::move(mapped));
}

// The map of no file in a LockedMmap, as mint_mmap_anon maps it.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Marker, typename Tag,
          ::foundation::brand::IsFreshBrand Brand>
    requires CtxFitsLockedAnonMint<Ctx, Marker, Atoms...>
[[nodiscard]] auto mint_locked_mmap_anon(Ctx const& ctx, Marker allocation,
                                         ::foundation::permissions::Permission<Tag, Brand>&& exclusive,
                                         ::foundation::core::ByteCount length) noexcept
    -> LockedMint<Tag, Brand, Atoms...> {
    using Locked = locked_mmap_of_t<Tag, Brand, Atoms...>;
    auto mapped = detail::mint_region_with_<typename Locked::mapping_type>(detail::region_modifiers_t<Atoms...>{}, ctx,
                                                                           exclusive, length.raw());
    return LockedMmapDoor::lock_<Locked>(allocation, std::move(exclusive), std::move(mapped));
}

}  // namespace mmap

}  // namespace fixy
