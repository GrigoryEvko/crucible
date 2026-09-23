// No proof type in foundation or fixy is built without a constructor.
//
// A proof type is a class whose only public constructors copy or move
// it: a mint key, a context, a token, a read proof.  The private
// constructor and its friend list are the whole gate.  Two library
// routes build an object with no constructor call, so they never meet
// that gate:
//
//   std::bit_cast           builds any trivially copyable type from bytes
//   std::start_lifetime_as  starts the life of any implicit-lifetime type
//                           over a buffer
//
// Both are legal code with no undefined behavior.  So a proof type must
// be neither trivially copyable nor an implicit-lifetime type.  The
// usual repair is a user-provided constructor in place of a defaulted
// one: it compiles to nothing for an empty class, and it is not trivial.
//
// The walk reads every class that is not a template in ::foundation and
// ::fixy, from the headers that every_header.h names.  Reflection cannot
// enumerate the specializations of a template, so the template proof
// types come from a witness list.  The ledger at the foot names the
// forgeable proof types that remain.  It can only shrink: an entry that
// becomes safe, or that the walk no longer finds, fails the walk.

#include "every_header.h"

#include <cstddef>
#include <cstdio>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace forgeable_proofs {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// True when the class declares a constructor, and every constructor
// that is public, and not deleted, copies or moves the class.  An
// aggregate is not a proof type, because anyone can build one.
[[nodiscard]] consteval bool is_proof_shape(std::meta::info type) {
    if (!std::meta::is_class_type(type) || std::meta::is_union_type(type)) return false;
    if (!std::meta::is_complete_type(type) || std::meta::is_aggregate_type(type)) return false;
    bool declares_constructor = false;
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_constructor(member) && !std::meta::is_constructor_template(member)) continue;
        declares_constructor = true;
        if (std::meta::is_deleted(member)) continue;
        if (std::meta::is_copy_constructor(member) || std::meta::is_move_constructor(member)) continue;
        if (std::meta::is_public(member)) return false;
    }
    return declares_constructor;
}

[[nodiscard]] consteval bool is_implicit_lifetime(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^std::is_implicit_lifetime_v, {type}));
}

// True when one of the two routes builds the type with no constructor.
[[nodiscard]] consteval bool is_forgeable(std::meta::info type) {
    return std::meta::is_trivially_copyable_type(type) || is_implicit_lifetime(type);
}

// Complexity: linear in the number of declarations under the two
// namespaces.
consteval void collect_forgeable(std::meta::info ns, std::vector<std::meta::info>& out) {
    for (const std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            collect_forgeable(member, out);
            continue;
        }
        if (!std::meta::is_type(member) || std::meta::is_type_alias(member)) continue;
        if (std::meta::has_template_arguments(member)) continue;
        if (is_proof_shape(member) && is_forgeable(member)) out.push_back(member);
    }
}

// The template proof types, one specialization each.  A new proof
// template joins this list.
inline constexpr std::meta::info template_witnesses[] = {
    ^^fp::Permission<Region>,
    ^^fp::ReadView<Region>,
    ^^fp::SharedPermissionGuard<Region, ::foundation::brand::DefaultBrand>,
    ^^sess::Transferable<int, Region>,
    ^^sess::Returned<int, Region>,
    ^^sess::Borrowed<int, Region>,
};

// ── the ledger ──────────────────────────────────────────────────────
//
// The proof types the walk finds forgeable.  Each is in a header this
// file does not own, and each is a finding for that header's owner.
//
// The context and capability family of foundation/effects: the three
// mint keys of the contexts, the three contexts, and the capability
// mint key.  std::bit_cast<foundation::effects::Bg>(char{0}) builds a
// background context in any scope, and every ctx-bound gate then admits
// that scope.  Effect.h states that a context is copied into ExecCtx and
// must be trivially copyable, so the repair is a design decision there.
inline constexpr std::meta::info forgeable_ledger[] = {
    ^^::foundation::effects::detail::ctx_mint::bg_key,
    ^^::foundation::effects::detail::ctx_mint::init_key,
    ^^::foundation::effects::detail::ctx_mint::test_key,
    ^^::foundation::effects::Bg,
    ^^::foundation::effects::Init,
    ^^::foundation::effects::Test,
    ^^::foundation::effects::cap_mint_key,
    // A session event is a record on the replay path, and its private
    // constructor is its witness.  std::bit_cast builds one from bytes, so
    // a log can hold an event that no session step wrote.
    ^^::fixy::session::SessionEvent,
};

struct ForgeVerdict {
    std::size_t walked_forgeable = 0;
    std::size_t outside_ledger = 0;
    std::size_t stale_ledger = 0;
    std::size_t forgeable_witnesses = 0;
    std::meta::info first_outside{};
    std::meta::info first_stale{};
    std::meta::info first_witness{};
};

[[nodiscard]] consteval bool on_ledger(std::meta::info type) {
    for (const std::meta::info entry : forgeable_ledger) {
        if (entry == type) return true;
    }
    return false;
}

[[nodiscard]] consteval ForgeVerdict forge_verdict() {
    std::vector<std::meta::info> found;
    collect_forgeable(^^::foundation, found);
    collect_forgeable(^^::fixy, found);
    ForgeVerdict verdict;
    verdict.walked_forgeable = found.size();
    for (const std::meta::info type : found) {
        if (on_ledger(type)) continue;
        if (verdict.outside_ledger++ == 0) verdict.first_outside = type;
    }
    for (const std::meta::info entry : forgeable_ledger) {
        bool still_found = false;
        for (const std::meta::info type : found) {
            if (type == entry) still_found = true;
        }
        if (!still_found && verdict.stale_ledger++ == 0) verdict.first_stale = entry;
    }
    for (const std::meta::info witness : template_witnesses) {
        if (!is_proof_shape(witness) || !is_forgeable(witness)) continue;
        if (verdict.forgeable_witnesses++ == 0) verdict.first_witness = witness;
    }
    return verdict;
}

inline constexpr ForgeVerdict verdict = forge_verdict();

[[nodiscard]] consteval std::string_view named(std::string_view lead, std::meta::info type) {
    if (type == std::meta::info{}) return {};
    std::string text{lead};
    text += std::meta::display_string_of(type);
    return std::define_static_string(text);
}

static_assert(verdict.outside_ledger == 0,
              named("a proof type is trivially copyable or an implicit-lifetime type, so std::bit_cast or "
                    "std::start_lifetime_as builds it with no constructor.  Make its constructors user-provided.  "
                    "The first one: ",
                    verdict.first_outside));
static_assert(verdict.stale_ledger == 0,
              named("a ledger entry is no longer forgeable, or the walk no longer finds it.  Delete the entry: "
                    "the ledger only shrinks.  The entry: ",
                    verdict.first_stale));
static_assert(verdict.forgeable_witnesses == 0,
              named("a template proof type is trivially copyable or an implicit-lifetime type.  The witness: ",
                    verdict.first_witness));

// The walk is not vacuous: a proof type made to measure is found.
struct ForgeableProbe {
    ForgeableProbe(const ForgeableProbe&) = default;

private:
    constexpr ForgeableProbe() noexcept = default;
};
struct ClosedProbe {
    constexpr ClosedProbe(const ClosedProbe&) noexcept {}

private:
    constexpr ClosedProbe() noexcept {}
};
static_assert(is_proof_shape(^^ForgeableProbe) && is_forgeable(^^ForgeableProbe));
static_assert(is_proof_shape(^^ClosedProbe) && !is_forgeable(^^ClosedProbe));
static_assert(!is_proof_shape(^^Region), "an empty aggregate is not a proof type");

// A plain count, because the verdict holds reflections and cannot reach
// run time.
inline constexpr std::size_t forgeable_found = verdict.walked_forgeable;
inline constexpr std::size_t ledger_size = std::size(forgeable_ledger);

}  // namespace forgeable_proofs

int main() {
    std::printf("test_forgeable_proofs: %zu forgeable proof types found, all %zu on the ledger\n",
                forgeable_proofs::forgeable_found, forgeable_proofs::ledger_size);
    return 0;
}
