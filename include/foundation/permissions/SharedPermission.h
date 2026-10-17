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
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <atomic>
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

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

// The destructor gives the share back to the pool, so the end of the scope
// of a guard is its named door.
template <typename Tag, typename Brand>
class [[nodiscard]][[= ::foundation::linear::scope_end_door{}]] SharedPermissionGuard {
    SharedPermissionPool<Tag, Brand>* pool_ = nullptr;

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
    // succeeds, and the pool aborts when it ends.  The std::optional that
    // lend returns constructs the guard in place, so it stays admitted.
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
    // is about to die: `(*pool.lend()).token()` would hand back a proof
    // of a share released at the end of the statement.  No annotation
    // can be written on the implicit object parameter at all, so the
    // qualifier carries the claim instead.
    //
    // Measured: `const&` alone does NOT close this, because a const
    // lvalue reference binds an rvalue, so a const-ref-qualified member
    // is callable on a temporary.  Only the deleted `const&&` twin
    // refuses it, which is the same mechanism the borrow factories use.
    //
    // One route stays open and is stated rather than hidden:
    // `pool.lend()->token()` calls through optional's operator->, which
    // yields a pointer, so the object expression is an lvalue and no
    // qualifier on this member can see the temporary behind it.
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

// A lend found the share count at its maximum.  The body is in
// src/foundation/SharedPermission.cpp: it prints the text of the catalog
// tag SharedPermissionPoolSaturated, and only that file reads the tag.
[[noreturn]] CRUCIBLE_COLD void shared_permission_pool_saturated_abort_() noexcept;

// Each guard keeps a pointer into its pool, and its destructor writes
// the pool's count.  A pool that ends while a guard lives would take that
// write into freed memory, so the pool refuses to end while a share is
// out.  The body is in src/foundation/SharedPermission.cpp.
[[noreturn]] CRUCIBLE_COLD void shared_permission_pool_outlived_abort_() noexcept;

// The context a pool operation may run in: none, which is the token
// form and asks the tag's row to be empty in the body, or one that
// admits the tag's row.
namespace detail {
struct no_ctx {};
}  // namespace detail

template <typename Tag, typename Ctx>
concept PoolCtx = std::same_as<Ctx, detail::no_ctx> || CtxAdmitsPermission<Tag, Ctx>;

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
        : parked_{std::move(exc)}, state_{0} {}

    ~SharedPermissionPool() {
        if ((state_.load(std::memory_order_acquire) & COUNT_MASK) != 0) [[unlikely]] {
            shared_permission_pool_outlived_abort_();
        }
    }

    template <typename Ctx = detail::no_ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag, Brand>> lend(Ctx const& = {}) noexcept {
        static_assert(!std::same_as<Ctx, detail::no_ctx> || permission_row_empty(^^Tag),
                      "SharedPermissionPool<Tag>::lend() without ExecCtx is only valid "
                      "for permission_row<Tag> == Row<>.  Effectful permission tags "
                      "must use lend(ctx).");
        return lend_raw_();
    }

    // The mode transition has to be indivisible.  Reading a plain count
    // of zero and then taking the parked permission loses to a lend that
    // increments in between, leaving two holders of one region.  Folding
    // the count and the upgraded-out flag into one word makes the test
    // and the claim a single compare-exchange: a lend that was about to
    // succeed retries and then fails on the flag, and a lend that
    // already incremented makes this compare-exchange fail.
    template <typename Ctx = detail::no_ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<Permission<Tag, Brand>> try_upgrade(Ctx const& = {}) noexcept {
        static_assert(!std::same_as<Ctx, detail::no_ctx> || permission_row_empty(^^Tag),
                      "SharedPermissionPool<Tag>::try_upgrade() without ExecCtx is only "
                      "valid for permission_row<Tag> == Row<>.  Effectful permission "
                      "tags must use try_upgrade(ctx).");
        return try_upgrade_raw_();
    }

    // No postcondition: an upgrade on another thread can take the parked
    // permission before a witness reads it.
    void deposit_exclusive(Permission<Tag, Brand>&& exc) noexcept {
        CRUCIBLE_PRE(!parked_.has_value());
        parked_ = std::move(exc);
        // The count is zero whenever the exclusive-out bit is set, so a
        // plain store of zero clears the bit without discarding a count.
        // Release, so a later lend sees the freshly parked permission.
        state_.store(0, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t outstanding() const noexcept {
        return state_.load(std::memory_order_acquire) & COUNT_MASK;
    }

    [[nodiscard]] bool is_exclusive_out() const noexcept {
        return (state_.load(std::memory_order_acquire) & EXCLUSIVE_OUT_BIT) != 0;
    }

private:
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag, Brand>> lend_raw_() noexcept {
        std::uint64_t observed = state_.load(std::memory_order_acquire);
        for (;;) {
            if (observed & EXCLUSIVE_OUT_BIT) [[unlikely]] {
                return std::nullopt;
            }
            if ((observed & COUNT_MASK) == COUNT_MASK) [[unlikely]] {
                shared_permission_pool_saturated_abort_();
            }
            const std::uint64_t desired = observed + std::uint64_t{1};
            // Acquire-release, so this count update synchronizes with
            // the compare-exchange in try_upgrade.
            if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return SharedPermissionGuard<Tag, Brand>{*this};
            }
        }
    }

    [[nodiscard]] std::optional<Permission<Tag, Brand>> try_upgrade_raw_() noexcept {
        std::uint64_t expected = 0;
        if (!state_.compare_exchange_strong(expected, EXCLUSIVE_OUT_BIT, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return std::nullopt;
        }
        // The parked permission is populated whenever the exclusive-out
        // bit is clear, and the exchange above just moved that bit from
        // clear to set, so the dereference here cannot be empty.
        Permission<Tag, Brand> exc = std::move(*parked_);
        parked_.reset();
        return exc;
    }

    friend class SharedPermissionGuard<Tag, Brand>;

    // Holds a value exactly when the exclusive-out bit is clear.
    std::optional<Permission<Tag, Brand>> parked_;

    alignas(64) std::atomic<std::uint64_t> state_;
};

template <typename Tag, typename Brand>
SharedPermissionPool(Permission<Tag, Brand>&&) -> SharedPermissionPool<Tag, Brand>;

// Acquire-release, so the compare-exchange in try_upgrade observes this
// decrement.
template <typename Tag, typename Brand>
inline SharedPermissionGuard<Tag, Brand>::~SharedPermissionGuard() {
    if (pool_ != nullptr) {
        pool_->state_.fetch_sub(std::uint64_t{1}, std::memory_order_acq_rel);
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
// two predecessors did not.  The call leads with a context or with the
// pool, and the body is last; one shared body serves both.

namespace detail {
    // True when the argument list is an optional context, a pool as an
    // lvalue, and a body that takes a share of the pool, and the context,
    // when there is one, admits the row of the tag.  The test is a
    // function that is not a template, so no translation unit can
    // specialize it to lend from a class that is no pool.
    [[nodiscard]] consteval bool with_shared_read_shaped(std::initializer_list<std::meta::info> arguments) {
        const std::vector<std::meta::info> args(arguments);
        const std::size_t n_ctx = leading_ctx_count(arguments);
        if (n_ctx > 1 || args.size() != n_ctx + 2) return false;
        const std::meta::info pool = std::meta::dealias(std::meta::remove_cvref(args[n_ctx]));
        if (!std::meta::has_template_arguments(pool) || std::meta::template_of(pool) != ^^SharedPermissionPool) {
            return false;
        }
        if (!std::meta::is_lvalue_reference_type(args[n_ctx])) return false;
        const std::vector<std::meta::info> pool_arguments = std::meta::template_arguments_of(pool);
        const std::meta::info token = std::meta::substitute(^^SharedPermission, pool_arguments);
        if (!std::meta::is_invocable_type(args[n_ctx + 1], {token})) return false;
        if (n_ctx == 0) return true;
        return std::meta::extract<bool>(
            std::meta::substitute(^^CtxAdmitsPermission, {pool_arguments[0], std::meta::remove_cvref(args[0])}));
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
        auto guard_opt = [&] {
            if constexpr (leading_ctx_count({^^Args...}) == 1) {
                return pool.lend(std::get<0>(forwarded));
            } else {
                return pool.lend();
            }
        }();
        if constexpr (std::is_void_v<Result>) {
            if (!guard_opt) return false;
            std::forward<Body>(body)(guard_opt->token());
            return true;
        } else {
            if (!guard_opt) return std::optional<Result>{std::nullopt};
            return std::optional<Result>{std::forward<Body>(body)(guard_opt->token())};
        }
    }

}  // namespace detail

template <typename... Args>
concept WithSharedReadArgs = detail::with_shared_read_shaped({^^Args...});

template <typename... Args>
    requires WithSharedReadArgs<Args...> && (!std::is_void_v<detail::shared_read_result_t<Args...>>)
[[nodiscard]] std::optional<detail::shared_read_result_t<Args...>>
with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
}

template <typename... Args>
    requires WithSharedReadArgs<Args...> && std::is_void_v<detail::shared_read_result_t<Args...>>
bool with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
}

// The row-hash identities of the guard and the pool.  The identity of the
// exclusive token is in foundation/permissions/Permission.h.
namespace row_discipline {
struct shared_guard;
struct shared_pool;
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

}  // namespace foundation::diag
