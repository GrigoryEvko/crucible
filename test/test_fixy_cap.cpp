// Sentinel TU: compiles the alias header under the project warning flags so its
// static_asserts run.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Cap.h>

#include <type_traits>

namespace eff = crucible::effects;
namespace cap = crucible::fixy::cap;
namespace stst = crucible::fixy::cap::self_test;

static_assert(stst::same_mint_cap_v<eff::Effect::Alloc, eff::ctx_cap::Bg>,
              "fixy::cap::mint_cap<Alloc, Bg> must alias effects::mint_cap "
              "(same function pointer, not a re-declaration).");

static_assert(stst::same_mint_cap_v<eff::Effect::IO, eff::ctx_cap::Bg>,
              "fixy::cap::mint_cap<IO, Bg> must alias effects::mint_cap.");

static_assert(std::is_same_v<decltype(cap::mint_cap<eff::Effect::Alloc>(std::declval<eff::ctx_cap::Bg const&>())),
                             eff::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>>,
              "fixy::cap::mint_cap return type must match the substrate "
              "Capability<E, Source>.");

static_assert(std::is_same_v<cap::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>,
                             eff::Capability<eff::Effect::Alloc, eff::ctx_cap::Bg>>,
              "fixy::cap::Capability must alias effects::Capability.");

int main() {
    // The Bg constructor is private, so the capability source comes from the
    // testing minter.
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
