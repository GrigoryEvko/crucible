// No proof type in foundation or fixy is built without a constructor.
//
// A proof type is a class that only a holder of authority can build: a
// mint key, a context, a token, a read proof.  Its gate is either a
// private constructor with a friend list, or a public constructor that
// takes another proof type, such as a passkey.  Two library routes build
// an object with no constructor call, so they never meet that gate:
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
// types come from witness lists.  In the two namespaces that hold the
// gated types, foundation::effects and foundation::permissions, every
// class template must have a witness or a place on the open list, so a
// new template there fails the build until someone classifies it.  Each
// type of utils/scripts/witness-roster.txt gets the same test, and a byte
// ledger names each roster type that stays trivially copyable by design.
//
// The route ledger at the foot names the routes that still forge a proof
// type after the repair.  Each one reads an object whose lifetime never
// started, which is undefined behavior, and no property of a type
// refuses it.  The ledger can only shrink: a route that no longer
// compiles fails its pin.

#include "every_header.h"
#include "witness_roster.h"

#include <foundation/Lifetime.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <meta>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp = ::foundation::permissions;
namespace fe = ::foundation::effects;
namespace sess = ::fixy::session;

namespace forgeable_proofs {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// The brand of one region, for the proof templates that spell one.
struct RegionBrand {};

// A passkey chain is at most a key, a token that takes the key, and a
// carrier that takes the token.  The bound stops a cycle of types whose
// constructors take each other.
inline constexpr int max_gate_depth = 4;

[[nodiscard]] consteval bool is_proof_shape_at(std::meta::info type, int depth);

// True when a public constructor takes a proof type, so that only a
// holder of that proof can call it.
[[nodiscard]] consteval bool is_gated_constructor(std::meta::info constructor, int depth) {
    for (const std::meta::info parameter : std::meta::parameters_of(constructor)) {
        if (is_proof_shape_at(std::meta::remove_cvref(std::meta::type_of(parameter)), depth + 1)) return true;
    }
    return false;
}

// True when the class declares a constructor, and every constructor
// that is public, and not deleted, copies or moves the class or takes a
// proof type.  An aggregate is not a proof type, because anyone can
// build one.  A public constructor template counts as open, because
// its parameters are not known before it is instantiated.
[[nodiscard]] consteval bool is_proof_shape_at(std::meta::info spelled, int depth) {
    if (depth > max_gate_depth) return false;
    const std::meta::info type = std::meta::dealias(spelled);
    if (!std::meta::is_class_type(type) || std::meta::is_union_type(type)) return false;
    if (!std::meta::is_complete_type(type) || std::meta::is_aggregate_type(type)) return false;
    bool declares_constructor = false;
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_constructor(member) && !std::meta::is_constructor_template(member)) continue;
        declares_constructor = true;
        if (std::meta::is_deleted(member)) continue;
        if (std::meta::is_copy_constructor(member) || std::meta::is_move_constructor(member)) continue;
        if (!std::meta::is_public(member)) continue;
        if (std::meta::is_constructor_template(member)) return false;
        if (!is_gated_constructor(member, depth)) return false;
    }
    return declares_constructor;
}

[[nodiscard]] consteval bool is_proof_shape(std::meta::info type) { return is_proof_shape_at(type, 0); }

[[nodiscard]] consteval bool is_implicit_lifetime(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^std::is_implicit_lifetime_v, {type}));
}

// True when the checked lifetime start admits the type.  It refuses a
// class that carries no_start_over_bytes, and every class that holds one.
[[nodiscard]] consteval bool starts_over_bytes(std::meta::info type) {
    return std::meta::extract<bool>(
        std::meta::substitute(^^::foundation::lifetime::ImplicitLifetimeThroughout, {type}));
}

// True when one of the two routes builds the type with no constructor.  A
// type that keeps a trivial copy constructor, so that a call passes it in
// a register, is implicit-lifetime.  Its annotation closes the second
// route, because the checked lifetime start then refuses it.
[[nodiscard]] consteval bool is_forgeable(std::meta::info spelled) {
    const std::meta::info type = std::meta::dealias(spelled);
    return std::meta::is_trivially_copyable_type(type) || (is_implicit_lifetime(type) && starts_over_bytes(type));
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

// Complexity: linear in the number of declarations under the namespace.
consteval void collect_class_templates(std::meta::info ns, std::vector<std::meta::info>& out) {
    for (const std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            collect_class_templates(member, out);
            continue;
        }
        if (std::meta::is_class_template(member)) out.push_back(member);
    }
}

using BgBase = fe::detail::ContextBase<fe::Bg, fe::detail::ctx_mint::bg_key, fe::Effect::Bg, fe::Effect::Alloc,
                                       fe::Effect::IO, fe::Effect::Block>;

// The template proof types, one specialization each.  A new proof
// template joins this list.  An execution context over a rostered
// context is here, because its one constructor that is not a copy takes
// the context, and a forged context passes every ctx-bound gate.
inline constexpr std::meta::info template_witnesses[] = {
    ^^fp::Permission<Region>,
    ^^fp::ReadView<Region>,
    ^^fp::WriteView<Region, RegionBrand>,
    ^^fp::ReadLoan<Region>,
    ^^fp::LentPermission<Region>,
    ^^fp::SharedPermissionGuard<Region, ::foundation::brand::DefaultBrand>,
    ^^fp::SharedPermissionPool<Region, ::foundation::brand::DefaultBrand>,
    ^^fp::FederationAdmission<Region>,
    ^^fe::Capability<fe::Effect::Alloc, fe::Bg>,
    ^^fe::Capability<fe::Effect::Init, fe::Init>,
    ^^fe::Capability<fe::Effect::Block, fe::Test>,
    ^^fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg>>,
    ^^fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>,
    ^^fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test>>,
    ^^fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>,
    ^^fe::ExecCtx<fe::ctx_cap::BrandedFg<Region>, fe::Row<>>,
    ^^fe::ctx_cap::BrandedFg<Region>,
    ^^fe::host::ProducerClaim<Region>,
    ^^BgBase,
    ^^sess::Transferable<int, Region>,
    ^^sess::Returned<int, Region>,
    ^^sess::Borrowed<int, Region>,
    ^^sess::PermHold<fp::PermSet<Region>>,
    ^^sess::SharedReader<Region>,
};

// A gated template whose value confers nothing.  A forged one grants no
// access, and its header pins that it confers none.
inline constexpr std::meta::info inert_templates[] = {
    ^^fp::SharedPermission,
};
static_assert(!fp::SharedPermission<Region>::confers_runtime_access,
              "SharedPermission is on the inert list only while it confers no access.  Give it a user-provided "
              "copy and move, and move it to the witness list, before it gates anything.");

// The class templates of the two namespaces that anyone may build.  Each
// is a trait, a row, a set of tags, a fold helper, or a value carrier
// with a public constructor.
inline constexpr std::meta::info open_templates[] = {
    ^^fe::canonical_row,
    ^^fe::cap_permitted_row,
    ^^fe::Computation,
    ^^fe::detail::AtomField,
    ^^fe::detail::conveys_authority,
    ^^fe::detail::conveys_authority_directly,
    ^^fe::detail::conveys_authority_listed,
    ^^fe::detail::effect_row_to_at,
    ^^fe::detail::extract_admits_payload,
    ^^fe::detail::is_computation,
    ^^fe::detail::lift_row,
    ^^fe::detail::row_concat,
    ^^fe::detail::row_difference_impl,
    ^^fe::detail::row_insert_unique,
    ^^fe::detail::row_intersection_impl,
    ^^fe::detail::row_union_recursive,
    ^^fe::is_cap_type,
    ^^fe::Row,
    ^^fe::ConcurrentRow,
    ^^fe::concurrent_row_descriptors,
    ^^fe::concurrent_row_value,
    ^^fe::detail::concurrent_row_n,
    ^^fe::detail::concurrent_row_payloads,
    ^^fe::detail::kind_occurrence_count,
    ^^fe::resource::CarbonGramsPerKwh,
    ^^fe::resource::CpuCoreBudget,
    ^^fe::resource::HbmBandwidth,
    ^^fe::resource::HbmBytes,
    ^^fe::resource::L2Bytes,
    ^^fe::resource::LlcBytes,
    ^^fe::resource::NicCq,
    ^^fe::resource::NicMr,
    ^^fe::resource::NicQp,
    ^^fe::resource::NicQueueBudget,
    ^^fe::resource::NicRingDepth,
    ^^fe::resource::NvlinkBandwidth,
    ^^fe::resource::PcieBandwidth,
    ^^fe::resource::PowerWatts,
    ^^fe::resource::RackPowerKw,
    ^^fe::resource::RegistersPerWarp,
    ^^fe::resource::SmBudget,
    ^^fe::resource::SmemBytes,
    ^^fe::resource::SwitchBufferCells,
    ^^fe::resource::SwitchEgressBw,
    ^^fe::resource::TcamEntries,
    ^^fe::resource::ThermalCelsius,
    ^^fe::resource::WarpSchedulerSlots,
    ^^fp::PermSet,
    ^^fp::can_split_into,
    ^^fp::has_split_authoring_witness,
    ^^fp::can_split_into_pack,
    ^^fp::has_split_pack_authoring_witness,
    ^^fp::tag::FederatedPeer,
};

struct ForgeVerdict {
    std::size_t walked_forgeable = 0;
    std::size_t forgeable_witnesses = 0;
    std::size_t unclassified_templates = 0;
    std::meta::info first_forgeable{};
    std::meta::info first_witness{};
    std::meta::info first_unclassified{};
};

[[nodiscard]] consteval bool is_listed(std::span<const std::meta::info> list, std::meta::info entry) {
    for (const std::meta::info listed : list) {
        if (listed == entry) return true;
    }
    return false;
}

[[nodiscard]] consteval bool has_witness(std::meta::info class_template) {
    for (const std::meta::info witness : template_witnesses) {
        if (std::meta::template_of(std::meta::dealias(witness)) == class_template) return true;
    }
    return false;
}

[[nodiscard]] consteval ForgeVerdict forge_verdict() {
    std::vector<std::meta::info> found;
    collect_forgeable(^^::foundation, found);
    collect_forgeable(^^::fixy, found);
    ForgeVerdict verdict;
    verdict.walked_forgeable = found.size();
    if (!found.empty()) verdict.first_forgeable = found.front();
    // A witness is a proof type because the list says so.  Its shape is
    // not asked: a public constructor template, such as the erasure
    // constructor of Permission, hides its gate from the shape test.
    for (const std::meta::info witness : template_witnesses) {
        if (!is_forgeable(witness)) continue;
        if (verdict.forgeable_witnesses++ == 0) verdict.first_witness = witness;
    }
    std::vector<std::meta::info> templates;
    collect_class_templates(^^fe, templates);
    collect_class_templates(^^fp, templates);
    for (const std::meta::info class_template : templates) {
        if (has_witness(class_template) || is_listed(inert_templates, class_template)
            || is_listed(open_templates, class_template))
            continue;
        if (verdict.unclassified_templates++ == 0) verdict.first_unclassified = class_template;
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

static_assert(verdict.walked_forgeable == 0,
              named("a proof type is trivially copyable or an implicit-lifetime type, so std::bit_cast or "
                    "std::start_lifetime_as builds it with no constructor.  Make its constructors user-provided.  "
                    "The first one: ",
                    verdict.first_forgeable));
static_assert(verdict.forgeable_witnesses == 0,
              named("a template proof type is trivially copyable or an implicit-lifetime type.  The witness: ",
                    verdict.first_witness));
static_assert(verdict.unclassified_templates == 0,
              named("a class template in foundation::effects or foundation::permissions has no witness and no "
                    "place on the open list.  Add a specialization to the witness list if it gates authority, "
                    "or add the template to the open list if anyone may build it.  The template: ",
                    verdict.first_unclassified));

// ── the witness roster ──────────────────────────────────────────────
//
// Each type of utils/scripts/witness-roster.txt attests to a fact that it
// cannot see, and a caller cannot reach its constructor.  Reflection
// cannot list the specializations of a template, so the build lists the
// roster types in witness_roster.h.  The walk applies the test of the walk
// above to each roster type.  A type that is trivially copyable, or that
// the checked lifetime start admits, is built from bytes with no
// constructor call.  A proof closes the two routes with a seal member,
// foundation::lifetime::byte_seal.
//
// The ledger names each roster type that the byte routes still build, and
// says why a byte image makes nothing that the public members of the type
// do not make.  An entry names a class, or the class template of a roster
// type.  The ledger only shrinks: an entry that covers no roster type that
// the byte routes build fails the walk.

// True when std::bit_cast builds a T from a byte array of its size.  The
// requirement reads the route and not a trait.
template <class T>
concept BitCastBuildsFromBytes =
    requires(std::array<unsigned char, sizeof(T)> const bytes) { std::bit_cast<T>(bytes); };

[[nodiscard]] consteval bool is_built_by_bit_cast(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^BitCastBuildsFromBytes, {std::meta::dealias(type)}));
}

struct ByteCopyAllowance {
    std::meta::info entity;
    std::string_view reason;
};

inline constexpr ByteCopyAllowance byte_copy_ledger[] = {
    {^^::fixy::Qtt,
     "for a trivially copyable payload, peek and mint_linear already build the same duplicate.  A payload that is "
     "not trivially copyable makes the wrapper not trivially copyable"},
    {^^::fixy::Tagged,
     "mint_tagged builds a tag that names a source from any value.  An earned tag makes the wrapper not trivially "
     "copyable"},
    {^^::fixy::fn, "mint_fn builds a binding of each accepted pack from any value, and a refused pack is no type"},
    {^^::fixy::Machine,
     "mint_machine opens a machine in any state, and data_mut writes the data of a live machine, so a byte image "
     "adds no state"},
    {^^::fixy::Monotonic,
     "mint_monotonic takes any initial value, and reset_under_quiescence puts any value in a live counter through "
     "a public member, so a byte image adds no value"},
    {^^::fixy::AtomicMonotonic,
     "mint_atomic_monotonic takes any initial value, and reset_under_quiescence puts any value in a live counter "
     "through a public member, so a byte image adds no value"},
    {^^::fixy::WriteOnce,
     "mint_write_once and set build each state that the bytes can describe, and std::destroy_at with "
     "std::construct_at from a copy puts an unset slot at the address of a set one through public members"},
    {^^::fixy::WriteOnceNonNull,
     "mint_write_once_non_null and set build each state that the bytes can describe, a null pointer is the unset "
     "state, and std::destroy_at with std::construct_at from a copy puts an unset slot at the address of a set one"},
    {^^::foundation::algebra::Graded,
     "the substrate keeps the trivial copyability of its payload by design, and foundation/algebra/Graded.h pins "
     "that parity.  A band of fixy/Bands.h is the substrate, so a byte image of a band claims a tier that mint_band "
     "did not set.  The entry stays until the parity rule changes"},
    {^^::foundation::permissions::SharedPermission,
     "a share confers no run-time access, and its header pins that.  The guard of the pool is the object that "
     "stands for a live share, and the walk above keeps the token on the inert list"},
};

// The ledger only shrinks.  Lower this count in the commit that removes an
// entry.  A new entry also needs a higher count, so the change shows the
// reason of that entry to its reviewer.
inline constexpr std::size_t byte_copy_ledger_size = 10;
static_assert(std::size(byte_copy_ledger) == byte_copy_ledger_size,
              "the byte-copy ledger changed size.  Lower byte_copy_ledger_size when an entry leaves.  A new entry "
              "needs a reason that a reviewer accepts, and then a higher count");

[[nodiscard]] consteval bool allowance_covers(std::meta::info entity, std::meta::info spelled) {
    const std::meta::info type = std::meta::dealias(spelled);
    return type == entity || (std::meta::has_template_arguments(type) && std::meta::template_of(type) == entity);
}

[[nodiscard]] consteval bool is_allowed(std::span<const ByteCopyAllowance> ledger, std::meta::info type) {
    for (const ByteCopyAllowance& allowance : ledger) {
        if (allowance_covers(allowance.entity, type) && !allowance.reason.empty()) return true;
    }
    return false;
}

// The first forgeable types that a verdict names, and no more.
inline constexpr std::size_t named_forgeable_limit = 32;

struct RosterVerdict {
    std::size_t walked = 0;
    std::size_t refused = 0;
    std::size_t allowed = 0;
    std::size_t forgeable = 0;
    std::size_t refused_but_built_by_bit_cast = 0;
    std::size_t stale_allowances = 0;
    std::array<std::meta::info, named_forgeable_limit> forgeable_types{};
    std::meta::info first_stale{};
};

// Complexity: the size of the roster times the size of the ledger.
[[nodiscard]] consteval RosterVerdict roster_verdict(std::span<const std::meta::info> roster,
                                                     std::span<const ByteCopyAllowance> ledger) {
    RosterVerdict result;
    for (const std::meta::info type : roster) {
        ++result.walked;
        if (!is_forgeable(type)) {
            ++result.refused;
            if (is_built_by_bit_cast(type)) ++result.refused_but_built_by_bit_cast;
            continue;
        }
        if (is_allowed(ledger, type)) {
            ++result.allowed;
            continue;
        }
        if (result.forgeable < named_forgeable_limit) result.forgeable_types[result.forgeable] = type;
        ++result.forgeable;
    }
    for (const ByteCopyAllowance& allowance : ledger) {
        bool covers_a_forgeable_type = false;
        for (const std::meta::info type : roster) {
            if (allowance_covers(allowance.entity, type) && is_forgeable(type)) covers_a_forgeable_type = true;
        }
        if (!covers_a_forgeable_type && result.stale_allowances++ == 0) result.first_stale = allowance.entity;
    }
    return result;
}

// True when the outcome names the type among its forgeable types.
[[nodiscard]] consteval bool names_forgeable(const RosterVerdict& outcome, std::meta::info type) {
    for (const std::meta::info forgeable : outcome.forgeable_types) {
        if (forgeable == type) return true;
    }
    return false;
}

// The lead and the name of each forgeable type of the outcome.  The copy
// is built one character at a time: GCC does not compare the address of a
// static string with a null pointer in a constant evaluation, and each
// string append makes that comparison.
[[nodiscard]] consteval std::string_view listed(std::string_view lead, const RosterVerdict& outcome) {
    if (outcome.forgeable == 0) return {};
    std::string text;
    const auto append = [&text](std::string_view part) {
        for (const char character : part)
            text.push_back(character);
    };
    append(lead);
    for (std::size_t index = 0; index < outcome.forgeable && index < named_forgeable_limit; ++index) {
        if (index != 0) append("; ");
        append(std::meta::display_string_of(outcome.forgeable_types[index]));
    }
    return std::define_static_string(text);
}

inline constexpr RosterVerdict roster = roster_verdict(::witness_roster::types, byte_copy_ledger);

static_assert(roster.walked == std::size(::witness_roster::types) && roster.walked > 0,
              "the roster walk read no type, so it proves nothing");
static_assert(roster.forgeable == 0,
              listed("a byte route builds a type of the witness roster with no constructor call.  Give it a "
                     "foundation::lifetime::byte_seal member, or a ledger entry that says why a byte image makes "
                     "nothing new.  The types: ",
                     roster));
static_assert(roster.refused_but_built_by_bit_cast == 0,
              "std::bit_cast builds a roster type that the walk refuses, so the walk misreads the route");
static_assert(roster.stale_allowances == 0,
              named("a ledger entry covers no roster type that a byte route builds.  Delete it: the ledger only "
                    "shrinks.  The entry: ",
                    roster.first_stale));
static_assert(roster.refused + roster.allowed == roster.walked);

// The walk is not vacuous.  Over a roster made to measure, it flags the
// probe that std::bit_cast builds from its trivial copy, and the pinned
// probe whose copies are all deleted: GCC counts that class trivially
// copyable, and std::bit_cast builds it too.  It passes the two sealed
// probes, a ledger entry clears what it names, and an entry that clears
// nothing is reported.
class BitCastProbe {
    int value_ = 0;
    constexpr BitCastProbe() noexcept = default;
};
class PinnedProbe {
    constexpr PinnedProbe() noexcept {}

public:
    PinnedProbe(const PinnedProbe&) = delete;
    PinnedProbe& operator=(const PinnedProbe&) = delete;
};
class SealedProbe {
    int value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr SealedProbe() noexcept = default;
};
class SealedPinnedProbe {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr SealedPinnedProbe() noexcept {}

public:
    SealedPinnedProbe(const SealedPinnedProbe&) = delete;
    SealedPinnedProbe& operator=(const SealedPinnedProbe&) = delete;
};
inline constexpr std::meta::info probe_roster[] = {^^BitCastProbe, ^^PinnedProbe, ^^SealedProbe, ^^SealedPinnedProbe};
inline constexpr ByteCopyAllowance probe_ledger[] = {{^^BitCastProbe, "a probe"}, {^^PinnedProbe, "a probe"}};
inline constexpr ByteCopyAllowance stale_probe_ledger[] = {{^^SealedProbe, "a probe that no byte route builds"}};
static_assert(BitCastBuildsFromBytes<BitCastProbe> && BitCastBuildsFromBytes<PinnedProbe>
              && !BitCastBuildsFromBytes<SealedProbe> && !BitCastBuildsFromBytes<SealedPinnedProbe>);
static_assert(roster_verdict(probe_roster, {}).forgeable == 2
                  && names_forgeable(roster_verdict(probe_roster, {}), ^^BitCastProbe)
                  && names_forgeable(roster_verdict(probe_roster, {}), ^^PinnedProbe),
              "the roster walk misses a type that a byte route builds");
static_assert(roster_verdict(probe_roster, {}).refused == 2, "the roster walk flags a sealed type");
static_assert(roster_verdict(probe_roster, probe_ledger).forgeable == 0
                  && roster_verdict(probe_roster, probe_ledger).allowed == 2,
              "a ledger entry does not clear the type that it names");
static_assert(roster_verdict(probe_roster, stale_probe_ledger).stale_allowances == 1,
              "a ledger entry that covers no forgeable type is not reported");

// An aggregate that holds the proof.  An aggregate is an
// implicit-lifetime type whatever its members are.
template <class Proof>
struct HoldsProof {
    Proof proof;
};

// A union that holds the proof beside a byte.
template <class Proof>
union ProofOrByte {
    unsigned char byte;
    Proof proof;
};

// ── the union audit ─────────────────────────────────────────────────
//
// A union that holds a proof type beside another member starts with the
// other member alive, and the address of the proof member is a pointer to
// a proof whose lifetime never started.  No property of the proof type
// refuses that, so foundation and fixy declare no such union.  The audit
// walks every union in the two namespaces, also a union nested in a class,
// and refuses one whose members hold a proof type at any depth: through an
// array, a base class or a member of an aggregate.  Reflection cannot see a
// union template or a union local to a function, so
// utils/scripts/check-proof-routes.py refuses each union definition in the tree,
// user code included, outside a reviewed list.  This file is on that list,
// because its unions are the probes.

// A class nests deeper than this, and the walk reads it as holding a proof.
inline constexpr int max_hold_depth = 16;

// True when the type, an element of it, a base or a member of it holds a
// proof type.  A nest deeper than the bound counts as a hold, so the audit
// fails closed.  Complexity: linear in the number of subobject
// declarations that the walk reaches.
[[nodiscard]] consteval bool holds_proof_at(std::meta::info spelled, int depth) {
    if (depth > max_hold_depth) return true;
    const std::meta::info type = std::meta::remove_cv(std::meta::dealias(spelled));
    if (std::meta::is_array_type(type)) return holds_proof_at(std::meta::remove_all_extents(type), depth + 1);
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return false;
    if (!std::meta::is_complete_type(type)) return false;
    if (is_proof_shape(type)) return true;
    for (const std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked())) {
        if (holds_proof_at(std::meta::type_of(base), depth + 1)) return true;
    }
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
        if (holds_proof_at(std::meta::type_of(member), depth + 1)) return true;
    }
    return false;
}

struct UnionAudit {
    std::size_t unions_walked = 0;
    std::size_t unions_holding_a_proof = 0;
    std::meta::info first_holding{};
};

// Walks each union that is a member of the scope, of a namespace inside
// it, or of a class inside it.  Complexity: linear in the number of
// declarations under the scope.
consteval void audit_unions(std::meta::info scope, UnionAudit& audit) {
    for (const std::meta::info member : std::meta::members_of(scope, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member)) {
            if (!std::meta::is_namespace_alias(member)) audit_unions(member, audit);
            continue;
        }
        if (!std::meta::is_type(member) || std::meta::is_type_alias(member)) continue;
        if (std::meta::has_template_arguments(member) || !std::meta::is_complete_type(member)) continue;
        if (std::meta::is_union_type(member)) {
            ++audit.unions_walked;
            if (holds_proof_at(member, 0) && audit.unions_holding_a_proof++ == 0) audit.first_holding = member;
        }
        if (std::meta::is_class_type(member) || std::meta::is_union_type(member)) audit_unions(member, audit);
    }
}

[[nodiscard]] consteval UnionAudit union_audit() {
    UnionAudit audit;
    audit_unions(^^::foundation, audit);
    audit_unions(^^::fixy, audit);
    return audit;
}

inline constexpr UnionAudit unions = union_audit();
static_assert(unions.unions_holding_a_proof == 0,
              named("a union in foundation or fixy holds a proof type, so the address of that member reaches a "
                    "proof whose lifetime never started.  Hold the proof outside a union.  The union: ",
                    unions.first_holding));

// The audit is not vacuous: over a scope made to measure, it walks each
// union, also one nested in a class, and it finds the one that holds a
// proof through an array inside an aggregate.
template <class Proof>
struct NestsProof {
    int count;
    Proof proof[2];
};
union PlainValues {
    int whole;
    float part;
};
namespace union_audit_probe {
union HoldsNestedProof {
    unsigned char byte;
    NestsProof<fp::perm_mint_key> nested;
};
union Plain {
    int whole;
    float part;
};
struct Outer {
    union Inner {
        int whole;
        fe::Bg context;
    };
};
}  // namespace union_audit_probe

[[nodiscard]] consteval UnionAudit probe_audit() {
    UnionAudit audit;
    audit_unions(^^union_audit_probe, audit);
    return audit;
}
static_assert(probe_audit().unions_walked == 3 && probe_audit().unions_holding_a_proof == 2,
              "the union audit misses a union, or a union that holds a proof, in a scope made to measure");
static_assert(holds_proof_at(^^ProofOrByte<fe::Bg>, 0));
static_assert(holds_proof_at(^^NestsProof<fp::perm_mint_key>, 0));
static_assert(holds_proof_at(^^HoldsProof<fe::Capability<fe::Effect::Alloc, fe::Bg>>, 0));
static_assert(!holds_proof_at(^^PlainValues, 0) && !holds_proof_at(^^Region, 0));

// The walk is not vacuous: a proof type made to measure is found, and a
// passkey gate counts as a gate.
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
struct KeyedProbe {
    explicit constexpr KeyedProbe(ClosedProbe) noexcept {}
    KeyedProbe(const KeyedProbe&) = default;
};
struct OpenProbe {
    explicit constexpr OpenProbe(int) noexcept {}
};
static_assert(is_proof_shape(^^ForgeableProbe) && is_forgeable(^^ForgeableProbe));
static_assert(is_proof_shape(^^ClosedProbe) && !is_forgeable(^^ClosedProbe));
static_assert(is_proof_shape(^^KeyedProbe) && is_forgeable(^^KeyedProbe),
              "a public constructor that takes a proof type is a gate, and a defaulted copy leaves it open");
static_assert(!is_proof_shape(^^OpenProbe), "a public constructor from an int is no gate");
static_assert(!is_proof_shape(^^Region), "an empty aggregate is not a proof type");
static_assert(is_proof_shape(^^fe::ExecCtx<>) && !is_forgeable(^^fe::ExecCtx<>),
              "the foreground context is built only from the key of the producer claim");

// ── the route ledger ────────────────────────────────────────────────
//
// Routes that still forge every proof type above.  None of them is
// refused by a property of the type, and each one gives a pointer to an
// object whose lifetime never started.  Implicit object creation starts
// the life of an implicit-lifetime type only, and a proof type is not
// one, so an array or an aggregate of proof types starts with no element
// alive.  A read through the pointer is undefined behavior.  main()
// compiles and runs each route for each pinned proof type, and never
// reads through the pointer.  A route that no longer compiles fails the
// build, and its entry then leaves the ledger.
//
// Four routes left the ledger for a guard, not for a language rule.
// They still compile: std::start_lifetime_as over an array of proofs,
// std::start_lifetime_as_array over proofs, and std::start_lifetime_as
// over an aggregate or a std::array that holds a proof.  Each one names
// std::start_lifetime_as, and utils/scripts/check-start-lifetime.py refuses
// that name outside a reviewed list whose element types are
// implicit-lifetime types.  Its self-test plants each of the four routes.
// foundation::lifetime::start_as_array, which new code uses, refuses the
// same four routes at compile time.  The assertions below pin that
// refusal for each proof type of the ledger.
// Two routes stay open in the language, and main() runs each one:
//
//   the inactive member of a union   a union may hold any object type,
//                                    and taking the address of a member
//                                    that is not active is well-formed
//   a pointer from a void pointer    std::malloc, an allocator, an arena
//                                    and a cast through void each give a
//                                    typed pointer with no object, and
//                                    the language refuses none of them
//
// utils/scripts/check-proof-routes.py refuses the two routes in the tree.  It
// refuses each union definition outside a reviewed list, and each cast to
// a pointer or a reference, each allocator and each raw allocation whose
// type names a proof type.  This file is on its list as the probe.  Two
// shapes pass that guard, and they stay open with this argument:
//
//   a cast whose target is a         static_cast<T*>(buffer) in an arena
//   template parameter               or a container names T.  The guard
//                                    cannot know which argument reaches T,
//                                    and every allocator of the standard
//                                    library is such a site
//   a pointer value copied with      the copy names no proof type at the
//   std::memcpy                      call, so no text shows the route
//
// The same guard refuses three routes that turn off the access check: an
// explicit instantiation, an explicit specialization of a function
// template, and an explicit specialization whose template argument is a
// pointer to a member.  It also refuses a specialization of a class
// template that a proof type befriends, outside the file that defines it.

// True when the checked lifetime start refuses each of the four routes
// that left the ledger for the proof type.
template <class Proof>
inline constexpr bool checked_start_refuses_every_route =
    !::foundation::lifetime::ImplicitLifetimeThroughout<Proof>
    && !::foundation::lifetime::ImplicitLifetimeThroughout<Proof[1]>
    && !::foundation::lifetime::ImplicitLifetimeThroughout<HoldsProof<Proof>>
    && !::foundation::lifetime::ImplicitLifetimeThroughout<std::array<Proof, 1>>
    && !::foundation::lifetime::ImplicitLifetimeThroughout<ProofOrByte<Proof>>;

// Runs the two routes and counts the pointers they give.
// Complexity: constant.
template <class Proof>
[[nodiscard]] std::size_t count_open_routes() noexcept {
    alignas(Proof) unsigned char storage[sizeof(Proof)]{};
    ProofOrByte<Proof> proof_or_byte{.byte = 0};
    // A member of a union that is not active.
    Proof* const from_union = &proof_or_byte.proof;
    // A conversion through void names no reinterpret_cast, so the
    // reinterpret guard does not see it.  No rule of the language refuses
    // it, so this entry is permanent.
    Proof* const from_void = static_cast<Proof*>(static_cast<void*>(storage));
    const Proof* const routes[] = {from_union, from_void};
    std::size_t open = 0;
    for (const Proof* const route : routes)
        open += route != nullptr ? 1u : 0u;
    return open;
}

// The routes that the repair closed, and the routes that never worked.
// Each is a trait or a requires-expression, so a route that starts to
// compile fails the build here and not only in a negative fixture.
// std::start_lifetime_as checks its mandate in its body, which a
// requires-expression does not see, so its route is read off the trait
// that the mandate reads.
template <class Proof>
concept bit_cast_route_compiles = requires(unsigned char byte) { std::bit_cast<Proof>(byte); };
template <class Proof>
concept construct_at_route_compiles = requires(Proof* storage) { std::construct_at(storage); };
template <class Proof>
concept aggregate_bit_cast_route_compiles =
    requires(unsigned char byte) { std::bit_cast<HoldsProof<Proof>>(byte).proof; };

template <class Proof>
inline constexpr bool every_closed_route_refused =
    !bit_cast_route_compiles<Proof> && !std::is_implicit_lifetime_v<Proof> && !construct_at_route_compiles<Proof>
    && !aggregate_bit_cast_route_compiles<Proof>;

static_assert(every_closed_route_refused<fe::Bg> && every_closed_route_refused<fe::Init>
                  && every_closed_route_refused<fe::Test>,
              "a context is built without its door again");
static_assert(every_closed_route_refused<fe::detail::ctx_mint::bg_key>
                  && every_closed_route_refused<fe::detail::ctx_mint::init_key>
                  && every_closed_route_refused<fe::detail::ctx_mint::test_key>
                  && every_closed_route_refused<fe::cap_mint_key>,
              "a mint key is built without its door again");
static_assert(every_closed_route_refused<fe::Capability<fe::Effect::Block, fe::Bg>>,
              "a capability is built without its key again");
static_assert(every_closed_route_refused<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Block>>>,
              "an execution context over Bg is built without a Bg again");
static_assert(checked_start_refuses_every_route<fe::Bg>
                  && checked_start_refuses_every_route<fe::detail::ctx_mint::bg_key>
                  && checked_start_refuses_every_route<fe::cap_mint_key>
                  && checked_start_refuses_every_route<fp::perm_mint_key>
                  && checked_start_refuses_every_route<fp::Permission<Region>>,
              "the checked lifetime start admits an array, an aggregate or a union that holds a proof type");

// A key reaches the door only through a friend.  The empty braces name
// the private constructor from here, and the wrong key fails the
// constraint of the door.
template <class Ctx, class Key>
concept door_opens_from_here = requires { ::foundation::effects::mint_context<Ctx, Key>({}); };
static_assert(!door_opens_from_here<fe::Bg, fe::detail::ctx_mint::bg_key>);
static_assert(!door_opens_from_here<fe::Bg, fe::detail::ctx_mint::init_key>);

// A capability that a lambda captures by move stays linear: the lambda
// is not copyable.
[[nodiscard]] inline auto capture_by_move(fe::Capability<fe::Effect::Alloc, fe::Bg>&& capability) noexcept {
    return [held = std::move(capability)]() noexcept { (void)held; };
}
using CapturingLambda = decltype(capture_by_move(std::declval<fe::Capability<fe::Effect::Alloc, fe::Bg>>()));
static_assert(!std::is_copy_constructible_v<CapturingLambda> && std::is_move_constructible_v<CapturingLambda>);

// A class derived from the base of a context is not a context, and it
// cannot build the base: the constructors of the base are private, and
// the context is the one friend.  No object of the derived class exists,
// so no downcast from its base reaches a Bg.
struct DerivedFromContextBase : BgBase {};
static_assert(!fe::IsContext<DerivedFromContextBase> && !fe::IsCapType<DerivedFromContextBase>);
static_assert(!std::is_default_constructible_v<DerivedFromContextBase>
                  && !std::is_copy_constructible_v<DerivedFromContextBase>,
              "a class derived from the base of a context must not be buildable");

// The name of a declaration with each enclosing scope, from the global
// namespace down, so that a report of the guard names the real class.
// Complexity: linear in the depth of the scopes.
[[nodiscard]] consteval std::string qualified_name(std::meta::info declaration) {
    std::string name = "::" + std::string{std::meta::identifier_of(declaration)};
    for (std::meta::info scope = std::meta::parent_of(declaration); scope != ^^::;
         scope = std::meta::parent_of(scope)) {
        const std::string_view part =
            std::meta::has_identifier(scope) ? std::meta::identifier_of(scope) : std::string_view{"(anonymous)"};
        name = "::" + std::string{part} + name;
    }
    return name;
}

// One line of the dump: the name that a source spells, a tab, and the
// qualified name.
[[nodiscard]] consteval const char* proof_name_line(std::meta::info declaration) {
    return std::define_static_string(std::string{std::meta::identifier_of(declaration)} + '\t'
                                     + qualified_name(declaration));
}

// The names of the proof types, for utils/scripts/check-proof-routes.py: each
// class of the two namespaces that has the shape of a proof, and the
// template of each witness.  main() prints them under --proof-names.
// Complexity: linear in the number of declarations under the namespace.
consteval void collect_proof_names(std::meta::info ns, std::vector<const char*>& out) {
    for (const std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            collect_proof_names(member, out);
            continue;
        }
        if (!std::meta::is_type(member) || std::meta::is_type_alias(member)) continue;
        if (std::meta::has_template_arguments(member) || !std::meta::has_identifier(member)) continue;
        if (is_proof_shape(member)) out.push_back(proof_name_line(member));
    }
}

[[nodiscard]] consteval std::vector<const char*> proof_names() {
    std::vector<const char*> names;
    collect_proof_names(^^::foundation, names);
    collect_proof_names(^^::fixy, names);
    for (const std::meta::info witness : template_witnesses) {
        names.push_back(proof_name_line(std::meta::template_of(std::meta::dealias(witness))));
    }
    return names;
}

inline constexpr auto proof_name_list = std::define_static_array(proof_names());
static_assert(proof_name_list.size() > std::size(template_witnesses),
              "the walk found no proof-shaped class, so the name list proves nothing");

// A plain count, because the verdict holds reflections and cannot reach
// run time.
inline constexpr std::size_t witnesses_checked = std::size(template_witnesses);
inline constexpr std::size_t templates_open = std::size(open_templates);
inline constexpr std::size_t unions_walked = unions.unions_walked;

}  // namespace forgeable_proofs

int main(int argc, char** argv) {
    namespace fps = forgeable_proofs;
    if (argc > 1 && std::string_view{argv[1]} == "--proof-names") {
        for (const char* name : fps::proof_name_list)
            std::printf("%s\n", name);
        return 0;
    }
    // Two routes for each of the five pinned proof types.  A route that no
    // longer gives a pointer lowers the count, and the entry must leave
    // the ledger.
    constexpr std::size_t expected_open_routes = 2u * 5u;
    const std::size_t open_routes =
        fps::count_open_routes<fe::Bg>() + fps::count_open_routes<fe::detail::ctx_mint::bg_key>()
        + fps::count_open_routes<fe::cap_mint_key>() + fps::count_open_routes<fp::perm_mint_key>()
        + fps::count_open_routes<fp::Permission<fps::Region>>();
    if (open_routes != expected_open_routes) {
        std::fprintf(stderr,
                     "test_forgeable_proofs: %zu routes of the ledger still give a pointer, not %zu.  Delete each "
                     "closed route from the ledger: it only shrinks.\n",
                     open_routes, expected_open_routes);
        return 1;
    }
    std::printf("test_forgeable_proofs: no forgeable proof type, %zu template witnesses closed, %zu open "
                "templates, %zu unions audited, %zu ledger routes pinned\n",
                fps::witnesses_checked, fps::templates_open, fps::unions_walked, open_routes);
    return 0;
}
