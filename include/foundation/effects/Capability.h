#pragma once

// Two token families coexist, and they answer different questions.
// The bare capability tags are copyable value markers: a function
// taking one asks only "any tag of this kind".  A Capability is the
// strict form: move-only, tagged with the source that authorized it,
// and minted only where that source is in scope.  Reach for it when
// the signature must say that a specific source authorized this
// effect and that the operation happens once.
//
// Ownership is a separate axis.  A permission says which thread may
// touch a resource; a capability says which kind of operation the
// caller may perform.  A function may take both.  There is no reason
// to nest a Capability inside a general single-consume wrapper: it is
// already linear.

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::effects {

template <Effect E, class Source>
concept CanMintCap = IsCapType<Source> && row_contains(^^cap_permitted_row_t<Source>, E);

template <Effect Cap, class Source>
class Capability;

// The friendship that gates construction lives on this key rather than
// inside Capability.  The key has two friends, mint_cap and mint_from_ctx.
// A later edit to the template head or the requires-clause of either
// must be mirrored in its friend declaration, or the friendship resolves
// to a different overload and every minting site fails with a
// private-member error far from the cause.  Keeping the declarations in
// a one-purpose class puts them in front of whoever makes that edit.
//
// The key must not move into a nested namespace.  A templated friend
// declaration introduces a new declaration into the innermost enclosing
// namespace of the befriending class when no matching declaration is
// already visible there, so a key inside a detail namespace would
// befriend a fresh detail-scope mint_cap and silently open the gate.
//
// Both constructors of the key are user-provided, and not defaulted.  A
// key with a trivial copy is trivially copyable, and std::bit_cast then
// builds one from any byte.  A key with any trivial constructor is an
// implicit-lifetime type, and std::start_lifetime_as then builds one
// over a buffer.  Neither route names a constructor, so neither meets
// the access check below.  The same holds for Capability itself.
class cap_mint_key {
    constexpr cap_mint_key() noexcept {}

    template <Effect E, class S>
        requires CanMintCap<E, S>
    friend constexpr Capability<E, S> mint_cap(S const&) noexcept;

    template <Effect E, IsExecCtx Ctx>
        requires CtxOwnsCapability<Ctx, E>
    friend constexpr Capability<E, cap_type_of_t<Ctx>> mint_from_ctx(Ctx const&) noexcept;

public:
    constexpr cap_mint_key(const cap_mint_key&) noexcept {}
};

template <Effect Cap, class Source>
class [[nodiscard]] Capability {
public:
    // Holding the key is the proof of authority, and only the mint
    // factory can make one, so this is the sole route to a Capability.
    // Capability itself befriends nobody.
    explicit constexpr Capability(cap_mint_key) noexcept {}

    Capability(Capability const&) = delete;
    Capability& operator=(Capability const&) = delete;
    // User-provided, and not defaulted.  A defaulted move is trivial, so
    // the token was trivially copyable and an implicit-lifetime type:
    // std::bit_cast built a capability from a byte, and
    // std::start_lifetime_as built one over a buffer, each with no key.
    // With this move no constructor is trivial.  The destructor stays
    // trivial.
    constexpr Capability(Capability&&) noexcept {}
    Capability& operator=(Capability&&) noexcept = default;
    ~Capability() = default;

    static constexpr Effect cap_v = Cap;
    using source_type = Source;

    // The move is the consumption.  This body is empty on purpose: the
    // rvalue qualifier forces the call site to write std::move(c) first,
    // which puts the consumption point in the source where a reader and
    // a grep can both find it.
    //
    // A second call on the moved-from object still compiles, because
    // C++ move semantics leave it a valid object.  Catching that is a
    // job for use-after-move analysis, not for the type system.
    constexpr void consume() && noexcept {}

    [[nodiscard]] static consteval std::string_view kind_name() noexcept { return "Capability"; }
};

template <Effect E, class Source>
    requires CanMintCap<E, Source>
[[nodiscard]] constexpr Capability<E, Source> mint_cap(Source const&) noexcept {
    return Capability<E, Source>{cap_mint_key{}};
}

// The first template parameter of a Capability is a non-type, which the
// `template <class...> class` form of a hand-written detector cannot
// name.  The reflection query answers for it directly.  That query also
// strips cv and reference, so a reference to a Capability answers as the
// Capability does, and the readers below inherit the same strip.
//
// Each reader is a concept or a function that is not a template.  No
// translation unit can specialize one.  A variable template that a gate
// reads is a door: a specialization would add a class of the caller to
// the capabilities, or give a capability another effect.
template <class T>
concept IsCapability = ::foundation::reflect::IsInstanceOf<T, ^^Capability>;

namespace detail {

// Declared and not defined, and not constexpr.  A constant evaluation that
// calls it fails, and the diagnostic gives its name as the reason.  A
// missing capability then cannot read as a default effect or as a default
// source.
void type_is_not_a_capability() noexcept;

// The template argument at `index` of the Capability that the reflection
// names.
[[nodiscard]] consteval std::meta::info capability_argument_(std::meta::info capability, std::size_t index) {
    const std::meta::info type = std::meta::dealias(std::meta::remove_cvref(capability));
    if (!std::meta::has_template_arguments(type) || std::meta::template_of(type) != ^^Capability) {
        type_is_not_a_capability();
    }
    return std::meta::template_arguments_of(type)[index];
}

}  // namespace detail

// The effect of a Capability, written cap_of(^^T).
[[nodiscard]] consteval Effect cap_of(std::meta::info capability) {
    return std::meta::extract<Effect>(detail::capability_argument_(capability, 0));
}

template <class T>
using source_of_t = [:detail::capability_argument_(^^T, 1):];

// CapMatches ignores the source.  HasCapAndSource pins both, for a
// function that needs a capability from one specific source.  That
// concept keeps the exact-type test: it is the one place that means the
// type itself and not a reference to it.
template <class T, Effect E>
concept CapMatches = IsCapability<T> && cap_of(^^T) == E;

template <class T, Effect E, class S>
concept HasCapAndSource = std::is_same_v<T, Capability<E, S>>;

// The row of the context is the bound, not what its source permits.  A
// function handed a drain context, which claims Bg and Alloc, mints an
// Alloc capability and no IO capability, although the background source
// permits IO.  A context claims no more than its source permits, so the
// token names the source of the context and needs no second check.  The
// mint builds the token from the key, and never reads the source itself.
template <Effect E, IsExecCtx Ctx>
    requires CtxOwnsCapability<Ctx, E>
[[nodiscard]] constexpr Capability<E, cap_type_of_t<Ctx>> mint_from_ctx(Ctx const&) noexcept {
    return Capability<E, cap_type_of_t<Ctx>>{cap_mint_key{}};
}

// The capability may have been minted in another scope.  This says the
// surrounding context is still authorized for that effect.
template <class Cap, class Ctx>
concept CapMatchesCtx = IsCapability<Cap> && IsExecCtx<Ctx> && row_contains(^^row_type_of_t<Ctx>, cap_of(^^Cap));

// The bare tag of an atom is the type in namespace cap whose identifier
// is the enumerator's own, which is how Alloc, IO and Block each reach
// theirs without an arm written here.  The identifier is load-bearing:
// a new value atom gets a bare tag by declaring a type of the same name
// in cap, and a type in cap whose name matches no atom is reached by
// nothing.  The three thread atoms declare no such type, and the
// reflection is then not a type.
namespace detail {

template <Effect E>
[[nodiscard]] consteval std::meta::info bare_tag_info_() noexcept {
    static constexpr auto members = std::define_static_array(
        static_cast<::foundation::reflect::anchored_t<std::meta::reflect_constant(E), std::vector<std::meta::info>>>(
            std::meta::members_of(^^cap, std::meta::access_context::current())));
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member)) {
            if (std::meta::identifier_of(member) == ::foundation::reflect::enum_name(E)) return member;
        }
    }
#pragma GCC diagnostic pop
    return std::meta::info{};
}

}  // namespace detail

template <Effect E>
concept HasBareTag = std::meta::is_type(detail::bare_tag_info_<E>());

template <Effect E>
    requires HasBareTag<E>
using bare_tag_t = [:detail::bare_tag_info_<E>():];

// The bridge for a function that takes a bare tag by value: the caller
// mints a Capability, and trades it here for the tag.  A thread atom
// leaves HasBareTag unsatisfied, so this drops out of the overload set
// by substitution failure rather than by a hard error in the body.
template <Effect E, class S>
    requires HasBareTag<E>
[[nodiscard]] constexpr bare_tag_t<E> extract_bare(Capability<E, S>&& c) noexcept {
    std::move(c).consume();
    return bare_tag_t<E>{};
}

}  // namespace foundation::effects
