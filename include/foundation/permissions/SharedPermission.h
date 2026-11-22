#pragma once

// The share, the guard and the pool of a region.  A share is a copyable
// proof of a read of the region that an exclusive Permission names.  A
// guard is the lifetime of one share, and a pool counts the shares of one
// exclusive and gives the exclusive back when no share is out.  The token,
// the tag tree, the rows and the split and combine mints are in
// foundation/permissions/Permission.h.
//
// Fractional permissions generalize the binary own-or-not of plain
// separation logic to a share `e ↦_p v` for 0 < p ≤ 1.  A share of 1 is
// exclusive read-write, anything less is shared read, and shares summing
// to 1 recover the exclusive.  The split-merge law
// `e ↦_(p+q) v ⟺ e ↦_p v * e ↦_q v` is what licenses handing shares out
// and taking them back.
//
// The proof of a share and the lifetime of a share are separate objects
// here: the token below is the proof, and the guard further down is the
// lifetime.  Both carry the brand of the exclusive they came from, so a
// share of one region is not a share of another region of the same tag.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace foundation::permissions {

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedPermissionGuard;
template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedPermissionPool;
template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class ExclusiveLease;

template <typename T>
concept IsSharedPermission = ::foundation::reflect::IsInstanceOf<T, ^^SharedPermission>;

// The region tag of a share.  The extractor is constrained on its own
// predicate, as permission_tag_t of foundation/permissions/Permission.h
// is, so a type of the other kind, or of no kind, is refused at the alias
// and does not become void.
template <typename T>
    requires IsSharedPermission<T>
using shared_permission_tag_t = typename std::remove_cvref_t<T>::tag_type;

template <typename T, typename Tag>
concept IsSharedPermissionFor = IsSharedPermission<T> && std::is_same_v<shared_permission_tag_t<T>, Tag>;

// The fit of one share call: a permission, with one context or none in
// front of it.  A context must admit the row of the tag of the permission.
template <typename... Args>
concept PermissionShareArgs = detail::MintArgs<1, 1, Args...>
                           && (detail::leading_ctx_count({^^Args...}) == 0
                               || detail::CtxAdmitsEach<Args...[0], detail::perm_tag_t<Args...[1]>>);

template <typename... Args>
    requires PermissionShareArgs<Args...>
[[nodiscard]] constexpr SharedPermission<detail::perm_tag_t<Args...[sizeof...(Args) - 1]>,
                                         detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>
mint_permission_share(Args&&...) noexcept;

// A share is the authority to read a region, so it carries the annotation
// authority too.  A copy of a share proves nothing more than the share, so
// a share is not linear.
template <typename Tag, typename Brand>
class [[nodiscard]][[= ::foundation::linear::authority{}]][[= ::foundation::lifetime::no_start_over_bytes{}]]
    SharedPermission {
    static_assert(::foundation::brand::IsBrand<Brand>, "SharedPermission<Tag, Brand>: Brand must be an empty class "
                                                       "type: the closure of a mint's fresh lambda, or DefaultBrand.");

    constexpr SharedPermission() noexcept = default;

    template <typename T, typename B>
    friend class SharedPermissionGuard;

    template <typename... Args>
        requires PermissionShareArgs<Args...>
    friend constexpr SharedPermission<detail::perm_tag_t<Args...[sizeof...(Args) - 1]>,
                                      detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>
    mint_permission_share(Args&&...) noexcept;

public:
    using tag_type = Tag;
    using brand_type = Brand;

    // The token confers nothing: the class is empty and exposes no
    // accessor.  The object that stands for a live shared read is the
    // guard, whose lifetime is the pool's refcount.  A token copied out
    // of a guard and stashed past that guard's destruction is therefore
    // not a stale read-proof, because it was never a read-proof.  Code
    // gates access on holding a guard.
    static constexpr bool confers_runtime_access = false;

    // Modelling this token as a graded value carrying its own share
    // would cost every instance its empty layout, and would put the
    // share in the wrong place: the authoritative share count is the
    // pool's atomic state, not anything an individual token knows.  The
    // alias exists only so the token introspects like the other graded
    // wrappers.  Its value type is the tag, because the proof's value is
    // its identity.  A larger share is the stronger claim.  The lattice is
    // the order dual of the shares, because Graded asks for that
    // orientation of a grade stored beside a value.
    using value_type = Tag;
    using lattice_type =
        ::foundation::algebra::lattices::DualLattice<::foundation::algebra::lattices::FractionalLattice>;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    using graded_type = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, lattice_type, Tag>;

    // No conversion drops the brand of a share.  A share of one region is
    // never a share of the erased identity, which every region of the tag
    // shares.
    // The constructors and the destructor are trivial, so the ABI passes a
    // share in a register.  The assignments are user-provided, so the
    // share is not trivially copyable, and std::bit_cast builds no share
    // from a byte.
    constexpr SharedPermission(const SharedPermission&) noexcept = default;
    constexpr SharedPermission(SharedPermission&&) noexcept = default;
    constexpr SharedPermission& operator=(const SharedPermission&) noexcept { return *this; }
    constexpr SharedPermission& operator=(SharedPermission&&) noexcept { return *this; }
    ~SharedPermission() = default;

    [[nodiscard]] static consteval ::foundation::core::TextView value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval ::foundation::core::TextView lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// The destructor gives the share back to the pool, so the end of the scope
// of a guard is its named door.
// A guard points at its pool, which each thread that holds a share writes
// with atomic steps by design, so it moves between threads as its tag does
// (foundation/Transfer.h, rule 6).  The guard conveys the authority of its
// share, and it does not copy.  Many holders hold a share at one time, so the
// guard states shares{}: it proves no door that writes the plain parts of a
// class, and only the result of a claim door keeps it (THE SHARE of
// foundation/Transfer.h).
template <typename Tag, typename Brand>
class [[nodiscard]][[= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::by_arguments{}]]
                   [[= ::foundation::linear::authority{}]][[= ::foundation::transfer::shares{}]] SharedPermissionGuard {
    // The pool, through a pointer to const: the guard writes only the count
    // of the pool, a mutable atomic cell, and never the parked permission.
    SharedPermissionPool<Tag, Brand> const* pool_ = nullptr;

    constexpr explicit SharedPermissionGuard(SharedPermissionPool<Tag, Brand>& p) noexcept : pool_{&p} {}
    friend class SharedPermissionPool<Tag, Brand>;

public:
    using tag_type = Tag;
    using brand_type = Brand;

    SharedPermissionGuard(const SharedPermissionGuard&) =
        delete("RAII guard owns one outstanding share — copy would double-count");
    SharedPermissionGuard& operator=(const SharedPermissionGuard&) =
        delete("RAII guard owns one outstanding share — assignment would double-count");
    constexpr SharedPermissionGuard(SharedPermissionGuard&& other) noexcept
        : pool_{std::exchange(other.pool_, nullptr)} {}
    SharedPermissionGuard& operator=(SharedPermissionGuard&&) =
        delete("RAII guard's lifetime is fixed at construction; reassignment would double-decrement");

    ~SharedPermissionGuard();

    // The guard is the lifetime of one share, and the pool refuses to end
    // while a share is out.  A guard on the heap has no scope that ends
    // it, so a leaked guard keeps its share out: try_upgrade never
    // succeeds, and the pool aborts when it ends.  The Option that lend
    // returns builds the guard in its storage with the global placement
    // new, which these deletions do not refuse.
    static void* operator new(std::size_t) =
        delete("a SharedPermissionGuard lives in the scope that holds its share, not on the heap");
    static void* operator new[](std::size_t) =
        delete("a SharedPermissionGuard lives in the scope that holds its share, not on the heap");
    static void* operator new(std::size_t, std::align_val_t) =
        delete("a SharedPermissionGuard lives in the scope that holds its share, not on the heap");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("a SharedPermissionGuard lives in the scope that holds its share, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

    // The ref-qualifier pair is the enforcement, not a lifetime
    // annotation.  The token this returns is a proof that the guard's
    // share is outstanding, so it must not be minted from a guard that
    // is about to die: `pool.lend(ctx).expect(why).token()` would hand back a
    // proof of a share released at the end of the statement.  No
    // annotation can be written on the implicit object parameter at all,
    // so the qualifier carries the claim instead.
    //
    // Measured: `const&` alone does NOT close this, because a const
    // lvalue reference binds an rvalue, so a const-ref-qualified member
    // is callable on a temporary.  Only the deleted `const&&` twin
    // refuses it, which is the same mechanism the borrow factories use.
    //
    // The Option that lend returns has no operator* and no operator->, so
    // no pointer to the guard of a temporary Option reaches this member.
    // A loop over the Option binds the Option to a name for the loop, so
    // the guard lives as long as the body that reads it.
    [[nodiscard]] constexpr SharedPermission<Tag, Brand> token() const& noexcept {
        return SharedPermission<Tag, Brand>{};
    }

    constexpr SharedPermission<Tag, Brand> token() const&& = delete(
        "a share proof minted from a temporary guard outlives the share; bind the guard to a name that "
        "outlives the token");

    [[nodiscard]] constexpr bool holds_share() const noexcept { return pool_ != nullptr; }

    // True when this guard holds a share of that one pool.  A door that
    // pairs a guard with the region of a pool asks this, so a guard of one
    // pool never stands in for another pool of the same type.  Two pools
    // are one type when they share a brand, as two pools minted at one
    // site or two pools on the erased brand do.
    [[nodiscard]] constexpr bool is_share_of(SharedPermissionPool<Tag, Brand> const& pool) const noexcept {
        return pool_ == &pool;
    }
};

// THE LEASE.  The exclusive of a pool while it is out of the pool.
// try_upgrade gives it, and deposit_exclusive takes only it, so a token that
// did not come out of the pool, such as the root of a second run of the call
// site of the first root, has no door into the pool.  The lease keeps the
// identity of its pool as a word, and the deposit compares the word with its
// own address, so a lease of one pool does not go into another pool of the
// same type.  Two pools of one call site have one type, and only a run-time
// identity tells them apart (fact 2 of foundation/Brand.h).  The word names
// the pool only to compare it, and no code reads the pool through it.
//
// A lease is linear, as the token in it is: each path gives it back to its
// pool, or takes the token out for good (leave_pool).  A pool whose lease
// left for good lends no share again.  A lease moves to another thread as
// its token does, and it is not Shareable, for the reason of the token.
template <typename Tag, typename Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                   [[= ::foundation::transfer::not_shareable{}]] ExclusiveLease {
    [[no_unique_address]] Permission<Tag, Brand> exclusive_;
    __UINTPTR_TYPE__ pool_word_ = 0;

    // The word of the address of a pool.  The compare reads two addresses as
    // words, as holds_part of fixy/os/OwnedThread.h does, so a pool of
    // another object gives false with no compare of pointers of two objects.
    [[nodiscard]] static __UINTPTR_TYPE__ word_of_(SharedPermissionPool<Tag, Brand> const& pool) noexcept {
        return __builtin_bit_cast(__UINTPTR_TYPE__, static_cast<void const*>(__builtin_addressof(pool)));
    }

    ExclusiveLease(Permission<Tag, Brand>&& exclusive, SharedPermissionPool<Tag, Brand> const& pool) noexcept
        : exclusive_{std::move(exclusive)}, pool_word_{word_of_(pool)} {}
    friend class SharedPermissionPool<Tag, Brand>;

public:
    using tag_type = Tag;
    using brand_type = Brand;

    ExclusiveLease(ExclusiveLease const&) = delete("a lease is the one exclusive of its pool, and a copy is a second");
    ExclusiveLease&
    operator=(ExclusiveLease const&) = delete("a lease is the one exclusive of its pool, and a copy is a second");
    ExclusiveLease(ExclusiveLease&&) noexcept = default;
    ExclusiveLease& operator=(ExclusiveLease&&) = delete("an assignment over a live lease would end it with no door");
    ~ExclusiveLease() = default;

    // True when the lease came out of that one pool.
    [[nodiscard]] bool is_lease_of(SharedPermissionPool<Tag, Brand> const& pool) const noexcept {
        return pool_word_ == word_of_(pool);
    }

    // The exclusive token, lent for the call of a door that reads the proof,
    // such as advise_release_aware of fixy/os/Mmap.h.  The lease keeps the
    // token, and no door consumes a token through a reference to const.  The
    // deleted twin refuses a reference into a temporary lease, which would
    // outlive the lease.
    [[nodiscard]] Permission<Tag, Brand> const& peek() const& noexcept { return exclusive_; }
    Permission<Tag, Brand> const& peek() const&& = delete("a reference into a temporary lease outlives the lease");

    // The token leaves its pool for good, such as when the region of the
    // pool becomes an owned region.  The pool then holds its exclusive-out
    // state to its end, so it lends no share again.
    [[nodiscard]] Permission<Tag, Brand> leave_pool() && noexcept { return std::move(exclusive_); }
};

// A deposit took a lease of another pool.  The body is in
// src/foundation/SharedPermission.cpp.
[[noreturn]] CRUCIBLE_COLD void shared_permission_pool_foreign_lease_abort_() noexcept;

// A lend found the share count at its maximum.  The body is in
// src/foundation/SharedPermission.cpp: it prints the text of the catalog
// tag SharedPermissionPoolSaturated, and only that file reads the tag.
[[noreturn]] CRUCIBLE_COLD void shared_permission_pool_saturated_abort_() noexcept;

// Each guard keeps a pointer into its pool, and its destructor writes
// the pool's count.  A pool that ends while a guard lives would take that
// write into freed memory, so the pool refuses to end while a share is
// out.  The body is in src/foundation/SharedPermission.cpp.
[[noreturn]] CRUCIBLE_COLD void shared_permission_pool_outlived_abort_() noexcept;

// The context a pool operation runs in: one that admits the tag's row and
// holds Nondet.  A lend fails while the exclusive is out, and an upgrade
// fails while a share is out.  Another thread ends a share or deposits the
// exclusive at a time that the schedule decides, so two runs with the same
// inputs can get two outcomes.  So each lend and each upgrade takes a
// context, and no call with no context compiles.
template <typename Tag, typename Ctx>
concept PoolCtx = CtxAdmitsPermission<Tag, Ctx>
               && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>;

namespace detail {

// THE INTERNAL DOOR OF THE CHANNELS.  A lend and an upgrade with no
// context, for the members of the channels of fixy/concurrent and
// fixy/channels that take no context yet: producer,
// try_withdraw_producer_root and drain of the merge form of
// PermissionedChannel, and reader and try_withdraw_reader_root of the SWMR
// session.  Each of them asks for the share or the exclusive of a pool
// that the channel itself owns, for a tag whose row is empty.  The schedule
// decides its outcome, as it decides the outcome of lend(ctx), and no
// context gates these members yet.  They move to a form that takes the
// context, one channel at a time, and then the door goes.  No other code
// calls the door.  The rule owners keeps each use of a
// detail namespace of the base out of the code outside the base, and the
// fixtures neg_share_pool_*_without_ctx_* hold that a lend or an upgrade
// with no context does not compile.
class ChannelPoolDoor final : ::foundation::NoObject<ChannelPoolDoor> {
public:
    template <typename Tag, typename Brand>
    [[nodiscard]] static ::foundation::core::Option<SharedPermissionGuard<Tag, Brand>>
    lend(SharedPermissionPool<Tag, Brand>& pool) noexcept {
        static_assert(permission_row_empty(^^Tag), "the internal door of the channels lends only a share of a tag "
                                                   "whose permission row is empty.  A tag with effects uses "
                                                   "lend(ctx).");
        return pool.lend_raw_();
    }

    template <typename Tag, typename Brand>
    [[nodiscard]] static ::foundation::core::Option<ExclusiveLease<Tag, Brand>>
    try_upgrade(SharedPermissionPool<Tag, Brand>& pool) noexcept {
        static_assert(permission_row_empty(^^Tag), "the internal door of the channels upgrades only a pool of a tag "
                                                   "whose permission row is empty.  A tag with effects uses "
                                                   "try_upgrade(ctx).");
        return pool.try_upgrade_raw_();
    }
};

}  // namespace detail

// The pool carries the brand of the exclusive it was built from, so its
// shares are shares of that region and its upgrade hands that region
// back.  `SharedPermissionPool pool{std::move(exc)};` deduces both.
template <typename Tag, typename Brand>
class SharedPermissionPool : public ::foundation::Pinned<SharedPermissionPool<Tag, Brand>> {
public:
    using tag_type = Tag;
    using brand_type = Brand;

    static constexpr std::uint64_t EXCLUSIVE_OUT_BIT = std::uint64_t{1} << 63;
    static constexpr std::uint64_t COUNT_MASK = EXCLUSIVE_OUT_BIT - std::uint64_t{1};

    constexpr explicit SharedPermissionPool(Permission<Tag, Brand>&& exc) noexcept
        : parked_{std::move(exc)}, state_{std::uint64_t{0}} {}

    ~SharedPermissionPool() {
        if ((state_.load_acquire() & COUNT_MASK) != 0) [[unlikely]] {
            shared_permission_pool_outlived_abort_();
        }
    }

    // A share of the region, or no value while the exclusive is out.
    template <typename Ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] ::foundation::core::Option<SharedPermissionGuard<Tag, Brand>> lend(Ctx const&) noexcept {
        return lend_raw_();
    }

    // The mode transition has to be indivisible.  Reading a plain count
    // of zero and then taking the parked permission loses to a lend that
    // increments in between, leaving two holders of one region.  Folding
    // the count and the upgraded-out flag into one word makes the test
    // and the claim a single compare-exchange: a lend that was about to
    // succeed retries and then fails on the flag, and a lend that
    // already incremented makes this compare-exchange fail.  The exclusive
    // comes out as the lease of this pool (THE LEASE).
    template <typename Ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] ::foundation::core::Option<ExclusiveLease<Tag, Brand>> try_upgrade(Ctx const&) noexcept {
        return try_upgrade_raw_();
    }

    // The holder of the lease gives the exclusive back (THE LEASE).  While
    // the exclusive is out, the state is the exclusive-out bit alone: a lend
    // fails on the bit, and no guard lives, because the upgrade found a count
    // of zero.  So no other thread writes the state or the parked permission
    // here.
    //
    // The deposit takes only a lease, so a token of a second root of one call
    // site has no door here.  Two roots of one site share a brand, so only the
    // identity word of the pool tells a lease of this pool from a lease of
    // another pool of the same type.  A lease of another pool cannot occur in
    // correct code, so its deposit ends the process.  A deposit into a pool
    // that lends would store zero over live shares.  The checks below end the
    // process in each build, also in a unit with the ignore semantic, where a
    // precondition is only an assumption.  They come before the write of the
    // parked permission, so a refused deposit writes nothing.  The bit is set
    // only with a count of zero, so the bit alone tells that the exclusive is
    // out.  The compare-exchange clears the bit only from the state of the bit
    // alone.
    //
    // No postcondition: an upgrade on another thread can take the parked
    // permission before a witness reads it.
    void deposit_exclusive(ExclusiveLease<Tag, Brand>&& lease) noexcept {
        if (!lease.is_lease_of(*this)) [[unlikely]] {
            shared_permission_pool_foreign_lease_abort_();
        }
        CRUCIBLE_FATAL_INVARIANT(is_exclusive_out());
        // The token leaves the lease here, and the pool parks it again.
        parked_ = std::move(lease).leave_pool();
        // Release, so a later lend sees the freshly parked permission.
        const bool is_bit_cleared = state_.cas_acq_rel(EXCLUSIVE_OUT_BIT, std::uint64_t{0}).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_bit_cleared);
    }

    [[nodiscard]] std::uint64_t outstanding() const noexcept { return state_.load_acquire() & COUNT_MASK; }

    [[nodiscard]] bool is_exclusive_out() const noexcept { return (state_.load_acquire() & EXCLUSIVE_OUT_BIT) != 0; }

private:
    [[nodiscard]] ::foundation::core::Option<SharedPermissionGuard<Tag, Brand>> lend_raw_() noexcept {
        using Lent = ::foundation::core::Option<SharedPermissionGuard<Tag, Brand>>;
        std::uint64_t observed = state_.load_acquire();
        for (;;) {
            if (observed & EXCLUSIVE_OUT_BIT) [[unlikely]] {
                return Lent{::foundation::core::none};
            }
            if ((observed & COUNT_MASK) == COUNT_MASK) [[unlikely]] {
                shared_permission_pool_saturated_abort_();
            }
            const std::uint64_t desired = observed + std::uint64_t{1};
            // Acquire-release, so this count update synchronizes with
            // the compare-exchange in try_upgrade.  A failed exchange reads
            // the state again.
            if (state_.cas_weak_acq_rel(observed, desired).is_ok()) {
                return Lent::some(SharedPermissionGuard<Tag, Brand>{*this});
            }
            observed = state_.load_acquire();
        }
    }

    // The claim door of the pool: it makes the exclusive token of the pool
    // with one compare-exchange.  The write of the parked permission after
    // the compare-exchange happens only on the thread that won it, and no
    // other thread writes the parked permission until that thread deposits
    // the lease.
    [[nodiscard]] ::foundation::core::Option<ExclusiveLease<Tag, Brand>> try_upgrade_raw_() noexcept {
        using Upgraded = ::foundation::core::Option<ExclusiveLease<Tag, Brand>>;
        if (state_.cas_acq_rel(std::uint64_t{0}, EXCLUSIVE_OUT_BIT).is_err()) {
            return Upgraded{::foundation::core::none};
        }
        // The parked permission is populated whenever the exclusive-out
        // bit is clear, and the exchange above just moved that bit from
        // clear to set, so the dereference here cannot be empty.
        Permission<Tag, Brand> exc = std::move(*parked_);
        parked_.reset();
        return Upgraded::some(ExclusiveLease<Tag, Brand>{std::move(exc), *this});
    }

    friend class SharedPermissionGuard<Tag, Brand>;
    friend class detail::ChannelPoolDoor;

    // Holds a value exactly when the exclusive-out bit is clear.
    std::optional<Permission<Tag, Brand>> parked_;

    // The count of the shares and the exclusive-out bit.  It is mutable, so a
    // guard gives its share back through its pointer to const, which reaches
    // no other part of the pool.
    alignas(64) mutable ::foundation::core::Atomic<std::uint64_t> state_;
};

template <typename Tag, typename Brand>
SharedPermissionPool(Permission<Tag, Brand>&&) -> SharedPermissionPool<Tag, Brand>;

// Acquire-release, so the compare-exchange in try_upgrade observes this
// decrement.
template <typename Tag, typename Brand>
inline SharedPermissionGuard<Tag, Brand>::~SharedPermissionGuard() {
    if (pool_ != nullptr) {
        static_cast<void>(pool_->state_.fetch_sub_acq_rel(std::uint64_t{1}));
    }
}

// Converts an exclusive permission into an untracked share.  With no
// pool involved nothing counts the copies, so the exclusive can never be
// recovered.  This fits a one-shot share to a task that cannot outlive
// the caller.
template <typename... Args>
    requires PermissionShareArgs<Args...>
[[nodiscard]] constexpr SharedPermission<detail::perm_tag_t<Args...[sizeof...(Args) - 1]>,
                                         detail::perm_brand_t<Args... [sizeof...(Args) - 1]>>
mint_permission_share(Args&&...) noexcept {
    using Tag = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    using Brand = detail::perm_brand_t<Args...[sizeof...(Args) - 1]>;
    // With a context, the fit concept proves the row of the tag.  Without
    // one, the facts read the row one time.
    constexpr detail::rows_facts facts =
        detail::leading_ctx_count({^^Args...}) == 0 ? detail::rows_facts_of({^^Tag}) : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^Tag), "mint_permission_share: the tag declares no effect row.  Declare an "
                                                 "edge in foundation::permissions::permission_rows or a "
                                                 "permission_row member on the tag.");
    } else if constexpr (!facts.empty) {
        static_assert(!has_permission_row(^^Tag) || detail::leading_ctx_count({^^Args...}) != 0
                          || permission_row_empty(^^Tag),
                      "mint_permission_share(Permission<Tag>&&) without ExecCtx is only "
                      "valid for permission_row<Tag> == Row<>.  Effectful permission tags "
                      "must use mint_permission_share(ctx, Permission<Tag>&&).");
    }
    return SharedPermission<Tag, Brand>{};
}

// Runs the body under a lent share.  A body that returns a value comes
// back in an optional, empty when the lend failed.  A separate
// declaration serves a void-returning body, because there is no
// optional<void> to carry the lend-failed case: the bool says whether
// the body ran, and that declaration carries no [[nodiscard]], as its
// two predecessors did not.  The call leads with a context, the pool is
// next, and the body is last.  One shared body serves both.

namespace detail {
    // True when the argument list is a context, a pool as an lvalue, and a
    // body that takes a share of the pool, and the context fits the lend of
    // the pool (PoolCtx).  The test is a function that is not a template, so
    // no translation unit can specialize it to lend from a class that is no
    // pool.  Its lists are std::vector copies.  A ReflectionList here costs
    // each includer about 16 M instructions, and std::meta::substitute
    // takes no ReflectionList.
    [[nodiscard]] consteval bool with_shared_read_shaped(std::initializer_list<std::meta::info> arguments) {
        const std::vector<std::meta::info> args(arguments);
        const std::size_t n_ctx = leading_ctx_count(arguments);
        if (n_ctx != 1 || args.size() != n_ctx + 2) return false;
        const std::meta::info pool = std::meta::dealias(std::meta::remove_cvref(args[n_ctx]));
        if (!std::meta::has_template_arguments(pool) || std::meta::template_of(pool) != ^^SharedPermissionPool) {
            return false;
        }
        if (!std::meta::is_lvalue_reference_type(args[n_ctx])) return false;
        const std::vector<std::meta::info> pool_arguments = std::meta::template_arguments_of(pool);
        const std::meta::info token = std::meta::substitute(^^SharedPermission, pool_arguments);
        if (!std::meta::is_invocable_type(args[n_ctx + 1], {token})) return false;
        return std::meta::extract<bool>(
            std::meta::substitute(^^PoolCtx, {pool_arguments[0], std::meta::remove_cvref(args[0])}));
    }

    template <typename... Args>
    using shared_read_pool_t = std::remove_cvref_t<Args...[sizeof...(Args) - 2]>;

    template <typename... Args>
    using shared_read_token_t = SharedPermission<typename shared_read_pool_t<Args...>::tag_type,
                                                 typename shared_read_pool_t<Args...>::brand_type>;

    template <typename... Args>
    using shared_read_result_t = std::invoke_result_t<Args...[sizeof...(Args) - 1], shared_read_token_t<Args...>>;

    template <typename... Args>
    inline constexpr bool shared_read_nothrow_v =
        std::is_nothrow_invocable_v<Args...[sizeof...(Args) - 1], shared_read_token_t<Args...>>;

    template <typename... Args>
    auto with_shared_read_(Args && ... args) noexcept(shared_read_nothrow_v<Args...>) {
        using Pool = shared_read_pool_t<Args...>;
        using Body = Args...[sizeof...(Args) - 1];
        using Result = shared_read_result_t<Args...>;
        auto forwarded = std::forward_as_tuple(std::forward<Args>(args)...);
        Pool& pool = std::get<sizeof...(Args) - 2>(forwarded);
        Body&& body = std::get<sizeof...(Args) - 1>(std::move(forwarded));
        auto lent = pool.lend(std::get<0>(forwarded));
        if (lent.is_none()) {
            if constexpr (std::is_void_v<Result>) {
                return false;
            } else {
                return ::foundation::core::Option<Result>{::foundation::core::none};
            }
        }
        // The guard of the lent share moves to a local, which lives until the
        // return.  The body runs in this frame, so no closure captures it.
        SharedPermissionGuard<typename Pool::tag_type, typename Pool::brand_type> const guard =
            std::move(lent).expect("the pool lent a share, as the test above shows");
        if constexpr (std::is_void_v<Result>) {
            std::forward<Body>(body)(guard.token());
            return true;
        } else {
            return ::foundation::core::Option<Result>::some(std::forward<Body>(body)(guard.token()));
        }
    }

}  // namespace detail

template <typename... Args>
concept WithSharedReadArgs = detail::with_shared_read_shaped({^^Args...});

template <typename... Args>
    requires WithSharedReadArgs<Args...> && (!std::is_void_v<detail::shared_read_result_t<Args...>>)
[[nodiscard]] ::foundation::core::Option<detail::shared_read_result_t<Args...>>
with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
}

template <typename... Args>
    requires WithSharedReadArgs<Args...> && std::is_void_v<detail::shared_read_result_t<Args...>>
bool with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
}

// The row-hash identities of the guard, the pool and the lease.  The identity
// of the exclusive token is in foundation/permissions/Permission.h.
namespace row_discipline {
struct shared_guard;
struct shared_pool;
struct exclusive_lease;
}  // namespace row_discipline

}  // namespace foundation::permissions

// The tag is the region an instance owns, so it stays out of the hash.
// What the tag implies does fold: a share over a region whose row names IO
// is a different claim from a share over a pure region.
namespace foundation::diag {

// A share grades on the order dual of the fractional lattice, and its
// published value type is its tag, a bare type that contributes zero.
// Through the published shape alone, a share over an IO region and a
// share over a pure region were one claim.  This specialization keeps the
// graded fold and takes the tag's row as the payload, as the three
// carriers below do.
template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::SharedPermission<Tag, Brand>> {
    using share_type = ::foundation::permissions::SharedPermission<Tag, Brand>;
    static constexpr std::uint64_t value =
        graded_row_hash_v<share_type::modality, typename share_type::lattice_type,
                          ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::SharedPermissionGuard<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::shared_guard,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::SharedPermissionPool<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::shared_pool,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::ExclusiveLease<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::exclusive_lease,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

}  // namespace foundation::diag
