// Attacks on the production keys of the execution contexts.
//
// A background, init or foreground context is evidence: its key has a
// private constructor, and the friends of the key are a host owner and
// the test witness.  Each owner is defined in the header that declares
// its key, so a translation unit that names a key sees the definition,
// and a second definition is a redefinition error.  The foreground key
// reaches production only through a producer claim, and only the brand
// of a claim builds it.  The fixtures under neg/ named neg_host_* and
// neg_producer_claim_* pin the refusals that a static_assert cannot state.

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>

namespace host_owner_attacks {

namespace fe = ::foundation::effects;

template <class T>
concept Complete = requires { sizeof(T); };

// No key is built from nothing, and none is a byte.
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::init_key>);
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::bg_key>);
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::fg_key>);
static_assert(!std::is_trivially_copyable_v<fe::detail::ctx_mint::init_key>);

// The single-producer state of this test, and a state that is not it.
struct Producer {
    fe::host::ProducerClaim<Producer> claim;
};
struct Intruder {};

// Every owner is complete where its key is visible, so no translation
// unit that can name a key defines an owner first.
static_assert(Complete<fe::host::InitOwner> && Complete<fe::host::BackgroundOwner>
              && Complete<fe::host::ForegroundOwner> && Complete<fe::host::ProducerClaim<Producer>>);

// Each owner has one member, the door to its context, and no member that
// gives out its key.
static_assert(std::is_empty_v<fe::host::InitOwner> && std::is_empty_v<fe::host::BackgroundOwner>);
static_assert(std::is_same_v<decltype(fe::host::InitOwner::mint_init_context()), fe::Init>);
static_assert(noexcept(fe::host::InitOwner::mint_init_context()));
static_assert(std::is_same_v<decltype(fe::host::BackgroundOwner::mint_background_context()), fe::Bg>);
static_assert(noexcept(fe::host::BackgroundOwner::mint_background_context()));

// The background door gives the widest background context, and the
// context it gives claims no more than the background source permits.
using BgFromDoor = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>;
static_assert(std::is_same_v<decltype(BgFromDoor{fe::host::BackgroundOwner::mint_background_context()}), BgFromDoor>);
static_assert(fe::CtxOwnsCapability<BgFromDoor, fe::Effect::Block>);

// No owner gives out its key.  The foreground owner builds its key only
// for a producer claim.
template <class Owner>
concept KeyReachable = requires { Owner::key(); };
static_assert(!KeyReachable<fe::host::ForegroundOwner> && !KeyReachable<fe::host::InitOwner>
              && !KeyReachable<fe::host::BackgroundOwner>);

// No owner is a base, so no derived class borrows its position.
static_assert(std::is_final_v<fe::host::InitOwner> && std::is_final_v<fe::host::BackgroundOwner>
              && std::is_final_v<fe::host::ForegroundOwner>);

// Only the brand builds its claim, and a claim neither copies nor moves.
static_assert(!std::is_default_constructible_v<fe::host::ProducerClaim<Producer>>);
static_assert(!std::is_default_constructible_v<fe::host::ProducerClaim<Intruder>>);
static_assert(std::is_default_constructible_v<Producer>);
static_assert(!std::is_copy_constructible_v<fe::host::ProducerClaim<Producer>>
              && !std::is_move_constructible_v<fe::host::ProducerClaim<Producer>>);

// A claim is not built from bytes and is not started over a buffer.
static_assert(!std::is_trivially_copyable_v<fe::host::ProducerClaim<Producer>>
              && !std::is_implicit_lifetime_v<fe::host::ProducerClaim<Producer>>);

// The claim mints the context of its brand, which passes no gate that asks
// for another brand.
using ProducerCtx = fe::ExecCtx<fe::ctx_cap::BrandedFg<Producer>, fe::Row<>>;
using IntruderCtx = fe::ExecCtx<fe::ctx_cap::BrandedFg<Intruder>, fe::Row<>>;
using UnbrandedCtx = fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>;
static_assert(std::is_same_v<decltype(std::declval<Producer&>().claim.mint_producer_context()), ProducerCtx>);
static_assert(!std::is_convertible_v<ProducerCtx, IntruderCtx> && !std::is_constructible_v<IntruderCtx, ProducerCtx>);

// Any translation unit can declare a brand of its own and hold its claim,
// as SelfBranded below does.  So no branded context becomes the context
// with no brand, which names no single-producer state.
struct SelfBranded {
    fe::host::ProducerClaim<SelfBranded> claim;
};
using SelfBrandedCtx = fe::ExecCtx<fe::ctx_cap::BrandedFg<SelfBranded>, fe::Row<>>;
static_assert(std::is_same_v<decltype(std::declval<SelfBranded&>().claim.mint_producer_context()), SelfBrandedCtx>);
static_assert(!std::is_convertible_v<SelfBrandedCtx, UnbrandedCtx>
              && !std::is_constructible_v<UnbrandedCtx, SelfBrandedCtx>);
static_assert(!std::is_convertible_v<SelfBrandedCtx, ProducerCtx>
              && !std::is_constructible_v<ProducerCtx, SelfBrandedCtx>);
static_assert(!std::is_convertible_v<ProducerCtx, UnbrandedCtx> && !std::is_constructible_v<UnbrandedCtx, ProducerCtx>);

// The first thread that mints holds the claim, and a second thread reads
// that it cannot take it.
[[nodiscard]] inline bool a_second_thread_cannot_take_the_claim() {
    Producer producer;
    if (!producer.claim.is_claimable_by_caller()) return false;
    const ProducerCtx fg = producer.claim.mint_producer_context();
    (void)fg;
    if (!producer.claim.is_claimable_by_caller()) return false;
    bool other_thread_can_claim = true;
    std::thread other([&] { other_thread_can_claim = producer.claim.is_claimable_by_caller(); });
    other.join();
    return !other_thread_can_claim;
}

// A claim that no thread has used yet is claimable by any thread.
[[nodiscard]] inline bool a_fresh_claim_is_claimable_from_any_thread() {
    Producer producer;
    bool other_thread_can_claim = false;
    std::thread other([&] { other_thread_can_claim = producer.claim.is_claimable_by_caller(); });
    other.join();
    return other_thread_can_claim && producer.claim.is_claimable_by_caller();
}

}  // namespace host_owner_attacks

int main() {
    using namespace host_owner_attacks;
    if (!a_second_thread_cannot_take_the_claim()) {
        std::fprintf(stderr, "FAIL: a producer claim did not bind the thread that first minted from it\n");
        return EXIT_FAILURE;
    }
    if (!a_fresh_claim_is_claimable_from_any_thread()) {
        std::fprintf(stderr, "FAIL: an unused producer claim refused a thread\n");
        return EXIT_FAILURE;
    }
    std::printf("test_host_owner_attacks: every owner is complete, and a claim binds its first thread\n");
    return EXIT_SUCCESS;
}
