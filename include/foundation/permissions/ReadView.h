#pragma once

// The read borrow of the permission family, alongside the exclusive
// token and the pooled share.  It carries no runtime state, because it
// needs none: the borrow is scoped to a call, so the permission it was
// minted from necessarily outlives it and there is nothing to count.  A
// borrow that escapes to a consumer which may outlive the producer, or
// one that has to be turned back into an exclusive, wants the pooled
// share instead.
//
// Old spelling: include/crucible/permissions/ReadView.h, namespace
// crucible::safety.

#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::permissions {

template <typename Tag>
class ReadView;
template <typename Tag>
[[nodiscard]] constexpr ReadView<Tag> mint_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept;

// The twin that makes the lifetime bound above a rule rather than a
// claim.  A const lvalue reference binds a temporary, so without this
// overload `mint_read_view(mint_permission_root<Tag>())` compiled and
// handed back a borrow proof for a permission that died at the end of
// the statement.  That was measured, not suspected.  The rvalue
// reference is the better match for a prvalue, so the call now names a
// deleted function instead.
template <typename Tag>
constexpr ReadView<Tag> mint_read_view(Permission<Tag> const&&) =
    delete("a borrow proof minted from a temporary permission outlives what it proves; bind the permission to "
           "a name that outlives the view");

// The session layer's borrow payload embeds a view as the recipient's
// read proof for one protocol step and default-constructs it, which its
// own accounting of permissions makes sound.  That layer is above this
// one and cannot be named here, so it reaches the private constructor
// through this host type, which it declares and defines.
namespace host {
struct BorrowIssuer;
}  // namespace host

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

    // The session layer's borrow payload default-constructs a view
    // through this host type; see the declaration above.
    friend struct ::foundation::permissions::host::BorrowIssuer;
};

// The annotation on the parameter is the claim.  The deleted twin
// declared beside the first declaration is what enforces it, because no
// compiler this project builds with honours a lifetime attribute.

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

// The same twin for the scoped form.  The body runs while the view is
// alive, so a temporary permission survives the call, but the view the
// body receives proves a permission that is gone the moment the
// statement ends, and a body that stores the view keeps the proof.
template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, ReadView<Tag>>
constexpr auto with_read_view(Permission<Tag> const&&, Body&&) =
    delete("a borrow proof minted from a temporary permission outlives what it proves; bind the permission to "
           "a name that outlives the call");

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

}  // namespace foundation::permissions
