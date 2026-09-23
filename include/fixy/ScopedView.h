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
// A view carries a brand: the carrier's own when the carrier has one,
// so the view is pinned to that instance, and a fresh one per mint
// site otherwise.  A callee that wants a view of a particular carrier
// asks for the carrier's brand, and a view of another carrier of the
// same type fails to unify.  A view spelled without a brand is on the
// erased identity, which is what every view was before brands.
//
// A carrier opts into the field audit by writing a static_assert on
// no_scoped_view_field_check for its own type.
//
// Old spelling: include/crucible/safety/ScopedView.h.

#include <fixy/Qtt.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <new>
#include <type_traits>
#include <utility>

namespace fixy {

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
template <typename Tag>
struct scoped_view;
}  // namespace row_discipline

template <typename Carrier, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class ScopedView;

// The struct form stays because callers read `::value` off it. The
// answer is the one reflection query in foundation/reflect/Instance.h.
template <typename T>
struct is_scoped_view : std::bool_constant<::foundation::reflect::is_instance_of_v<T, ^^ScopedView>> {};

template <typename T>
inline constexpr bool is_scoped_view_v = is_scoped_view<std::remove_cvref_t<T>>::value;

// The brand a view of Carrier takes.
template <typename Carrier, typename Fresh>
using view_brand_t = ::foundation::brand::inherited_or_fresh_brand_t<Carrier, Fresh>;

// The type half of the gate.  view_ok is found by argument-dependent
// lookup on the carrier, so this asks whether the carrier declares a
// state predicate for this tag at all, which is a question about types
// and a concept can answer it.  Whether the carrier is presently in
// that state is the other half, and it stays the precondition below,
// because it reads the carrier's run-time state.
//
// The two were one gate before, and conflating them let the factory
// look callable everywhere.  Measured: `requires { mint_view<Tag>(c) }`
// answered true for a carrier that declares no view_ok at all, and true
// for a tag the carrier declares no predicate for.  Both then failed
// inside this header rather than at the call, and a caller that
// dispatched on that answer chose this factory believing a view of an
// unsupported state was mintable.
//
// The noexcept and bool requirements are the predicate's own contract,
// which fixy/SessView.h states as a static_assert for one carrier.  The
// factory is noexcept and evaluates view_ok inside itself, so a
// throwing predicate would terminate rather than report.
template <typename Carrier, typename Tag>
concept CarrierDeclaresViewState = requires(Carrier const& c) {
    { view_ok(c, std::type_identity<Tag>{}) } noexcept -> std::same_as<bool>;
};

// The single point at which a state is asserted.
//
// The fresh brand is the last template parameter, after the two the
// call site can name, so `mint_view<Ready>(carrier)` is the whole
// spelling and no caller can hand in a brand of its own choosing.
template <typename Tag, typename Carrier, typename Fresh = CRUCIBLE_FRESH_BRAND>
    requires CarrierDeclaresViewState<Carrier, Tag>
[[nodiscard]] constexpr ScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>>
mint_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept pre(view_ok(c, std::type_identity<Tag>{}));

// Measured, not suspected: mint_view<Ready>(Carrier{}) compiled and
// handed back a view of a carrier that was gone at the end of the
// statement.  This twin is what refuses it.  The deduced parameter is a
// const rvalue reference rather than a forwarding reference, so a
// non-const lvalue carrier still reaches the factory above.
//
// The twin carries the same constraint as the factory, so a carrier
// that declares no predicate for the tag is refused by the gate that
// names the reason rather than by the twin, whose message names the
// lifetime instead.
template <typename Tag, typename Carrier, typename Fresh = CRUCIBLE_FRESH_BRAND>
    requires CarrierDeclaresViewState<Carrier, Tag>
constexpr ScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>> mint_view(Carrier const&&) =
    delete("a view over a temporary carrier outlives it; bind the carrier to a name that outlives the view");

template <typename Carrier, typename Tag, typename Brand>
class [[nodiscard]] ScopedView {
    static_assert(::foundation::brand::IsBrand<Brand>, "ScopedView<Carrier, Tag, Brand>: Brand must be an empty "
                                                       "class type: the carrier's own, a mint's fresh brand, or "
                                                       "DefaultBrand.");

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

    // The constraint is repeated here because a friend declaration whose
    // constraints differ from the factory's declares a different
    // template, and the friendship would then attach to nothing.
    template <typename Tag_, typename Carrier_, typename Fresh_>
        requires CarrierDeclaresViewState<Carrier_, Tag_>
    friend constexpr ScopedView<Carrier_, Tag_, view_brand_t<Carrier_, Fresh_>>
    mint_view(Carrier_ const& c CRUCIBLE_LIFETIMEBOUND) noexcept;

public:
    using carrier_type = Carrier;
    using tag_type = Tag;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::scoped_view<Tag>;
    using row_payload = Carrier;

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

    // Erasure, one way only: a view of one instance becomes a view on
    // the erased identity.  Nothing gives an erased view a brand.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr ScopedView(ScopedView<Carrier, Tag, Other> const& other) noexcept : ptr_{other.operator->()} {}

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

template <typename Tag, typename Carrier, typename Fresh>
    requires CarrierDeclaresViewState<Carrier, Tag>
[[nodiscard]] constexpr ScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>>
mint_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept pre(view_ok(c, std::type_identity<Tag>{})) {
    return ScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>>{c};
}

// A one-shot state proof, for a transition the holder must prove the
// right to make and hands over rather than shares.  The token is gone
// after it is consumed, so the transition happens at most once.
//
// This bounds the token, not observation of the carrier: separate
// copyable views minted in parallel still read it.  Guard the
// transition method with its own state precondition to cover a view
// that has gone stale.
template <typename Carrier, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
using LinearScopedView = Linear<ScopedView<Carrier, Tag, Brand>>;

// The brand is passed through to mint_view by name, because a bare
// `mint_view<Tag>(c)` here would draw a second fresh brand for the
// inner call and the linear view would not carry this site's.
//
// The gate is the same one, spelled here rather than only inherited from
// the inner call, so the refusal lands on this factory's name.
template <typename Tag, typename Carrier, typename Fresh = CRUCIBLE_FRESH_BRAND>
    requires CarrierDeclaresViewState<Carrier, Tag>
[[nodiscard]] constexpr LinearScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>>
mint_linear_view(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept pre(view_ok(c, std::type_identity<Tag>{})) {
    return mint_linear<ScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>>>(mint_view<Tag, Carrier, Fresh>(c));
}

// The linear form carries its view further than the scoped one does, so
// it needs the same twin and needs it more.
template <typename Tag, typename Carrier, typename Fresh = CRUCIBLE_FRESH_BRAND>
    requires CarrierDeclaresViewState<Carrier, Tag>
constexpr LinearScopedView<Carrier, Tag, view_brand_t<Carrier, Fresh>> mint_linear_view(Carrier const&&) =
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
struct sv_brand_a {};
struct sv_brand_b {};
// A carrier that carries a brand, so a view of it inherits the brand.
struct sv_branded_carrier {
    using brand_type = sv_brand_a;
    int value = 1;
};
constexpr bool view_ok(sv_test_carrier const&, std::type_identity<sv_test_tag>) noexcept { return true; }
constexpr bool view_ok(sv_branded_carrier const&, std::type_identity<sv_test_tag>) noexcept { return true; }

// Two carriers that must not be mintable, kept beside the two that
// must.  The first declares no predicate at all; the second declares
// one for another tag.
struct sv_other_tag {};
struct sv_no_predicate_carrier {};
}  // namespace detail

static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag>) == sizeof(void*),
              "ScopedView<C, T> must be exactly a Carrier pointer");
static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>) == sizeof(void*),
              "a branded view keeps the layout of an erased one");
static_assert(std::is_trivially_copyable_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(std::is_trivially_destructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);

static_assert(is_scoped_view_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(is_scoped_view_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag> const&>);
static_assert(is_scoped_view_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>>);
static_assert(!is_scoped_view_v<detail::sv_test_carrier>);
static_assert(!is_scoped_view_v<void>);

// The erasure runs one way, and a view of one brand is not a view of
// another.
static_assert(std::is_convertible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                    ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(!std::is_constructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                       ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(!std::is_constructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                       ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_b>>);

namespace detail::scoped_view_self_test {

// Two mints of an unbranded carrier are two brands; a mint of a
// branded carrier takes the carrier's brand; the linear form carries
// the same brand as the scoped one minted beside it.
[[nodiscard]] consteval bool mints_brand() noexcept {
    sv_test_carrier plain{};
    auto first = mint_view<sv_test_tag>(plain);
    auto second = mint_view<sv_test_tag>(plain);
    static_assert(!std::is_same_v<decltype(first), decltype(second)>, "two mint sites are two brands");
    static_assert(::foundation::brand::IsBranded<decltype(first)>);
    sv_branded_carrier branded{};
    auto of_branded = mint_view<sv_test_tag>(branded);
    static_assert(std::is_same_v<::foundation::brand::brand_of_t<decltype(of_branded)>, sv_brand_a>,
                  "a view of a branded carrier takes the carrier's brand");
    ScopedView<sv_test_carrier, sv_test_tag> erased = first;
    return erased.operator->() == &plain && of_branded->value == 1;
}
static_assert(mints_brand());

// ── The gate answers, where it used to fail inside the header ────────
//
// Each pair below was `true` before CarrierDeclaresViewState, for both
// factories, and each then failed on the contract predicate inside
// mint_view rather than at the call.  The four assertions are what
// holds that: a caller may now ask whether a view of this state is
// mintable and get an answer.
template <typename Tag, typename Carrier>
concept ViewMintable = requires(Carrier const& c) { mint_view<Tag>(c); };

template <typename Tag, typename Carrier>
concept LinearViewMintable = requires(Carrier const& c) { mint_linear_view<Tag>(c); };

static_assert(ViewMintable<sv_test_tag, sv_test_carrier>, "the carrier that declares the predicate stays mintable");
static_assert(!ViewMintable<sv_test_tag, sv_no_predicate_carrier>,
              "a carrier that declares no view_ok must not look mintable");
static_assert(!ViewMintable<sv_other_tag, sv_test_carrier>,
              "a tag the carrier declares no predicate for must not look mintable");

static_assert(LinearViewMintable<sv_test_tag, sv_test_carrier>, "the linear form keeps the same admissions");
static_assert(!LinearViewMintable<sv_test_tag, sv_no_predicate_carrier>, "the linear form keeps the same refusals");
static_assert(!LinearViewMintable<sv_other_tag, sv_test_carrier>, "the linear form refuses the wrong tag as well");

}  // namespace detail::scoped_view_self_test

}  // namespace fixy
