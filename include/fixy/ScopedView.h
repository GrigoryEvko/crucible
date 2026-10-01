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
// erased identity.
//
// A carrier opts into the field audit by writing a static_assert on
// no_scoped_view_field_check for its own type.

#include <fixy/Qtt.h>
#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

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

// The one reflection query in foundation/reflect/Instance.h.  It is a
// concept, so no translation unit can specialize it.  The field audit
// below reads it, and a class template or a variable template in its
// place would let a specialization hide a stored view from the audit.
template <typename T>
concept IsScopedView = ::foundation::reflect::IsInstanceOf<T, ^^ScopedView>;

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
// The two halves are two gates.  One gate for both would make the
// factory look callable everywhere: `requires { mint_view<Tag>(c) }`
// would answer true for a carrier that declares no view_ok at all, and
// true for a tag the carrier declares no predicate for.  Both would
// then fail inside this header rather than at the call.  A caller that
// dispatches on that answer would choose this factory for a view of an
// unsupported state.
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

// Without this twin, mint_view<Ready>(Carrier{}) compiles and hands back
// a view of a carrier that is gone at the end of the statement.  This
// twin refuses it.  The deduced parameter is a const rvalue reference
// rather than a forwarding reference, so a non-const lvalue carrier
// still reaches the factory above.
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
    // No byte route builds a view, so a view names only a carrier that a
    // mint checked.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

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

// The walk below reaches each container and product shape without a
// list of names.  A base class is walked like a member, a union like a
// class, an array through its element, and the reflection reads private
// members.
//
// optional and variant keep their payload in a union inside a base.
// tuple keeps its elements in a chain of bases, and pair keeps them in
// members.  array and a C array are arrays, and Linear keeps its payload
// in a private member.  vector, unique_ptr, shared_ptr and weak_ptr hold
// a pointer, and the associated-type rule below covers them.
//
// Node-based and type-erased containers keep their elements behind a
// pointer or a raw byte buffer, so the reflective walk over their
// fields sees only `X*` or `unsigned char[N]` and stops there.  What
// every such container does expose is its element typedef.  Recursing
// through that typedef is a structural rule rather than an
// enumeration, so a container shape the tree has not used yet is
// covered the day someone uses it.  Without the rule, std::deque,
// std::list, std::forward_list, std::span, std::inplace_vector,
// std::expected and the associative containers would audit clean.
//
// The sizeof() probe keeps an incomplete, void or reference associated
// type out, since naming it would be ill-formed and a container whose
// element type is not complete here cannot be storing a view anyway.
// The is_same guard stops a self-referential typedef recursing forever.
// nonstatic_data_members_of throws on an incomplete class, so the walk
// has to stop at one.  A pimpl reaches one: it keeps its
// `unique_ptr<State>` private and State forward-declared, and under
// unchecked() the walk names the private member and so the incomplete
// State.
//
// Stopping there leaves the one hole this audit knowingly has: a State
// defined in a .cpp could hold a view and nothing here would see it.
// The hole has the same shape as the rule that the walk does not follow
// a raw pointer.  Everything reachable by value is audited.
//
// The walk is a search over the types that a value of the root type
// holds, with one list of the classes it has seen.  It reads each class
// one time, so a class that two members or two bases share costs one
// visit, and a class that the associated-type rule reaches again —
// `struct Node { std::vector<Node> children; }` — stops the search
// there.  The answer is the answer to one question: can the search reach
// a ScopedView from the root.  The variable template below keeps that
// answer, so each root type costs one search in a translation unit.
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

// The three concepts as variables, so that the search can ask each one
// of a type that it holds as a reflection.  Asking the concept also
// instantiates a class template specialization that nothing has used
// yet, as the sizeof in the concept requires.
template <typename T>
inline constexpr bool sv_is_complete_v = sv_complete<T>;
template <typename T>
inline constexpr bool sv_has_value_v = sv_has_associated_value<T>;
template <typename T>
inline constexpr bool sv_has_element_v = sv_has_associated_element<T>;
template <typename T>
using sv_value_t = typename T::value_type;
template <typename T>
using sv_element_t = typename T::element_type;

[[nodiscard]] consteval std::meta::info sv_bare_(std::meta::info type) {
    return std::meta::dealias(std::meta::remove_cvref(std::meta::dealias(type)));
}

[[nodiscard]] consteval bool sv_asks_(std::meta::info question, std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(question, {type}));
}

// Complexity: one visit for each class reachable from the root, and a
// linear scan of the seen list for each type that a visit finds.  The
// list holds only classes and unions, because no other type can hold a
// view by value.  An array is replaced by its element type.  A pointer
// is not followed.
template <class = void>
[[nodiscard]] consteval bool sv_reaches_view_(std::meta::info root) {
    using namespace std::meta;
    // unchecked(), not current().  access_context::current() is fixed
    // at the point it is written, which is inside this namespace, so
    // the walk would see only the members this namespace may name.  A
    // carrier that makes its ScopedView field private — ordinary
    // encapsulation, not evasion — would audit clean.  The audit asks a
    // structural question about layout, not an access question, so it
    // takes the context that answers the question it is asking.
    // Secret.h's policy-roster walk uses unchecked() for the same
    // reason.
    constexpr access_context ctx = access_context::unchecked();
    std::vector<info> seen;
    bool found = false;
    const auto visit = [&](info held) {
        info type = sv_bare_(held);
        if (is_array_type(type)) type = sv_bare_(remove_all_extents(type));
        if (has_template_arguments(type) && template_of(type) == ^^ScopedView) {
            found = true;
            return;
        }
        if (!is_class_type(type) && !is_union_type(type)) return;
        // An index loop over the data, because the constant evaluator
        // runs a loop through vector iterators about four times slower.
        const info* const known = seen.data();
        const std::size_t known_count = seen.size();
        for (std::size_t index = 0; index < known_count; ++index) {
            if (known[index] == type) return;
        }
        seen.push_back(type);
    };
    visit(root);
    for (std::size_t next = 0; next < seen.size() && !found; ++next) {
        const info type = seen[next];
        // A type that is complete here needs no instantiation, and the
        // cheap query answers for it.
        if (!is_complete_type(type) && !sv_asks_(^^sv_is_complete_v, type)) continue;
        for (const info base : bases_of(type, ctx))
            visit(type_of(base));
        for (const info member : nonstatic_data_members_of(type, ctx))
            visit(type_of(member));
        if (sv_asks_(^^sv_has_value_v, type)) visit(substitute(^^sv_value_t, {type}));
        if (sv_asks_(^^sv_has_element_v, type)) visit(substitute(^^sv_element_t, {type}));
    }
    return found;
}

template <typename T>
inline constexpr bool sv_contains_view_v = sv_reaches_view_(^^T);

}  // namespace detail

template <typename T>
consteval bool contains_scoped_view() {
    return detail::sv_contains_view_v<T>;
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

using sv_sealed_view = ScopedView<sv_test_carrier, sv_test_tag, sv_brand_a>;
}  // namespace detail

}  // namespace fixy
