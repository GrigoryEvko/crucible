#pragma once

// The read borrow of the permission family, alongside the exclusive
// token and the pooled share.  It carries no runtime state, because it
// needs none: the borrow is scoped to a call, so the permission it was
// minted from necessarily outlives it and there is nothing to count.  A
// borrow that escapes to a consumer which may outlive the producer, or
// one that has to be turned back into an exclusive, wants the pooled
// share instead.

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace crucible::safety {

template <typename Tag>
class ReadView;
template <typename Tag>
[[nodiscard]] constexpr ReadView<Tag> mint_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept;

}  // namespace crucible::safety

namespace crucible::safety::proto {
template <typename T, typename Tag>
struct Borrowed;
}  // namespace crucible::safety::proto

namespace crucible::safety {

template <typename Tag>
class [[nodiscard]] ReadView {
public:
    using tag_type = Tag;

    constexpr ReadView(const ReadView&) noexcept = default;
    constexpr ReadView(ReadView&&) noexcept = default;

    ReadView& operator=(const ReadView&) = delete(
        "ReadView is single-binding; rebinding hides lifetime relationships — construct a fresh view via mint_read_view");
    ReadView& operator=(ReadView&&) = delete("ReadView is single-binding; rebinding hides lifetime relationships");

    ~ReadView() = default;

    static void* operator new(std::size_t) =
        delete("ReadView must live on the stack; heap allocation defeats the lifetime contract");
    static void* operator new[](std::size_t) = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void* operator new(std::size_t, std::align_val_t) = delete("ReadView must live on the stack");
    static void* operator new[](std::size_t,
                                std::align_val_t) = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

private:
    // A borrow proof that can be conjured from nothing is not a proof, so
    // only the issuers below reach this.  Copy and move stay public, which
    // closes construction from nothing without hindering an already minted
    // view being carried around.
    constexpr ReadView() noexcept = default;

    // Every entry in the friend list below is another way to mint a
    // borrow.  Additions need review.

    friend constexpr ReadView<Tag> mint_read_view<Tag>(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept;

    // The session-layer borrow payload embeds a view as the recipient's
    // read proof for one protocol step, and default-constructs it in its
    // aggregate constructor.  What makes that sound is the protocol's own
    // accounting of permissions, not this construction.
    template <typename T, typename FriendTag>
    friend struct ::crucible::safety::proto::Borrowed;
};

// The lifetime bound the parameter's attribute announces is not
// enforced.  The attribute macro expands to nothing on this compiler, so
// a view minted from a temporary permission compiles and then dangles.
// Review is what catches it.

template <typename Tag>
[[nodiscard]] constexpr ReadView<Tag> mint_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept {
    (void)p;
    return ReadView<Tag>{};
}

template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, ReadView<Tag>>
[[nodiscard]] constexpr auto with_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND,
                                            Body&& body) noexcept(std::is_nothrow_invocable_v<Body, ReadView<Tag>>)
    -> std::invoke_result_t<Body, ReadView<Tag>> {
    return body(mint_read_view(p));
}

namespace detail {
struct read_view_test_tag {};
}  // namespace detail

static_assert(sizeof(ReadView<detail::read_view_test_tag>) == 1, "ReadView<Tag> must be a 1-byte empty class");

static_assert(std::is_trivially_copyable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must be trivially copyable (zero-cost copy)");
static_assert(std::is_trivially_destructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> destructor must be trivial");

static_assert(std::is_copy_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be copy-constructible (multi-reader semantics)");
static_assert(!std::is_copy_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be copy-assignable (single-binding)");
static_assert(std::is_move_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be move-constructible");
static_assert(!std::is_move_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be move-assignable (single-binding)");

}  // namespace crucible::safety
