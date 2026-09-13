// Each cheat below is an attempt to slip a binding past the
// known-unsoundness corpus, and each one must fail.  A green compile
// means none of the attacks escaped.
//
// The corpus entry under attack fires on a binding that carries a
// secret grant and an effect grant and no declassification.  An attack
// therefore has two shapes: suppress one of the two positive
// detections, or fabricate a declassification nobody granted.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
namespace th = crucible::fixy::theory;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// This pack engages every axis correctly and pairs a secret grant
// with an effect grant and no declassification, which is exactly what
// the corpus entry targets.  It must always reject.

template <typename ExtraGrant>
inline constexpr bool implicit_flow_pack_rejects =
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>,
                      gr::with_io,  // Effect = IO
                      gr::as_secret,  // Security = Secret
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, ExtraGrant>;

// The same pack without the secret-and-effect pairing accepts, so the
// gate is not simply refusing everything.
namespace counter_witness {
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Counter-witness: the canonical fully-engaged strict pack MUST "
    "accept, so the gate is not broken shut.");
}  // namespace counter_witness

// Cheat 1.  The attacker invents an empty struct with no
// declassification shape and specializes the declassify trait on it to
// true, hoping the fold over the grants pack sees a declassification
// and flips the corpus match to false.
//
// It fails because every grant must satisfy the grant-tag concept
// before the corpus is consulted at all.  A type that is neither final
// nor derived from the grant base loses at engagement, and the corpus
// check never runs.

namespace cheat_1_rogue_declassify_trait {
struct rogue {};  // not a grant tag: no base, not final
}  // namespace cheat_1_rogue_declassify_trait
namespace crucible::fixy::theory::detail {
template <>
struct is_declassify_grant<::cheat_1_rogue_declassify_trait::rogue> : std::true_type {};
}  // namespace crucible::fixy::theory::detail
namespace cheat_1_rogue_declassify_trait {
static_assert(!fixy::IsAcceptedGrants<rogue>, "Cheat 1: a rogue type cannot satisfy IsAcceptedGrants — "
                                              "engagement gate fires before the corpus.");
static_assert(implicit_flow_pack_rejects<rogue>, "Cheat 1: even with rogue specialized as a declassify, the "
                                                 "outer IsAccepted must reject because the rogue type is not "
                                                 "a well-formed grant.");
}  // namespace cheat_1_rogue_declassify_trait

// Cheat 2.  The attacker specializes the secret trait on the canonical
// secret tag to false, hoping to suppress the corpus's positive
// detection.
//
// The language forecloses it: the substrate already defines that
// specialization, and a second definition is a one-definition-rule
// violation the compiler rejects.  What remains testable is the shape
// the cheat would need, so the witnesses below assert the substrate's
// detection instead.

namespace cheat_2_flip_secret_trait {
// Redefining the trait here is not possible, so the witness is
// that the corpus detects the canonical security tag even beside
// an attacker-supplied effect grant.
static_assert(th::detail::is_secret_grant<gr::as_secret>::value, "Cheat 2 defense witness: the substrate's positive "
                                                                 "is_secret_grant specialization is definitive.");
static_assert(th::corpus::classified_io_without_declassify::matches<int, gr::as_secret, gr::with_io>(),
              "Cheat 2 defense witness: the corpus correctly detects "
              "the implicit-flow shape via the substrate's "
              "specializations — no user-side flip is possible.");
}  // namespace cheat_2_flip_secret_trait

// Cheat 3.  The attacker writes a final wrapper that derives from the
// grant base and engages the security axis, but that the secret trait
// does not recognise, since that trait names only the canonical tags.
// The pack then engages security through the wrapper, the corpus sees
// no secret, and the binding is accepted even though the intent
// matches the pattern the corpus exists to catch.
//
// This is a limit of the corpus rather than a hole in it.  The corpus
// is a closed set: it fires on named patterns drawn from the
// literature, and a tag it does not name is not classified as far as
// it is concerned.  The wrapper still engages the security axis at the
// resolver, which is a separate layer.  A user tag that means the same
// thing as the canonical one has to be added to the trait by name.

namespace cheat_3_transparent_secret_wrapper {
struct final_wrapper final : gr::grant_base {};
}  // namespace cheat_3_transparent_secret_wrapper
// fixy-CR-09: known residual gap
// Engaging the security axis for the wrapper means reopening the grant
// namespace, which is what an attacker would have to do.  C++ offers
// no access control over specializations, so a CI guard forbids the
// reopening everywhere else and greps the line above to except this
// file.  Deleting that line breaks the build.
namespace crucible::fixy::grant {
template <>
struct which_dim<::cheat_3_transparent_secret_wrapper::final_wrapper>
    : std::integral_constant<::crucible::fixy::dim::DimensionAxis, ::crucible::fixy::dim::DimensionAxis::Security> {};
}  // namespace crucible::fixy::grant
namespace cheat_3_transparent_secret_wrapper {
// The wrapper is a well-formed grant, as intended.
static_assert(gr::IsGrantTag<final_wrapper>, "Cheat 3 setup: the wrapper IS a structural grant.");

// The corpus does not treat it as a secret tag, because it is not
// one of the canonical two.  The witness records that limit rather
// than hiding it.
static_assert(!th::detail::is_secret_grant<final_wrapper>::value,
              "Cheat 3 architectural-limit witness: the corpus does not "
              "detect user-defined Security tags — closed-set discipline.");
}  // namespace cheat_3_transparent_secret_wrapper

// Cheat 4 is the mirror of cheat 1: specialize the effect trait on a
// rogue type to inject an effect the pack never carried.  That
// specialization is dead, because the rogue type is not in the pack
// and is not a grant tag either.  Masking a real effect grant instead
// runs into the same one-definition-rule wall as cheat 2, so the
// witnesses assert the substrate's detection.

namespace cheat_4_rogue_io_trait {
struct rogue {};  // not a grant tag
}  // namespace cheat_4_rogue_io_trait
namespace crucible::fixy::theory::detail {
template <>
struct is_io_effect_grant<::cheat_4_rogue_io_trait::rogue> : std::true_type {};
}  // namespace crucible::fixy::theory::detail
namespace cheat_4_rogue_io_trait {
static_assert(!fixy::IsAcceptedGrants<rogue>, "Cheat 4: a rogue type cannot satisfy IsAcceptedGrants — "
                                              "the engagement gate fires before the corpus.");
static_assert(th::detail::is_io_effect_grant<gr::with_io>::value,
              "Cheat 4 defense witness: the substrate's positive "
              "is_io_effect_grant specialization remains definitive on "
              "the canonical with_io tag.");
}  // namespace cheat_4_rogue_io_trait

// Cheat 5 engages the security axis twice, with two tags the corpus
// both recognises.  The pack never reaches the corpus: one grant per
// axis is enforced first.  The two gates are layers, and neither one
// alone would be enough.

namespace cheat_5_double_security_engagement {
static_assert(!fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, gr::with_io,
                                gr::as_secret,  // Security #1
                                strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                                strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                                strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                                strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
                                gr::as_classified>,  // Security #2 (duplicate axis)
              "Cheat 5: double-Security engagement must reject — "
              "UniqueEngagementPerAxis fires before the corpus.");
}  // namespace cheat_5_double_security_engagement

// Cheat 6 buries the declassification among other grants, in the hope
// that the fold over the pack short-circuits past it.  A fold
// expression computes every element before combining them, so the
// position makes no difference.  This one is therefore a
// counter-witness: a genuine declassification does take the binding
// out of the corpus.

namespace cheat_6_declassify_threading {
// A declassification names a policy, and the policy has to satisfy
// the policy concept.  An ad-hoc empty struct will not do, so the
// sample below names one from the catalog.
namespace sp = ::crucible::safety::secret_policy;

// The pack carries the pattern the corpus fires on, and the
// declassification discharges it, so the binding accepts.
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, gr::with_io,
                     gr::declassify<sp::AuditedLogging>,  // remediation
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 6 counter-witness: declassify<Policy> correctly "
    "discharges the corpus — the binding ACCEPTS even with the "
    "Secret×IO pattern present.");
}  // namespace cheat_6_declassify_threading

// Cheat 7 exploits the gap between syntax and meaning.  The strict
// default for the security axis resolves to the classified tier, so a
// pack that engages security only through that default means the same
// thing as one that names the secret tag outright.  A detector that
// matched tag shapes alone would see the first and miss the second.
//
// The secret trait therefore recognises the strict default as well as
// the named tag, with an assertion tying that default to the
// classified tier so the two cannot drift apart.  Both spellings reach
// the corpus.

namespace cheat_7_strict_default_security_bypass {
// The strict default paired with an effect grant and no
// declassification is the semantic twin of the explicit form, and
// rejects with it.
static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, gr::with_io,
                      strict<D::Security>,  // strict default = Classified
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 7 defense witness (classified_io): the corpus rejects "
    "strict-default Security x IO without declassify, so the "
    "syntactic and the semantic form are treated alike.");

// Pack 2: strict-default Security × Bg, NO declassify.
static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, gr::with_bg, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 7 defense witness (classified_bg): the corpus rejects "
    "strict-default Security x Bg without declassify.");

// Pack 3: strict-default Security × stale_to<N>, NO declassify.
static_assert(!fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                                strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                                strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                                strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                                strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, gr::stale_to<100>>,
              "Cheat 7 defense witness (staleness_secret): the corpus rejects "
              "strict-default Security x stale_to<N> without declassify.");

// There is no counter-witness pairing the strict default with a
// declassification, because a declassification engages the
// security axis itself.  The two together would double-engage that
// axis and reject on uniqueness before the corpus is consulted.
// Cheat 6 already covers the discharging shape.
}  // namespace cheat_7_strict_default_security_bypass

// Cheat 8 pays for a discharge on one axis and spends it on another.
// A declassification engages the security axis whatever policy it
// names, so a matcher that asked only whether some declassification
// was present would let a policy written for export audit trails
// silence a staleness entry.
//
// A discharge has to match the axis it authorizes: permission to log a
// value is not permission to replay a stale one.  Each policy
// therefore declares which axis it discharges, and the matcher asks
// for that axis by name.  Every policy declares none of them unless it
// says otherwise, so only the replay policy silences the staleness
// entry.

namespace cheat_8_wrong_axis_declassify {
namespace sp = ::crucible::safety::secret_policy;

// The attack: a logging policy paired with a staleness bound.
// The declassification engages the security axis on its own, and
// the per-axis check rejects because the policy discharges a
// different axis.
static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
                      gr::declassify<sp::AuditedLogging>,  // engages security, discharges the wrong axis
                      gr::stale_to<100>,  // engages Staleness
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Synchronization>, strict<D::Regime>,
                      strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                      strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                      strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 8 defense witness (wrong-axis declassify rejected): "
    "AuditedLogging discharges IO export, not Staleness-replay, and "
    "a discharge must match the axis it authorizes.");

// The same binding shape with the policy that does declare the
// staleness axis, which accepts.
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
                     gr::declassify<sp::AuthorizedReplay>,  // discharges the staleness axis
                     gr::stale_to<100>, strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
                     strict<D::Trust>, strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                     strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                     strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
                     strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                     strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                     strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 8 counter-witness: AuthorizedReplay correctly "
    "discharges the staleness axis — the binding ACCEPTS with the "
    "Secret × stale_to pattern present because the named policy "
    "carries DischargeAxis::Staleness in its axes_discharged_of "
    "specialization.");

// Three further policies declare no axis at all, and each rejects
// the same combination.  They are separate mismatch classes, not
// repetitions of one.
static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
                      gr::declassify<sp::HashForCompare>,  // engages security, declares no axis
                      gr::stale_to<50>, strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
                      strict<D::Trust>, strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                      strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                      strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 8 defense witness (HashForCompare doesn't discharge "
    "Staleness): hash-release is a Security-axis discharge, not "
    "temporal.");

static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::declassify<sp::LengthOnly>,
                      gr::stale_to<200>, strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
                      strict<D::Trust>, strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                      strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                      strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 8 defense witness (LengthOnly doesn't discharge "
    "Staleness): length-metadata release is a Security-axis "
    "discharge, not temporal.");

static_assert(
    !fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::declassify<sp::UserDisplay>,
                      gr::stale_to<10>, strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
                      strict<D::Trust>, strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                      strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                      strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Cheat 8 defense witness (UserDisplay doesn't discharge "
    "Staleness): UI-display release is an IO-axis discharge, not "
    "temporal.");
}  // namespace cheat_8_wrong_axis_declassify

int main() { return 0; }
