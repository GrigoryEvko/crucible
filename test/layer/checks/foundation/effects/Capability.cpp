// The compile-time checks of foundation/effects/Capability.h.

#include <foundation/effects/Capability.h>

namespace foundation::effects {

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
struct can_consume_from_lvalue : std::false_type {};
template <class C>
struct can_consume_from_lvalue<C, std::void_t<decltype(std::declval<C&>().consume())>> : std::true_type {};

template <class, class = void>
struct can_consume_from_rvalue : std::false_type {};
template <class C>
struct can_consume_from_rvalue<C, std::void_t<decltype(std::declval<C>().consume())>> : std::true_type {};

static_assert(!can_consume_from_lvalue<Capability<Effect::Alloc, Bg>>::value,
              "Capability::consume must not be callable on an lvalue.  The call site has to spell the "
              "move, which is what makes the consumption point visible.");
static_assert(can_consume_from_rvalue<Capability<Effect::Alloc, Bg>>::value,
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
static_assert(noexcept(mint_from_ctx<Effect::Alloc>(std::declval<detail::ctx_witnesses::BgWitness const&>())),
              "mint_from_ctx for an atom the context claims must resolve and be noexcept.  If it does not, the "
              "cap_mint_key friend declaration has drifted from the factory signature.");

static_assert(std::is_empty_v<cap_mint_key>);

// No route builds the key or the token without a constructor.
// std::bit_cast builds any trivially copyable type from bytes, and
// std::start_lifetime_as builds any implicit-lifetime type over a
// buffer.  Neither names a constructor, so neither meets the access
// check.  Each assertion fails if a constructor becomes defaulted again.
static_assert(!std::is_trivially_copyable_v<cap_mint_key> && !std::is_implicit_lifetime_v<cap_mint_key>,
              "cap_mint_key must have no trivial constructor, or std::bit_cast and std::start_lifetime_as "
              "build the key that mints every Capability.");
static_assert(!std::is_trivially_copyable_v<Capability<Effect::Alloc, Bg>>
                  && !std::is_implicit_lifetime_v<Capability<Effect::Alloc, Bg>>,
              "Capability must have no trivial constructor, or std::bit_cast and std::start_lifetime_as build "
              "a capability with no key.");
static_assert(std::is_trivially_destructible_v<Capability<Effect::Alloc, Bg>>,
              "The destructor of a Capability must stay trivial, so that its end costs nothing.");
static_assert(sizeof(Capability<Effect::Alloc, Bg>) == 1,
              "The passkey constructor must preserve the 1-byte size.  The key is empty and the "
              "Capability holds no members.");

static_assert(CanMintCap<Effect::Alloc, Bg>);
static_assert(CanMintCap<Effect::IO, Bg>);
static_assert(CanMintCap<Effect::Block, Bg>);
static_assert(CanMintCap<Effect::Bg, Bg>);
static_assert(!CanMintCap<Effect::Init, Bg>);
static_assert(!CanMintCap<Effect::Test, Bg>);

// An init source authorizes Block, because process startup waits in the
// kernel.  It still cannot stand in for a background or a test source.
static_assert(CanMintCap<Effect::Alloc, Init>);
static_assert(CanMintCap<Effect::IO, Init>);
static_assert(CanMintCap<Effect::Init, Init>);
static_assert(CanMintCap<Effect::Block, Init>);
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

static_assert(IsCapability<Capability<Effect::Alloc, Bg>>);
static_assert(!IsCapability<int>);
static_assert(!IsCapability<Bg>);
static_assert(!IsCapability<cap::Alloc>);

static_assert(cap_of(^^Capability<Effect::Alloc, Bg>) == Effect::Alloc);
static_assert(cap_of(^^Capability<Effect::IO, Init>) == Effect::IO);
static_assert(cap_of(^^Capability<Effect::Block, Test>) == Effect::Block);

static_assert(std::is_same_v<source_of_t<Capability<Effect::Alloc, Bg>>, Bg>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::IO, Init>>, Init>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::Block, Test>>, Test>);

static_assert(CapMatches<Capability<Effect::Alloc, Bg>, Effect::Alloc>);
static_assert(!CapMatches<Capability<Effect::Alloc, Bg>, Effect::IO>);
static_assert(CapMatches<Capability<Effect::Alloc, Init>, Effect::Alloc>);
static_assert(!CapMatches<int, Effect::Alloc>);

// The reflection query strips cv and reference, which the partial
// specializations it replaces did not.  A function template that
// deduces its parameter as Cap&& can now ask these readers about the
// deduced type without spelling the strip itself.  Nothing is admitted
// that was rejected on its merits: a type that is not a Capability
// still answers no, and the mint passkey is the gate on authority.
static_assert(IsCapability<Capability<Effect::Alloc, Bg> const&>);
static_assert(IsCapability<Capability<Effect::Alloc, Bg>&&>);
static_assert(cap_of(^^Capability<Effect::IO, Init> const&) == Effect::IO);
static_assert(std::is_same_v<source_of_t<Capability<Effect::IO, Init>&&>, Init>);
static_assert(CapMatches<Capability<Effect::Alloc, Bg> const&, Effect::Alloc>);

// Exactly the three value atoms carry a bare tag.  The three thread
// atoms leave the constraint unsatisfied, which is what keeps
// extract_bare out of the overload set for them.
static_assert(HasBareTag<Effect::Alloc>);
static_assert(HasBareTag<Effect::IO>);
static_assert(HasBareTag<Effect::Block>);
static_assert(!HasBareTag<Effect::Bg>, "A thread atom has no value-level tag.  Giving Effect::Bg one would "
                                       "let a context stand in for the capability it grants.");
static_assert(!HasBareTag<Effect::Init>);
static_assert(!HasBareTag<Effect::Test>);

static_assert(std::is_same_v<bare_tag_t<Effect::Alloc>, cap::Alloc>);
static_assert(std::is_same_v<bare_tag_t<Effect::IO>, cap::IO>);
static_assert(std::is_same_v<bare_tag_t<Effect::Block>, cap::Block>);

// The mapping reads namespace cap by identifier, so the membership of
// that namespace decides which atoms have a bridge.  A pin on the count
// makes a new member a two-place edit that a reviewer sees.
//
// The pin reads the namespace as this header defines it, and a reopen
// in a later translation unit does not move it.  That blindness is
// harmless for the reason it exists: every reflection query here
// resolves once, at this point, so a type smuggled in afterwards
// reaches neither this count nor bare_tag_t.  Measured by reopening the
// namespace with a type named for a thread atom, after which
// HasBareTag<Effect::Bg> stayed false.
[[nodiscard]] consteval std::size_t cap_tag_count_() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^cap, std::meta::access_context::current()));
    std::size_t count = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member)) ++count;
    }
#pragma GCC diagnostic pop
    return count;
}
static_assert(cap_tag_count_() == 3, "Namespace cap holds one type for each of Alloc, IO and Block.  A "
                                     "fourth means either a new value atom, whose name must match its "
                                     "enumerator for extract_bare to reach it, or a type that no atom "
                                     "names and that the bridge therefore ignores.");

// The rejection for a thread atom must be substitution failure.  A
// requires-expression answers false for that and fails to compile for a
// hard error, so this pins the mechanism and not only the outcome.
template <Effect E, class S>
concept can_extract_bare_ = requires(Capability<E, S>&& c) { extract_bare(std::move(c)); };

static_assert(can_extract_bare_<Effect::Alloc, Bg>);
static_assert(can_extract_bare_<Effect::IO, Init>);
static_assert(can_extract_bare_<Effect::Block, Test>);
static_assert(!can_extract_bare_<Effect::Bg, Bg>, "extract_bare must drop out of the overload set for a "
                                                  "thread atom, and must not reject from inside its body.");
static_assert(!can_extract_bare_<Effect::Init, Init>);
static_assert(!can_extract_bare_<Effect::Test, Test>);

static_assert(HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Bg>);
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Init>);
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::IO, Bg>);

static_assert(CapMatchesCtx<Capability<Effect::Bg, Bg>, detail::ctx_witnesses::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Bg>, detail::ctx_witnesses::BgWitness>);
static_assert(!CapMatchesCtx<Capability<Effect::IO, Bg>, detail::ctx_witnesses::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::IO, Bg>, detail::ctx_witnesses::BgIoWitness>);
static_assert(!CapMatchesCtx<Capability<Effect::Bg, Bg>, detail::ctx_witnesses::FgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Test, Test>, detail::ctx_witnesses::TestWitnessCtx>);

// A capability is not locked to the source that minted it.  The last
// two pairs cross sources on purpose.
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Init>, detail::ctx_witnesses::BgWitness>);
static_assert(CapMatchesCtx<Capability<Effect::Alloc, Test>, detail::ctx_witnesses::BgIoWitness>);

}  // namespace detail::capability_self_test

}  // namespace foundation::effects
