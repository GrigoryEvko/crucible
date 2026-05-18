// ── test_fixy_cap — sentinel TU for fixy/Cap.h ─────────────────────
//
// Pulls fixy/Cap.h into a TU compiled under project warning flags so
// the header's static_asserts execute under enforcement.  Witnesses:
//
//   1. fixy::cap::mint_cap names the same function as effects::mint_cap
//      (the using-declaration is a name-lookup pass-through, not a
//      re-declaration).
//   2. fixy::cap::mint_from_ctx names the same function as
//      effects::mint_from_ctx.
//   3. Capability return type matches the substrate.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_cap_*.cpp.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Cap.h>

#include <type_traits>

namespace eff  = crucible::effects;
namespace cap  = crucible::fixy::cap;
namespace stst = crucible::fixy::cap::self_test;

// ─── 1. fixy::cap::mint_cap is the substrate's mint_cap ───────────

static_assert(stst::same_mint_cap_v<eff::Effect::Alloc, eff::ctx_cap::Bg>,
    "fixy::cap::mint_cap<Alloc, Bg> must alias effects::mint_cap "
    "(same function pointer, not a re-declaration).");

static_assert(stst::same_mint_cap_v<eff::Effect::IO, eff::ctx_cap::Bg>,
    "fixy::cap::mint_cap<IO, Bg> must alias effects::mint_cap.");

// ─── 2. Capability return type is preserved ───────────────────────

static_assert(std::is_same_v<
    decltype(cap::mint_cap<eff::Effect::Alloc>(std::declval<eff::ctx_cap::Bg const&>())),
    eff::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>>,
    "fixy::cap::mint_cap return type must match the substrate "
    "Capability<E, Source>.");

// ─── 3. Type carrier re-export ────────────────────────────────────

static_assert(std::is_same_v<
    cap::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>,
    eff::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>>,
    "fixy::cap::Capability must alias effects::Capability.");

// ─── 4. Runtime sanity — mint via fixy::, consume, observe ────────

int main() {
    // fixy-A3-005: Bg ctor is private — route through testing minter.
    auto bg = eff::testing::bg();
    {
        auto alloc_cap = cap::mint_cap<eff::Effect::Alloc>(bg);
        std::move(alloc_cap).consume();
    }
    {
        eff::BgCompileCtx bg_ctx{};
        auto io_cap = cap::mint_from_ctx<eff::Effect::IO>(bg_ctx);
        std::move(io_cap).consume();
    }
    return 0;
}
