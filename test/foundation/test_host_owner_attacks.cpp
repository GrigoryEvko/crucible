// Attacks on the production keys of the execution contexts.
//
// A background, init or foreground context is evidence: its key has a
// private constructor, and the friends of the key are a host owner type
// that the layer owning the entry point defines, and the test witness.
// Each attack below is legal C++ that tries to build a key without that
// owner.  An attack that still succeeds is an entry of the ledger at the
// foot of this file, and the ledger only shrinks.

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <type_traits>

// The three host owners are declared in foundation and defined nowhere,
// so the first translation unit that defines one is its owner, and no
// rule is broken.  This file is that translation unit.
namespace foundation::effects::host {
struct InitOwner {
    [[nodiscard]] static constexpr auto key() noexcept { return detail::ctx_mint::init_key{}; }
};
struct BackgroundOwner {
    [[nodiscard]] static constexpr auto key() noexcept { return detail::ctx_mint::bg_key{}; }
};
struct ForegroundOwner {
    [[nodiscard]] static constexpr auto key() noexcept { return detail::ctx_mint::fg_key{}; }
};
}  // namespace foundation::effects::host

namespace host_owner_attacks {

namespace fe = ::foundation::effects;

// ── Attacks that the keys refuse ─────────────────────────────────────

// No key is built from nothing, and none is a byte.
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::init_key>);
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::bg_key>);
static_assert(!std::is_default_constructible_v<fe::detail::ctx_mint::fg_key>);
static_assert(!std::is_trivially_copyable_v<fe::detail::ctx_mint::init_key>);

// ── The ledger ───────────────────────────────────────────────────────

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"a translation unit defines host::InitOwner, host::BackgroundOwner or host::ForegroundOwner and mints the "
     "production context",
     "the owners are declared and defined nowhere, so a first definition is legal C++; only a definition in the "
     "home of each entry point, and a guard that refuses every other definition, would refuse it"},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

// The ledger entry reproduces: three production contexts from a test file.
[[nodiscard]] inline bool a_self_made_owner_mints_every_context() {
    const fe::Init init = fe::mint_context<fe::Init>(fe::host::InitOwner::key());
    const fe::Bg bg = fe::mint_context<fe::Bg>(fe::host::BackgroundOwner::key());
    const auto fg = fe::mint_foreground_context(fe::host::ForegroundOwner::key());
    (void)init;
    (void)bg;
    return std::is_same_v<decltype(fg), const fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>;
}

}  // namespace host_owner_attacks

int main() {
    using namespace host_owner_attacks;
    if (!a_self_made_owner_mints_every_context()) {
        std::fprintf(stderr, "FAIL: stale ledger entry: a self-made owner no longer mints a context, delete it\n");
        return EXIT_FAILURE;
    }
    std::printf("test_host_owner_attacks: %zu ledger entry reproduces\n", std::size(kLedger));
    return EXIT_SUCCESS;
}
