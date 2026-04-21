#pragma once

// ── crucible::safety::ScopedView<Carrier, Tag> ──────────────────────
//
// Non-owning typed reference to a Carrier proving Carrier is in the
// state denoted by Tag.  The right type-state primitive for Crucible's
// shape — non-movable carriers, in-place state mutation, multi-site
// read access, hot-path-friendly.
//
//   Axiom coverage: TypeSafe + state-discipline (code_guide §II + §XVI).
//   Runtime cost:   sizeof(ScopedView<C, T>) == sizeof(Carrier*).
//                   Construction performs ONE contract check; methods
//                   that take a View as proof do zero further checks.
//
// Compared to Linear/Machine: doesn't require Carrier to be movable.
// The Carrier stays put; its existing `= delete("interior pointers")`
// stays.  Only the View moves around.
//
// ── Discipline (the four enforcement tiers) ─────────────────────────
//
// Tier 1 — Compiler-enforced (this header):
//   - [[nodiscard]] at class level — caller MUST capture a freshly-
//     minted view, otherwise warn-as-error.
//   - Constructor parameter Carrier& tagged CRUCIBLE_LIFETIMEBOUND so
//     -Wdangling-reference fires when a view of a local is returned
//     from the function that minted it.
//   - Constructor private + only crucible::safety::mint_view friended;
//     callers cannot fabricate views by hand.  The single chokepoint
//     is grep-discoverable (`mint_view<`).
//   - Copy/move ASSIGNMENT deleted with a reason string.  Kills both
//     "store in vector then overwrite" and "reassign to a fresh view
//     of a different state" — the two main escape patterns.
//   - Copy/move CONSTRUCTION allowed; multiple read-only views can
//     coexist (e.g. one in an optional, one passed to a callee).
//
// Tier 2 — Reflection audit (this header, see no_scoped_view_field_check):
//   - consteval helper that walks T's nonstatic data members (and
//     known wrapper types optional/vector/array/pair/tuple/variant)
//     and static_asserts that no field is a ScopedView.  Catches
//     "stored in a struct field" at compile time.
//   - Each Carrier opts in by adding
//         static_assert(crucible::safety::no_scoped_view_field_check<MyType>());
//     near the top-level declaration.
//
// Tier 4 — Negative-compile tests (test/safety/scoped_view_neg_*.cpp):
//   - CMake target with WILL_FAIL=TRUE; each .cpp exercises one of
//     the violations Tier 1+2 should catch.
//
// Tier 3 (CI grep lint) is NOT included — Tiers 1+2+4 are sufficient
// in practice; see the design discussion in misc/.

#include <crucible/Platform.h>

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

// Forward decl + trait.
template <typename Carrier, typename Tag> class ScopedView;

template <typename T>
struct is_scoped_view : std::false_type {};

template <typename Carrier, typename Tag>
struct is_scoped_view<ScopedView<Carrier, Tag>> : std::true_type {};

template <typename T>
inline constexpr bool is_scoped_view_v =
    is_scoped_view<std::remove_cvref_t<T>>::value;

// ── ScopedView ──────────────────────────────────────────────────────

template <typename Carrier, typename Tag>
class [[nodiscard]] ScopedView {
    // Const pointer — the view is proof-only, never a mutable handle.
    // Methods that mutate the carrier already hold their own `this`
    // reference; they don't need write access through the view.
    // Storing const* lets mint_view accept `Carrier const&` and keeps
    // const member functions minting-capable (e.g. diagnostic reads
    // on a `Carrier const&` accessor).
    Carrier const* ptr_;

    // Private constructor.  Only mint_view (friended below) may call.
    constexpr explicit ScopedView(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept
        : ptr_{&c} {}

    template <typename Tag_, typename Carrier_>
    friend constexpr ScopedView<Carrier_, Tag_>
    mint_view(Carrier_ const& c CRUCIBLE_LIFETIMEBOUND) noexcept;

public:
    using carrier_type = Carrier;
    using tag_type     = Tag;

    // Tier 1: copy-construct allowed, assignment deleted.
    constexpr ScopedView(const ScopedView&) noexcept = default;
    constexpr ScopedView(ScopedView&&)      noexcept = default;
    ScopedView& operator=(const ScopedView&)
        = delete("ScopedView is single-binding; assignment hides state transitions");
    ScopedView& operator=(ScopedView&&)
        = delete("ScopedView is single-binding; assignment hides state transitions");
    constexpr ~ScopedView() = default;

    // Access is always const.  A caller that needs to mutate the
    // carrier does so via a direct reference (typically `this` in a
    // non-const member that accepts a view as proof of state).
    [[nodiscard]] constexpr Carrier const* operator->() const noexcept { return ptr_; }
    [[nodiscard]] constexpr Carrier const& carrier()    const noexcept { return *ptr_; }

    // Tier 1: forbid heap allocation.  Without these deletes, a caller
    // could do `new ScopedView<...>(existing)` via the public copy
    // ctor and escape the stack frame.  The placement-new overload
    // (void* operator new(size_t, void*)) is NOT deleted — it's used
    // by std::optional, std::variant, and any arena or bump-pointer
    // allocator that stores Views inline.  Those in-place storage
    // sites still have to pass the Tier 2 field audit.
    static void* operator new(std::size_t)
        = delete("ScopedView must live on the stack; heap allocation defeats the lifetime contract");
    static void* operator new[](std::size_t)
        = delete("ScopedView arrays on the heap defeat the lifetime contract");
    static void* operator new(std::size_t, std::align_val_t)
        = delete("ScopedView must live on the stack");
    static void* operator new[](std::size_t, std::align_val_t)
        = delete("ScopedView arrays on the heap defeat the lifetime contract");
    static void operator delete(void*)                     = delete;
    static void operator delete[](void*)                   = delete;
    static void operator delete(void*, std::align_val_t)   = delete;
    static void operator delete[](void*, std::align_val_t) = delete;
};

// ── mint_view: the single chokepoint for state assertion ───────────
//
// mint_view<Tag>(carrier) calls `view_ok(carrier, type_identity<Tag>{})`
// (found via ADL on Carrier) as a contract precondition.  Returns a
// freshly-constructed view.  Anyone may call it — but every call is
// discoverable via grep `mint_view<`, and every call pays one runtime
// state check.
template <typename Tag, typename Carrier>
[[nodiscard]] constexpr ScopedView<Carrier, Tag>
mint_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept
    pre(view_ok(c, std::type_identity<Tag>{}))
{
    return ScopedView<Carrier, Tag>{c};
}

// ── Tier 2: reflection-driven storage audit ─────────────────────────
//
// contains_scoped_view<T>() returns true iff T is a ScopedView, OR T
// is a known wrapper (std::optional<X>, std::vector<X>, std::array<X,N>,
// std::pair<X,Y>, std::tuple<...>, std::variant<...>) whose parameter
// type contains a ScopedView, OR T is a class type with a non-static
// data member whose type contains a ScopedView.
//
// no_scoped_view_field_check<T>() static_asserts the negation, naming
// the failing type in the diagnostic.

template <typename T>
consteval bool contains_scoped_view();   // forward decl for recursion

namespace detail {

// Single-element wrappers (optional, vector, array, smart pointers, etc.).
template <typename T> struct sv_unwrap_single { using type = void; };
template <typename X>                 struct sv_unwrap_single<std::optional<X>>     { using type = X; };
template <typename X, typename A>     struct sv_unwrap_single<std::vector<X, A>>    { using type = X; };
template <typename X, std::size_t N>  struct sv_unwrap_single<std::array<X, N>>     { using type = X; };
template <typename X, typename D>     struct sv_unwrap_single<std::unique_ptr<X, D>>{ using type = X; };
template <typename X>                 struct sv_unwrap_single<std::shared_ptr<X>>   { using type = X; };
template <typename X>                 struct sv_unwrap_single<std::weak_ptr<X>>     { using type = X; };
// C arrays in struct fields — `ScopedView<...> views[N];` escape vector.
template <typename X, std::size_t N>  struct sv_unwrap_single<X[N]>                 { using type = X; };

template <typename T>
using sv_unwrap_single_t = typename sv_unwrap_single<T>::type;

// Multi-element wrappers (pair, tuple, variant) — converted to a
// tuple of element types for uniform iteration.
template <typename T> struct sv_pack_for { using type = void; };
template <typename A, typename B>  struct sv_pack_for<std::pair<A, B>>     { using type = std::tuple<A, B>; };
template <typename... Xs>          struct sv_pack_for<std::tuple<Xs...>>   { using type = std::tuple<Xs...>; };
template <typename... Xs>          struct sv_pack_for<std::variant<Xs...>> { using type = std::tuple<Xs...>; };

template <typename T>
using sv_pack_for_t = typename sv_pack_for<T>::type;

template <typename Tup, std::size_t... Is>
consteval bool any_contains_view_seq(std::index_sequence<Is...>) {
    return (... || contains_scoped_view<std::tuple_element_t<Is, Tup>>());
}

template <typename T>
consteval bool reflect_contains_view() {
    using namespace std::meta;
    constexpr auto ctx = access_context::current();
    static constexpr auto members = std::define_static_array(
        nonstatic_data_members_of(^^T, ctx));
    bool found = false;
    template for (constexpr auto m : members) {
        using F = typename [:type_of(m):];
        if (contains_scoped_view<F>()) found = true;
    }
    return found;
}

} // namespace detail

template <typename T>
consteval bool contains_scoped_view() {
    using U = std::remove_cvref_t<T>;
    if constexpr (is_scoped_view_v<U>) {
        return true;
    } else if constexpr (!std::is_void_v<detail::sv_unwrap_single_t<U>>) {
        return contains_scoped_view<detail::sv_unwrap_single_t<U>>();
    } else if constexpr (!std::is_void_v<detail::sv_pack_for_t<U>>) {
        using Tup = detail::sv_pack_for_t<U>;
        return detail::any_contains_view_seq<Tup>(
            std::make_index_sequence<std::tuple_size_v<Tup>>{});
    } else if constexpr (std::is_class_v<U>
                         && !std::is_fundamental_v<U>
                         && !std::is_pointer_v<U>) {
        return detail::reflect_contains_view<U>();
    } else {
        return false;
    }
}

template <typename T>
consteval bool no_scoped_view_field_check() {
    static_assert(!contains_scoped_view<T>(),
        "Type contains a safety::ScopedView<> in some field. "
        "Views must not escape their construction scope; storing a "
        "view in a struct, container, optional, variant, etc. defeats "
        "the lifetime contract.  See safety/ScopedView.h discipline.");
    return true;
}

// ── Zero-cost contract lock ─────────────────────────────────────────

namespace detail {
struct sv_test_carrier {};
struct sv_test_tag     {};
}

static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag>)
              == sizeof(void*),
              "ScopedView<C, T> must be exactly a Carrier pointer");
static_assert(std::is_trivially_copyable_v<
    ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(std::is_trivially_destructible_v<
    ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);

} // namespace crucible::safety
