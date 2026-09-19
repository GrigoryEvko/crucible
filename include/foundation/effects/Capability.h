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

#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::effects {

template <Effect E, class Source>
concept CanMintCap = is_cap_type_v<Source> && row_contains_v<cap_permitted_row_t<Source>, E>;

template <Effect Cap, class Source>
class Capability;

// The friendship that gates construction lives on this key rather than
// inside Capability.  A later edit to the requires-clause of mint_cap
// must be mirrored in the friend declaration, or the friendship
// resolves to a different overload and every minting site fails with a
// private-member error far from the cause.  Keeping the declaration in
// a one-purpose class puts it in front of whoever makes that edit.
//
// The key must not move into a nested namespace.  A templated friend
// declaration introduces a new declaration into the innermost enclosing
// namespace of the befriending class when no matching declaration is
// already visible there, so a key inside a detail namespace would
// befriend a fresh detail-scope mint_cap and silently open the gate.
class cap_mint_key {
    constexpr cap_mint_key() noexcept = default;

    template <Effect E, class S>
        requires CanMintCap<E, S>
    friend constexpr Capability<E, S> mint_cap(S const&) noexcept;
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
    Capability(Capability&&) noexcept = default;
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

template <class T>
struct is_capability : std::false_type {};
template <Effect E, class S>
struct is_capability<Capability<E, S>> : std::true_type {};
template <class T>
inline constexpr bool is_capability_v = is_capability<T>::value;
template <class T>
concept IsCapability = is_capability_v<T>;

// cap_matches ignores the source.  HasCapAndSource pins both, for a
// function that needs a capability from one specific source.
template <class T, Effect E>
struct cap_matches : std::false_type {};
template <Effect E, class S>
struct cap_matches<Capability<E, S>, E> : std::true_type {};
template <class T, Effect E>
inline constexpr bool cap_matches_v = cap_matches<T, E>::value;

template <class T, Effect E, class S>
concept HasCapAndSource = std::is_same_v<T, Capability<E, S>>;

// Both are left undefined for a type that is not a Capability, so
// asking for the effect or source of something else is a hard error.
template <class T>
struct cap_of;
template <Effect E, class S>
struct cap_of<Capability<E, S>> {
    static constexpr Effect value = E;
};
template <class T>
inline constexpr Effect cap_of_v = cap_of<T>::value;

template <class T>
struct source_of;
template <Effect E, class S>
struct source_of<Capability<E, S>> {
    using type = S;
};
template <class T>
using source_of_t = typename source_of<T>::type;

template <Effect E, IsExecCtx Ctx>
    requires CtxCanMint<Ctx, E>
[[nodiscard]] constexpr Capability<E, cap_type_of_t<Ctx>> mint_from_ctx(Ctx const& ctx) noexcept {
    // The context's own capability member is the proof of authority,
    // and it is passed along rather than default-constructed here: the
    // source types have private default constructors, so a fresh one
    // could not be made at this scope anyway.
    return mint_cap<E>(ctx.cap());
}

// The capability may have been minted in another scope.  This says the
// surrounding context is still authorized for that effect.
template <class Cap, class Ctx>
concept CapMatchesCtx = IsCapability<Cap> && IsExecCtx<Ctx> && row_contains_v<row_type_of_t<Ctx>, cap_of_v<Cap>>;

// The bridge for a function that takes a bare tag by value: the caller
// mints a Capability, and trades it here for the tag.  There is no
// overload for the thread atoms, which have no value-level tag.
template <Effect E, class S>
[[nodiscard]] constexpr cap::Alloc extract_bare(Capability<E, S>&& c) noexcept
    requires(E == Effect::Alloc)
{
    std::move(c).consume();
    return cap::Alloc{};
}

template <Effect E, class S>
[[nodiscard]] constexpr cap::IO extract_bare(Capability<E, S>&& c) noexcept
    requires(E == Effect::IO)
{
    std::move(c).consume();
    return cap::IO{};
}

template <Effect E, class S>
[[nodiscard]] constexpr cap::Block extract_bare(Capability<E, S>&& c) noexcept
    requires(E == Effect::Block)
{
    std::move(c).consume();
    return cap::Block{};
}

namespace detail::capability_self_test {

static_assert(sizeof(Capability<Effect::Alloc, Bg>) == 1,
              "A Capability must be 1 byte.  It is an empty class and its source is a phantom type.");
static_assert(sizeof(Capability<Effect::IO, Init>) == 1);
static_assert(sizeof(Capability<Effect::Block, Test>) == 1);

static_assert(!std::is_copy_constructible_v<Capability<Effect::Alloc, Bg>>,
              "A Capability must not be copyable.  Copying it would duplicate a single-use proof.");
static_assert(!std::is_copy_assignable_v<Capability<Effect::Alloc, Bg>>);
static_assert(std::is_move_constructible_v<Capability<Effect::Alloc, Bg>>);
static_assert(std::is_move_assignable_v<Capability<Effect::Alloc, Bg>>);
static_assert(std::is_nothrow_move_constructible_v<Capability<Effect::Alloc, Bg>>);

// The detection below uses the void_t idiom rather than a requires
// expression.  GCC 16.1.1 lets a reference-qualifier mismatch escape a
// requires-expression body as a hard error instead of a substitution
// failure, so the requires form would not compile at all here.
template <class, class = void>
struct cap_consume_callable_lvalue : std::false_type {};
template <class C>
struct cap_consume_callable_lvalue<C, std::void_t<decltype(std::declval<C&>().consume())>> : std::true_type {};

template <class, class = void>
struct cap_consume_callable_rvalue : std::false_type {};
template <class C>
struct cap_consume_callable_rvalue<C, std::void_t<decltype(std::declval<C>().consume())>> : std::true_type {};

static_assert(!cap_consume_callable_lvalue<Capability<Effect::Alloc, Bg>>::value,
              "Capability::consume must not be callable on an lvalue.  The call site has to spell the "
              "move, which is what makes the consumption point visible.");
static_assert(cap_consume_callable_rvalue<Capability<Effect::Alloc, Bg>>::value,
              "Capability::consume must be callable on a non-const rvalue, which is the consumption path.");

static_assert(!std::is_default_constructible_v<Capability<Effect::Alloc, Bg>>);
static_assert(!std::is_default_constructible_v<Capability<Effect::IO, Init>>);
static_assert(!std::is_default_constructible_v<Capability<Effect::Block, Test>>);

// Both halves are load-bearing.  Drop either and the token becomes
// forgeable.
static_assert(std::is_constructible_v<Capability<Effect::Alloc, Bg>, cap_mint_key>);
static_assert(std::is_constructible_v<Capability<Effect::IO, Init>, cap_mint_key>);
static_assert(std::is_constructible_v<Capability<Effect::Block, Test>, cap_mint_key>);
static_assert(!std::is_default_constructible_v<cap_mint_key>,
              "The default constructor of cap_mint_key must not be public.  Only the mint factory is "
              "friended to build one.");

static_assert(!std::is_constructible_v<Capability<Effect::Alloc, Bg>, int>);
static_assert(!std::is_convertible_v<cap_mint_key, Capability<Effect::Alloc, Bg>>,
              "The passkey constructor must be explicit.  An implicit conversion would let a plain "
              "copy of the key mint a Capability without naming the construction.");

// This resolves the friendship at header inclusion.  Should the friend
// declaration drift from the factory signature, the private
// constructor becomes unreachable from the factory body and this fails
// to compile.
static_assert(noexcept(mint_cap<Effect::Alloc>(std::declval<Bg const&>())),
              "mint_cap for Effect::Alloc from a Bg source must resolve and be noexcept.  If it does "
              "not, the cap_mint_key friend declaration has drifted from the factory signature.");
static_assert(noexcept(mint_cap<Effect::IO>(std::declval<Init const&>())));
static_assert(noexcept(mint_cap<Effect::Block>(std::declval<Test const&>())));

static_assert(std::is_empty_v<cap_mint_key>);
static_assert(sizeof(Capability<Effect::Alloc, Bg>) == 1,
              "The passkey constructor must preserve the 1-byte size.  The key is empty and the "
              "Capability holds no members.");

static_assert(CanMintCap<Effect::Alloc, Bg>);
static_assert(CanMintCap<Effect::IO, Bg>);
static_assert(CanMintCap<Effect::Block, Bg>);
static_assert(CanMintCap<Effect::Bg, Bg>);
static_assert(!CanMintCap<Effect::Init, Bg>);
static_assert(!CanMintCap<Effect::Test, Bg>);

static_assert(CanMintCap<Effect::Alloc, Init>);
static_assert(CanMintCap<Effect::IO, Init>);
static_assert(CanMintCap<Effect::Init, Init>);
static_assert(!CanMintCap<Effect::Block, Init>);
static_assert(!CanMintCap<Effect::Bg, Init>);
static_assert(!CanMintCap<Effect::Test, Init>);

// The two rejections below are the structural boundary that stops a
// test context from standing in for a background or initialization
// one.
static_assert(CanMintCap<Effect::Alloc, Test>);
static_assert(CanMintCap<Effect::IO, Test>);
static_assert(CanMintCap<Effect::Block, Test>);
static_assert(CanMintCap<Effect::Test, Test>);
static_assert(!CanMintCap<Effect::Bg, Test>);
static_assert(!CanMintCap<Effect::Init, Test>);

static_assert(!CanMintCap<Effect::Alloc, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::IO, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Block, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Bg, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Init, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Test, ctx_cap::Fg>);

static_assert(!CanMintCap<Effect::Alloc, int>);
static_assert(!CanMintCap<Effect::Alloc, void>);

static_assert(is_capability_v<Capability<Effect::Alloc, Bg>>);
static_assert(!is_capability_v<int>);
static_assert(!is_capability_v<Bg>);
static_assert(!is_capability_v<cap::Alloc>);

static_assert(cap_of_v<Capability<Effect::Alloc, Bg>> == Effect::Alloc);
static_assert(cap_of_v<Capability<Effect::IO, Init>> == Effect::IO);
static_assert(cap_of_v<Capability<Effect::Block, Test>> == Effect::Block);

static_assert(std::is_same_v<source_of_t<Capability<Effect::Alloc, Bg>>, Bg>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::IO, Init>>, Init>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::Block, Test>>, Test>);

static_assert(cap_matches_v<Capability<Effect::Alloc, Bg>, Effect::Alloc>);
static_assert(!cap_matches_v<Capability<Effect::Alloc, Bg>, Effect::IO>);
static_assert(cap_matches_v<Capability<Effect::Alloc, Init>, Effect::Alloc>);
static_assert(!cap_matches_v<int, Effect::Alloc>);

static_assert(HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Bg>);
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Init>);
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::IO, Bg>);

static_assert(CapMatchesCtx<Capability<Effect::Bg, Bg>, detail::exec_ctx_self_test::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Bg>, detail::exec_ctx_self_test::BgWitness>);
static_assert(!CapMatchesCtx<Capability<Effect::IO, Bg>, detail::exec_ctx_self_test::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::IO, Bg>, detail::exec_ctx_self_test::BgIoWitness>);
static_assert(!CapMatchesCtx<Capability<Effect::Bg, Bg>, detail::exec_ctx_self_test::FgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Test, Test>, detail::exec_ctx_self_test::TestWitnessCtx>);

// A capability is not locked to the source that minted it.  The last
// two pairs cross sources on purpose.
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Init>, detail::exec_ctx_self_test::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Test>, detail::exec_ctx_self_test::BgIoWitness>);

}  // namespace detail::capability_self_test

[[gnu::cold]] inline void runtime_smoke_test_capability() noexcept {
    auto bg = testing::bg();
    auto bg_alloc = mint_cap<Effect::Alloc>(bg);
    auto bg_io = mint_cap<Effect::IO>(bg);
    auto bg_block = mint_cap<Effect::Block>(bg);
    auto bg_self = mint_cap<Effect::Bg>(bg);

    auto init = testing::init();
    auto init_alloc = mint_cap<Effect::Alloc>(init);
    auto init_io = mint_cap<Effect::IO>(init);
    auto init_self = mint_cap<Effect::Init>(init);

    auto test = testing::test();
    auto test_alloc = mint_cap<Effect::Alloc>(test);
    auto test_block = mint_cap<Effect::Block>(test);

    Capability<Effect::Alloc, Bg> moved = std::move(bg_alloc);
    std::move(moved).consume();

    static_assert(cap_of_v<decltype(bg_io)> == Effect::IO);
    static_assert(cap_of_v<decltype(test_block)> == Effect::Block);
    static_assert(std::is_same_v<source_of_t<decltype(init_io)>, Init>);
    static_assert(std::is_same_v<source_of_t<decltype(test_alloc)>, Test>);

    static_assert(IsCapability<decltype(bg_io)>);
    static_assert(!IsCapability<int>);

    static_cast<void>(bg_io);
    static_cast<void>(bg_block);
    static_cast<void>(bg_self);
    static_cast<void>(init_alloc);
    static_cast<void>(init_io);
    static_cast<void>(init_self);
    static_cast<void>(test_alloc);
    static_cast<void>(test_block);

    detail::exec_ctx_self_test::BgWitness bg_ctx;
    detail::exec_ctx_self_test::BgIoWitness bg_compile_ctx;
    auto from_ctx_alloc = mint_from_ctx<Effect::Alloc>(bg_ctx);
    auto from_ctx_io = mint_from_ctx<Effect::IO>(bg_compile_ctx);
    static_assert(std::is_same_v<decltype(from_ctx_alloc), Capability<Effect::Alloc, Bg>>);
    static_assert(std::is_same_v<decltype(from_ctx_io), Capability<Effect::IO, Bg>>);
    static_cast<void>(from_ctx_alloc);
    static_cast<void>(from_ctx_io);

    auto a = mint_cap<Effect::Alloc>(bg);
    [[maybe_unused]] cap::Alloc bare_a = extract_bare(std::move(a));

    auto i = mint_cap<Effect::IO>(bg);
    [[maybe_unused]] cap::IO bare_i = extract_bare(std::move(i));

    auto b = mint_cap<Effect::Block>(bg);
    [[maybe_unused]] cap::Block bare_b = extract_bare(std::move(b));
}

}  // namespace foundation::effects
