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
// to nest a Capability inside a general single-consume wrapper: it
// gives its authority one time and no more.

#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

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

// A token gives its authority one time.  A move hands the authority to
// the target and spends the source, and a consume spends the token
// itself.  A spent token is still an object, because C++ leaves a
// moved-from object valid, so the token records that it is spent.  A
// move or a consume of a spent token ends the process, and in a constant
// evaluation it stops the build.  A token can end with no consume: the
// operation that it authorized does not occur, and nothing claims that
// it did.
template <Effect Cap, class Source>
class [[nodiscard]] Capability {
public:
    // Holding the key is the proof of authority, and only the mint
    // factory can make one, so this is the sole route to a Capability.
    // Capability itself befriends nobody.
    explicit constexpr Capability(cap_mint_key) noexcept {}

    Capability(Capability const&) = delete("a copy of a capability gives its authority two times");
    Capability& operator=(Capability const&) = delete("a copy of a capability gives its authority two times");

    // User-provided, and not defaulted.  A defaulted move is trivial, so
    // the token was trivially copyable and an implicit-lifetime type:
    // std::bit_cast built a capability from a byte, and
    // std::start_lifetime_as built one over a buffer, each with no key.
    // With this move no constructor is trivial.  The destructor stays
    // trivial.
    constexpr Capability(Capability&& other) noexcept : state_{other.spend_()} {}

    // The target can be live.  It then ends with no consume, as a token
    // that goes out of scope does.
    constexpr Capability& operator=(Capability&& other) noexcept {
        state_ = other.spend_();
        return *this;
    }

    ~Capability() = default;

    static constexpr Effect cap_v = Cap;
    using source_type = Source;

    // The rvalue qualifier forces the call site to write std::move(c)
    // first, which puts the consumption point in the source where a
    // reader and a grep can both find it.
    constexpr void consume() && noexcept { static_cast<void>(spend_()); }

    [[nodiscard]] static consteval std::string_view kind_name() noexcept { return "Capability"; }

private:
    enum class State : std::uint8_t {
        spent = 0,
        live = 1,
    };

    // Spends a live token and gives the state that it had.  The check is in
    // the body, so it fires in a constant evaluation too.
    [[nodiscard]] constexpr State spend_() noexcept {
        CRUCIBLE_PRE(state_ == State::live);
        state_ = State::spent;
        return State::live;
    }

    State state_{State::live};
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

// The bare tag of an atom straight from a context, extract_bare_from_ctx, is
// in foundation/effects/Ctx.h, because the context names it as a friend.  It
// makes no token, so it holds no spend check.

// The capability may have been minted in another scope.  This says the
// surrounding context is still authorized for that effect.
template <class Cap, class Ctx>
concept CapMatchesCtx = IsCapability<Cap> && IsExecCtx<Ctx> && row_contains(^^row_type_of_t<Ctx>, cap_of(^^Cap));

// The bridge from a Capability to the bare tag of its atom, extract_bare,
// and HasBareTag and bare_tag_t are in foundation/effects/Effect.h, beside
// the owner of the atoms that builds the tag.

}  // namespace foundation::effects
