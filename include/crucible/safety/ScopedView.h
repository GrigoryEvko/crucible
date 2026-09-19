#pragma once

// Non-owning typed reference to a Carrier, proving the Carrier is in
// the state denoted by Tag.  The Carrier never moves, so it stays
// non-movable; only the view travels.
//
// A view is checked once, where it is minted.  Every method that takes
// one as proof of state performs no further check.
//
// Copy construction is allowed, so several read-only views of one
// carrier may coexist.  Assignment is not, because reassigning a view
// hides the state transition that should have re-minted it.
//
// A carrier opts into the field audit by writing a static_assert on
// no_scoped_view_field_check for its own type.

#include <crucible/Platform.h>
#include <crucible/safety/_Linear.h>

#include <array>
#include <cstddef>
#include <memory>
#include <meta>
#include <new>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace crucible::safety {

template <typename Carrier, typename Tag>
class ScopedView;

template <typename T>
struct is_scoped_view : std::false_type {};

template <typename Carrier, typename Tag>
struct is_scoped_view<ScopedView<Carrier, Tag>> : std::true_type {};

template <typename T>
inline constexpr bool is_scoped_view_v = is_scoped_view<std::remove_cvref_t<T>>::value;

template <typename Carrier, typename Tag>
class [[nodiscard]] ScopedView {
    // The pointer is const because the view is proof, not a handle.  A
    // method that mutates the carrier already holds its own reference.
    // Holding a const pointer is also what lets a const member
    // function mint a view of itself.
    Carrier const* ptr_;

    constexpr explicit ScopedView(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept : ptr_{&c} {}

    template <typename Tag_, typename Carrier_>
    friend constexpr ScopedView<Carrier_, Tag_> mint_view(Carrier_ const& c CRUCIBLE_LIFETIMEBOUND) noexcept;

public:
    using carrier_type = Carrier;
    using tag_type = Tag;

    // The private converting constructor already suppresses the
    // default one.  The explicit delete is here for its message: a
    // string this project controls survives a compiler upgrade, where
    // the compiler's own wording does not, and the negative-compile
    // tests match on it.
    ScopedView() = delete(
        "ScopedView default-construction is meaningless — every view must witness a specific Carrier instance; use mint_view<Tag>(carrier) at the construction site");

    constexpr ScopedView(const ScopedView&) noexcept = default;
    constexpr ScopedView(ScopedView&&) noexcept = default;
    ScopedView&
    operator=(const ScopedView&) = delete("ScopedView is single-binding; assignment hides state transitions");
    ScopedView& operator=(ScopedView&&) = delete("ScopedView is single-binding; assignment hides state transitions");
    constexpr ~ScopedView() = default;

    [[nodiscard]] constexpr Carrier const* operator->() const noexcept { return ptr_; }
    [[nodiscard]] constexpr Carrier const& carrier() const noexcept { return *ptr_; }

    // Without these deletes a caller could reach the public copy
    // constructor through `new` and escape the stack frame.  The
    // placement-new overload stays available, because optional,
    // variant and any bump-pointer allocator that stores a view inline
    // need it, and those storage sites still face the field audit.
    static void* operator new(std::size_t) =
        delete("ScopedView must live on the stack; heap allocation defeats the lifetime contract");
    static void* operator new[](std::size_t) = delete("ScopedView arrays on the heap defeat the lifetime contract");
    static void* operator new(std::size_t, std::align_val_t) = delete("ScopedView must live on the stack");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("ScopedView arrays on the heap defeat the lifetime contract");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;
};

// The single point at which a state is asserted.  view_ok is found by
// argument-dependent lookup on the carrier.
//
// The gate is a precondition and not a requires-clause because view_ok
// inspects the carrier's run-time state rather than its type alone.
// One consequence matters at call sites: overload resolution and
// concepts cannot see the gate, so this factory looks callable
// everywhere and only rejects when the precondition fires.
// §XXI carve-out: rq=pre — the gate is a precondition, not a requires-clause.
template <typename Tag, typename Carrier>
[[nodiscard]] constexpr ScopedView<Carrier, Tag> mint_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept
    pre(view_ok(c, std::type_identity<Tag>{})) {
    return ScopedView<Carrier, Tag>{c};
}

// A one-shot state proof, for a transition the holder must prove the
// right to make and hands over rather than shares.  The token is gone
// after it is consumed, so the transition happens at most once.
//
// This bounds the token, not observation of the carrier: separate
// copyable views minted in parallel still read it.  Guard the
// transition method with its own state precondition to cover a view
// that has gone stale.
template <typename Carrier, typename Tag>
using LinearScopedView = Linear<ScopedView<Carrier, Tag>>;

// §XXI carve-out: rq=pre — the gate is a precondition, not a requires-clause.
template <typename Tag, typename Carrier>
[[nodiscard]] constexpr LinearScopedView<Carrier, Tag>
mint_linear_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept pre(view_ok(c, std::type_identity<Tag>{})) {
    return LinearScopedView<Carrier, Tag>{mint_view<Tag>(c)};
}

template <typename T>
consteval bool contains_scoped_view();

namespace detail {

template <typename T>
struct sv_unwrap_single {
    using type = void;
};
template <typename X>
struct sv_unwrap_single<std::optional<X>> {
    using type = X;
};
template <typename X, typename A>
struct sv_unwrap_single<std::vector<X, A>> {
    using type = X;
};
template <typename X, std::size_t N>
struct sv_unwrap_single<std::array<X, N>> {
    using type = X;
};
template <typename X, typename D>
struct sv_unwrap_single<std::unique_ptr<X, D>> {
    using type = X;
};
template <typename X>
struct sv_unwrap_single<std::shared_ptr<X>> {
    using type = X;
};
template <typename X>
struct sv_unwrap_single<std::weak_ptr<X>> {
    using type = X;
};
template <typename X, std::size_t N>
struct sv_unwrap_single<X[N]> {
    using type = X;
};
// Linear stores its payload in a private member.  Since the walk moved
// to access_context::unchecked() reflection reaches that member on its
// own, so this entry is no longer the only thing standing between a
// Linear-wrapped view and the audit.  It stays because it is the
// cheaper path — one alias substitution instead of a member walk — and
// because naming the wrapper here documents that the audit deliberately
// looks through it.
template <typename X>
struct sv_unwrap_single<crucible::safety::Linear<X>> {
    using type = X;
};

template <typename T>
using sv_unwrap_single_t = typename sv_unwrap_single<T>::type;

template <typename T>
struct sv_pack_for {
    using type = void;
};
template <typename A, typename B>
struct sv_pack_for<std::pair<A, B>> {
    using type = std::tuple<A, B>;
};
template <typename... Xs>
struct sv_pack_for<std::tuple<Xs...>> {
    using type = std::tuple<Xs...>;
};
template <typename... Xs>
struct sv_pack_for<std::variant<Xs...>> {
    using type = std::tuple<Xs...>;
};

template <typename T>
using sv_pack_for_t = typename sv_pack_for<T>::type;

template <typename Tup, std::size_t... Is>
consteval bool any_contains_view_seq(std::index_sequence<Is...>) {
    return (... || contains_scoped_view<std::tuple_element_t<Is, Tup>>());
}

// Node-based and type-erased containers keep their elements behind a
// pointer or a raw byte buffer, so the reflective walk over their
// fields sees only `X*` or `unsigned char[N]` and stops there.  What
// every such container does expose is its element typedef.  Recursing
// through that typedef is a structural rule rather than an
// enumeration, so a container shape the tree has not used yet is
// covered the day someone uses it: std::deque, std::list,
// std::forward_list, std::span, std::inplace_vector, std::expected and
// the associative containers all audited clean before this.
//
// The sizeof() probe keeps an incomplete, void or reference associated
// type out, since naming it would be ill-formed and a container whose
// element type is not complete here cannot be storing a view anyway.
// The is_same guard stops a self-referential typedef recursing forever.
// nonstatic_data_members_of throws on an incomplete class, so the walk
// has to stop at one.  It could not reach one before: a pimpl keeps its
// `unique_ptr<State>` private and State forward-declared, and under
// access_context::current() the private member was invisible, so the
// incomplete State was never named.  Under unchecked() it is.
//
// Stopping there leaves the one hole this audit knowingly has: a State
// defined in a .cpp could hold a view and nothing here would see it.
// That hole is not new — the whole pimpl was invisible before — and it
// is the same shape as the existing rule that the walk does not follow
// a raw pointer.  Everything reachable by value is still audited.
template <typename T>
concept sv_complete = requires { sizeof(T); };

template <typename T>
concept sv_has_associated_value = requires {
    typename T::value_type;
    sizeof(typename T::value_type);
} && !std::is_same_v<std::remove_cv_t<typename T::value_type>, std::remove_cv_t<T>>;

template <typename T>
concept sv_has_associated_element = requires {
    typename T::element_type;
    sizeof(typename T::element_type);
} && !std::is_same_v<std::remove_cv_t<typename T::element_type>, std::remove_cv_t<T>>;

template <typename T>
consteval bool associated_contains_view() {
    bool found = false;
    if constexpr (sv_has_associated_value<T>) {
        if (contains_scoped_view<typename T::value_type>()) found = true;
    }
    if constexpr (sv_has_associated_element<T>) {
        if (contains_scoped_view<typename T::element_type>()) found = true;
    }
    return found;
}

template <typename T>
consteval bool reflect_contains_view() {
    using namespace std::meta;
    // unchecked(), not current().  access_context::current() is fixed
    // at the point it is written, which is inside this namespace, so
    // the walk saw only the members this namespace may name.  A
    // carrier that made its ScopedView field private — ordinary
    // encapsulation, not evasion — audited clean.  The audit asks a
    // structural question about layout, not an access question, so it
    // takes the context that answers the question it is asking.
    // Secret.h's policy-roster walk already uses unchecked() for the
    // same reason.
    constexpr auto ctx = access_context::unchecked();
    static constexpr auto members = std::define_static_array(nonstatic_data_members_of(^^T, ctx));
    bool found = false;
    template for (constexpr auto m : members) {
        using F = typename[:type_of(m):];
        if (contains_scoped_view<F>()) found = true;
    }
    return found;
}

}  // namespace detail

template <typename T>
consteval bool contains_scoped_view() {
    using U = std::remove_cvref_t<T>;
    if constexpr (is_scoped_view_v<U>) {
        return true;
    } else if constexpr (!std::is_void_v<detail::sv_unwrap_single_t<U>>) {
        return contains_scoped_view<detail::sv_unwrap_single_t<U>>();
    } else if constexpr (!std::is_void_v<detail::sv_pack_for_t<U>>) {
        using Tup = detail::sv_pack_for_t<U>;
        return detail::any_contains_view_seq<Tup>(std::make_index_sequence<std::tuple_size_v<Tup>>{});
    } else if constexpr (std::is_class_v<U> && !std::is_fundamental_v<U> && !std::is_pointer_v<U>) {
        if constexpr (detail::sv_complete<U>) {
            return detail::reflect_contains_view<U>() || detail::associated_contains_view<U>();
        } else {
            return false;
        }
    } else {
        return false;
    }
}

template <typename T>
consteval bool no_scoped_view_field_check() {
    static_assert(!contains_scoped_view<T>(), "Type contains a safety::ScopedView<> in some field. "
                                              "Views must not escape their construction scope; storing a "
                                              "view in a struct, container, optional, variant, etc. defeats "
                                              "the lifetime contract.");
    return true;
}

namespace detail {
struct sv_test_carrier {};
struct sv_test_tag {};
}  // namespace detail

static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag>) == sizeof(void*),
              "ScopedView<C, T> must be exactly a Carrier pointer");
static_assert(std::is_trivially_copyable_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(std::is_trivially_destructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);

}  // namespace crucible::safety
