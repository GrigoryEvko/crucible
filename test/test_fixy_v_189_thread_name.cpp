// FIXY-V-189 sentinel TU: safety/ThreadName.h — pthread_setname_np as an
// Init-row syscall mint.  Unlike its four sibling wrappers (V-185..188,
// value-level Graded carriers), this ships a TASK_COMM_LEN-bounded literal
// type (ThreadNameLiteral<N>), a phantom witness (ThreadNamed<Name>), an
// IsThreadNamed extractor, the §XXI gate CtxIsInitPhase, and mint_thread_name.
//
// THE LOAD-BEARING PROPERTIES this TU defends:
//   (1) a name > 15 visible chars is a COMPILE ERROR (no silent kernel
//       truncation) — exercised in the too-long neg fixture;
//   (2) the mint admits an init-phase context (bare effects::Init OR an
//       Init-row ExecCtx like ColdInitCtx) and rejects Bg / Test / Fg /
//       BgDrain contexts — both branches of CtxIsInitPhase asserted here.
//
// HS14 negative coverage lives in two distinct-mismatch-class fixtures:
//   - neg_thread_name_too_long   (TASK_COMM_LEN static_assert at the literal)
//   - neg_thread_name_wrong_ctx  (CtxIsInitPhase rejects a Bg context)

#include <crucible/safety/ThreadName.h>
#include <crucible/effects/ExecCtx.h>

#include <string_view>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;

// ── Witness layout + distinctness ───────────────────────────────────
static_assert(sizeof(sf::ThreadNamed<"x">) == 1, "witness must be empty");
static_assert(!std::is_same_v<sf::ThreadNamed<"fg">, sf::ThreadNamed<"bg">>);
static_assert( std::is_same_v<sf::ThreadNamed<"bg">, sf::ThreadNamed<"bg">>);

// ── Bounded literal — visible-length accounting ─────────────────────
static_assert(sf::ThreadNameLiteral<12>{"crucible-bg"}.visible_length == 11);
static_assert(sf::ThreadNameLiteral<16>{"123456789012345"}.visible_length == 15);

// ── IsThreadNamed concept extractor ─────────────────────────────────
static_assert( sf::IsThreadNamed<sf::ThreadNamed<"crucible-fg">>);
static_assert(!sf::IsThreadNamed<int>);
static_assert(!sf::IsThreadNamed<eff::Init>);

// ── CtxIsInitPhase — BOTH branches of the §XXI gate ─────────────────
// bare-Init branch (same_as<effects::Init>):
static_assert( sf::CtxIsInitPhase<eff::Init>);
// ExecCtx branch (row contains Effect::Init):
static_assert( sf::CtxIsInitPhase<eff::ColdInitCtx>,
    "ColdInitCtx::row = Row<Init,Alloc,IO> — the ExecCtx branch must admit it.");
// rejections — wrong bare context AND wrong-row ExecCtx:
static_assert(!sf::CtxIsInitPhase<eff::Bg>);
static_assert(!sf::CtxIsInitPhase<eff::Test>);
static_assert(!sf::CtxIsInitPhase<eff::HotFgCtx>,
    "HotFgCtx is Fg-row — not an init phase.");
static_assert(!sf::CtxIsInitPhase<eff::BgDrainCtx>,
    "BgDrainCtx is Bg-row — not an init phase.");

// ── mint_thread_name return type is the concrete witness ────────────
static_assert(std::is_same_v<
    decltype(sf::mint_thread_name<"probe">(std::declval<eff::Init const&>())),
    sf::ThreadNamed<"probe">>);

}  // namespace

int main() {
    // Bare-Init path (renames this thread to "crux-smoke").
    ::crucible::safety::detail::thread_name_self_test::runtime_smoke_test();

    // ExecCtx-branch path: mint through an Init-row ExecCtx at runtime.
    eff::ColdInitCtx cold{};
    auto witness = sf::mint_thread_name<"crux-cold">(cold);
    if (std::string_view{witness.c_str()} != "crux-cold") return 1;
    if (witness.visible_length() != 9) return 2;
    return 0;
}
