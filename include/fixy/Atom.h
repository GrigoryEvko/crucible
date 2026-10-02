#pragma once

// The atom catalog of fixy.  An atom is what a binding names to say
// more than the strict pole of one axis: `affine` relaxes Usage,
// `with<IO>` relaxes Effect, `declassify<Policy>` relaxes Security.
// Every atom is an empty final type that derives `atom_base` and
// carries the axis it engages as `static constexpr Axis axis`.  The axis
// is one line on the atom itself, so no header reopens a namespace to
// register it and no table of witnesses has to agree with it.
//
// An atom-less axis resolves to the strict pole in fixy/Axis.h.  That
// table is the only default.  There is no marker that accepts a
// default explicitly and no list of the axes that ship no atom.
//
// The families that reach the operating system live in fixy/atoms/Os.h,
// and the control-flow, call-shape, stack, global-state and stdio
// families in fixy/atoms/{Ctrl,Dispatch,Stack,Global,Stdio}.h.

#include <fixy/Axis.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <array>
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
// reads three facts that the type cannot state about itself, and a
// fourth fact about its name.
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
//   4. The identity.  The name of an atom is its identity in the row hash
//      of every binding that names it, so the name must be a function of
//      the type in every translation unit.  An atom whose arguments name
//      a closure type, an unnamed class or an entity with internal
//      linkage has no such name.  foundation::reflect::HasStableIdentity
//      reads the name, and an atom that fails it is refused.  A binding
//      that the gate admits can therefore always be keyed.
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
    ^^barrier, ^^ctrl,   ^^dispatch, ^^fp,      ^^global, ^^hw,    ^^io,    ^^fs,    ^^mmap, ^^leak,
    ^^observe, ^^regime, ^^scope,    ^^session, ^^simd,   ^^spawn, ^^stack, ^^stdio, ^^sync, ^^syscall,
};

// Why a type with the shape of an atom is refused, or none.
enum class atom_refusal : std::uint8_t {
    none,
    outside_the_catalog,  // not declared directly in fixy::atom or in a family
    namespace_unsealed,  // the namespace holds no atom_seal
    namespace_sealed_twice,  // the namespace holds two atom_seal variables
    declared_outside_its_seal,  // declared in a file other than the one that seals its namespace
    no_stable_identity  // its name is not a function of the type in every translation unit
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

// The refusal for a type that already has the shape.  The function is
// not a template, so no translation unit can specialize a verdict in
// front of it.  Complexity: linear in the members of the owning
// namespace, plus the identity walk of the type.
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
    if (!::foundation::reflect::has_stable_identity(type)) return atom_refusal::no_stable_identity;
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
        case atom_refusal::no_stable_identity:
            return "an argument names a closure type, an unnamed class or an entity with internal linkage, so "
                   "the name of the atom is not a function of its type, and the name is its key in the row hash";
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

}  // namespace detail

// The gate calls the refusal function itself.  A concept has no
// specialization, so no translation unit can put a different verdict in
// front of the four reads.  The namespace, the file and the identity are
// facts of the declaration.  GCC keeps the first satisfaction of the gate
// for each atom, so a second seal that a translation unit adds later
// refuses each atom first asked about after it.
template <class G>
concept IsAtom = detail::HasAtomShape<G> && detail::atom_refusal_of_(^^G) == detail::atom_refusal::none;

// Each concept below is the minimum structural bar a parametric atom's
// parameter must clear.  A parameter that fails one makes the atom
// template-id ill-formed where it is named, so the compiler names the
// parameter before any resolver reads the pack.  The identity of the
// parameter is not one of these bars: IsAtom reads the identity of the
// whole atom, for every parametric atom at once.

// The parameter of `protocol<P>` is a session type or a machine state type,
// so this gate stays open-world.  Enumerating the legal session combinators
// instead would be tighter but would refuse any user-defined state class.
template <typename Proto>
concept IsSessionProtocol = std::is_same_v<Proto, std::remove_cvref_t<Proto>> && std::is_class_v<Proto>;

// Whether a predicate is invocable on a given value type cannot be folded
// into a per-predicate gate, so that check happens per-type at construction
// and is deliberately absent here.
//
// The type of the predicate is the whole claim of `refined_with<Pred>`.  A
// stateful predicate, such as a threshold held in a member, makes a claim
// that its type does not state, so the gate asks for an empty class that
// the type alone can construct.
//
// `pole::pred::True` is refused.  It is the strict pole of the Refinement
// axis, the claim of a binding that names no refinement, and it proves
// nothing.  Written as an atom it would read as a witness: H002 asks a hot
// binding for one, and the vacuous predicate would satisfy it.  A binding
// that proves nothing names no Refinement atom.
//
// A closure is an empty class that the type alone can construct, so it
// passes this gate.  IsAtom refuses the atom, because the name of a
// closure type is not a function of the type.
template <typename Pred>
concept IsRefinementPredicate =
    std::is_same_v<Pred, std::remove_cvref_t<Pred>> && std::is_class_v<Pred> && std::is_empty_v<Pred>
    && std::is_default_constructible_v<Pred> && !std::is_same_v<Pred, ::fixy::pole::pred::True>;

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
// declared effects.  Without the lift, foundation/effects/Lift.h could
// not see the Effect axis's own atom.  A binding that declared IO could
// then be called from a context that admits nothing, because the row
// nobody computed is the empty row, and the empty row is a Subrow of
// every context's.
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
// the same shape as foundation/diag/FailClosed.h's relations and as
// payload_effect_row_t.
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
// lifts, and so do the waits of fixy/atoms/Sync.h, the system calls of
// fixy/atoms/Syscall.h and fixy/atoms/Os.h, and the writes of
// fixy/atoms/Stdio.h.  An atom with no lift reaches no operation, so it
// adds nothing to the union.
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

// `declassify<Policy>` names the policy that licenses a drop to the public
// level.  A policy licenses exactly the channels that its mask in
// fixy/Corpus.h names.  On each other channel the binding stays classified.

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
// A reader with its own table and a default of "public" would read a new
// point on the axis as public until somebody updated it.  On this axis
// "public" is the answer that lets a binding through, so that default
// fails open.
//
// So the relation names each grade and gives no answer for any other
// type.  A grade that it does not name fails at the use with its own name
// in the diagnostic, and the walk in the check file of this header makes
// sure every Security atom in the roster has an answer.
//
// The relation is one function over reflections, and each reading is a
// concept over it.  No translation unit can specialize a function that is
// not a template, or a concept.  A class template or a variable template
// that a rule reads is a door: a specialization for a class of a caller
// would give that class the public answer.
enum class SecurityClass : std::uint8_t {
    Public = 0,  // as_public, as_unclassified: the data was never classified
    Internal = 1,  // as_internal: below the classified carrier, above public
    Classified = 2,  // the strict pole, as_classified, as_secret
    ConstantTime = 3,  // constant_time: classified, with the timing channel closed
    Declassified = 4,  // declassify<Policy>: a named policy licenses the drop on the channels of its mask
};

namespace detail {

// The answer of the relation for one type: whether the type is a Security
// grade, and its class when it is one.
struct security_class_answer {
    bool is_grade = false;
    SecurityClass security_class = SecurityClass::Classified;
};

// The strict pole is a binding that says nothing about Security, and it is
// classified.  That is what reject-by-default means on this axis.  A
// qualified atom is not a grade, because the relation does not strip it.
[[nodiscard]] consteval security_class_answer security_class_answer_of_(std::meta::info grade) {
    const std::meta::info type = std::meta::dealias(grade);
    if (type == ^^as_unclassified || type == ^^as_public) return {true, SecurityClass::Public};
    if (type == ^^as_internal) return {true, SecurityClass::Internal};
    if (type == ^^as_classified || type == ^^as_secret
        || type == std::meta::dealias(^^axis_traits<Axis::Security>::strict)) {
        return {true, SecurityClass::Classified};
    }
    if (type == ^^constant_time) return {true, SecurityClass::ConstantTime};
    if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^declassify) {
        return {true, SecurityClass::Declassified};
    }
    return {};
}

// Declared and not defined, and not constexpr.  A constant evaluation that
// calls it fails, and the diagnostic gives its name as the reason.
void type_is_not_a_security_grade() noexcept;

}  // namespace detail

template <class Grade>
concept IsSecurityGrade = detail::security_class_answer_of_(^^Grade).is_grade;

// The class of a Security grade, written security_class_of(^^Grade).  A
// type that is not a Security grade has no class, and a call for one is
// not a constant expression.
[[nodiscard]] consteval SecurityClass security_class_of(std::meta::info grade) {
    const detail::security_class_answer answer = detail::security_class_answer_of_(grade);
    if (!answer.is_grade) detail::type_is_not_a_security_grade();
    return answer.security_class;
}

// The two readings every rule shares.  A carrier holds classified data,
// with or without the timing claim.  A declassified grade is not a
// carrier.  fixy/Corpus.h reads its policy for each channel, because a
// policy licenses only the channels of its mask.  A type that is not a
// Security grade satisfies neither.
template <class Grade>
concept IsClassifiedCarrier = IsSecurityGrade<Grade>
                           && (security_class_of(^^Grade) == SecurityClass::Classified
                               || security_class_of(^^Grade) == SecurityClass::ConstantTime);

template <class Grade>
concept IsConstantTime = IsSecurityGrade<Grade> && security_class_of(^^Grade) == SecurityClass::ConstantTime;

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

// One atom per remaining trust level.  `trust_verified` names the
// Verified level.  The strict pole in fixy/Axis.h is Unverified, so a
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
// The checks of a family read one hand list, the roster: a std::tuple of
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
// here without a hand list of the enumerators.  The loop runs only when a
// call evaluates it, so an includer that checks no roster walks no enum.
[[nodiscard]] consteval bool is_axis_enumerator_(Axis value) noexcept {
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^::fixy::Axis)) {
        if (std::meta::extract<Axis>(std::meta::constant_of(enumerator)) == value) return true;
    }
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

// No member of the roster lifts to an effect row.
template <class Roster>
[[nodiscard]] consteval bool no_roster_member_lifts_() noexcept {
    bool none_lift = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        none_lift = none_lift && !::foundation::effects::LiftsToRow<A>;
    }
#pragma GCC diagnostic pop
    return none_lift;
}

// ── The ladder families ─────────────────────────────────────────────
//
// A ladder family gives one atom to each enumerator of an enum: the
// regime tiers, the instruction tiers, the barrier strengths, the memory
// scopes, the wait strategies and the SIMD instruction sets.  Each atom
// names its enumerator in one static data member of the enum type.  The
// walk below reads that member by reflection, so a family states its
// roster and its enum and writes no walk of its own.

// How many static data members of Atom have the type Enum.
template <class Atom, class Enum>
[[nodiscard]] consteval std::size_t ladder_members_of_() noexcept {
    std::size_t found = 0;
    for (const std::meta::info member :
         std::meta::static_data_members_of(^^Atom, std::meta::access_context::current())) {
        if (std::meta::remove_cv(std::meta::type_of(member)) == ^^Enum) ++found;
    }
    return found;
}

// The enumerator that Atom names.  The walk reads it only after
// ladder_members_of_ finds exactly one member of the enum type.
template <class Atom, class Enum>
[[nodiscard]] consteval Enum ladder_grade_of_() noexcept {
    Enum grade{};
    for (const std::meta::info member :
         std::meta::static_data_members_of(^^Atom, std::meta::access_context::current())) {
        if (std::meta::remove_cv(std::meta::type_of(member)) == ^^Enum) grade = std::meta::extract<Enum>(member);
    }
    return grade;
}

// Each member of the roster names exactly one enumerator of Enum, and each
// enumerator is named by exactly one member.  A count of atoms would pass
// against two atoms that name one enumerator, so the walk counts the claims
// on each enumerator.  An enumerator with no atom cannot be written, and one
// with two makes one of them unreachable.  A member that names a value
// outside the enumerators is refused too.
//
// The walk expands the roster one time.  Each member adds one claim to each
// enumerator that holds the value it names, in a plain loop.  Then each
// enumerator must hold exactly one claim.  Complexity: the roster size
// times the number of enumerators.
template <class Roster, class Enum>
    requires std::is_scoped_enum_v<Enum>
[[nodiscard]] consteval bool every_enumerator_has_exactly_one_atom_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Enum));
    std::array<std::size_t, enumerators.size()> claims{};
    bool exact = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (ladder_members_of_<A, Enum>() == 1) {
            const Enum grade = ladder_grade_of_<A, Enum>();
            bool names_an_enumerator = false;
            for (std::size_t index = 0; index < enumerators.size(); ++index) {
                if (std::meta::extract<Enum>(std::meta::constant_of(enumerators[index])) == grade) {
                    ++claims[index];
                    names_an_enumerator = true;
                }
            }
            exact = exact && names_an_enumerator;
        } else {
            exact = false;
        }
    }
#pragma GCC diagnostic pop
    for (const std::size_t claim_count : claims) {
        exact = exact && claim_count == 1;
    }
    return exact;
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

// Protocol, Provenance and Refinement atoms are parameterized by a caller
// tag type, and their concepts require a COMPLETE empty class — an
// elaborated `struct X` written inline forward-declares instead, which the
// concept rejects.  Three local witnesses, defined here rather than
// borrowed from a production tag tree so the roster does not couple to
// one.  `in_region` takes a non-type `auto` parameter, so it witnesses
// with a value rather than one of these tags.
struct atom_axis_witness_proto final {};
struct atom_axis_witness_source final {};
struct atom_axis_witness_predicate final {};

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
               sized_at<1>, productive, version<3>, stale_to<5>, refined_with<atom_axis_witness_predicate>>;

}  // namespace detail

}  // namespace fixy::atom
