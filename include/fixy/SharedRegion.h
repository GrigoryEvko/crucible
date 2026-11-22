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
//   A read handle that a function returns past its share.  The handle does
//   not copy and does not move, and it lends its elements only to a body
//   whose result holds no borrow.  So a handle kept in a longer-lived name
//   after the scope of the guard ends does not compile, and no View of its
//   elements outlives a call of its doors.  The guard parameter carries the
//   lifetime annotation and its rvalue twin refuses a guard that dies at
//   the end of the statement.  But a function can still return the read
//   that its mint gives, with no copy, while the guard is a local of that
//   function.  That is the flow-sensitive half, the same half
//   fixy/OwnedRegion.h names for a spent receipt: a borrow checker reasons
//   from one program point to another, and a template argument does not.
//
//   Nothing about two regions a compiler cannot tell apart, at compile
//   time.  A brand names a mint SITE, not a mint CALL, per fact 2 of
//   foundation/Brand.h, so two regions built by one statement in a loop
//   are one type and so are their pools and their guards.  The mint
//   therefore also asks the guard at run time whether it holds a share
//   of this region's pool, and a guard of the other region aborts.  The
//   erased brand would make every region of the tag one type, so the
//   gate refuses it and a read needs a region of a fresh brand.
//
//   What the context claims.  A context is a type minted behind its own
//   door, and the read weighs that claim without re-checking it at run
//   time.  The gate is only worth what mint_context is worth.
//
//   Whether the bytes mean anything.  The composition proves the read is
//   authorized.  It does not prove the region was ever written.

#include <fixy/OwnedRegion.h>
#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

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

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
struct shared_region;
struct shared_read;
}  // namespace row_discipline

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedRead;

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedRegion;

// The one gate of the read, as §XXI asks: one concept, every conjunct
// inside it.  Brand agreement is not a conjunct here because it is not
// a predicate: the guard and the region name one Brand parameter, so a
// mismatch is a deduction conflict before any constraint is checked.
// The brand must be fresh.  On the erased brand every region of the tag
// is one type, so the deduction would pair any guard with any region.
template <typename T, typename Tag, typename Brand, typename Ctx>
concept CtxFitsSharedRead =
    std::is_object_v<T> && ::foundation::brand::IsFreshBrand<Brand> && ::foundation::effects::IsExecCtx<Ctx>
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

// A region that dies at the end of the statement leaves the read pointing
// into storage that is gone, so this twin refuses an rvalue region.  A
// guard and a region that are both rvalues match the two twins equally,
// and the call is ambiguous, which refuses it too.
template <typename T, typename Tag, typename Brand, typename Ctx>
    requires CtxFitsSharedRead<T, Tag, Brand, Ctx>
constexpr SharedRead<T, Tag, Brand>
mint_shared_read(Ctx const&, ::foundation::permissions::SharedPermissionGuard<Tag, Brand> const&,
                 SharedRegion<T, Tag, Brand> const&&) =
    delete("the region is destroyed at the end of the full expression, so the read would point into storage "
           "that is gone; bind the region to a name that outlives the read");

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
    mint_shared_read(Ctx const& ctx, ::foundation::permissions::SharedPermissionGuard<UTag, UBrand> const& guard,
                     SharedRegion<U, UTag, UBrand> const& region) noexcept;

public:
    using value_type = T;
    using tag_type = Tag;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::shared_region;
    using row_payload = T;

    // The door consumes the exclusive region, which is the evidence:
    // there is no shared region without one.  The door takes the base and
    // the count out of that region, so it is empty afterwards and cannot
    // write beside a share.  This is the shape SharedPermissionPool uses for the
    // same reason, and it is why this class is not on the witness
    // roster: a constructor that consumes its own evidence guards
    // nothing a private one would guard better.
    constexpr explicit SharedRegion(OwnedRegion<T, Tag, Brand>&& region) noexcept
        : base_{std::exchange(region.base_, nullptr)},
          count_{std::exchange(region.count_, 0)},
          pool_{std::move(region.perm_)} {}

    // A caller who offers the region without surrendering it means to go
    // on reading it exclusively, and the rvalue reference alone answers
    // that with a binding error rather than with the reason.  The twin
    // says the reason.
    SharedRegion(OwnedRegion<T, Tag, Brand>&) = delete(
        "a shared region consumes the exclusive one: the permission it parks is the region's, and a "
        "region that kept it would read beside every share; pass std::move(region)");

    // The extent is not the data.  A reader that has no share can still
    // ask how large the region is, which is what a scheduler does before
    // it decides whether to take one.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    // Both forward to the pool, which already weighs the tag's row
    // against the context.  They are here so a caller holds one object
    // rather than a region beside a pool that happens to match it.
    template <typename Ctx>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx>
    [[nodiscard]] ::foundation::core::Option<::foundation::permissions::SharedPermissionGuard<Tag, Brand>>
    lend(Ctx const& ctx) noexcept {
        return pool_.lend(ctx);
    }

    // The inverse of the constructor: when no share is outstanding the
    // exclusive comes back, and with it the region it covered.
    template <typename Ctx>
        requires ::foundation::permissions::PoolCtx<Tag, Ctx>
    [[nodiscard]] ::foundation::core::Option<OwnedRegion<T, Tag, Brand>> try_upgrade(Ctx const& ctx) noexcept {
        using Upgraded = ::foundation::core::Option<OwnedRegion<T, Tag, Brand>>;
        auto exclusive = pool_.try_upgrade(ctx);
        if (exclusive.is_none()) return Upgraded{::foundation::core::none};
        return Upgraded::some(OwnedRegion<T, Tag, Brand>{
            base_, count_, std::move(exclusive).expect("the test above found the exclusive").leave_pool()});
    }

    [[nodiscard]] std::uint64_t outstanding() const noexcept { return pool_.outstanding(); }
};

template <typename T, typename Tag, typename Brand>
SharedRegion(OwnedRegion<T, Tag, Brand>&&) -> SharedRegion<T, Tag, Brand>;

// A read the three axes agreed on.  It is a span and nothing else: the
// share it stands on is the guard's, and the instance it names is in
// its own type.
//
// The read lends its elements only to a body, for the time of the call, and
// the result of the body holds no borrow (HoldsNoBorrow of
// foundation/core/Choice.h).  So no View and no reference of an element
// outlives the call.  The read itself does not copy and does not move, so
// it lives only where its mint made it, beside a guard that is alive at the
// mint.  A read kept in a longer-lived name, such as an Option outside the
// scope of the guard, does not compile.  What stays open is the return of
// a read from a function whose guard is a local of that function: the
// class of a returned borrow of a local, which no template argument sees.
template <typename T, typename Tag, typename Brand>
class [[nodiscard]] SharedRead {
    static_assert(::foundation::brand::IsBrand<Brand>, "SharedRead<T, Tag, Brand>: Brand must be an empty class "
                                                       "type: the brand of the region read, or DefaultBrand.");

    std::span<T const> span_{};
    // No byte route builds a read, so a read names only a region that its
    // mint paired with a live share.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr SharedRead(detail::shared_read_mint_t, std::span<T const> span) noexcept : span_{span} {}

    // The View of the elements.  The mint made the span from the base and the
    // count of the region, which the mint of the region checked, so the View
    // takes the door of the base layer with no check.
    [[nodiscard]] constexpr ::foundation::core::View<T const> run_() const noexcept {
        return ::foundation::core::View<T const>{::foundation::core::detail::ViewDoor::key(), span_.data(),
                                                 span_.size()};
    }

    template <typename U, typename UTag, typename UBrand, typename Ctx>
        requires CtxFitsSharedRead<U, UTag, UBrand, Ctx>
    friend constexpr SharedRead<U, UTag, UBrand>
    mint_shared_read(Ctx const& ctx, ::foundation::permissions::SharedPermissionGuard<UTag, UBrand> const& guard,
                     SharedRegion<U, UTag, UBrand> const& region) noexcept;

public:
    using element_type = T const;
    using tag_type = Tag;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::shared_read;
    using row_payload = T;

    SharedRead() = delete;
    SharedRead(SharedRead const&) = delete(
        "a read stands on the share of its guard: a copy could outlive the guard, and "
        "read the region after the pool gives the exclusive back");
    SharedRead& operator=(SharedRead const&) = delete("a read stands on the share of its guard: a copy could outlive "
                                                      "the guard");
    SharedRead(SharedRead&&) = delete("a read stands on the share of its guard: a move could carry it past the guard, "
                                      "and read the region after the pool gives the exclusive back");
    SharedRead& operator=(SharedRead&&) = delete("a read stands on the share of its guard: a move could carry it past "
                                                 "the guard");
    ~SharedRead() = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return span_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return span_.empty(); }

    // The read door of the run: body gets the View of the const elements for
    // the time of the call, and the door gives the result of body, which
    // holds no borrow.  Complexity: O(1), and the cost of body.
    template <class Body>
        requires std::is_invocable_v<Body, ::foundation::core::View<T const>>
              && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body, ::foundation::core::View<T const>>>
    constexpr std::invoke_result_t<Body, ::foundation::core::View<T const>> with_view(Body&& body) const
        noexcept(std::is_nothrow_invocable_v<Body, ::foundation::core::View<T const>>) {
        return static_cast<Body&&>(body)(run_());
    }

    // A body whose result holds a borrow binds to this overload, so the
    // refusal gives its reason.
    template <class Body>
        requires std::is_invocable_v<Body, ::foundation::core::View<T const>>
                  && (!::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body, ::foundation::core::View<T const>>>)
    void with_view(Body&& body) const = delete("the result of a read door holds no borrow: a borrow of an element "
                                               "would outlive the share of the guard.  Give a value with no "
                                               "reference and no address");

    // The read door of one element: on_some gets the element at position for
    // the time of the call, and at or past size() on_none runs.  The two arms
    // give the same type, which holds no borrow.  No type holds the count of
    // a read, so the position is a count, as for with_element of a HeapArray.
    // Complexity: one compare, and the cost of the arm.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
              && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<OnNone>>
    constexpr std::invoke_result_t<OnNone> with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) const
        noexcept(std::is_nothrow_invocable_v<OnSome, T const&> && std::is_nothrow_invocable_v<OnNone>) {
        if (position >= span_.size()) {
            return static_cast<OnNone&&>(on_none)();
        }
        return static_cast<OnSome&&>(on_some)(span_[position]);
    }
};

template <typename T, typename Tag, typename Brand, typename Ctx>
    requires CtxFitsSharedRead<T, Tag, Brand, Ctx>
[[nodiscard]] constexpr SharedRead<T, Tag, Brand>
mint_shared_read(Ctx const& /*ctx*/,
                 ::foundation::permissions::SharedPermissionGuard<Tag, Brand> const& guard CRUCIBLE_LIFETIMEBOUND,
                 SharedRegion<T, Tag, Brand> const& region CRUCIBLE_LIFETIMEBOUND) noexcept {
    // The context is read by the constraint on the declaration.  The
    // guard is read once, here: two regions minted at one site share a
    // brand, so the type cannot tell their guards apart, and one pointer
    // comparison can.  After the mint the read runs at the speed of a span.
    CRUCIBLE_FATAL_INVARIANT(guard.is_share_of(region.pool_));
    return SharedRead<T, Tag, Brand>{detail::shared_read_mint_t{}, std::span<T const>{region.base_, region.count_}};
}

}  // namespace fixy
