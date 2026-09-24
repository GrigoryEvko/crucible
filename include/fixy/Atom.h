#pragma once

// The atom catalog of fixy.  An atom is what a binding names to say
// more than the strict pole of one axis: `affine` relaxes Usage,
// `with<IO>` relaxes Effect, `declassify<Policy>` relaxes Security.
// Every atom is an empty final type that derives `atom_base` and
// carries the axis it engages as `static constexpr Axis axis`.  That
// member replaces the `which_dim` partial specialisation of the old
// tree: the axis is one line on the atom itself, so no header reopens
// a namespace to register it and no table of witnesses has to agree
// with it.
//
// An atom-less axis resolves to the strict pole in fixy/Axis.h.  That
// table is the only default.  There is no marker that accepts a
// default explicitly and no list of the axes that ship no atom.
//
// Old spelling: include/crucible/fixy/Grant.h (grant_base, IsGrantTag,
// which_dim, the tags of eighteen axes).  A grant is renamed an atom
// throughout, because a grant is an effect atom.  The families that
// reach the operating system live in fixy/atoms/Os.h and the
// control-flow, call-shape, stack, global-state and stdio families in
// fixy/atoms/{Ctrl,Dispatch,Stack,Global,Stdio}.h.

#include <fixy/Axis.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom {

struct atom_base {
    constexpr atom_base() noexcept = default;
    constexpr atom_base(const atom_base&) noexcept = default;
    constexpr atom_base(atom_base&&) noexcept = default;
    constexpr atom_base& operator=(const atom_base&) noexcept = default;
    constexpr atom_base& operator=(atom_base&&) noexcept = default;
    ~atom_base() = default;
};

// The one base every atom derives.  The axis is a data member of the
// base, so an atom is a one-line declaration and a reader sees its
// axis where the atom is declared.
template <Axis A>
struct atom_of : atom_base {
    static constexpr Axis axis = A;
};

// An atom that reaches a real operation also carries the effect row of
// that operation, so that a gate can lift an atom pack into the row the
// binding's context has to admit.  foundation/effects/Lift.h reads
// `lifts_to` structurally and never names this base.
template <Axis A, ::foundation::effects::IsEffectRow LiftRow>
struct lifting_atom_of : atom_of<A> {
    static constexpr LiftRow lifts_to{};
};

// ── The catalog is closed ───────────────────────────────────────────
//
// The shape of an atom is a recipe: final, derives atom_base, names an
// axis.  Any namespace can repeat it, so a check on the shape alone
// admits a type that a user declares in a namespace of their own, and
// every rule then reads that type as a shipped grade.  IsAtom therefore
// reads three facts that the type cannot state about itself.
//
//   1. The namespace.  An atom is declared directly in fixy::atom or
//      directly in one of the family namespaces below.  parent_of reads
//      the namespace from the declaration, so a namespace that a user
//      names fixy::atom inside a namespace of their own is a different
//      namespace, and a family that a user opens beside these is not in
//      the list.  The list is the closed set of families: a new family
//      is a line here and a header under fixy/atoms/.
//   2. The seal.  Each admitted namespace holds exactly one variable of
//      type atom_seal.  A namespace with no seal, or with two, admits no
//      atom.  Each query reads the seal again.
//   3. The file.  An atom is declared in the file that declares the seal
//      of its namespace.  source_location_of gives the file of a class
//      definition, and the file of an explicit or partial
//      specialization, so an atom planted in a family from another file,
//      or a specialization of a shipped atom template written anywhere
//      else, is refused.  The rule reads the declaration and not the
//      point of the query, so the order of the includes cannot change
//      the answer.
//
// A `#line` directive that names a family header defeats the third read.
// That forges the position of the source, and no property of a type can
// refuse it.

// The families.  Each is opened here, empty, so that the list below can
// name it before its header is included.
namespace barrier {}
namespace ctrl {}
namespace dispatch {}
namespace fp {}
namespace global {}
namespace hw {}
namespace io {}
namespace fs {}
namespace mmap {}
namespace leak {}
namespace observe {}
namespace regime {}
namespace scope {}
namespace session {}
namespace simd {}
namespace spawn {}
namespace stack {}
namespace stdio {}
namespace sync {}
namespace syscall {}

// The seal of one admitted namespace.  It carries no data: its position
// is the whole claim, the namespace and the file that declare it.
struct atom_seal {};

inline constexpr atom_seal atom_namespace_seal{};

namespace detail {

inline constexpr std::meta::info atom_families[] = {
    ^^barrier, ^^ctrl,    ^^dispatch, ^^fp,    ^^global, ^^hw,    ^^io,    ^^fs,    ^^mmap, ^^leak,
    ^^observe, ^^regime,  ^^scope,    ^^session, ^^simd, ^^spawn, ^^stack, ^^stdio, ^^sync,
    ^^syscall,
};

// Why a type with the shape of an atom is refused, or none.
enum class atom_refusal : std::uint8_t {
    none,
    outside_the_catalog,       // not declared directly in fixy::atom or in a family
    namespace_unsealed,        // the namespace holds no atom_seal
    namespace_sealed_twice,    // the namespace holds two atom_seal variables
    declared_outside_its_seal  // declared in a file other than the one that seals its namespace
};

// The namespace that declares a type.  A specialization is placed where
// its template is declared.
[[nodiscard]] consteval std::meta::info atom_owner_(std::meta::info type) {
    const std::meta::info declared = std::meta::dealias(type);
    if (std::meta::has_template_arguments(declared)) return std::meta::parent_of(std::meta::template_of(declared));
    return std::meta::parent_of(declared);
}

[[nodiscard]] consteval bool is_admitted_atom_namespace_(std::meta::info ns) {
    if (ns == ^^::fixy::atom) return true;
    for (const std::meta::info family : atom_families) {
        if (ns == family) return true;
    }
    return false;
}

[[nodiscard]] consteval bool same_file_(std::meta::info lhs, std::meta::info rhs) {
    const std::string_view lhs_file = std::meta::source_location_of(lhs).file_name();
    const std::string_view rhs_file = std::meta::source_location_of(rhs).file_name();
    return lhs_file == rhs_file;
}

// The refusal for a type that already has the shape.  Complexity: linear
// in the members of the owning namespace.
[[nodiscard]] consteval atom_refusal atom_refusal_of_(std::meta::info type) {
    const std::meta::info owner = atom_owner_(type);
    if (!is_admitted_atom_namespace_(owner)) return atom_refusal::outside_the_catalog;
    std::meta::info seal{};
    std::size_t seals = 0;
    for (const std::meta::info member : std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (!std::meta::is_variable(member)) continue;
        if (std::meta::remove_cvref(std::meta::type_of(member)) != ^^atom_seal) continue;
        seal = member;
        ++seals;
    }
    if (seals == 0) return atom_refusal::namespace_unsealed;
    if (seals > 1) return atom_refusal::namespace_sealed_twice;
    if (!same_file_(std::meta::dealias(type), seal)) return atom_refusal::declared_outside_its_seal;
    return atom_refusal::none;
}

[[nodiscard]] consteval std::string_view atom_refusal_text_(atom_refusal refusal) {
    switch (refusal) {
        case atom_refusal::none:
            return "none";
        case atom_refusal::outside_the_catalog:
            return "it is not declared directly in fixy::atom or in a family namespace of fixy/Atom.h";
        case atom_refusal::namespace_unsealed:
            return "its namespace holds no fixy::atom::atom_seal";
        case atom_refusal::namespace_sealed_twice:
            return "its namespace holds two fixy::atom::atom_seal variables";
        case atom_refusal::declared_outside_its_seal:
            return "it is declared in a file other than the one that seals its namespace";
        default:
            break;
    }
    return "an unknown refusal";
}

// The shape, which a user can repeat.  The cv-ref clause refuses rather
// than strips: an atom is a zero-state marker, so a qualified one comes
// from a decltype on a variable, and stripping would coerce that mistake
// into the bare atom.  The final clause stops a subclass from injecting
// behavior into the acceptance check.
template <class G>
concept HasAtomShape =
    std::same_as<G, std::remove_cvref_t<G>> && std::is_final_v<G> && std::derived_from<G, atom_base> && requires {
        { G::axis } -> std::convertible_to<Axis>;
    };

// The answer for one type, computed once at the first query.  The
// namespace and the file are facts of the declaration.  A second seal
// that a translation unit adds later refuses each atom first asked
// about after it.
template <class G>
inline constexpr atom_refusal atom_refusal_v = atom_refusal_of_(^^G);

}  // namespace detail

template <class G>
concept IsAtom = detail::HasAtomShape<G> && detail::atom_refusal_v<G> == detail::atom_refusal::none;

// Each concept below is the minimum structural bar a parametric atom's
// parameter must clear.  A parameter that fails one makes the atom
// template-id ill-formed, so `IsAtom` rejects the atom by substitution
// failure instead of the build hitting a hard error inside the resolver.

// The parameter of `protocol<P>` is a session type or a machine state type,
// so this gate stays open-world.  Enumerating the legal session combinators
// instead would be tighter but would refuse any user-defined state class.
template <typename Proto>
concept IsSessionProtocol = std::is_same_v<Proto, std::remove_cvref_t<Proto>> && std::is_class_v<Proto>;

// Whether a predicate is invocable on a given value type cannot be folded
// into a per-predicate gate, so that check happens per-type at construction
// and is deliberately absent here.
//
// Emptiness and default-constructibility are required for a reason unrelated
// to calling the predicate.  A binding that engages `refined_with<Pred>`
// carries `Pred` into its cache key, and every distinct `Pred` type claims
// its own slot.  A stateful predicate struct, or a capturing lambda, has a
// fresh type per declaration site, so two textually identical ones fragment
// the cache into two slots.  Requiring empty and default-constructible
// forces bindings onto named predicates, which share a type across call
// sites.  A capture-less lambda is empty and default-constructible, so it
// still passes.
template <typename Pred>
concept IsRefinementPredicate = std::is_same_v<Pred, std::remove_cvref_t<Pred>> && std::is_class_v<Pred>
                             && std::is_empty_v<Pred> && std::is_default_constructible_v<Pred>;

// A provenance source is an empty marker class by convention, and this gate
// is stricter than the two above because that convention is stricter.  A
// source tag that starts carrying state surfaces here rather than downstream
// in resolution.
template <typename Source>
concept IsProvenanceSource =
    std::is_same_v<Source, std::remove_cvref_t<Source>> && std::is_class_v<Source> && std::is_empty_v<Source>;

// This is what makes the audit trail structural rather than a
// convention: an ad-hoc struct is rejected, so every declassification
// must name a tag from the closed set in fixy/Tags.h.  The tags there
// are final, and the clause here repeats that, so a subclass of a
// shipped policy cannot launder the trail through subtype coercion.
//
// The base and final alone are a recipe that any namespace can repeat,
// so the set is closed the way the atom catalog is: a policy is declared
// directly in fixy::tags::secret_policy, in the file that declares the
// base.
namespace detail {

[[nodiscard]] consteval bool is_catalog_policy_(std::meta::info policy) {
    constexpr std::meta::info base = ^^::fixy::tags::secret_policy::secret_policy_base;
    return std::meta::parent_of(std::meta::dealias(policy)) == std::meta::parent_of(base)
        && same_file_(std::meta::dealias(policy), base);
}

}  // namespace detail

template <typename Policy>
concept IsDeclassificationPolicy =
    std::is_class_v<Policy> && std::derived_from<Policy, ::fixy::tags::secret_policy::secret_policy_base>
    && std::is_final_v<Policy> && detail::is_catalog_policy_(^^Policy);

// A relaxation atom carries no meaning of its own beyond the axis it engages.
// The resolver that reads an atom pack decides what each atom resolves to;
// this header supplies only the axis the engagement check needs.

struct affine final : atom_of<Axis::Usage> {};
struct copy final : atom_of<Axis::Usage> {};
struct ghost final : atom_of<Axis::Usage> {};
struct borrow final : atom_of<Axis::Usage> {};
// The other four Usage atoms are bare nouns.  This one takes a suffix because
// the effect-axis capability token is spelled `Capability` and reaches most
// call sites that pull this header.  A bare `capability` here would still
// compile, and would read at those sites as the effect token.
struct capability_usage final : atom_of<Axis::Usage> {};

// An empty pack means the same thing as the strict pole for this axis:
// both resolve to the empty effect row.  An audit for pure-effect bindings
// has to recognise both spellings.
//
// It lifts, and the lift is what lets a context be gated on a binding's
// declared effects.  Until it did, the Effect axis's own atom was the one
// thing foundation/effects/Lift.h could not see: the syscall and wait
// atoms lifted, so the os mints could fold a pack into a required row,
// and a binding that had declared it performs IO could still be called
// from a context admitting nothing, because the row nobody computed is
// the empty row and the empty row is a Subrow of every context's.  A
// caller who wanted the gate wrote the row a second time by hand.
template <::foundation::effects::Effect... Es>
struct with final : lifting_atom_of<Axis::Effect, ::foundation::effects::Row<Es...>> {};

using with_alloc = with<::foundation::effects::Effect::Alloc>;
using with_io = with<::foundation::effects::Effect::IO>;
using with_block = with<::foundation::effects::Effect::Block>;
using with_bg = with<::foundation::effects::Effect::Bg>;
using with_init = with<::foundation::effects::Effect::Init>;
using with_test = with<::foundation::effects::Effect::Test>;

// ---------------------------------------------------------------------
// The Effect axis's grade in its two shapes, and the row each names.
//
// fn resolves an axis to the atom the pack states, or to the axis's
// strict pole when the pack states nothing, and Axis::Effect's strict
// pole is a bare Row rather than an atom.  So the grade is with<Es...>
// on a binding that declared its effects and Row<Es...> on one that did
// not, and anything reading the grade has to answer for both.
//
// The relation is CLOSED.  A grade of any other shape fails the
// constraint with its own name in the diagnostic rather than being
// answered with the empty row, because the empty row is a Subrow of
// every context's: a quiet default here would admit the binding
// everywhere, which is the failure the relation exists to stop.  That is
// the same shape as foundation/diag/FailClosed.h's relations and as the
// repair to payload_effect_row_t.
namespace detail {

template <class Grade>
struct effect_grade_row_;

template <::foundation::effects::Effect... Es>
struct effect_grade_row_<with<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};

template <::foundation::effects::Effect... Es>
struct effect_grade_row_<::foundation::effects::Row<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};

}  // namespace detail

template <class Grade>
concept IsEffectGrade = requires { typename detail::effect_grade_row_<Grade>::type; };

template <IsEffectGrade Grade>
using effect_row_of_t = typename detail::effect_grade_row_<Grade>::type;

// ---------------------------------------------------------------------
// The row that a pack lifts to, and the row of a binding.
//
// An atom that reaches a real operation carries the row of that
// operation as `lifts_to`.  The stated with<Es...> of the Effect axis
// lifts, and so do the waits of fixy/atoms/Sync.h and the system calls
// of fixy/atoms/Syscall.h and fixy/atoms/Os.h.  An atom with no lift
// reaches no operation, so it adds nothing to the union.
//
// The row of a binding is the row that its Effect grade states, joined
// with the row that its atoms lift to.  The Effect grade alone is not
// this row.  A binding can state a futex call and leave its Effect grade
// at the strict pole.  A row read from the grade alone is then the empty
// row, and the empty row is a Subrow of the row of every context.
// fixy/Fn.h reads this relation for context admission, and the collision
// rules and the corpus read it for their premises.
//
// Complexity: linear in the length of the pack.  Each union is quadratic
// in the size of one row, and the effect catalog bounds that size.
namespace detail {

template <class Atom>
struct lifted_row_of_atom_ {
    using type = ::foundation::effects::Row<>;
};
template <::foundation::effects::LiftsToRow Atom>
struct lifted_row_of_atom_<Atom> {
    using type = ::foundation::effects::lift_row_t<Atom>;
};

template <class... Atoms>
struct lifted_row_of_pack_ {
    using type = ::foundation::effects::Row<>;
};
template <class First, class... Rest>
struct lifted_row_of_pack_<First, Rest...> {
    using type = ::foundation::effects::row_union_t<typename lifted_row_of_atom_<First>::type,
                                                    typename lifted_row_of_pack_<Rest...>::type>;
};

}  // namespace detail

template <class... Atoms>
using lifted_row_of_t = typename detail::lifted_row_of_pack_<Atoms...>::type;

// Grade is the resolved Effect grade of the pack, which the caller
// reads with its own resolver: fn::grade_on in fixy/Fn.h and
// collision::grades in fixy/Collision.h.
template <IsEffectGrade Grade, class... Atoms>
using binding_row_of_t = ::foundation::effects::row_union_t<effect_row_of_t<Grade>, lifted_row_of_t<Atoms...>>;

// `declassify<Policy>` drops the binding to the public security level and
// names the policy that licenses the drop.  The policy is opaque to the
// engagement check and exists for the audit trail.

template <typename Policy>
    requires IsDeclassificationPolicy<Policy>
struct declassify final : atom_of<Axis::Security> {};

// One atom per point of the security lattice, ordered unclassified, public,
// internal, classified, secret.  `as_public` differs from `declassify<P>` in
// carrying no policy: it asserts the data was never classified rather than
// licensing a drop.  `as_classified` names the strict pole explicitly.
// `as_secret` pins the top of the lattice, where no declassification is
// permitted at all.
struct as_unclassified final : atom_of<Axis::Security> {};
struct as_public final : atom_of<Axis::Security> {};
struct as_internal final : atom_of<Axis::Security> {};
struct as_classified final : atom_of<Axis::Security> {};
struct as_secret final : atom_of<Axis::Security> {};

// The binding's data is classified, AND no branch, no memory index and no
// variable-latency instruction in the body depends on it.  This is the one
// spelling of constant time in the tree.
//
// It is a grade of the binding and not an effect in the row, and
// foundation/effects/Effect.h says why at the top: a row grows by union,
// and a constant-time claim holds for a composition only when it holds
// for every part.  Every rule that reads the claim reads it on the one
// binding, so a grade is enough.
//
// It is a point of the Security axis and not an axis of its own, for two
// reasons.  Constant time has a meaning only for classified data:
// fixy/ConstantTime.h refuses public inputs for that reason.  So the claim
// states the classification too, and a constant-time claim over public
// data cannot be written.  And a new axis would add one step to the fold
// in fixy/Fn.h that keys every binding, which moves the federation cache
// key of every binding in the tree.  A new point on an axis moves no
// existing key.
//
// The cost: an axis takes one grade, so a binding cannot state
// constant_time and as_secret together.  No rule reads the residual claim
// of as_secret, that no declassification is permitted, so no rule loses a
// premise.  The discipline is discharged by measurement, as Regime's is.
// The type refuses only the combinations that contradict it, which are
// the constant-time rules of fixy/Collision.h.
struct constant_time final : atom_of<Axis::Security> {};

// ---------------------------------------------------------------------
// What a Security grade says, as one closed relation.
//
// A rule asks one of three questions of the Security grade: is the data
// classified, is it in constant time, is it declassified under a policy.
// Each reader used to answer with its own partial specialisation and a
// false primary, so a new point on the axis read as "public" in every
// reader that nobody updated.  On this axis "public" is the answer that
// lets a binding through, so that default failed open.
//
// Here the primary is declared and not defined.  A grade this relation
// does not name fails at the use with its own name in the diagnostic, and
// the walk in the self-test below makes sure every Security atom in the
// roster has an answer.
enum class SecurityClass : std::uint8_t {
    Public = 0,  // as_public, as_unclassified: the data was never classified
    Internal = 1,  // as_internal: below the classified carrier, above public
    Classified = 2,  // the strict pole, as_classified, as_secret
    ConstantTime = 3,  // constant_time: classified, with the timing channel closed
    Declassified = 4,  // declassify<Policy>: a named policy licenses the drop
};

namespace detail {

template <class Grade>
struct security_class_of_;

template <>
struct security_class_of_<as_unclassified> : std::integral_constant<SecurityClass, SecurityClass::Public> {};
template <>
struct security_class_of_<as_public> : std::integral_constant<SecurityClass, SecurityClass::Public> {};
template <>
struct security_class_of_<as_internal> : std::integral_constant<SecurityClass, SecurityClass::Internal> {};
template <>
struct security_class_of_<as_classified> : std::integral_constant<SecurityClass, SecurityClass::Classified> {};
template <>
struct security_class_of_<as_secret> : std::integral_constant<SecurityClass, SecurityClass::Classified> {};
template <>
struct security_class_of_<constant_time> : std::integral_constant<SecurityClass, SecurityClass::ConstantTime> {};
template <typename Policy>
struct security_class_of_<declassify<Policy>> : std::integral_constant<SecurityClass, SecurityClass::Declassified> {};
// The strict pole: a binding that says nothing about Security is
// classified.  That is what reject-by-default means on this axis.
template <>
struct security_class_of_<axis_traits<Axis::Security>::strict>
    : std::integral_constant<SecurityClass, SecurityClass::Classified> {};

}  // namespace detail

template <class Grade>
concept IsSecurityGrade = requires { detail::security_class_of_<Grade>::value; };

template <IsSecurityGrade Grade>
inline constexpr SecurityClass security_class_of_v = detail::security_class_of_<Grade>::value;

// The two readings every rule shares.  A carrier holds classified data,
// with or without the timing claim.  A declassified grade is not a
// carrier: the policy it names is the discharge.
template <IsSecurityGrade Grade>
inline constexpr bool is_classified_carrier_v = security_class_of_v<Grade> == SecurityClass::Classified
                                             || security_class_of_v<Grade> == SecurityClass::ConstantTime;

template <IsSecurityGrade Grade>
inline constexpr bool is_constant_time_v = security_class_of_v<Grade> == SecurityClass::ConstantTime;

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct protocol final : atom_of<Axis::Protocol> {};

template <auto RegionTag>
struct in_region final : atom_of<Axis::Lifetime> {};

template <typename Source>
    requires IsProvenanceSource<Source>
struct from_source final : atom_of<Axis::Provenance> {};

// The rationale is a non-type parameter, typically a character-array
// literal holding the human-readable justification.  The engagement check
// treats it opaquely, so it exists for the audit trail alone.  It has no
// default: a default would make "I gave no rationale" indistinguishable
// from a deliberate choice of that value, which is the one thing an audit
// of these sites needs to tell apart.
template <auto Rationale>
struct trust_assumed final : atom_of<Axis::Trust> {};

// One atom per remaining trust level.  `trust_verified` names the level the
// old default asserted; the strict pole in fixy/Axis.h is Unverified, so a
// binding names this atom only after it has discharged the proof.
// `trust_external` delegates the claim to a foreign source such as a vendor
// library or firmware, and a binding that takes it has to confirm that
// claim through some channel of its own.
struct trust_verified final : atom_of<Axis::Trust> {};
struct trust_tested final : atom_of<Axis::Trust> {};
struct trust_unverified final : atom_of<Axis::Trust> {};
struct trust_external final : atom_of<Axis::Trust> {};

template <::fixy::pole::ReprKind Kind>
struct repr final : atom_of<Axis::Representation> {};

// The Observability axis has no relaxation atom at all, and its absence is
// deliberate.  That axis is derived from the effect row, so an atom that let a
// binding state an observability of its own would contradict the derivation.

struct cost_constant final : atom_of<Axis::Complexity> {};
template <auto N>
struct cost_linear final : atom_of<Axis::Complexity> {};
template <auto N>
struct cost_quadratic final : atom_of<Axis::Complexity> {};
struct cost_unbounded final : atom_of<Axis::Complexity> {};

struct precision_f32 final : atom_of<Axis::Precision> {};
struct precision_f64 final : atom_of<Axis::Precision> {};
template <auto Bound>
struct precision_higham final : atom_of<Axis::Precision> {};

template <auto N>
struct space_bounded final : atom_of<Axis::Space> {};
struct space_unbounded final : atom_of<Axis::Space> {};

struct overflow_wrap final : atom_of<Axis::Overflow> {};
struct overflow_saturate final : atom_of<Axis::Overflow> {};
struct overflow_widen final : atom_of<Axis::Overflow> {};

struct mut_mutable final : atom_of<Axis::Mutation> {};
struct mut_append final : atom_of<Axis::Mutation> {};
struct mut_monotonic final : atom_of<Axis::Mutation> {};

// Unqualified, `coroutine` reads as a control-flow claim about suspending,
// which it is not: it engages the Reentrancy axis.  The control-flow atoms
// sit in a namespace of their own, so a reader who sees only the bare noun
// cannot tell the two apart.
struct reentrant final : atom_of<Axis::Reentrancy> {};
struct coroutine final : atom_of<Axis::Reentrancy> {};

// These names exist so a call site can spell the axis it engages.  The
// top-level spellings above stay valid, so this is the preferred form rather
// than the only one.
namespace reentrancy {
using reentrant = ::fixy::atom::reentrant;
using coroutine = ::fixy::atom::coroutine;
}  // namespace reentrancy

static_assert(std::is_same_v<reentrancy::coroutine, coroutine>,
              "atom::reentrancy::coroutine must alias the top-level "
              "atom::coroutine — the sub-namespace is a re-export, not a "
              "separate type.");
static_assert(std::is_same_v<reentrancy::reentrant, reentrant>, "atom::reentrancy::reentrant must alias the top-level "
                                                                "atom::reentrant — the sub-namespace is a re-export.");

template <auto Depth>
struct sized_at final : atom_of<Axis::Size> {};
struct productive final : atom_of<Axis::Size> {};

template <std::uint32_t V>
struct version final : atom_of<Axis::Version> {};

template <auto TauMax>
struct stale_to final : atom_of<Axis::Staleness> {};

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct refined_with final : atom_of<Axis::Refinement> {};

// The Type axis has no atom.  The bound type is the binding's own first
// template parameter, so no call site writes one.

// ── The roster walk ─────────────────────────────────────────────────
//
// A family's self-test is one hand list, the roster: a std::tuple of
// the atoms the family ships, with one instantiation per parametric
// atom.  Everything else derives from the roster by reflection.  The
// walk below checks each member once, so a family adds an atom by
// adding it to its roster and writes no per-atom assertion.

namespace detail {

// The members of a roster, as reflections of its template arguments.
// The roster is dealiased first, because the reflection of an alias
// has no template arguments of its own, and a joined roster is an
// alias.
template <class Roster>
inline constexpr auto roster_members_v =
    std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^Roster)));

// Reads the enum, so an axis value that is not an enumerator fails
// here without a hand list of the enumerators.
[[nodiscard]] consteval bool is_axis_enumerator_(Axis value) noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        if (value == [:en:]) return true;
    }
#pragma GCC diagnostic pop
    return false;
}

// Every member of the roster is an atom, is empty and one byte, names
// an axis that is an enumerator, and, if it lifts to a row, lifts to
// an effect row.
template <class Roster>
[[nodiscard]] consteval bool every_roster_member_is_atom_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!IsAtom<A>) {
            return false;
        } else {
            if constexpr (sizeof(A) != 1 || !std::is_empty_v<A>) return false;
            if (!is_axis_enumerator_(A::axis)) return false;
            if constexpr (::foundation::effects::LiftsToRow<A>) {
                if constexpr (!::foundation::effects::IsEffectRow<decltype(A::lifts_to)>) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Every member of the roster engages the one axis the family serves.
template <class Roster, Axis Expected>
[[nodiscard]] consteval bool every_roster_member_on_axis_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!IsAtom<A>) {
            return false;
        } else {
            if (A::axis != Expected) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Every member of the roster lifts to an effect row.
template <class Roster>
[[nodiscard]] consteval bool every_roster_member_lifts_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!::foundation::effects::LiftsToRow<A>) {
            return false;
        } else {
            if constexpr (!::foundation::effects::IsEffectRow<decltype(A::lifts_to)>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Joins rosters, so a sentinel TU can walk every family as one list.
template <class... Rosters>
struct roster_cat;

template <>
struct roster_cat<> {
    using type = std::tuple<>;
};

template <class... As>
struct roster_cat<std::tuple<As...>> {
    using type = std::tuple<As...>;
};

template <class... As, class... Bs, class... Rest>
struct roster_cat<std::tuple<As...>, std::tuple<Bs...>, Rest...> {
    using type = typename roster_cat<std::tuple<As..., Bs...>, Rest...>::type;
};

template <class... Rosters>
using roster_cat_t = typename roster_cat<Rosters...>::type;

// Every atom class declared directly in Ns appears in the roster.  A
// roster is a hand list, and a check that walks the list cannot see
// what the list omits, so this reads the family namespace instead: an
// atom added there and forgotten in the roster is caught here rather
// than going unchecked.
//
// A class in the namespace that is not an atom is skipped.  The family
// namespaces hold policy tags beside their atoms, and a tag is an
// argument to an atom rather than an atom itself.
//
// Only the plain atom classes are checked.  A parametric atom reaches
// the roster as an instantiation, and deciding whether a class template
// is an atom template would mean instantiating it: there is no way to
// ask from the declaration alone.  That probe is not safe here.
// can_substitute on a template whose body holds a static_assert hard
// errors the translation unit rather than answering false, so a family
// that later grew such an atom would break every build instead of
// failing one check.  A parametric atom left out of a roster therefore
// stays uncaught, and the count pin beside each roster is what narrows
// that.
template <std::meta::info Ns, class Roster>
[[nodiscard]] consteval bool every_atom_in_is_rostered_() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fixy/Atom.h: every_atom_in_is_rostered_<Ns, Roster> takes a "
                                               "reflection of a namespace, written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using A = [:member:];
            // The shape, not IsAtom: a class with the shape of an atom
            // that the catalog refuses, because another file planted it
            // here, is still a class the family did not list.
            if constexpr (HasAtomShape<A>) {
                bool is_rostered = false;
                template for (constexpr auto listed : roster_members_v<Roster>) {
                    // The splice is named before the comparison: one in
                    // a template-argument position needs a
                    // disambiguator, and an alias sidesteps that.
                    using Listed = [:listed:];
                    if constexpr (std::is_same_v<A, Listed>) is_rostered = true;
                }
                if (!is_rostered) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Protocol, Lifetime and Provenance atoms are parameterized by a caller tag
// type, and their concepts require a COMPLETE empty class — an elaborated
// `struct X` written inline forward-declares instead, which the concept
// rejects.  Two local witnesses, defined here rather than borrowed from a
// production tag tree so the roster does not couple to one.
// `in_region` takes a non-type `auto` parameter, so it witnesses with a
// value rather than one of these tags.
struct atom_axis_witness_proto final {};
struct atom_axis_witness_source final {};

// The roster of this header: every atom above, one instantiation per
// parametric atom.
using core_atom_roster =
    std::tuple<affine, copy, ghost, borrow, capability_usage, with<>, with<::foundation::effects::Effect::IO>,
               with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO>,
               declassify<::fixy::tags::secret_policy::AuditedLogging>, as_unclassified, as_public, as_internal,
               as_classified, as_secret, constant_time, protocol<atom_axis_witness_proto>,
               protocol<::fixy::pole::proto::None>, in_region<0>, from_source<atom_axis_witness_source>,
               from_source<::fixy::tags::source::FromUser>, from_source<::fixy::tags::source::ForgePhase<'F'>>,
               trust_assumed<0>, trust_verified, trust_tested, trust_unverified, trust_external,
               repr<::fixy::pole::ReprKind::C>, cost_constant, cost_linear<1>, cost_quadratic<1>, cost_unbounded,
               precision_f32, precision_f64, precision_higham<1>, space_bounded<1>, space_unbounded, overflow_wrap,
               overflow_saturate, overflow_widen, mut_mutable, mut_append, mut_monotonic, reentrant, coroutine,
               sized_at<1>, productive, version<3>, stale_to<5>, refined_with<::fixy::pole::pred::True>>;

}  // namespace detail

namespace detail::atom_self_test {

static_assert(every_roster_member_is_atom_<core_atom_roster>(),
              "fixy/Atom.h: a member of core_atom_roster is not an atom, is not one empty byte, or names "
              "an axis that is not a fixy::Axis enumerator.");

// ── Every Security atom has a class ──────────────────────────────────
//
// The count of Security atoms in the roster, and the count that the
// closed relation above answers for.  An atom that reaches the axis and
// not the relation is counted as unproven, so the two counts differ and
// the assertion names the gap.  There is no else branch: a roster member
// whose shape the walk does not expect cannot be counted as proven.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::size_t security_atoms_rostered_() noexcept {
    std::size_t found = 0;
    template for (constexpr auto member : roster_members_v<core_atom_roster>) {
        using A = [:member:];
        if constexpr (IsAtom<A>) {
            if constexpr (A::axis == Axis::Security) ++found;
        }
    }
    return found;
}

[[nodiscard]] consteval std::size_t security_atoms_classified_() noexcept {
    std::size_t proven = 0;
    template for (constexpr auto member : roster_members_v<core_atom_roster>) {
        using A = [:member:];
        if constexpr (IsAtom<A>) {
            if constexpr (A::axis == Axis::Security && IsSecurityGrade<A>) ++proven;
        }
    }
    return proven;
}

#pragma GCC diagnostic pop

static_assert(security_atoms_rostered_() > 0, "fixy/Atom.h: the roster holds no Security atom, so the walk below "
                                              "proves nothing.  Either the atoms left core_atom_roster, or they "
                                              "stopped naming Axis::Security.");
static_assert(security_atoms_classified_() == security_atoms_rostered_(),
              "fixy/Atom.h: a Security atom in core_atom_roster has no entry in security_class_of_.  Every "
              "reader of the Security grade asks that relation, so an atom without an entry cannot be "
              "used in a binding.  Give it a SecurityClass, next to the others.");
static_assert(IsSecurityGrade<typename axis_traits<Axis::Security>::strict>,
              "fixy/Atom.h: the strict Security pole must have a class, because a binding that names no "
              "Security atom resolves to it.");

// The relation answers what it must and refuses what it must.
static_assert(security_class_of_v<typename axis_traits<Axis::Security>::strict> == SecurityClass::Classified);
static_assert(security_class_of_v<constant_time> == SecurityClass::ConstantTime);
static_assert(security_class_of_v<as_secret> == SecurityClass::Classified);
static_assert(security_class_of_v<as_public> == SecurityClass::Public);
static_assert(security_class_of_v<as_internal> == SecurityClass::Internal);
static_assert(security_class_of_v<declassify<::fixy::tags::secret_policy::AuditedLogging>>
              == SecurityClass::Declassified);
static_assert(is_classified_carrier_v<constant_time> && is_constant_time_v<constant_time>);
static_assert(is_classified_carrier_v<as_classified> && !is_constant_time_v<as_classified>);
static_assert(!is_classified_carrier_v<as_internal> && !is_classified_carrier_v<as_public>);
static_assert(!is_classified_carrier_v<declassify<::fixy::tags::secret_policy::AuditedLogging>>,
              "a declassified grade is the discharge, not a carrier");
static_assert(!IsSecurityGrade<int>, "a type that is not a Security grade has no class");
static_assert(!IsSecurityGrade<affine>, "an atom on another axis has no Security class");
static_assert(!IsSecurityGrade<const as_public>, "a qualified atom is refused rather than stripped");
static_assert(constant_time::axis == Axis::Security);
static_assert(sizeof(constant_time) == 1 && std::is_empty_v<constant_time>);

static_assert(IsRefinementPredicate<::fixy::pole::pred::True>,
              "A named empty default-constructible predicate must satisfy "
              "IsRefinementPredicate.");

// What IsRefinementPredicate admits and refuses, on four shapes: the
// two that carry state or a constructor argument, and the empty
// default-constructible one that a capture-less lambda produces.
namespace refinement_predicate_shape_witness {
struct StatefulPredicate {
    int threshold = 0;
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > threshold; }
};
static_assert(!IsRefinementPredicate<StatefulPredicate>,
              "A stateful predicate must be rejected by IsRefinementPredicate "
              "— each unique stateful Pred type fragments the cache.");

struct NonDefaultConstructiblePredicate {
    constexpr NonDefaultConstructiblePredicate(int) noexcept {}
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > 0; }
};
static_assert(!IsRefinementPredicate<NonDefaultConstructiblePredicate>,
              "A non-default-constructible predicate must be rejected — the "
              "refinement machinery instantiates Pred freely.");

using CaptureLessLambdaType = decltype([](int v) noexcept { return v > 0; });
static_assert(std::is_empty_v<CaptureLessLambdaType>);
static_assert(std::is_default_constructible_v<CaptureLessLambdaType>);
static_assert(IsRefinementPredicate<CaptureLessLambdaType>,
              "A capture-less lambda is empty and default-constructible, so "
              "IsRefinementPredicate must admit it.");
}  // namespace refinement_predicate_shape_witness

// A forge-phase source carries its phase as a non-type parameter and stays
// empty, so the empty-marker gate admits every instantiation.  A refactor
// that gave it state reds here rather than at the use sites.  The twelve
// phases A thru L are the range fixy/Tags.h pins on ForgePhase.
template <char... Offsets>
[[nodiscard]] consteval bool every_forge_phase_is_a_source_(std::integer_sequence<char, Offsets...>) noexcept {
    return (IsProvenanceSource<::fixy::tags::source::ForgePhase<static_cast<char>('A' + Offsets)>> && ...);
}
static_assert(every_forge_phase_is_a_source_(std::make_integer_sequence<char, 'L' - 'A' + 1>{}));

// The policy gate admits only the closed set in fixy/Tags.h.
struct ad_hoc_policy final {};
struct open_policy : ::fixy::tags::secret_policy::secret_policy_base {};
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuditedLogging>);
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(!IsDeclassificationPolicy<ad_hoc_policy>, "A policy outside the closed set must be rejected.");
static_assert(!IsDeclassificationPolicy<open_policy>, "A policy that is not final must be rejected.");
struct forged_policy final : ::fixy::tags::secret_policy::secret_policy_base {};
static_assert(!IsDeclassificationPolicy<forged_policy>,
              "A final policy with the base, declared outside fixy::tags::secret_policy, must be rejected.");

// The gate rejects a cv-qualified or reference-qualified atom rather than
// stripping it, and rejects the two halves of the recipe on their own.
struct not_final : atom_of<Axis::Usage> {};
struct no_axis final : atom_base {};

static_assert(!IsAtom<const affine>);
static_assert(!IsAtom<volatile affine>);
static_assert(!IsAtom<const volatile affine>);
static_assert(!IsAtom<affine&>);
static_assert(!IsAtom<const affine&>);
static_assert(!IsAtom<affine&&>);
static_assert(!IsAtom<not_final>);
static_assert(!IsAtom<no_axis>);
static_assert(!IsAtom<atom_base>);
static_assert(!IsAtom<int>);

// The shape alone is not an atom.  A type with every part of the recipe,
// declared here in detail, is refused because detail is not a family.
struct shaped_outside_the_catalog final : atom_of<Axis::Usage> {};
static_assert(detail::HasAtomShape<shaped_outside_the_catalog>);
static_assert(!IsAtom<shaped_outside_the_catalog>);
static_assert(atom_refusal_v<shaped_outside_the_catalog> == atom_refusal::outside_the_catalog);

// The core namespace and each family carry one seal, in this file for
// the core.  A shipped atom passes each of the three reads.
static_assert(atom_refusal_v<affine> == atom_refusal::none);
static_assert(atom_refusal_v<with_io> == atom_refusal::none, "an alias is read through to the atom it names");
static_assert(atom_refusal_v<declassify<::fixy::tags::secret_policy::AuditedLogging>> == atom_refusal::none);
static_assert(same_file_(^^affine, ^^::fixy::atom::atom_namespace_seal));
static_assert(is_admitted_atom_namespace_(^^::fixy::atom) && is_admitted_atom_namespace_(^^::fixy::atom::sync));
static_assert(!is_admitted_atom_namespace_(^^::fixy::atom::detail), "detail holds rosters and probes, not atoms");
static_assert(!is_admitted_atom_namespace_(^^::fixy), "the parent of the catalog is not the catalog");

// Each family in the list is a namespace directly in fixy::atom.  A
// family that moved would leave a list entry that names nothing.
[[nodiscard]] consteval bool every_family_is_a_child_of_the_catalog() {
    for (const std::meta::info family : atom_families) {
        if (!std::meta::is_namespace(family) || std::meta::parent_of(family) != ^^::fixy::atom) return false;
    }
    return true;
}
static_assert(every_family_is_a_child_of_the_catalog());

// ── The Effect grade reads the same both ways ────────────────────────
//
// `with` lifts, so foundation/effects/Lift.h can fold it into a
// required row alongside the syscall and wait atoms.  The closed
// relation above answers for the same atom AND for the bare Row the
// strict pole leaves behind.  The two readings have to agree wherever
// both apply, or a gate written against one would admit what a gate
// written against the other refuses.
namespace fe_ = ::foundation::effects;

static_assert(fe_::LiftsToRow<with<>>);
static_assert(fe_::LiftsToRow<with_io>);
static_assert(std::is_same_v<fe_::lift_row_t<with<>>, fe_::Row<>>);
static_assert(std::is_same_v<fe_::lift_row_t<with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<fe_::lift_row_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>,
                             fe_::Row<fe_::Effect::Bg, fe_::Effect::Alloc>>);

static_assert(std::is_same_v<effect_row_of_t<with<>>, fe_::lift_row_t<with<>>>);
static_assert(std::is_same_v<effect_row_of_t<with_io>, fe_::lift_row_t<with_io>>);
static_assert(std::is_same_v<effect_row_of_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>,
                             fe_::lift_row_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>>);

// The strict pole's shape, which no lift can answer for because a bare
// row is not an atom and declares no `lifts_to`.  This arm is the whole
// reason the relation exists beside the lift rather than instead of it.
static_assert(IsEffectGrade<fe_::Row<>>);
static_assert(!fe_::LiftsToRow<fe_::Row<>>);
static_assert(std::is_same_v<effect_row_of_t<fe_::Row<>>, fe_::Row<>>);
static_assert(std::is_same_v<effect_row_of_t<fe_::Row<fe_::Effect::IO>>, fe_::Row<fe_::Effect::IO>>);

// Closed.  A grade of any other shape is refused rather than answered
// with the empty row, because the empty row is a Subrow of every
// context's and a quiet default would admit the binding everywhere.
static_assert(!IsEffectGrade<int>);
static_assert(!IsEffectGrade<affine>);
static_assert(!IsEffectGrade<with_io&>);
static_assert(!IsEffectGrade<const with_io>);

// `with` stays an atom, and stays empty: the lift is a static member,
// so nothing about the pack's size or its axis moves.
static_assert(IsAtom<with_io>);
static_assert(with_io::axis == Axis::Effect);
static_assert(sizeof(with_io) == 1 && std::is_empty_v<with_io>);

// ── The row of a pack, and the row of a binding ──────────────────────
//
// The union of the lifts in the pack.  An atom with no lift adds nothing,
// and the result is the canonical row, so two orders give one type.
static_assert(std::is_same_v<lifted_row_of_t<>, fe_::Row<>>);
static_assert(std::is_same_v<lifted_row_of_t<affine>, fe_::Row<>>);
static_assert(std::is_same_v<lifted_row_of_t<affine, with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<lifted_row_of_t<with<fe_::Effect::IO, fe_::Effect::Bg>>,
                             lifted_row_of_t<with<fe_::Effect::Bg, fe_::Effect::IO>>>);

// The row of a binding joins the grade and the lifts.  The strict pole
// states no effect, so a binding at the pole requires what its atoms
// lift to and nothing more.
static_assert(std::is_same_v<binding_row_of_t<fe_::Row<>, affine>, fe_::Row<>>);
static_assert(std::is_same_v<binding_row_of_t<with_io, with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<binding_row_of_t<fe_::Row<>, with_io>, fe_::Row<fe_::Effect::IO>>);

}  // namespace detail::atom_self_test

}  // namespace fixy::atom
