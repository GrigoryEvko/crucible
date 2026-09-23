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
// new template there fails the build until someone classifies it.
//
// The route ledger at the foot names the routes that still compile after
// the repair.  Each one reads an object whose lifetime never started,
// which is undefined behavior, and no property of a type refuses it.
// The ledger can only shrink: a route that no longer compiles fails its
// pin.

#include "every_header.h"

#include <array>
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

// True when one of the two routes builds the type with no constructor.
[[nodiscard]] consteval bool is_forgeable(std::meta::info spelled) {
    const std::meta::info type = std::meta::dealias(spelled);
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
    ^^fp::SharedPermissionGuard<Region, ::foundation::brand::DefaultBrand>,
    ^^fp::SharedPermissionPool<Region, ::foundation::brand::DefaultBrand>,
    ^^fe::Capability<fe::Effect::Alloc, fe::Bg>,
    ^^fe::Capability<fe::Effect::Init, fe::Init>,
    ^^fe::Capability<fe::Effect::Block, fe::Test>,
    ^^fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg>>,
    ^^fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>,
    ^^fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test>>,
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
// is a trait, a row, a set of tags, a fold helper, a self-test probe, or
// a value carrier with a public constructor.
inline constexpr std::meta::info open_templates[] = {
    ^^fe::canonical_row,
    ^^fe::cap_permitted_row,
    ^^fe::Computation,
    ^^fe::detail::AtomField,
    ^^fe::detail::capability_self_test::cap_consume_callable_lvalue,
    ^^fe::detail::capability_self_test::cap_consume_callable_rvalue,
    ^^fe::detail::conveys_authority,
    ^^fe::detail::conveys_authority_directly,
    ^^fe::detail::conveys_authority_listed,
    ^^fe::detail::effect_row_to_at,
    ^^fe::detail::extract_admits_payload,
    ^^fe::detail::is_computation,
    ^^fe::detail::lift_row,
    ^^fe::detail::lift_self_test::sample_non_row,
    ^^fe::detail::lift_self_test::sample_row,
    ^^fe::detail::row_concat,
    ^^fe::detail::row_difference_impl,
    ^^fe::detail::row_insert_unique,
    ^^fe::detail::row_intersection_impl,
    ^^fe::detail::row_union_recursive,
    ^^fe::is_cap_type,
    ^^fe::is_effect_row,
    ^^fe::is_exec_ctx,
    ^^fe::is_subrow,
    ^^fe::Row,
    ^^fp::detail::all_distinct_tags_rec,
    ^^fp::detail::combine_n_manifest,
    ^^fp::detail::ctx_admits_tuple,
    ^^fp::detail::is_permission_impl,
    ^^fp::detail::is_shared_permission_impl,
    ^^fp::detail::perm_brand_or_void,
    ^^fp::detail::permission_fork_ctx_callables,
    ^^fp::detail::permission_row_lookup,
    ^^fp::detail::row_payload_of_tag,
    ^^fp::PermSet,
    ^^fp::perm_set_canonicalize,
    ^^fp::perm_set_difference,
    ^^fp::perm_set_insert,
    ^^fp::perm_set_remove,
    ^^fp::perm_set_union,
    ^^fp::splits_into,
    ^^fp::splits_into_authoring_witness,
    ^^fp::splits_into_pack,
    ^^fp::splits_into_pack_authoring_witness,
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
static_assert(!is_proof_shape(^^fe::ExecCtx<>), "the foreground context claims nothing, and anyone may build it");

// ── the route ledger ────────────────────────────────────────────────
//
// Routes that still compile for every proof type above.  None of them
// is refused by a property of the type, and each one gives a pointer to
// an object whose lifetime never started.  Implicit object creation
// starts the life of an implicit-lifetime type only, and a proof type is
// not one, so an array or an aggregate of proof types starts with no
// element alive.  A read through the pointer is undefined behavior.
// main() compiles and runs each route for each pinned proof type, and
// never reads through the pointer.  A route that no longer compiles
// fails the build, and its entry then leaves the ledger.

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

// Runs the six routes over one buffer and counts the pointers they give.
// Complexity: constant.
template <class Proof>
[[nodiscard]] std::size_t count_open_routes() noexcept {
    alignas(std::max_align_t) unsigned char storage[sizeof(std::array<Proof, 1>) + sizeof(HoldsProof<Proof>)]{};
    ProofOrByte<Proof> proof_or_byte{.byte = 0};
    // An array type is an implicit-lifetime type for any element.
    Proof* const from_array = *std::start_lifetime_as<Proof[1]>(storage);
    // The helper for arrays has no mandate on the element type at all.
    Proof* const from_array_helper = std::start_lifetime_as_array<Proof>(storage, 1);
    Proof* const from_aggregate = &std::start_lifetime_as<HoldsProof<Proof>>(storage)->proof;
    Proof* const from_std_array = std::start_lifetime_as<std::array<Proof, 1>>(storage)->data();
    // A member of a union that is not active.
    Proof* const from_union = &proof_or_byte.proof;
    // A conversion through void names no reinterpret_cast, so the
    // reinterpret guard does not see it.  No rule of the language refuses
    // it, so this entry is permanent.
    Proof* const from_void = static_cast<Proof*>(static_cast<void*>(storage));
    const Proof* const routes[] = {from_array, from_array_helper, from_aggregate,
                                   from_std_array, from_union, from_void};
    std::size_t open = 0;
    for (const Proof* const route : routes) open += route != nullptr ? 1u : 0u;
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
concept aggregate_bit_cast_route_compiles = requires(unsigned char byte) {
    std::bit_cast<HoldsProof<Proof>>(byte).proof;
};

template <class Proof>
inline constexpr bool every_closed_route_refused = !bit_cast_route_compiles<Proof> && !std::is_implicit_lifetime_v<Proof>
                                                   && !construct_at_route_compiles<Proof>
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

// A plain count, because the verdict holds reflections and cannot reach
// run time.
inline constexpr std::size_t witnesses_checked = std::size(template_witnesses);
inline constexpr std::size_t templates_open = std::size(open_templates);

}  // namespace forgeable_proofs

int main() {
    namespace fps = forgeable_proofs;
    // Six routes for each of the five pinned proof types.  A route that no
    // longer gives a pointer lowers the count, and the entry must leave
    // the ledger.
    constexpr std::size_t expected_open_routes = 6u * 5u;
    const std::size_t open_routes = fps::count_open_routes<fe::Bg>()
                                    + fps::count_open_routes<fe::detail::ctx_mint::bg_key>()
                                    + fps::count_open_routes<fe::cap_mint_key>()
                                    + fps::count_open_routes<fp::perm_mint_key>()
                                    + fps::count_open_routes<fp::Permission<fps::Region>>();
    if (open_routes != expected_open_routes) {
        std::fprintf(stderr,
                     "test_forgeable_proofs: %zu routes of the ledger still give a pointer, not %zu.  Delete each "
                     "closed route from the ledger: it only shrinks.\n",
                     open_routes, expected_open_routes);
        return 1;
    }
    std::printf("test_forgeable_proofs: no forgeable proof type, %zu template witnesses closed, %zu open "
                "templates, %zu ledger routes pinned\n",
                fps::witnesses_checked, fps::templates_open, open_routes);
    return 0;
}
