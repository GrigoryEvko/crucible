// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.

#include <crucible/safety/ThreadName.h>
#include <crucible/effects/_ExecCtx.h>

#include <string_view>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;

static_assert(sizeof(sf::ThreadNamed<"x">) == 1, "witness must be empty");
static_assert(!std::is_same_v<sf::ThreadNamed<"fg">, sf::ThreadNamed<"bg">>);
static_assert(std::is_same_v<sf::ThreadNamed<"bg">, sf::ThreadNamed<"bg">>);

static_assert(sf::ThreadNameLiteral<12>{"crucible-bg"}.visible_length == 11);
static_assert(sf::ThreadNameLiteral<16>{"123456789012345"}.visible_length == 15);

static_assert(sf::IsThreadNamed<sf::ThreadNamed<"crucible-fg">>);
static_assert(!sf::IsThreadNamed<int>);
static_assert(!sf::IsThreadNamed<eff::Init>);

// The gate is a disjunction.  This is the bare-Init branch.
static_assert(sf::CtxIsInitPhase<eff::Init>);
// This is the ExecCtx branch, where the row contains Effect::Init.
static_assert(sf::CtxIsInitPhase<eff::ColdInitCtx>,
              "ColdInitCtx::row = Row<Init,Alloc,IO> — the ExecCtx branch must admit it.");
static_assert(!sf::CtxIsInitPhase<eff::Bg>);
static_assert(!sf::CtxIsInitPhase<eff::Test>);
static_assert(!sf::CtxIsInitPhase<eff::HotFgCtx>, "HotFgCtx is Fg-row — not an init phase.");
static_assert(!sf::CtxIsInitPhase<eff::BgDrainCtx>, "BgDrainCtx is Bg-row — not an init phase.");

static_assert(std::is_same_v<decltype(sf::mint_thread_name<"probe">(std::declval<eff::Init const&>())),
                             sf::ThreadNamed<"probe">>);

}  // namespace

int main() {
    // This renames the running thread to crux-smoke.
    ::crucible::safety::detail::thread_name_self_test::runtime_smoke_test();

    eff::ColdInitCtx cold{};
    auto witness = sf::mint_thread_name<"crux-cold">(cold);
    if (std::string_view{witness.c_str()} != "crux-cold") return 1;
    if (witness.visible_length() != 9) return 2;
    return 0;
}
