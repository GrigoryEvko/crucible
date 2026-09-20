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
//
// Old spelling: include/crucible/safety/ScopedView.h.

#include <fixy/Qtt.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <meta>
#include <new>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename Carrier, typename Tag>
class ScopedView;

// The struct form stays because callers read `::value` off it. The
// answer is the one reflection query in foundation/reflect/Instance.h.
template <typename T>
struct is_scoped_view : std::bool_constant<::foundation::reflect::is_instance_of_v<T, ^^ScopedView>> {};

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

    // The twin that makes the bound above a rule.  A const lvalue
    // reference binds a temporary, so the view would hold a pointer to
    // a carrier that died at the end of the statement.  The rvalue
    // reference is the better match for a prvalue, so such a call names
    // a deleted function instead.
    explicit ScopedView(Carrier const&&) =
        delete("a view over a temporary carrier outlives it; bind the carrier to a name that outlives the view");

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

// Measured, not suspected: mint_view<Ready>(Carrier{}) compiled and
// handed back a view of a carrier that was gone at the end of the
// statement.  This twin is what refuses it.  The deduced parameter is a
// const rvalue reference rather than a forwarding reference, so a
// non-const lvalue carrier still reaches the factory above.
template <typename Tag, typename Carrier>
constexpr ScopedView<Carrier, Tag> mint_view(Carrier const&&) =
    delete("a view over a temporary carrier outlives it; bind the carrier to a name that outlives the view");

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
    return mint_linear<ScopedView<Carrier, Tag>>(mint_view<Tag>(c));
}

// The linear form carries its view further than the scoped one does, so
// it needs the same twin and needs it more.
template <typename Tag, typename Carrier>
constexpr LinearScopedView<Carrier, Tag> mint_linear_view(Carrier const&&) =
    delete("a view over a temporary carrier outlives it; bind the carrier to a name that outlives the view");

template <typename T>
consteval bool contains_scoped_view();

namespace detail {

// The audit used to open eight container shapes by name — optional,
// vector, array, unique_ptr, shared_ptr, weak_ptr, a C array, Linear —
// and three product shapes — pair, tuple, variant — through partial
// specializations that named each one's payload.  The walk below
// reaches every one of them without a list: a base class is walked
// like a member, a union like a class, an array through its element,
// and the reflection reads private members.  optional and variant keep
// their payload in a union inside a base; tuple keeps its elements in
// a chain of bases; array and a C array are arrays; Linear keeps its
// payload in a private member; the pointer-holding containers are
// covered by the associated-type rule below.
//
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
//
// The Visited pack is the set of classes already open on the walk.  A
// type cannot hold itself by value, so the member walk alone always
// terminates, but the associated-type rule can point back at an
// enclosing class — `struct Node { std::vector<Node> children; }` —
// and a second visit of one class answers nothing the first did not.
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

template <typename T, typename... Visited>
consteval bool contains_scoped_view_();

template <typename T, typename... Visited>
consteval bool associated_contains_view() {
    bool found = false;
    if constexpr (sv_has_associated_value<T>) {
        if (contains_scoped_view_<typename T::value_type, Visited...>()) found = true;
    }
    if constexpr (sv_has_associated_element<T>) {
        if (contains_scoped_view_<typename T::element_type, Visited...>()) found = true;
    }
    return found;
}

template <typename T, typename... Visited>
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
    bool found = false;
    static constexpr auto bases = std::define_static_array(bases_of(^^T, ctx));
    static constexpr auto members = std::define_static_array(nonstatic_data_members_of(^^T, ctx));
    // -Wshadow fires on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto b : bases) {
        using B = typename[:type_of(b):];
        if (contains_scoped_view_<B, Visited...>()) found = true;
    }
    template for (constexpr auto m : members) {
        using F = typename[:type_of(m):];
        if (contains_scoped_view_<F, Visited...>()) found = true;
    }
#pragma GCC diagnostic pop
    return found;
}

template <typename T, typename... Visited>
consteval bool contains_scoped_view_() {
    using U = std::remove_cvref_t<T>;
    if constexpr ((std::is_same_v<U, Visited> || ...)) {
        return false;
    } else if constexpr (is_scoped_view_v<U>) {
        return true;
    } else if constexpr (std::is_array_v<U>) {
        return contains_scoped_view_<std::remove_all_extents_t<U>, Visited...>();
    } else if constexpr ((std::is_class_v<U> || std::is_union_v<U>) && sv_complete<U>) {
        return reflect_contains_view<U, U, Visited...>() || associated_contains_view<U, U, Visited...>();
    } else {
        return false;
    }
}

}  // namespace detail

template <typename T>
consteval bool contains_scoped_view() {
    return detail::contains_scoped_view_<T>();
}

// The audit is opt-in: a carrier that never writes this static_assert
// is never walked, and that omission is the one back door the audit
// has.  Write it once per carrier, next to the carrier's definition.
template <typename T>
consteval bool no_scoped_view_field_check() {
    static_assert(!contains_scoped_view<T>(), "Type contains a fixy::ScopedView<> in some field. "
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

static_assert(is_scoped_view_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(is_scoped_view_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag> const&>);
static_assert(!is_scoped_view_v<detail::sv_test_carrier>);
static_assert(!is_scoped_view_v<void>);

}  // namespace fixy
