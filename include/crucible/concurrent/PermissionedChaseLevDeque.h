#pragma once

// A work-stealing deque behind one linear owner permission and
// fractional thief shares.  The handle types carry the role, so a thief
// cannot push or pop the bottom.
//
// The deque's memory-ordering argument holds only when the owner is
// exactly one thread, and the bare deque cannot say so: its bottom-side
// methods are callable from anywhere, and two threads pushing at once
// race silently with no diagnostic.  A linear owner permission makes
// the second owner a compile error instead.
//
// The thief side is fractional because stealing is already safe under
// concurrent thieves, and their count is what lets the deque be drained
// for a reset or a migration.
//
// Each deque needs a UserTag of its own.  Two deques sharing a tag
// share Permission types, and their endpoints become interchangeable.
// Mint each owner root once per program: nothing checks that at
// runtime.

#include <crucible/Platform.h>
#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/WorkingSet.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/_Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

namespace deque_tag {

template <typename UserTag>
struct Whole {};
template <typename UserTag>
struct Owner {};
template <typename UserTag>
struct Thief {};

}  // namespace deque_tag

// The session-typed facade for this tag tree stays in the session
// layer.  Putting it here would make this header depend on that layer
// while that layer already depends on this one.

template <DequeValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedChaseLevDeque : public safety::Pinned<PermissionedChaseLevDeque<T, Capacity, UserTag>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using whole_tag = deque_tag::Whole<UserTag>;
    using owner_tag = deque_tag::Owner<UserTag>;
    using thief_tag = deque_tag::Thief<UserTag>;

    static constexpr std::size_t deque_capacity = Capacity;

    // The thief root is minted here rather than passed in, because the
    // pool is the root of trust for its own tag.  The caller mints and
    // keeps the owner permission.

    PermissionedChaseLevDeque() noexcept : thief_pool_{safety::mint_permission_root<thief_tag>()} {}

    // A reference rather than a pointer, because a handle binds to one
    // deque for life.  The reference also deletes move assignment,
    // which matters: a defaulted move of an empty Permission is a
    // no-op, so the source and the target would both go on claiming the
    // linear token.

    class OwnerHandle {
        PermissionedChaseLevDeque& deque_;
        [[no_unique_address]] safety::Permission<owner_tag> perm_;

        constexpr OwnerHandle(PermissionedChaseLevDeque& d, safety::Permission<owner_tag>&& p) noexcept
            : deque_{d}, perm_{std::move(p)} {}
        friend class PermissionedChaseLevDeque;

    public:
        using value_type = T;
        using tag_type = owner_tag;
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;

        OwnerHandle(const OwnerHandle&) = delete(
            "OwnerHandle owns the linear Owner Permission — copy would duplicate the token, allowing two threads to race on push_bottom/pop_bottom (data race on bottom_)");
        OwnerHandle& operator=(const OwnerHandle&) =
            delete("OwnerHandle owns the linear Owner Permission — assignment would overwrite the linear token");
        constexpr OwnerHandle(OwnerHandle&&) noexcept = default;
        OwnerHandle& operator=(OwnerHandle&&) = delete(
            "OwnerHandle binds to ONE deque for life — rebinding would orphan the original Permission and silently allow a second owner to coexist (CL's push_bottom/pop_bottom is single-owner-only)");

        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept { return deque_.deque_.push_bottom(item); }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return deque_.deque_.pop_bottom(); }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] std::size_t size_approx() const noexcept { return deque_.deque_.size_approx(); }
        [[nodiscard]] bool empty_approx() const noexcept { return deque_.deque_.empty_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    };

    // Holds a pool share for its whole lifetime and gives it back on
    // destruction.

    class ThiefHandle {
        PermissionedChaseLevDeque* deque_ = nullptr;
        safety::SharedPermissionGuard<thief_tag> guard_;

        constexpr ThiefHandle(PermissionedChaseLevDeque& d, safety::SharedPermissionGuard<thief_tag>&& g) noexcept
            : deque_{&d}, guard_{std::move(g)} {}
        friend class PermissionedChaseLevDeque;

    public:
        using value_type = T;
        using tag_type = thief_tag;
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;

        ThiefHandle(const ThiefHandle&) =
            delete("ThiefHandle owns a thief-pool refcount share — copy would double-count");
        ThiefHandle& operator=(const ThiefHandle&) =
            delete("ThiefHandle owns a thief-pool refcount share — assignment would double-count");
        constexpr ThiefHandle(ThiefHandle&&) noexcept = default;
        // Move assignment stays deleted, because the share's lifetime
        // is fixed at construction.

        [[nodiscard, gnu::hot]] std::optional<T> try_steal() noexcept { return deque_->deque_.steal_top(); }

        [[nodiscard]] std::size_t size_approx() const noexcept { return deque_->deque_.size_approx(); }
        [[nodiscard]] bool empty_approx() const noexcept { return deque_->deque_.empty_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr safety::SharedPermission<thief_tag> token() const noexcept { return guard_.token(); }
    };

    [[nodiscard]] constexpr OwnerHandle owner(safety::Permission<owner_tag>&& perm) noexcept {
        return OwnerHandle{*this, std::move(perm)};
    }

    // Lends a pool share, and refuses while an exclusive transition is
    // in flight.

    [[nodiscard]] std::optional<ThiefHandle> thief() noexcept {
        auto guard = thief_pool_.lend();
        if (!guard) return std::nullopt;
        return ThiefHandle{*this, std::move(*guard)};
    }

    // Runs the body with every thief out.  The owner permission is
    // linear and independent, so this leaves the owner side alone and
    // covers only transitions that do not involve it.  Returns false
    // when thieves were still out and the body did not run.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = thief_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        thief_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_thieves() const noexcept { return thief_pool_.outstanding(); }
    [[nodiscard]] bool is_exclusive_active() const noexcept { return thief_pool_.is_exclusive_out(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return deque_.size_approx(); }
    [[nodiscard]] bool empty_approx() const noexcept { return deque_.empty_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    ChaseLevDeque<T, Capacity> deque_;
    safety::SharedPermissionPool<thief_tag> thief_pool_;
};

}  // namespace crucible::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::deque_tag::Whole<UserTag>, concurrent::deque_tag::Owner<UserTag>,
                   concurrent::deque_tag::Thief<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_authoring_witness<concurrent::deque_tag::Whole<UserTag>, concurrent::deque_tag::Owner<UserTag>,
                                     concurrent::deque_tag::Thief<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack_authoring_witness<concurrent::deque_tag::Whole<UserTag>, concurrent::deque_tag::Owner<UserTag>,
                                          concurrent::deque_tag::Thief<UserTag>> : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::deque_tag::Whole<UserTag>, concurrent::deque_tag::Owner<UserTag>,
                        concurrent::deque_tag::Thief<UserTag>> : std::true_type {};

}  // namespace crucible::safety
