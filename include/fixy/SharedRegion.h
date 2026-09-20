#pragma once

// Three axes, one surface: a read of a region that a brand, a share and
// an effect row all agree on.
//
// Each axis already refuses something on its own, and each is blind to
// what the other two see.
//
//   The brand names one instance.  Two regions of one tag minted at two
//   sites are two types, so a borrow of the first cannot stand in for
//   the second.  It says nothing about whether anyone may read now.
//
//   The fractional axis names a live share.  A SharedPermissionGuard is
//   outstanding exactly while its share is, and the pool's upgrade
//   cannot take the exclusive back while one is out.  It says nothing
//   about WHICH region the share is of, beyond the tag, and a tag is a
//   kind rather than an instance.
//
//   The effect row names what the caller is allowed to do at all.
//   CtxAdmitsPermission weighs the tag's row against the context's, so
//   a foreground context cannot touch a region whose tag says IO.  It
//   says nothing about instances or shares.
//
// What the composition refuses that no single axis does.  A read that
// is fractionally legitimate and effect-legitimate and still wrong: two
// pools over two regions of one tag, a guard genuinely outstanding on
// the first, a context that genuinely admits the tag's row, and the
// read taken against the second region.  Every axis alone says yes.
// mint_shared_read says no, because the guard and the region are one
// Brand parameter and the two brands do not agree, and the compiler
// reports it as a deduction conflict rather than as a rule.
//
// The other two refusals come out of the same signature.  Without a
// guard there is no read at all, because the guard is a parameter and
// nothing else produces the read.  Under a context that does not admit
// the tag's row the mint is not viable, because CtxAdmitsPermission is
// in its one concept.
//
// What this does not catch, stated rather than implied.
//
//   A read handle that outlives the share.  The handle is a span and a
//   value, and copying it is ordinary.  The guard parameter carries the
//   lifetime annotation and its rvalue twin refuses a guard that dies at
//   the end of the statement, but a handle assigned to a longer-lived
//   name and read after the guard's scope ends is well typed here.  That
//   is the flow-sensitive half, the same half fixy/OwnedRegion.h names
//   for a spent receipt: a borrow checker reasons from one program point
//   to another, and a template argument does not.
//
//   Two regions a compiler cannot tell apart.  A brand names a mint
//   SITE, not a mint CALL, per fact 2 of foundation/Brand.h, so two
//   regions built by one statement in a loop are one type and so are
//   their pools, their guards and their reads.  Mixing those is well
//   typed, and it is the same limit under a new surface rather than a
//   new one.
//
//   What the context claims.  A context is a type minted behind its own
//   door, and the read weighs that claim without re-checking it at run
//   time.  The gate is only worth what mint_context is worth.
//
//   Whether the bytes mean anything.  The composition proves the read is
//   authorized.  It does not prove the region was ever written.

#include <fixy/Borrowed.h>
#include <fixy/OwnedRegion.h>
#include <foundation/Brand.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace fixy {

namespace detail {

// The one door to a shared read.  Only mint_shared_read holds it.
struct shared_read_mint_t {};

}  // namespace detail

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedRead;

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedRegion;

// The one gate of the read, as §XXI asks: one concept, every conjunct
// inside it.  Brand agreement is not a conjunct here because it is not
// a predicate: the guard and the region name one Brand parameter, so a
// mismatch is a deduction conflict before any constraint is checked.
template <typename T, typename Tag, typename Brand, typename Ctx>
concept CtxFitsSharedRead = std::is_object_v<T> && ::foundation::brand::IsBrand<Brand>
                         && ::foundation::effects::IsExecCtx<Ctx>
                         && ::foundation::permissions::CtxAdmitsPermission<Tag, Ctx>;

template <typename T, typename Tag, typename Brand, typename Ctx>
    requires CtxFitsSharedRead<T, Tag, Brand, Ctx>
[[nodiscard]] constexpr SharedRead<T, Tag, Brand>
mint_shared_read(Ctx const& ctx,
                 ::foundation::permissions::SharedPermissionGuard<Tag, Brand> const& guard CRUCIBLE_LIFETIMEBOUND,
                 SharedRegion<T, Tag, Brand> const& region CRUCIBLE_LIFETIMEBOUND) noexcept;

// A guard that dies at the end of the statement proves a share that is
// already released by the time the read is used, so the twin refuses it
// rather than letting the annotation carry the whole claim.
template <typename T, typename Tag, typename Brand, typename Ctx>
    requires CtxFitsSharedRead<T, Tag, Brand, Ctx>
constexpr SharedRead<T, Tag, Brand>
mint_shared_read(Ctx const&, ::foundation::permissions::SharedPermissionGuard<Tag, Brand> const&&,
                 SharedRegion<T, Tag, Brand> const&) =
    delete("the guard is released at the end of the full expression, so the read would outlive the share it "
           "stands on; bind the guard to a name that outlives the read");

// A region whose exclusive permission is parked in a pool, so several
// readers may hold a share of it at once.  It is Pinned twice over: the
// pool's atomic state is the channel identity, and a read handed out
// points into this object's storage.
template <typename T, typename Tag, typename Brand>
class SharedRegion : public ::foundation::Pinned<SharedRegion<T, Tag, Brand>> {
    static_assert(::foundation::brand::IsBrand<Brand>, "SharedRegion<T, Tag, Brand>: Brand must be an empty class "
                                                       "type: the brand of the region that was surrendered, or "
                                                       "DefaultBrand.");

    T* base_ = nullptr;
    std::size_t count_ = 0;
    ::foundation::permissions::SharedPermissionPool<Tag, Brand> pool_;

    template <typename U, typename UTag, typename UBrand, typename Ctx>
        requires CtxFitsSharedRead<U, UTag, UBrand, Ctx>
    friend constexpr SharedRead<U, UTag, UBrand>
    mint_shared_read(Ctx const& ctx,
                     ::foundation::permissions::SharedPermissionGuard<UTag, UBrand> const& guard,
                     SharedRegion<U, UTag, UBrand> const& region) noexcept;

public:
    using value_type = T;
    using tag_type = Tag;
    using brand_type = Brand;

    // The door consumes the exclusive region, which is the evidence:
    // there is no shared region without one, and the region it was built
    // from is gone.  This is the shape SharedPermissionPool uses for the
    // same reason, and it is why this class is not on the witness
    // roster: a constructor that consumes its own evidence guards
    // nothing a private one would guard better.
    constexpr explicit SharedRegion(OwnedRegion<T, Tag, Brand>&& region) noexcept
        : base_{region.base_}, count_{region.count_}, pool_{std::move(region.perm_)} {}

    // A caller who offers the region without surrendering it means to go
    // on reading it exclusively, and the rvalue reference alone answers
    // that with a binding error rather than with the reason.  The twin
    // says the reason.
    SharedRegion(OwnedRegion<T, Tag, Brand>&) =
        delete("a shared region consumes the exclusive one: the permission it parks is the region's, and a "
               "region that kept it would read beside every share; pass std::move(region)");

    // The extent is not the data.  A reader that has no share can still
    // ask how large the region is, which is what a scheduler does before
    // it decides whether to take one.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    // Both forward to the pool, which already weighs the tag's row
    // against the context.  They are here so a caller holds one object
    // rather than a region beside a pool that happens to match it.
    template <typename Ctx = ::foundation::permissions::detail::no_ctx>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<::foundation::permissions::SharedPermissionGuard<Tag, Brand>>
    lend(Ctx const& ctx = {}) noexcept {
        return pool_.lend(ctx);
    }

    // The inverse of the constructor: when no share is outstanding the
    // exclusive comes back, and with it the region it covered.
    template <typename Ctx = ::foundation::permissions::detail::no_ctx>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<OwnedRegion<T, Tag, Brand>> try_upgrade(Ctx const& ctx = {}) noexcept {
        auto exclusive = pool_.try_upgrade(ctx);
        if (!exclusive.has_value()) return std::nullopt;
        return OwnedRegion<T, Tag, Brand>{base_, count_, std::move(*exclusive)};
    }

    [[nodiscard]] std::uint64_t outstanding() const noexcept { return pool_.outstanding(); }
};

template <typename T, typename Tag, typename Brand>
SharedRegion(OwnedRegion<T, Tag, Brand>&&) -> SharedRegion<T, Tag, Brand>;

// A read the three axes agreed on.  It is a span and nothing else: the
// share it stands on is the guard's, and the instance it names is in
// its own type.
template <typename T, typename Tag, typename Brand>
class [[nodiscard]] SharedRead {
    static_assert(::foundation::brand::IsBrand<Brand>, "SharedRead<T, Tag, Brand>: Brand must be an empty class "
                                                       "type: the brand of the region read, or DefaultBrand.");

    std::span<T const> span_{};

    constexpr SharedRead(detail::shared_read_mint_t, std::span<T const> span) noexcept : span_{span} {}

    template <typename U, typename UTag, typename UBrand, typename Ctx>
        requires CtxFitsSharedRead<U, UTag, UBrand, Ctx>
    friend constexpr SharedRead<U, UTag, UBrand>
    mint_shared_read(Ctx const& ctx,
                     ::foundation::permissions::SharedPermissionGuard<UTag, UBrand> const& guard,
                     SharedRegion<U, UTag, UBrand> const& region) noexcept;

public:
    using element_type = T const;
    using tag_type = Tag;
    using brand_type = Brand;
    using span_type = std::span<T const>;

    SharedRead() = delete;

    [[nodiscard]] constexpr span_type cspan() const noexcept { return span_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return span_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return span_.empty(); }
    [[nodiscard]] constexpr T const& operator[](std::size_t index) const noexcept { return span_[index]; }
    [[nodiscard]] constexpr T const* begin() const noexcept { return span_.data(); }
    [[nodiscard]] constexpr T const* end() const noexcept { return span_.data() + span_.size(); }
};

template <typename T, typename Tag, typename Brand, typename Ctx>
    requires CtxFitsSharedRead<T, Tag, Brand, Ctx>
[[nodiscard]] constexpr SharedRead<T, Tag, Brand>
mint_shared_read(Ctx const& /*ctx*/,
                 ::foundation::permissions::SharedPermissionGuard<Tag, Brand> const& /*guard*/
                     CRUCIBLE_LIFETIMEBOUND,
                 SharedRegion<T, Tag, Brand> const& region CRUCIBLE_LIFETIMEBOUND) noexcept {
    // The context is read by the constraint on the declaration and the
    // guard by the type system.  Neither is read here, which is the
    // point: after the mint the read runs at the speed of a span.
    return SharedRead<T, Tag, Brand>{detail::shared_read_mint_t{}, std::span<T const>{region.base_, region.count_}};
}

namespace detail::shared_region_self_test {

struct probe_tag {
    using permission_row = ::foundation::effects::Row<>;
};
struct io_tag {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using Fg = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
using Erased = ::foundation::brand::DefaultBrand;

// The gate admits a pure tag under a foreground context and refuses an
// IO tag there, which is the effect axis on its own.
static_assert(CtxFitsSharedRead<int, probe_tag, Erased, Fg>);
static_assert(!CtxFitsSharedRead<int, io_tag, Erased, Fg>, "a foreground context does not admit an IO region");
static_assert(!CtxFitsSharedRead<int, probe_tag, Erased, int>, "a context is a context, not any type");
static_assert(!CtxFitsSharedRead<void, probe_tag, Erased, Fg>, "a region of void has no elements to read");

// The read is a span and nothing else, and it has no door of its own.
using Read = SharedRead<int, probe_tag>;
static_assert(sizeof(Read) == sizeof(std::span<int const>));
static_assert(!std::is_default_constructible_v<Read>, "a read nobody minted proves nothing");
static_assert(std::is_copy_constructible_v<Read>, "a read is a value, and the header says what that costs");
static_assert(std::is_same_v<Read::brand_type, Erased>);

// A shared region holds its address, so it is neither copied nor moved.
//
// The door's claim is spelled at a named brand rather than at the
// erased one.  A region left at the old arity is DefaultBrand, which
// scripts/check-brand-drain.sh counts and does not let a new file add,
// and the claim reads the same at either brand.
struct probe_brand {};
using Shared = SharedRegion<int, probe_tag, probe_brand>;
static_assert(!std::is_copy_constructible_v<Shared>);
static_assert(!std::is_move_constructible_v<Shared>);
static_assert(std::is_constructible_v<Shared, OwnedRegion<int, probe_tag, probe_brand>&&>,
              "the door consumes the exclusive");
static_assert(!std::is_constructible_v<Shared, OwnedRegion<int, probe_tag, probe_brand>&>,
              "and consumes it, rather than borrowing");

}  // namespace detail::shared_region_self_test

}  // namespace fixy
