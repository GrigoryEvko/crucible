// Adversarial tests of the old-tree background, startup and test contexts.
// Each case uses legal C++ and the public surface only.  The target is a
// context over Bg, Init or Test, built with no key and no source context.
// A case either proves that the surface refuses the attack, or it
// reproduces a route the surface cannot close.  Each such route is an
// entry of the ledger at the foot of this file, and the ledger only
// shrinks.
//
// The attacks that fail as a hard error are negative fixtures under
// test/effects_neg/, registered beside this test.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SessionMint.h>
#include <foundation/Lifetime.h>

#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

// This program does not link the hardware-probe tool, so the definition
// below is the only one.  It is the reproducer of the first ledger entry.
namespace crucible::tools {
struct HwProbeEntry {
    // Builds a background context through the key that the tool entry is
    // a friend of.
    [[nodiscard]] static constexpr effects::Bg background_context() noexcept {
        return effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});
    }
};
}  // namespace crucible::tools

namespace ctx_forge_attacks {

// ── The attacks the surface refuses ─────────────────────────────────

// A default constructor on a non-foreground context.
template <class T>
concept builds_from_empty_braces = requires { T{}; };
static_assert(!builds_from_empty_braces<eff::BgDrainCtx>);
static_assert(!builds_from_empty_braces<eff::BgCompileCtx>);
static_assert(!builds_from_empty_braces<eff::ColdInitCtx>);
static_assert(!builds_from_empty_braces<eff::TestRunnerCtx>);
static_assert(!builds_from_empty_braces<proto::EpochExecCtx<1, 1, eff::BgDrainCtx>>);
static_assert(builds_from_empty_braces<eff::HotFgCtx>, "the foreground context claims nothing, so it stays free");

// A default argument and a static member each name the default
// constructor, which does not exist.
static_assert(!std::is_default_constructible_v<eff::BgDrainCtx>);
static_assert(!std::is_default_constructible_v<eff::ColdInitCtx>);

// A copy list initialized from empty braces.
template <class T>
concept builds_from_nested_empty_braces = requires { T{{}}; };
static_assert(!builds_from_nested_empty_braces<eff::BgDrainCtx>);

// std::optional::emplace and std::construct_at each call the default
// constructor.
template <class T>
concept emplaces_from_nothing = requires(std::optional<T>& slot) { slot.emplace(); };
template <class T>
concept constructs_at_from_nothing = requires(T* where) { std::construct_at(where); };
static_assert(!emplaces_from_nothing<eff::Bg>);
static_assert(!emplaces_from_nothing<eff::BgDrainCtx>);
static_assert(!constructs_at_from_nothing<eff::Bg>);

// A promotion by the name of the capability alone.
template <class Ctx, class NewCap>
concept promotes_by_type_only = requires(Ctx const& ctx) { ctx.template with_cap<NewCap>(); };
static_assert(!promotes_by_type_only<eff::HotFgCtx, eff::Bg>);
static_assert(!promotes_by_type_only<eff::HotFgCtx, eff::Init>);
static_assert(!promotes_by_type_only<eff::HotFgCtx, eff::Test>);

// A widened row does not change the capability, so a foreground context
// cannot claim Bg through in_row.
template <class Ctx, class Row>
concept widens_row_to = requires(Ctx const& ctx) { ctx.template in_row<Row>(); };
static_assert(!widens_row_to<eff::HotFgCtx, eff::Row<eff::Effect::Bg>>);

// A rebuild that changes the capability.
template <class NewCtx, class OldCtx>
concept rebuilds_to = requires(OldCtx const& old) { eff::rebuild_ctx_to<NewCtx>(old); };
static_assert(!rebuilds_to<eff::BgDrainCtx, eff::HotFgCtx>);
static_assert(!rebuilds_to<eff::BgDrainCtx, eff::ColdInitCtx>);
static_assert(rebuilds_to<eff::BgDrainCtx, eff::BgCompileCtx>, "a rebuild keeps the capability it is handed");
static_assert(rebuilds_to<eff::HotFgCtx, eff::BgDrainCtx>, "a rebuild can drop to the foreground context");

// A derived class inherits no constructor it can use.  The aggregate
// form fails as a hard error, so neg_effects_bg_forged_by_derived_class
// pins it.
struct DerivedFromBgDefaulted : eff::Bg {
    DerivedFromBgDefaulted() = default;
};
static_assert(!std::is_default_constructible_v<DerivedFromBgDefaulted>);

// A copy of a context out of a holder raises no authority.  The holder
// needs a context to exist first, and the aggregate holder that builds
// its member from nothing is pinned by
// neg_effects_bg_drain_ctx_forged_by_aggregate_holder.
struct HolderOfBgDrain {
    eff::BgDrainCtx held;
};
static_assert(!builds_from_empty_braces<HolderOfBgDrain>);
static_assert(std::is_constructible_v<HolderOfBgDrain, eff::BgDrainCtx>);

// The byte routes.  Each of these types has user-provided constructors.
template <class T>
inline constexpr bool is_forgeable_from_bytes_v = std::is_trivially_copyable_v<T> || std::is_implicit_lifetime_v<T>;
static_assert(!is_forgeable_from_bytes_v<eff::Bg>);
static_assert(!is_forgeable_from_bytes_v<eff::Init>);
static_assert(!is_forgeable_from_bytes_v<eff::Test>);
static_assert(!is_forgeable_from_bytes_v<eff::detail::ctx_mint::bg_key>);
static_assert(!is_forgeable_from_bytes_v<eff::BgDrainCtx>);
static_assert(!is_forgeable_from_bytes_v<eff::ColdInitCtx>);
static_assert(!is_forgeable_from_bytes_v<proto::EpochExecCtx<1, 1, eff::BgDrainCtx>>);
static_assert(!is_forgeable_from_bytes_v<eff::Capability<eff::Effect::Block, eff::Bg>>);

// ── Routes that left the ledger for a guard ─────────────────────────
//
// Two routes still compile, and a guard refuses each one where code ships.
//
// The testing door.  testing::bg(), init() and test() mint a context in
// any translation unit.  scripts/check-ctx-testing-boundary.py counts each
// use of the door in include/, src/, vessel/, tools/ and examples/, the old
// tree included, and a use beyond the reviewed count of its file fails.
//
// An array, an aggregate or a union over a context.  Each is an
// implicit-lifetime type whatever its elements are, so
// std::start_lifetime_as gives a pointer to a context whose lifetime never
// started.  scripts/check-start-lifetime.sh refuses the library start
// outside the checked start, a frozen file and a negative fixture, and it
// reads the preprocessed text, so a macro does not hide it.  The checked
// start refuses each such type at compile time, as the assertions below
// pin.
struct HoldsBgDrain {
    eff::BgDrainCtx held;
};
union BgDrainOrByte {
    unsigned char byte;
    eff::BgDrainCtx held;
};
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<eff::BgDrainCtx[1]>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<HoldsBgDrain>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<BgDrainOrByte>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<eff::Bg[1]>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<eff::detail::ctx_mint::bg_key[1]>,
              "the checked lifetime start admits an array, an aggregate or a union over an old-tree context");

// ── The ledger ──────────────────────────────────────────────────────
//
// Each entry is a route that builds a context over Bg with legal code.
// Each has a reproducer that must keep reproducing: a fix that closes an
// entry makes its reproducer fail, and the fix then removes the entry
// and lowers the bound.  The bound only goes down.

struct KnownRoute {
    std::string_view name;
    std::string_view why_it_stays_open;
};

inline constexpr KnownRoute kLedger[] = {
    {"a friended host class defined by the attacker",
     "The keys friend BackgroundThread, Vigil, tools::HwProbeEntry and TestWitness by name. A program that links none "
     "of them can define the class itself and reach the key. Two definitions of one class in one program are an ODR "
     "violation, and no diagnostic is required. No friend spelling closes it: a friend function, a friend template "
     "and a key defined out of line in a library each have a definition that such a program can also write."},
    {"the inactive member of a union, or a pointer from a void pointer",
     "A union that holds a context beside a byte starts with the byte alive, and the address of its context member "
     "is a pointer to a context whose lifetime never started. A cast through void, std::malloc and an arena give "
     "the same pointer. A read through it is undefined behavior, and no property of the type refuses it: a union "
     "may hold any object type, and a static_cast from void to an object pointer is always well-formed. "
     "test/fixy/test_forgeable_proofs.cpp pins the same two routes for the new tree."},
};
static_assert(std::size(kLedger) <= 2, "the ledger only shrinks");

// The reproducers.  Each returns true while its route stays open.
[[nodiscard]] inline bool reproduces_host_class_route() noexcept {
    const eff::BgDrainCtx ctx{crucible::tools::HwProbeEntry::background_context()};
    return std::is_same_v<std::remove_cvref_t<decltype(ctx.cap())>, eff::Bg>;
}

// Counts the pointers the two routes give, and never reads through one.
[[nodiscard]] inline bool reproduces_union_and_void_routes() noexcept {
    alignas(eff::BgDrainCtx) unsigned char storage[sizeof(eff::BgDrainCtx)]{};
    BgDrainOrByte context_or_byte{.byte = 0};
    const eff::BgDrainCtx* const from_union = &context_or_byte.held;
    const eff::BgDrainCtx* const from_void = static_cast<const eff::BgDrainCtx*>(static_cast<const void*>(storage));
    return from_union != nullptr && from_void != nullptr;
}

// The epoch wrapper left the ledger.  Its one door checks the claim against
// the live epoch source, so a caller cannot state an epoch.  Neither empty
// braces nor the old one-argument door builds a wrapper any more, and
// test_session_epoch_source proves a false claim ends the process.
template <class Ctx>
concept builds_epoch_wrapper_without_a_source = requires(Ctx const& ctx) {
    proto::with_session_epoch<999, 999>(ctx);
};
static_assert(!std::is_default_constructible_v<proto::EpochExecCtx<999, 999, eff::HotFgCtx>>);
static_assert(!builds_epoch_wrapper_without_a_source<eff::HotFgCtx>);

}  // namespace ctx_forge_attacks

int main() {
    namespace cfa = ctx_forge_attacks;
    const bool open[] = {cfa::reproduces_host_class_route(), cfa::reproduces_union_and_void_routes()};
    static_assert(std::size(open) == std::size(cfa::kLedger), "each ledger entry has one reproducer");
    for (std::size_t index = 0; index < std::size(open); ++index) {
        if (!open[index]) {
            std::fprintf(stderr,
                         "test_effects_ctx_forge_attacks: the route '%.*s' no longer reproduces. Delete it from "
                         "the ledger and lower the bound: the ledger only shrinks.\n",
                         static_cast<int>(cfa::kLedger[index].name.size()), cfa::kLedger[index].name.data());
            return 1;
        }
    }
    std::printf("test_effects_ctx_forge_attacks: every refused attack refused, %zu ledger routes pinned\n",
                std::size(cfa::kLedger));
    return 0;
}
