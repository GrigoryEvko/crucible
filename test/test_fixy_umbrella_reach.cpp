// This translation unit includes the fixy umbrella and nothing else.
//
// Every claim below names a symbol through a fixy namespace and checks
// it against the substrate symbol it is supposed to alias.  So the TU
// stops compiling the moment the umbrella drops one of its sub-includes,
// which is the failure this file exists to catch: a downstream consumer
// that includes only the umbrella silently loses a whole surface, and
// nothing else notices.
//
// The sub-headers carry their own sentinels, but those fire only for a
// caller that already includes them directly, so they cannot witness
// umbrella reach.  That is why the gate lives in a separate TU.

#include <crucible/Fixy.h>

#include <type_traits>
#include <utility>

namespace fixy = ::crucible::fixy;
namespace fcc = ::crucible::fixy::contract::cipher;
namespace cs = ::crucible::safety;
namespace cc = ::crucible::cipher;

#if CRUCIBLE_FIXY_STRICT
static_assert(fixy::fixy_is_strict, "umbrella reach: fixy::fixy_is_strict must be true under "
                                    "CRUCIBLE_FIXY_STRICT=1.  If this red-lights, fixy/Profile.h is "
                                    "not pulled in by <crucible/Fixy.h>.");
#else
static_assert(!fixy::fixy_is_strict, "umbrella reach: fixy::fixy_is_strict must be false under "
                                     "CRUCIBLE_FIXY_STRICT=0.");
#endif

static_assert(fixy::IsAcceptedSketch<int>, "umbrella reach: fixy::IsAcceptedSketch must resolve through the "
                                           "umbrella.");

// An empty Grants pack engages nothing, so the toggle alone decides the
// verdict.
#if CRUCIBLE_FIXY_STRICT
static_assert(!fixy::IsAcceptedActive<int>, "umbrella reach: under STRICT, IsAcceptedActive<int> with empty "
                                            "Grants must reject.");
#else
static_assert(fixy::IsAcceptedActive<int>, "umbrella reach: under SKETCH, IsAcceptedActive<int> with empty "
                                           "Grants must accept.");
#endif

// This witnesses that the concept template instantiates at all, not that
// it gives a particular verdict.  The verdict is already pinned above.
template <typename T>
constexpr bool umbrella_reach_active_resolves = requires { requires fixy::IsAcceptedActive<T>; };

// Only the sketch branch can assert a true result, because under strict
// mode the empty Grants pack makes the concept false.
#if !CRUCIBLE_FIXY_STRICT
static_assert(umbrella_reach_active_resolves<int>, "umbrella reach: under SKETCH, fixy::IsAcceptedActive<int> must "
                                                   "instantiate true through the umbrella.");
#endif

static_assert(
    std::is_same_v<fcc::CipherTier<cs::CipherTierTag_v::Hot, int>, cs::CipherTier<cs::CipherTierTag_v::Hot, int>>,
    "umbrella reach: fixy::contract::cipher::CipherTier must alias "
    "safety::CipherTier when reached via the umbrella.");

static_assert(std::is_same_v<fcc::HotTierHandle<int>, cs::cipher_tier::Hot<int>>,
              "umbrella reach: fixy::contract::cipher::HotTierHandle must alias "
              "cipher_tier::Hot when reached via the umbrella.");

static_assert(std::is_same_v<fcc::WarmTierHandle<int>, cs::cipher_tier::Warm<int>>,
              "umbrella reach: fixy::contract::cipher::WarmTierHandle must alias "
              "cipher_tier::Warm when reached via the umbrella.");

static_assert(std::is_same_v<fcc::ColdTierHandle<int>, cs::cipher_tier::Cold<int>>,
              "umbrella reach: fixy::contract::cipher::ColdTierHandle must alias "
              "cipher_tier::Cold when reached via the umbrella.");

static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &fcc::mint_promote<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot, int>)
        == static_cast<
            cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
            &cc::mint_promote<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot, int>),
    "umbrella reach: fixy::contract::cipher::mint_promote must be the "
    "substrate cipher::mint_promote when reached via the umbrella.");

// The contract macros are reached the same way as a type: the function
// below only compiles if they expand, and the static_assert only passes
// if they expand in a consteval context.

[[nodiscard]] constexpr int umbrella_reach_contract_demo(int n) noexcept {
    CRUCIBLE_PRE(n > 0);
    int const result = n * 2;
    CRUCIBLE_POST(result, result == n * 2);
    return result;
}

static_assert(umbrella_reach_contract_demo(7) == 14, "umbrella reach: CRUCIBLE_PRE/CRUCIBLE_POST must expand cleanly "
                                                     "from <crucible/Fixy.h>.");

namespace fsd = ::crucible::fixy::sess::declassify;

namespace u052a_reach_probe {
struct WirePayload {};
using WireMsg = fsd::DeclassifyOnSend<WirePayload, ::crucible::safety::secret_policy::WireSerialize>;
}  // namespace u052a_reach_probe

static_assert(std::is_same_v<u052a_reach_probe::WireMsg,
                             ::crucible::safety::DeclassifyOnSend<u052a_reach_probe::WirePayload,
                                                                  ::crucible::safety::secret_policy::WireSerialize>>,
              "umbrella reach: fixy::sess::declassify::DeclassifyOnSend must "
              "alias safety::DeclassifyOnSend when reached via the umbrella.  "
              "If this red-lights, fixy/SessDecl.h is not pulled in by "
              "<crucible/Fixy.h>.");

static_assert(fsd::DeclassifyOnSendable<u052a_reach_probe::WireMsg>,
              "umbrella reach: fixy::sess::declassify::DeclassifyOnSendable "
              "must accept DeclassifyOnSend specialisations.");
static_assert(!fsd::DeclassifyOnSendable<u052a_reach_probe::WirePayload>,
              "umbrella reach: fixy::sess::declassify::DeclassifyOnSendable "
              "must reject bare payloads.");

static_assert(std::is_same_v<fsd::wire_payload_type_t<u052a_reach_probe::WireMsg>, u052a_reach_probe::WirePayload>,
              "umbrella reach: fixy::sess::declassify::wire_payload_type_t must "
              "extract the inner payload through the umbrella.");
static_assert(std::is_same_v<fsd::wire_payload_type_t<int>, int>,
              "umbrella reach: fixy::sess::declassify::wire_payload_type_t must "
              "pass non-DeclassifyOnSend types through unchanged.");

static_assert(
    std::is_same_v<fsd::wire_policy_t<u052a_reach_probe::WireMsg>, ::crucible::safety::secret_policy::WireSerialize>,
    "umbrella reach: fixy::sess::declassify::wire_policy_t must "
    "extract the wire-policy tag through the umbrella.");

namespace fsct = ::crucible::fixy::sess::ct;

namespace u052b_reach_probe {
struct AuthTag {
    std::byte bytes[16]{};
};
struct PlainTag {
    std::byte bytes[16]{};
};  // deliberately not opted in
}  // namespace u052b_reach_probe

// A specialization has to be declared in the namespace of the primary
// template, so the opt-in for the probe type above lands here rather
// than next to the probe.
namespace crucible::safety::ct {
template <>
struct requires_ct<u052b_reach_probe::AuthTag> : std::true_type {};
}  // namespace crucible::safety::ct

static_assert(std::is_same_v<fsct::CTPayload<u052b_reach_probe::AuthTag>,
                             ::crucible::safety::ct::CTPayload<u052b_reach_probe::AuthTag>>,
              "umbrella reach: fixy::sess::ct::CTPayload must alias "
              "safety::ct::CTPayload when reached via the umbrella.  If this "
              "red-lights, fixy/SessCT.h is not pulled in by <crucible/Fixy.h>.");

static_assert(fsct::requires_ct_v<u052b_reach_probe::AuthTag>,
              "umbrella reach: fixy::sess::ct::requires_ct_v must observe the "
              "opt-in specialization through the umbrella.");
static_assert(!fsct::requires_ct_v<u052b_reach_probe::PlainTag>,
              "umbrella reach: fixy::sess::ct::requires_ct_v must reject "
              "non-opted-in types through the umbrella.");

static_assert(fsct::RequiresCT<u052b_reach_probe::AuthTag>);
static_assert(!fsct::RequiresCT<u052b_reach_probe::PlainTag>);

static_assert(fsct::is_ct_payload_v<fsct::CTPayload<u052b_reach_probe::AuthTag>>);
static_assert(!fsct::is_ct_payload_v<u052b_reach_probe::AuthTag>);
static_assert(fsct::CTPayloadType<fsct::CTPayload<u052b_reach_probe::AuthTag>>);

static_assert(std::is_same_v<fsct::ct_payload_value_type_t<fsct::CTPayload<u052b_reach_probe::AuthTag>>,
                             u052b_reach_probe::AuthTag>,
              "umbrella reach: fixy::sess::ct::ct_payload_value_type_t must "
              "extract the inner payload through the umbrella.");
static_assert(std::is_same_v<fsct::ct_payload_value_type_t<int>, int>,
              "umbrella reach: fixy::sess::ct::ct_payload_value_type_t must "
              "pass non-CTPayload types through unchanged.");

namespace fsca = ::crucible::fixy::sess::contentaddr;

namespace u052c_reach_probe {
struct Payload {};
}  // namespace u052c_reach_probe

static_assert(std::is_same_v<fsca::ContentAddressed<u052c_reach_probe::Payload>,
                             ::crucible::safety::proto::ContentAddressed<u052c_reach_probe::Payload>>,
              "umbrella reach: fixy::sess::contentaddr::ContentAddressed must "
              "alias safety::proto::ContentAddressed when reached via the "
              "umbrella.  If this red-lights, fixy/SessContentAddr.h is not "
              "pulled in by <crucible/Fixy.h>.");

static_assert(fsca::is_content_addressed_v<fsca::ContentAddressed<u052c_reach_probe::Payload>>,
              "umbrella reach: fixy::sess::contentaddr::is_content_addressed_v "
              "must observe wrapped payloads through the umbrella.");
static_assert(!fsca::is_content_addressed_v<u052c_reach_probe::Payload>,
              "umbrella reach: fixy::sess::contentaddr::is_content_addressed_v "
              "must reject bare payloads through the umbrella.");

static_assert(fsca::ContentAddressedType<fsca::ContentAddressed<u052c_reach_probe::Payload>>);
static_assert(!fsca::ContentAddressedType<u052c_reach_probe::Payload>);

static_assert(
    std::is_same_v<
        fsca::unwrap_content_addressed_t<fsca::ContentAddressed<fsca::ContentAddressed<u052c_reach_probe::Payload>>>,
        u052c_reach_probe::Payload>,
    "umbrella reach: fixy::sess::contentaddr::unwrap_content_addressed_t "
    "must strip all wrapper layers through the umbrella.");

static_assert(fsca::content_addressed_depth_v<u052c_reach_probe::Payload> == 0);
static_assert(fsca::content_addressed_depth_v<fsca::ContentAddressed<u052c_reach_probe::Payload>> == 1);

namespace fsel = ::crucible::fixy::sess::eventlog;

static_assert(std::is_same_v<fsel::StepId, ::crucible::safety::proto::StepId>,
              "umbrella reach: fixy::sess::eventlog::StepId must alias "
              "safety::proto::StepId.  If this red-lights, fixy/SessEventLog.h is "
              "not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fsel::SessionTagId, ::crucible::safety::proto::SessionTagId>);
static_assert(std::is_same_v<fsel::StepIdKeyFn, ::crucible::safety::proto::StepIdKeyFn>);
static_assert(std::is_same_v<fsel::StepIdLess, ::crucible::safety::proto::StepIdLess>);

// The 72-byte size is the cold-tier record layout on disk.
static_assert(std::is_same_v<fsel::SessionEvent, ::crucible::safety::proto::SessionEvent>);
static_assert(sizeof(fsel::SessionEvent) == 72, "umbrella reach: fixy::sess::eventlog::SessionEvent must keep its "
                                                "72-byte cold-tier wire format through the umbrella.");

static_assert(std::is_same_v<fsel::SessionEventLog, ::crucible::safety::proto::SessionEventLog>);
static_assert(std::is_same_v<fsel::SessionOp, ::crucible::safety::proto::SessionOp>);

// SessionEvent is exported under two fixy namespaces.  Both have to
// name one substrate type, or a consumer that mixes the two spellings
// gets two incompatible records.
static_assert(std::is_same_v<fsel::SessionEvent, ::crucible::fixy::contract::cipher::SessionEvent>,
              "dual-export: fixy::sess::eventlog::SessionEvent and "
              "fixy::contract::cipher::SessionEvent must be the same substrate type.");

namespace fss = ::crucible::fixy::sess::subtype;

static_assert(fss::is_subtype_sync_v<::crucible::safety::proto::End, ::crucible::safety::proto::End>,
              "umbrella reach: fixy::sess::subtype::is_subtype_sync_v must reach "
              "safety::proto.  If this red-lights, fixy/SessSubtype.h is not "
              "pulled in by <crucible/Fixy.h>.");
static_assert(!fss::is_subtype_sync_v<::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                                      ::crucible::safety::proto::Recv<int, ::crucible::safety::proto::End>>,
              "umbrella reach: Send/Recv shape mismatch must be rejected through "
              "the fixy path too.");

static_assert(fss::SubtypeSync<::crucible::safety::proto::End, ::crucible::safety::proto::End>);

static_assert(std::is_same_v<fss::SubtypeOk, ::crucible::safety::proto::SubtypeOk>);
static_assert(
    std::is_same_v<fss::subtype_rejection_reason_t<::crucible::safety::proto::End, ::crucible::safety::proto::End>,
                   fss::SubtypeOk>,
    "umbrella reach: End ⩽ End yields the SubtypeOk sentinel through "
    "the fixy::sess::subtype path.");

namespace fsq = ::crucible::fixy::sess::queue;

namespace u052f_reach {
struct RoleA {};
struct RoleB {};
using Msg = fsq::QueuedMsg<RoleA, RoleB, int>;
}  // namespace u052f_reach

static_assert(std::is_same_v<fsq::EmptyQueue, ::crucible::safety::proto::EmptyQueue>,
              "umbrella reach: fixy::sess::queue::EmptyQueue must alias "
              "safety::proto::EmptyQueue.  If this red-lights, fixy/SessQueue.h is "
              "not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fsq::QueuedMsg<u052f_reach::RoleA, u052f_reach::RoleB, int>,
                             ::crucible::safety::proto::QueuedMsg<u052f_reach::RoleA, u052f_reach::RoleB, int>>);

static_assert(std::is_same_v<fsq::enqueue_queue_t<fsq::EmptyQueue, u052f_reach::Msg>, fsq::Queue<u052f_reach::Msg>>,
              "umbrella reach: enqueue right-appends through the fixy path.");
static_assert(fsq::queue_size_v<fsq::Queue<u052f_reach::Msg, u052f_reach::Msg>> == 2);

static_assert(fsq::queue_contains_v<fsq::Queue<u052f_reach::Msg>, u052f_reach::RoleA, u052f_reach::RoleB>);
static_assert(fsq::is_queue_state_v<fsq::EmptyQueue>);

// This is the session diagnostic catalog, which is a different catalog
// from the one under fixy::diag::.

namespace fsdiag = ::crucible::fixy::sess::diagnostic;

static_assert(std::is_same_v<fsdiag::SubtypeMismatch, ::crucible::safety::proto::diagnostic::SubtypeMismatch>,
              "umbrella reach: fixy::sess::diagnostic::SubtypeMismatch must alias "
              "safety::proto::diagnostic::SubtypeMismatch.  If this red-lights, "
              "fixy/SessDiagnostic.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fsdiag::Catalog, ::crucible::safety::proto::diagnostic::Catalog>);

// The exact count is pinned next to the catalog tuple itself, so an
// addition is caught there.  This end holds only the floor, which
// catches a removal.  A floor here rather than an equality also means
// adding a diagnostic does not force an edit in two places.
static_assert(fsdiag::is_diagnostic_class_v<fsdiag::SubtypeMismatch>);
static_assert(!fsdiag::is_diagnostic_class_v<int>);
static_assert(fsdiag::catalog_size >= 23, "floor: fixy::sess::diagnostic::catalog_size regressed below 23 "
                                          "— a session-diagnostic Catalog entry was removed without "
                                          "updating both SessDiagnostic.h's colocated ceiling pin AND this "
                                          "floor witness.");

static_assert(fsdiag::is_diagnostic_v<fsdiag::Diagnostic<fsdiag::SubtypeMismatch, int>>);

// The payload-subsort axioms are specializations of the subtype
// relation, so they travel through the subtype header rather than one
// of their own.  Losing their visibility is the dangerous case: the
// primary template wins silently and these claims flip to false rather
// than failing to compile, which is why they are asserted positively.

static_assert(fss::is_subsort_v<::crucible::safety::Refined<::crucible::safety::positive, int>, int>,
              "umbrella reach: the Refined<P,T> ⩽ T narrowing axiom must be "
              "visible — if false, fixy/SessSubtype.h dropped its include of "
              "<crucible/sessions/SessionPayloadSubsort.h> and the primary template "
              "silently won.");

// Sanitized provenance is safe to erase, external provenance is not.
static_assert(fss::is_subsort_v<::crucible::safety::Tagged<int, ::crucible::safety::source::Sanitized>, int>);
static_assert(!fss::is_subsort_v<::crucible::safety::Tagged<int, ::crucible::safety::source::External>, int>,
              "trust-boundary discipline must reach the umbrella consumer: "
              "External provenance does not flow to bare T.");

namespace fsctx = ::crucible::fixy::sess::context;

namespace u052i_reach {
struct Sess {};
struct RoleP {};
struct RoleC {};
struct TyP {};
struct TyC {};
using Ctx = fsctx::Context<fsctx::Entry<Sess, RoleP, TyP>, fsctx::Entry<Sess, RoleC, TyC>>;
}  // namespace u052i_reach

static_assert(std::is_same_v<fsctx::EmptyContext, ::crucible::safety::proto::EmptyContext>,
              "umbrella reach: fixy::sess::context::EmptyContext must alias "
              "safety::proto::EmptyContext.  If this red-lights, fixy/SessContext.h "
              "is not pulled in by <crucible/Fixy.h>.");

static_assert(fsctx::context_size_v<u052i_reach::Ctx> == 2);
static_assert(
    std::is_same_v<fsctx::lookup_context_t<u052i_reach::Ctx, u052i_reach::Sess, u052i_reach::RoleP>, u052i_reach::TyP>);
static_assert(fsctx::contains_key_v<u052i_reach::Ctx, u052i_reach::Sess, u052i_reach::RoleC>);

namespace fsg = ::crucible::fixy::sess::grade;

namespace u052j_reach {
struct Payload {};
using P = ::crucible::safety::proto::Send<
    ::crucible::safety::NumericalTier<::crucible::safety::proto::Tolerance::BITEXACT, Payload>,
    ::crucible::safety::proto::End>;
}  // namespace u052j_reach

static_assert(std::is_same_v<fsg::axis::NumericalTier, ::crucible::safety::proto::axis::NumericalTier>,
              "umbrella reach: fixy::sess::grade::axis::NumericalTier must alias "
              "safety::proto::axis::NumericalTier.  If this red-lights, "
              "fixy/SessGrade.h is not pulled in by <crucible/Fixy.h>.");

static_assert(fsg::protocol_grade_numerical_tier_v<u052j_reach::P> == ::crucible::safety::proto::Tolerance::BITEXACT);
static_assert(fsg::grade_for_axis_v<fsg::axis::NumericalTier,
                                    ::crucible::safety::NumericalTier<::crucible::safety::proto::Tolerance::BITEXACT,
                                                                      u052j_reach::Payload>>
              == ::crucible::safety::proto::Tolerance::BITEXACT);

static_assert(fsg::protocol_grade_aggregate_satisfies_v<u052j_reach::P, u052j_reach::P>);

// These are free functions, not types, so identity is witnessed on the
// function-pointer type rather than by comparing addresses.  Comparing
// addresses trips -Werror=tautological-compare, because the compiler
// folds both sides of a `==` between two names of one function.  A
// using-declaration introduces no new function entity, so equal
// pointer types is the structural fact that matters here.

namespace fwrap = ::crucible::fixy::wrap;

static_assert(std::is_same_v<decltype(&fwrap::add_sat_checked<std::uint32_t>),
                             decltype(&::crucible::safety::add_sat_checked<std::uint32_t>)>,
              "umbrella reach: fixy::wrap::add_sat_checked must alias "
              "safety::add_sat_checked when reached via the umbrella.  If this "
              "red-lights, fixy/Wrap.h dropped the using-decl or Fixy.h fails "
              "to pull in fixy/Wrap.h.");

static_assert(std::is_same_v<decltype(&fwrap::sub_sat_checked<std::int32_t>),
                             decltype(&::crucible::safety::sub_sat_checked<std::int32_t>)>,
              "umbrella reach: fixy::wrap::sub_sat_checked must alias "
              "safety::sub_sat_checked when reached via the umbrella.");

static_assert(std::is_same_v<decltype(&fwrap::mul_sat_checked<std::uint16_t>),
                             decltype(&::crucible::safety::mul_sat_checked<std::uint16_t>)>,
              "umbrella reach: fixy::wrap::mul_sat_checked must alias "
              "safety::mul_sat_checked when reached via the umbrella.");

// The behavioral check runs in a consteval context, so it does not
// depend on the linker resolving the substrate symbol.
namespace u096b_reach_probe {
consteval bool fixy_wrap_sat_smoke() noexcept {
    auto a = fwrap::add_sat_checked<std::uint8_t>(std::uint8_t{200}, std::uint8_t{100});  // 300>255
    if (a.value() != std::uint8_t{255}) return false;
    if (!a.was_clamped()) return false;
    auto b = fwrap::sub_sat_checked<std::uint8_t>(std::uint8_t{10}, std::uint8_t{50});  // -40<0
    if (b.value() != std::uint8_t{0}) return false;
    if (!b.was_clamped()) return false;
    auto c = fwrap::mul_sat_checked<std::uint8_t>(std::uint8_t{2}, std::uint8_t{3});  // 6, no clamp
    if (c.value() != std::uint8_t{6}) return false;
    if (c.was_clamped()) return false;
    return true;
}
static_assert(fixy_wrap_sat_smoke(), "umbrella reach: fixy::wrap:: saturating-arithmetic free functions "
                                     "must produce the substrate's behavioral semantics through the "
                                     "umbrella path.");
}  // namespace u096b_reach_probe

namespace fsassoc = ::crucible::fixy::sess::assoc;

namespace v059_reach {
struct SessZ {};
struct RoleA {};
struct RoleB {};
struct Msg {};
using G = ::crucible::safety::proto::Transmission<RoleA, RoleB, Msg, ::crucible::safety::proto::End_G>;
using Gamma = fsassoc::projected_context_t<G, SessZ>;
}  // namespace v059_reach

static_assert(
    std::is_same_v<v059_reach::Gamma, ::crucible::safety::proto::projected_context_t<v059_reach::G, v059_reach::SessZ>>,
    "umbrella reach: fixy::sess::assoc::projected_context_t must alias "
    "safety::proto::projected_context_t.  If this red-lights, "
    "fixy/SessAssoc.h is not pulled in by <crucible/Fixy.h>.");

static_assert(fsassoc::domain_matches_v<v059_reach::Gamma, v059_reach::G, v059_reach::SessZ>);
static_assert(fsassoc::all_entries_refine_projection_v<v059_reach::Gamma, v059_reach::G, v059_reach::SessZ>);
static_assert(fsassoc::is_associated_v<v059_reach::Gamma, v059_reach::G, v059_reach::SessZ>);

namespace fsdelegate = ::crucible::fixy::sess::delegate;

namespace v060_reach {
struct Req {};
struct Ack {};
struct CrashTag {};
using T = ::crucible::safety::proto::Send<Req, ::crucible::safety::proto::End>;
using K = ::crucible::safety::proto::End;
using D = fsdelegate::Delegate<T, K>;
using A = fsdelegate::Accept<T, K>;
using ED = fsdelegate::EpochedDelegate<T, K, 11u, 13u>;
using EA = fsdelegate::EpochedAccept<T, K, 11u, 13u>;
}  // namespace v060_reach

static_assert(std::is_same_v<v060_reach::D, ::crucible::safety::proto::Delegate<v060_reach::T, v060_reach::K>>,
              "umbrella reach: fixy::sess::delegate::Delegate must alias "
              "safety::proto::Delegate.  If this red-lights, fixy/SessDelegate.h "
              "is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<v060_reach::A, ::crucible::safety::proto::Accept<v060_reach::T, v060_reach::K>>);

// The non-type parameters have to survive the alias, not just the type.
static_assert(v060_reach::ED::min_epoch == 11u);
static_assert(v060_reach::ED::min_generation == 13u);
static_assert(v060_reach::EA::min_epoch == 11u);
static_assert(v060_reach::EA::min_generation == 13u);

static_assert(fsdelegate::is_delegate_v<v060_reach::D>);
static_assert(fsdelegate::is_accept_v<v060_reach::A>);
static_assert(fsdelegate::is_delegation_head_v<v060_reach::ED>);
static_assert(fsdelegate::is_delegation_head_v<v060_reach::EA>);

static_assert(
    std::is_same_v<fsdelegate::Delegate_seq<v060_reach::Req, v060_reach::Ack, v060_reach::K>,
                   ::crucible::safety::proto::Delegate<
                       v060_reach::Req, ::crucible::safety::proto::Delegate<v060_reach::Ack, v060_reach::K>>>);
static_assert(std::is_same_v<fsdelegate::Redelegate<v060_reach::T, v060_reach::K>,
                             ::crucible::safety::proto::Accept<
                                 v060_reach::T, ::crucible::safety::proto::Delegate<v060_reach::T, v060_reach::K>>>);

// A concept can only be witnessed by constraining something, so each
// one gets a consteval function whose instantiation is the proof.
template <typename P, typename R>
    requires fsdelegate::CanDelegate<P, R>
consteval bool v060_can_delegate_witness() {
    return true;
}
static_assert(v060_can_delegate_witness<v060_reach::T, v060_reach::CrashTag>());

template <typename C, typename Td>
    requires fsdelegate::DelegatesTo<C, Td>
consteval bool v060_delegates_to_witness() {
    return true;
}
static_assert(v060_delegates_to_witness<v060_reach::D, v060_reach::T>());

consteval bool v060_assert_delegates_to_witness() {
    fsdelegate::assert_delegates_to<v060_reach::D, v060_reach::T>();
    return true;
}
static_assert(v060_assert_delegates_to_witness());

namespace fscheckpoint = ::crucible::fixy::sess::checkpoint;

namespace v061_reach {
struct Req {};
struct Resp {};
struct Err {};
using CommitPath =
    ::crucible::safety::proto::Send<Req, ::crucible::safety::proto::Recv<Resp, ::crucible::safety::proto::End>>;
using RollbackPath =
    ::crucible::safety::proto::Send<Req, ::crucible::safety::proto::Recv<Err, ::crucible::safety::proto::End>>;
using Ckpt = fscheckpoint::CheckpointedSession<CommitPath, RollbackPath>;
using Plain = ::crucible::safety::proto::Send<Req, ::crucible::safety::proto::End>;
}  // namespace v061_reach

static_assert(
    std::is_same_v<v061_reach::Ckpt,
                   ::crucible::safety::proto::CheckpointedSession<v061_reach::CommitPath, v061_reach::RollbackPath>>,
    "umbrella reach: fixy::sess::checkpoint::CheckpointedSession must "
    "alias safety::proto::CheckpointedSession.  If this red-lights, "
    "fixy/SessCheckpoint.h is not pulled in by <crucible/Fixy.h>.");

static_assert(fscheckpoint::is_checkpointed_session_v<v061_reach::Ckpt>);
static_assert(!fscheckpoint::is_checkpointed_session_v<v061_reach::Plain>);

static_assert(std::is_same_v<fscheckpoint::checkpoint_base_t<v061_reach::Ckpt>, v061_reach::CommitPath>);
static_assert(std::is_same_v<fscheckpoint::checkpoint_rollback_t<v061_reach::Ckpt>, v061_reach::RollbackPath>);

template <typename P>
    requires fscheckpoint::Checkpointed<P>
consteval bool v061_checkpointed_witness() {
    return true;
}
static_assert(v061_checkpointed_witness<v061_reach::Ckpt>());

consteval bool v061_assert_matches_witness() {
    fscheckpoint::assert_checkpointed_matches<v061_reach::Ckpt, v061_reach::CommitPath, v061_reach::RollbackPath>();
    return true;
}
static_assert(v061_assert_matches_witness());

namespace fsrow = ::crucible::fixy::sess::row;

namespace v062_reach {
using IoComp = ::crucible::effects::Computation<::crucible::effects::Row<::crucible::effects::Effect::IO>, int>;
using SendIo = ::crucible::safety::proto::Send<IoComp, ::crucible::safety::proto::End>;
}  // namespace v062_reach

static_assert(
    std::is_same_v<
        fsrow::NumericalPayloadRow<::crucible::safety::Tolerance::BITEXACT,
                                   ::crucible::effects::Row<::crucible::effects::Effect::IO>>,
        ::crucible::safety::proto::NumericalPayloadRow<::crucible::safety::Tolerance::BITEXACT,
                                                       ::crucible::effects::Row<::crucible::effects::Effect::IO>>>,
    "umbrella reach: fixy::sess::row::NumericalPayloadRow must alias "
    "safety::proto::NumericalPayloadRow.  If this red-lights, "
    "fixy/SessRowExtraction.h is not pulled in by <crucible/Fixy.h>.");

static_assert(std::is_same_v<fsrow::payload_row_t<v062_reach::IoComp>,
                             ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

static_assert(std::is_same_v<fsrow::payload_effect_row_t<v062_reach::IoComp>,
                             ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

// The protocol form has to walk through Send to reach the payload row.
static_assert(std::is_same_v<fsrow::protocol_effect_row_t<v062_reach::SendIo>,
                             ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

namespace fsview = ::crucible::fixy::sess::view;

namespace v063_reach {
struct FakeResource {};
struct Msg {};
using SendProto = ::crucible::safety::proto::Send<Msg, ::crucible::safety::proto::End>;
using SendHandle = ::crucible::safety::proto::SessionHandle<SendProto, FakeResource, void>;
}  // namespace v063_reach

static_assert(std::is_same_v<fsview::AtSend, ::crucible::safety::proto::AtSend>,
              "umbrella reach: fixy::sess::view::AtSend must alias "
              "safety::proto::AtSend.  If this red-lights, "
              "fixy/SessView.h is not pulled in by <crucible/Fixy.h>.");

static_assert(std::is_same_v<fsview::AtTerminal, ::crucible::safety::proto::AtTerminal>);

static_assert(fsview::handle_is_at_v<v063_reach::SendHandle, fsview::AtSend>);

static_assert(fsview::HandleIsAt<v063_reach::SendHandle, fsview::AtSend>);
static_assert(!fsview::HandleIsAt<v063_reach::SendHandle, fsview::AtRecv>);

// The mint is witnessed at the type level only.  A handle parked in the
// Send state aborts in its destructor if it goes out of scope
// unconsumed, so it must not be constructed for real here.
using V063MintedView =
    decltype(fsview::mint_session_view<fsview::AtSend>(std::declval<v063_reach::SendHandle const&>()));
static_assert(std::is_same_v<V063MintedView, ::crucible::safety::ScopedView<v063_reach::SendHandle, fsview::AtSend>>);

static_assert(std::is_same_v<fsview::session_view_message_type_t<V063MintedView>, v063_reach::Msg>);

namespace fscrash = ::crucible::fixy::sess::crash;

namespace v064_reach {
struct Alice {};
struct Bob {};
struct Msg {};
struct Ack {};

namespace proto = ::crucible::safety::proto;

using EndProto = proto::End;
using StopProto = fscrash::Stop;

using AliceCrashOffer = proto::Offer<proto::Recv<Msg, EndProto>, proto::Recv<fscrash::Crash<Alice>, EndProto>>;
using NormalOffer = proto::Offer<proto::Recv<Msg, EndProto>, proto::Recv<Ack, EndProto>>;

using CrashAwareClient = proto::Send<Msg, AliceCrashOffer>;
using CrashOblivClient = proto::Send<Msg, NormalOffer>;
}  // namespace v064_reach

static_assert(std::is_same_v<fscrash::Stop, ::crucible::safety::proto::Stop>,
              "umbrella reach: fixy::sess::crash::Stop must alias "
              "safety::proto::Stop.  If this red-lights, "
              "fixy/SessCrash.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fscrash::CrashClass, ::crucible::safety::proto::CrashClass>);
static_assert(std::is_same_v<fscrash::Stop_g<fscrash::CrashClass::NoThrow>,
                             ::crucible::safety::proto::Stop_g<::crucible::safety::proto::CrashClass::NoThrow>>);

static_assert(std::is_same_v<fscrash::Crash<v064_reach::Alice>, ::crucible::safety::proto::Crash<v064_reach::Alice>>);
static_assert(fscrash::is_crash_v<fscrash::Crash<v064_reach::Alice>>);
static_assert(!fscrash::is_crash_v<v064_reach::Msg>);
static_assert(std::is_same_v<fscrash::UnreliableAll, fscrash::ReliableSet<>>);
static_assert(fscrash::is_reliable_v<fscrash::ReliableSet<v064_reach::Alice>, v064_reach::Alice>);
static_assert(!fscrash::is_reliable_v<fscrash::ReliableSet<v064_reach::Alice>, v064_reach::Bob>);

static_assert(!fscrash::has_crash_branch_for_peer_v<v064_reach::NormalOffer, v064_reach::Alice>);
static_assert(fscrash::has_crash_branch_for_peer_v<v064_reach::AliceCrashOffer, v064_reach::Alice>);

static_assert(fscrash::every_offer_has_crash_branch_for_peer_v<v064_reach::CrashAwareClient, v064_reach::Alice>);
static_assert(!fscrash::every_offer_has_crash_branch_for_peer_v<v064_reach::CrashOblivClient, v064_reach::Alice>);

static_assert(fscrash::CrashAwareForTransport<v064_reach::CrashAwareClient, v064_reach::Alice>,
              "CrashAwareForTransport must admit a well-formed client whose "
              "every Offer<> has a Recv<Crash<Alice>, _> branch.");
static_assert(!fscrash::CrashAwareForTransport<v064_reach::CrashOblivClient, v064_reach::Alice>,
              "CrashAwareForTransport must REJECT a well-formed but crash-"
              "oblivious client — well-formedness alone is insufficient.");

// End and Stop carry no Offer at all, so the walk finds nothing to
// reject and they pass vacuously.
static_assert(fscrash::CrashAwareForTransport<v064_reach::EndProto, v064_reach::Alice>);
static_assert(fscrash::CrashAwareForTransport<v064_reach::StopProto, v064_reach::Alice>);

namespace ffed = ::crucible::fixy::sess::federation;
namespace pfed = ::crucible::safety::proto::federation;

namespace v065_reach {
struct ReachKey {};
}  // namespace v065_reach

static_assert(std::is_same_v<ffed::SenderRole, pfed::SenderRole>,
              "umbrella reach: fixy::sess::federation::SenderRole must alias "
              "safety::proto::federation::SenderRole.  If this red-lights, "
              "fixy/SessFederation.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole, pfed::CoordRole>);
static_assert(std::is_same_v<ffed::AnyFederationKey, pfed::AnyFederationKey>);

static_assert(std::is_same_v<ffed::SenderProto<v065_reach::ReachKey>, pfed::SenderProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::ReceiverProto<v065_reach::ReachKey>, pfed::ReceiverProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::CoordProto<v065_reach::ReachKey>, pfed::CoordProto<v065_reach::ReachKey>>);

static_assert(
    std::is_same_v<ffed::ExpectedSenderProto<v065_reach::ReachKey>, pfed::ExpectedSenderProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::ExpectedReceiverProto<v065_reach::ReachKey>,
                             pfed::ExpectedReceiverProto<v065_reach::ReachKey>>);
static_assert(
    std::is_same_v<ffed::ExpectedCoordProto<v065_reach::ReachKey>, pfed::ExpectedCoordProto<v065_reach::ReachKey>>);

static_assert(std::is_same_v<ffed::FederationProtocol, pfed::FederationProtocol>);
static_assert(std::is_same_v<ffed::FederationProtocolFor<v065_reach::ReachKey>,
                             pfed::FederationProtocolFor<v065_reach::ReachKey>>);

static_assert(std::is_same_v<ffed::Ack<v065_reach::ReachKey>, pfed::Ack<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::PullRequest<v065_reach::ReachKey>, pfed::PullRequest<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::FederationEntryPayload<v065_reach::ReachKey>,
                             pfed::FederationEntryPayload<v065_reach::ReachKey>>);

static_assert(
    ffed::role_protocol_matches_v<pfed::SenderRole, ffed::SenderProto<v065_reach::ReachKey>, v065_reach::ReachKey>,
    "umbrella reach: role_protocol_matches_v admits matching "
    "(role, proto) pair through fixy::sess::federation::.");
static_assert(
    !ffed::role_protocol_matches_v<pfed::SenderRole, ffed::ReceiverProto<v065_reach::ReachKey>, v065_reach::ReachKey>,
    "umbrella reach: role_protocol_matches_v rejects mismatched pair.");

namespace fsess = ::crucible::fixy::sess;
static_assert(std::is_same_v<decltype(fsess::federation_required_row{}), decltype(pfed::federation_required_row{})>,
              "umbrella reach: fixy::sess::federation_required_row must alias "
              "the substrate row.");

// A positive witness would need a fitting context fixture.  Rejecting a
// plain struct is enough to prove the concept both reaches and
// evaluates, which is all this file claims.
struct NonExecCtxProbe {};
static_assert(!fsess::CtxFitsFederation<NonExecCtxProbe>,
              "umbrella reach: fixy::sess::CtxFitsFederation must reach AND "
              "reject a non-IsExecCtx argument (clause 1 of the packaged gate).");

// The shape predicates are reachable under two spellings: the canonical
// sub-namespace, and a compatibility using-declaration one level up.
// Both are asserted, because dropping either one breaks a different set
// of call sites.

namespace fshape = ::crucible::fixy::sess::shape;
namespace fsess_umbrella = ::crucible::fixy::sess;
namespace pproto = ::crucible::safety::proto;

namespace v066_reach {
struct Probe {};
using SendP = pproto::Send<Probe, pproto::End>;
using RecvP = pproto::Recv<Probe, pproto::End>;
using SelectP = pproto::Select<SendP>;
using OfferP = pproto::Offer<RecvP>;
using LoopP = pproto::Loop<pproto::End>;
using EndP = pproto::End;
}  // namespace v066_reach

static_assert(fshape::is_send_v<v066_reach::SendP>,
              "umbrella reach: fixy::sess::shape::is_send_v must admit Send<T, K> "
              "through the umbrella.  If this red-lights, fixy/SessShape.h is not "
              "pulled in by <crucible/Fixy.h> OR the substrate predicate moved.");
static_assert(!fshape::is_send_v<v066_reach::RecvP>);
static_assert(fshape::is_recv_v<v066_reach::RecvP>);
static_assert(!fshape::is_recv_v<v066_reach::SendP>);

static_assert(fshape::is_select_v<v066_reach::SelectP>);
static_assert(!fshape::is_select_v<v066_reach::OfferP>);
static_assert(fshape::is_offer_v<v066_reach::OfferP>);
static_assert(!fshape::is_offer_v<v066_reach::SelectP>);

static_assert(fshape::is_loop_v<v066_reach::LoopP>);
static_assert(!fshape::is_loop_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::EndP>);
static_assert(!fshape::is_head_v<v066_reach::LoopP>, "umbrella reach: is_head_v = !is_loop_v by substrate definition; "
                                                     "Loop must classify as non-head.");

static_assert(fsess_umbrella::is_send_v<v066_reach::SendP>,
              "umbrella reach: fixy::sess::is_send_v must reach through Sess.h's "
              "compatibility using-decl.  If this red-lights, Sess.h's umbrella "
              "using-decls were dropped — restore them or every call site that "
              "uses the shorter spelling breaks.");
static_assert(fsess_umbrella::is_recv_v<v066_reach::RecvP>);
static_assert(fsess_umbrella::is_select_v<v066_reach::SelectP>);
static_assert(fsess_umbrella::is_offer_v<v066_reach::OfferP>);
static_assert(fsess_umbrella::is_loop_v<v066_reach::LoopP>);
static_assert(fsess_umbrella::is_head_v<v066_reach::SendP>);

// These two claims check agreement of the resolved values, not the
// routing.  A compatibility declaration rewritten to reach the substrate
// directly still agrees, because the substrate is the one definition.
static_assert(fshape::is_send_v<v066_reach::SendP> == fsess_umbrella::is_send_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::LoopP> == fsess_umbrella::is_head_v<v066_reach::LoopP>);

// The umbrella reaches the MPST global types through two headers: the
// canonical one and a shim that re-includes it.  Both must land on one
// substrate type.  A shim rewritten to redeclare the types instead of
// re-including them would give a consumer two distinct type families
// depending on which spelling it used, and the claims below catch that.

namespace fmpst = ::crucible::fixy::sess::mpst;
namespace pproto_v068 = ::crucible::safety::proto;

namespace v068_reach {
struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
}  // namespace v068_reach

using EndG_via_fixy = fmpst::End_G;
using VarG_via_fixy = fmpst::Var_G;
using Trans_via_fixy = fmpst::Transmission<v068_reach::Alice, v068_reach::Bob, v068_reach::Ping, fmpst::End_G>;
using Choice_via_fixy =
    fmpst::Choice<v068_reach::Alice, v068_reach::Bob, fmpst::BranchG<v068_reach::Ping, fmpst::End_G>>;
using RecG_via_fixy = fmpst::Rec_G<fmpst::End_G>;
using StopG_via_fixy = fmpst::StopG<v068_reach::Alice>;

static_assert(fmpst::is_end_g_v<EndG_via_fixy>, "umbrella reach: fixy::sess::mpst::is_end_g_v must admit End_G "
                                                "through the umbrella.  If this red-lights, fixy/SessGlobal.h is "
                                                "not pulled in by <crucible/Fixy.h> OR the substrate predicate "
                                                "moved.");
static_assert(fmpst::is_var_g_v<VarG_via_fixy>);
static_assert(fmpst::is_transmission_v<Trans_via_fixy>);
static_assert(fmpst::is_choice_v<Choice_via_fixy>);
static_assert(fmpst::is_rec_g_v<RecG_via_fixy>);
static_assert(fmpst::is_stop_g_v<StopG_via_fixy>);
static_assert(!fmpst::is_end_g_v<VarG_via_fixy>);
static_assert(!fmpst::is_transmission_v<EndG_via_fixy>);

// Identity is checked against the substrate rather than between the two
// fixy spellings, because both spellings route to the same substrate
// namespace and that is the fact worth pinning.
static_assert(std::is_same_v<EndG_via_fixy, pproto_v068::End_G>,
              "umbrella reach: fixy::sess::mpst::End_G must resolve to "
              "safety::proto::End_G regardless of whether the consumer reaches "
              "the surface through fixy/SessGlobal.h (canonical) or fixy/Mpst.h "
              "(shim).  If this red-lights, audit the shim header and the "
              "canonical header for divergent type declarations.");
static_assert(std::is_same_v<VarG_via_fixy, pproto_v068::Var_G>);
static_assert(std::is_same_v<Trans_via_fixy, pproto_v068::Transmission<v068_reach::Alice, v068_reach::Bob,
                                                                       v068_reach::Ping, pproto_v068::End_G>>);

using G_AB_v068 = fmpst::Transmission<v068_reach::Alice, v068_reach::Bob, v068_reach::Ping, fmpst::End_G>;
using L_Alice_v068 = fmpst::project_t<G_AB_v068, v068_reach::Alice>;
using L_Bob_v068 = fmpst::project_t<G_AB_v068, v068_reach::Bob>;
using L_Carol_v068 = fmpst::project_t<G_AB_v068, v068_reach::Carol>;
static_assert(std::is_same_v<L_Alice_v068, pproto_v068::Send<v068_reach::Ping, pproto_v068::End>>,
              "umbrella reach: project_t<G_AB, Alice> must yield Send<Ping, End> "
              "via the umbrella.  If this red-lights, the projection's substrate "
              "wiring is severed at the fixy layer.");
static_assert(std::is_same_v<L_Bob_v068, pproto_v068::Recv<v068_reach::Ping, pproto_v068::End>>);
static_assert(std::is_same_v<L_Carol_v068, pproto_v068::End>);

using RL_AB_v068 = fmpst::RoleList<v068_reach::Alice, v068_reach::Bob>;
static_assert(std::is_same_v<fmpst::insert_unique_t<v068_reach::Carol, RL_AB_v068>,
                             fmpst::RoleList<v068_reach::Carol, v068_reach::Alice, v068_reach::Bob>>,
              "umbrella reach: insert_unique_t must reach + behave identically "
              "through the fixy umbrella.");
static_assert(std::is_same_v<fmpst::EmptyRoleList, fmpst::RoleList<>>);

// This predicate is reachable at three levels: the fixy namespace, the
// substrate public namespace, and the substrate detail namespace where
// it is defined.  The claims below check all three agree, which is what
// proves the two outer names are pass-throughs and not forks.

using G_AB_Ping_v169 = fmpst::Transmission<v068_reach::Alice, v068_reach::Bob, v068_reach::Ping, fmpst::End_G>;

static_assert(fmpst::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>,
              "umbrella reach: fixy::sess::mpst::has_interaction_between_v "
              "must admit the (Alice, Bob) pair on Transmission<Alice, Bob, "
              "Ping, End> through the umbrella.  If this red-lights, the "
              "using-decl in SessGlobal.h was dropped or rewritten.");
static_assert(!fmpst::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Carol>,
              "umbrella reach: fixy::sess::mpst::has_interaction_between_v "
              "must reject (Alice, Carol) on Transmission<Alice, Bob, Ping, "
              "End> through the umbrella — Carol does not participate.");
// The query is over an unordered pair, so swapping the roles must not
// change the verdict.
static_assert(fmpst::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Bob, v068_reach::Alice>,
              "umbrella reach: predicate must be symmetric over (A, B) — "
              "(Bob, Alice) must admit the same protocol that (Alice, Bob) "
              "admits.");
static_assert(fmpst::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>
              == pproto_v068::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>);
static_assert(
    pproto_v068::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>
        == pproto_v068::detail::global::has_interaction_between_v<G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>,
    "cross-binding: substrate-public alias must forward verbatim to "
    "detail::global:: — if this red-lights, the alias introduced a "
    "fresh predicate rather than a pass-through.");
// End and Var_G terminate the walk, so no role pair interacts.
static_assert(!fmpst::has_interaction_between_v<fmpst::End_G, v068_reach::Alice, v068_reach::Bob>,
              "umbrella reach: End_G admits no interactions through the "
              "umbrella for any role pair.");
static_assert(!fmpst::has_interaction_between_v<fmpst::Var_G, v068_reach::Alice, v068_reach::Bob>,
              "umbrella reach: Var_G admits no interactions through the "
              "umbrella for any role pair.");

// Each of the five context-fit concepts below is claimed three times:
// once against the substrate to fix the expected verdict, once through
// the fixy spelling, and once as an equality of the two.  The equality
// is the part that catches a using-declaration quietly rewritten into a
// fresh concept, which would otherwise still compile and still be true.

namespace v168_reach {
struct PayloadProbe {};
using EndP = pproto::End;
using SendP = pproto::Send<PayloadProbe, EndP>;
// An Offer with no branches is the canonical unrunnable protocol: there
// is nothing to select, so admitting it would be vacuous truth.
using EmptyOfferP = pproto::Offer<>;
struct NonExecCtxProbe {};
// This context carries Row<Test, Alloc, IO, Block>, a superrow of the
// empty row that End engages, so it fits.
using FittingCtx = ::crucible::effects::TestRunnerCtx;
}  // namespace v168_reach

namespace fsess_v168 = ::crucible::fixy::sess;
namespace pproto_v168 = ::crucible::safety::proto;

static_assert(pproto_v168::ProtocolVendorAdmittedByLoopCtx<v168_reach::EndP, void>,
              "baseline: substrate ProtocolVendorAdmittedByLoopCtx "
              "must admit End under the no-LoopCtx (void) sentinel.");
static_assert(fsess_v168::ProtocolVendorAdmittedByLoopCtx<v168_reach::EndP, void>,
              "umbrella reach: fixy::sess::ProtocolVendorAdmittedByLoopCtx "
              "must admit the same baseline through the umbrella.  If this "
              "red-lights, the using-decl in fixy/Sess.h was dropped.");
static_assert(fsess_v168::ProtocolVendorAdmittedByLoopCtx<v168_reach::EndP, void>
                  == pproto_v168::ProtocolVendorAdmittedByLoopCtx<v168_reach::EndP, void>,
              "cross-binding: umbrella concept value must equal "
              "substrate concept value on identical arguments.");

static_assert(pproto_v168::ProtocolEpochAdmittedByLoopCtx<v168_reach::EndP, void>);
static_assert(fsess_v168::ProtocolEpochAdmittedByLoopCtx<v168_reach::EndP, void>,
              "umbrella reach: fixy::sess::ProtocolEpochAdmittedByLoopCtx "
              "must admit End under the no-LoopCtx (void) sentinel through "
              "the umbrella.");
static_assert(fsess_v168::ProtocolEpochAdmittedByLoopCtx<v168_reach::EndP, void>
              == pproto_v168::ProtocolEpochAdmittedByLoopCtx<v168_reach::EndP, void>);

static_assert(pproto_v168::ProtocolPermissionedRunnable<v168_reach::EndP>,
              "baseline: substrate ProtocolPermissionedRunnable "
              "must admit End.");
static_assert(pproto_v168::ProtocolPermissionedRunnable<v168_reach::SendP>,
              "baseline: substrate ProtocolPermissionedRunnable "
              "must admit Send<Probe, End> via tail recursion.");
static_assert(!pproto_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>,
              "baseline: substrate ProtocolPermissionedRunnable "
              "must reject empty Offer<> — an empty branch pack is structurally "
              "not runnable, and admitting it would let an unrunnable protocol "
              "through the channel mint on vacuous truth.");
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::EndP>,
              "umbrella reach: fixy::sess::ProtocolPermissionedRunnable "
              "must admit End through the umbrella.");
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::SendP>,
              "umbrella reach: fixy::sess::ProtocolPermissionedRunnable "
              "must admit Send<Probe, End> through the umbrella.");
static_assert(!fsess_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>,
              "umbrella reach: fixy::sess::ProtocolPermissionedRunnable "
              "must reject empty Offer<> through the umbrella.");
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::EndP>
              == pproto_v168::ProtocolPermissionedRunnable<v168_reach::EndP>);
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>
              == pproto_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>);

static_assert(
    pproto_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::FittingCtx, fsess_v168::EmptyPermSet>,
    "baseline: substrate CtxFitsPermissionedProtocol "
    "must admit End under TestRunnerCtx + EmptyPermSet — the row "
    "engaged by End is Row<> ⊆ Row<Test, Alloc, IO, Block>, and the "
    "empty PermSet trivially closes.");
static_assert(
    !pproto_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::NonExecCtxProbe, fsess_v168::EmptyPermSet>,
    "baseline: substrate CtxFitsPermissionedProtocol "
    "must reject a non-IsExecCtx Ctx argument (CtxFitsProtocol's "
    "IsExecCtx clause must fire first, before any row check).");
static_assert(
    fsess_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::FittingCtx, fsess_v168::EmptyPermSet>,
    "umbrella reach: fixy::sess::CtxFitsPermissionedProtocol "
    "must admit the same fitting (End, TestRunnerCtx, EmptyPermSet) "
    "triple through the umbrella.");
static_assert(
    !fsess_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::NonExecCtxProbe, fsess_v168::EmptyPermSet>,
    "umbrella reach: fixy::sess::CtxFitsPermissionedProtocol "
    "must reject the same non-IsExecCtx Ctx through the umbrella.");
static_assert(
    fsess_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::FittingCtx, fsess_v168::EmptyPermSet>
        == pproto_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::FittingCtx, fsess_v168::EmptyPermSet>,
    "cross-binding: umbrella + substrate must agree on "
    "the fitting cell.");
static_assert(
    fsess_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::NonExecCtxProbe, fsess_v168::EmptyPermSet>
    == pproto_v168::CtxFitsPermissionedProtocol<v168_reach::EndP, v168_reach::NonExecCtxProbe,
                                                fsess_v168::EmptyPermSet>);

// End is its own dual, so one protocol can sit at both endpoints of the
// channel and the pair still type-checks.
static_assert(pproto_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::FittingCtx, v168_reach::FittingCtx>,
              "baseline: substrate CtxFitsChannel must admit "
              "(End, TestRunnerCtx, TestRunnerCtx) — End is self-dual, "
              "EmptyPermSet closes on both endpoints.");
static_assert(!pproto_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::NonExecCtxProbe, v168_reach::FittingCtx>,
              "baseline: substrate CtxFitsChannel must reject "
              "when either endpoint's Ctx is not an IsExecCtx (the per-endpoint "
              "CtxFitsPermissionedProtocol gate fires).");
static_assert(fsess_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::FittingCtx, v168_reach::FittingCtx>,
              "umbrella reach: fixy::sess::CtxFitsChannel must admit the "
              "same baseline through the umbrella.");
static_assert(!fsess_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::NonExecCtxProbe, v168_reach::FittingCtx>,
              "umbrella reach: fixy::sess::CtxFitsChannel must reject the "
              "same non-IsExecCtx endpoint through the umbrella.");
static_assert(fsess_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::FittingCtx, v168_reach::FittingCtx>
              == pproto_v168::CtxFitsChannel<v168_reach::EndP, v168_reach::FittingCtx, v168_reach::FittingCtx>);

// ── fixy::is:: alias reach, one claim per symbol ──────────────────────
//
// fixy/Is.h re-exports the 96 public `_v` traits and `_t` slot
// extractors of crucible::safety::extract through using-declarations.
// Nothing here instantiates them: naming both spellings of one symbol
// in a single scope is legal only when the two name the SAME entity,
// so each pair below is a two-line proof that the re-export exists and
// resolves where it claims to.  The failure modes are distinct and both
// are compile errors:
//
//   * fixy/Is.h drops a row        -> "X has not been declared in
//                                      crucible::fixy::is"
//   * fixy::is::X names something
//     other than extract::X        -> "conflicts with a previous
//                                      declaration"
//
// The claims need no witness type, which is why every symbol can be
// covered rather than a sampled few — a constrained alias template such
// as `bits_enum_t` has no argument that is valid for every symbol, and
// a sampled gate is how a surface silently loses rows.
//
// Concepts are deliberately absent.  fixy/Is.h cannot carry a concept
// across by using-declaration, so it re-declares each one as a fresh
// concept that delegates to the substrate concept.  A fresh concept is
// by construction a different entity, so the pair claim does not apply;
// a missing concept surfaces as an ordinary compile error at the
// `fixy::is::IsX` use sites elsewhere in this TU and in production.
//
// This gate proves fidelity, not completeness: it says every symbol
// named here resolves correctly, not that the list covers the whole
// substrate surface.  Completeness is scripts/check-isx-parity.sh,
// which derives the demand set from the headers, so a newly added
// substrate alias is demanded without anyone editing this file.
namespace isx_alias_reach {
using ::crucible::fixy::is::alloc_class_value_t;
using ::crucible::safety::extract::alloc_class_value_t;
using ::crucible::fixy::is::barrier_guarded_value_t;
using ::crucible::safety::extract::barrier_guarded_value_t;
using ::crucible::fixy::is::bits_enum_t;
using ::crucible::safety::extract::bits_enum_t;
using ::crucible::fixy::is::bits_underlying_t;
using ::crucible::safety::extract::bits_underlying_t;
using ::crucible::fixy::is::borrowed_ref_value_t;
using ::crucible::safety::extract::borrowed_ref_value_t;
using ::crucible::fixy::is::borrowed_source_t;
using ::crucible::safety::extract::borrowed_source_t;
using ::crucible::fixy::is::borrowed_value_t;
using ::crucible::safety::extract::borrowed_value_t;
using ::crucible::fixy::is::budgeted_value_t;
using ::crucible::safety::extract::budgeted_value_t;
using ::crucible::fixy::is::cipher_tier_value_t;
using ::crucible::safety::extract::cipher_tier_value_t;
using ::crucible::fixy::is::clock_source_value_t;
using ::crucible::safety::extract::clock_source_value_t;
using ::crucible::fixy::is::consistency_value_t;
using ::crucible::safety::extract::consistency_value_t;
using ::crucible::fixy::is::consumer_handle_value_t;
using ::crucible::safety::extract::consumer_handle_value_t;
using ::crucible::fixy::is::cpu_pinned_value_t;
using ::crucible::safety::extract::cpu_pinned_value_t;
using ::crucible::fixy::is::crash_value_t;
using ::crucible::safety::extract::crash_value_t;
using ::crucible::fixy::is::det_safe_value_t;
using ::crucible::safety::extract::det_safe_value_t;
using ::crucible::fixy::is::epoch_versioned_value_t;
using ::crucible::safety::extract::epoch_versioned_value_t;
using ::crucible::fixy::is::hot_path_value_t;
using ::crucible::safety::extract::hot_path_value_t;
using ::crucible::fixy::is::hw_value_t;
using ::crucible::safety::extract::hw_value_t;
using ::crucible::fixy::is::is_alloc_class_v;
using ::crucible::safety::extract::is_alloc_class_v;
using ::crucible::fixy::is::is_barrier_guarded_v;
using ::crucible::safety::extract::is_barrier_guarded_v;
using ::crucible::fixy::is::is_bits_v;
using ::crucible::safety::extract::is_bits_v;
using ::crucible::fixy::is::is_borrowed_ref_v;
using ::crucible::safety::extract::is_borrowed_ref_v;
using ::crucible::fixy::is::is_borrowed_v;
using ::crucible::safety::extract::is_borrowed_v;
using ::crucible::fixy::is::is_budgeted_v;
using ::crucible::safety::extract::is_budgeted_v;
using ::crucible::fixy::is::is_cipher_tier_v;
using ::crucible::safety::extract::is_cipher_tier_v;
using ::crucible::fixy::is::is_clock_source_v;
using ::crucible::safety::extract::is_clock_source_v;
using ::crucible::fixy::is::is_consistency_v;
using ::crucible::safety::extract::is_consistency_v;
using ::crucible::fixy::is::is_consumer_handle_v;
using ::crucible::safety::extract::is_consumer_handle_v;
using ::crucible::fixy::is::is_cpu_pinned_v;
using ::crucible::safety::extract::is_cpu_pinned_v;
using ::crucible::fixy::is::is_crash_v;
using ::crucible::safety::extract::is_crash_v;
using ::crucible::fixy::is::is_det_safe_v;
using ::crucible::safety::extract::is_det_safe_v;
using ::crucible::fixy::is::is_epoch_versioned_v;
using ::crucible::safety::extract::is_epoch_versioned_v;
using ::crucible::fixy::is::is_hot_path_v;
using ::crucible::safety::extract::is_hot_path_v;
using ::crucible::fixy::is::is_hw_v;
using ::crucible::safety::extract::is_hw_v;
using ::crucible::fixy::is::is_join_policy_v;
using ::crucible::safety::extract::is_join_policy_v;
using ::crucible::fixy::is::is_linear_v;
using ::crucible::safety::extract::is_linear_v;
using ::crucible::fixy::is::is_mem_order_v;
using ::crucible::safety::extract::is_mem_order_v;
using ::crucible::fixy::is::is_numa_placement_v;
using ::crucible::safety::extract::is_numa_placement_v;
using ::crucible::fixy::is::is_numerical_tier_v;
using ::crucible::safety::extract::is_numerical_tier_v;
using ::crucible::fixy::is::is_opaque_lifetime_v;
using ::crucible::safety::extract::is_opaque_lifetime_v;
using ::crucible::fixy::is::is_owned_mmap_v;
using ::crucible::safety::extract::is_owned_mmap_v;
using ::crucible::fixy::is::is_owned_region_v;
using ::crucible::safety::extract::is_owned_region_v;
using ::crucible::fixy::is::is_permission_v;
using ::crucible::safety::extract::is_permission_v;
using ::crucible::fixy::is::is_producer_handle_v;
using ::crucible::safety::extract::is_producer_handle_v;
using ::crucible::fixy::is::is_progress_v;
using ::crucible::safety::extract::is_progress_v;
using ::crucible::fixy::is::is_recipe_spec_v;
using ::crucible::safety::extract::is_recipe_spec_v;
using ::crucible::fixy::is::is_reduce_into_v;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::fixy::is::is_refined_v;
using ::crucible::safety::extract::is_refined_v;
using ::crucible::fixy::is::is_residency_heat_v;
using ::crucible::safety::extract::is_residency_heat_v;
using ::crucible::fixy::is::is_sched_class_v;
using ::crucible::safety::extract::is_sched_class_v;
using ::crucible::fixy::is::is_scoped_fence_v;
using ::crucible::safety::extract::is_scoped_fence_v;
using ::crucible::fixy::is::is_secret_v;
using ::crucible::safety::extract::is_secret_v;
using ::crucible::fixy::is::is_session_handle_v;
using ::crucible::safety::extract::is_session_handle_v;
using ::crucible::fixy::is::is_shared_permission_v;
using ::crucible::safety::extract::is_shared_permission_v;
using ::crucible::fixy::is::is_simd_width_pinned_v;
using ::crucible::safety::extract::is_simd_width_pinned_v;
using ::crucible::fixy::is::is_stale_v;
using ::crucible::safety::extract::is_stale_v;
using ::crucible::fixy::is::is_suspend_behavior_v;
using ::crucible::safety::extract::is_suspend_behavior_v;
using ::crucible::fixy::is::is_swmr_reader_v;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::fixy::is::is_swmr_writer_v;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::fixy::is::is_tagged_v;
using ::crucible::safety::extract::is_tagged_v;
using ::crucible::fixy::is::is_vendor_v;
using ::crucible::safety::extract::is_vendor_v;
using ::crucible::fixy::is::is_wait_v;
using ::crucible::safety::extract::is_wait_v;
using ::crucible::fixy::is::join_policy_value_t;
using ::crucible::safety::extract::join_policy_value_t;
using ::crucible::fixy::is::linear_value_t;
using ::crucible::safety::extract::linear_value_t;
using ::crucible::fixy::is::mem_order_value_t;
using ::crucible::safety::extract::mem_order_value_t;
using ::crucible::fixy::is::numa_placement_value_t;
using ::crucible::safety::extract::numa_placement_value_t;
using ::crucible::fixy::is::numerical_tier_value_t;
using ::crucible::safety::extract::numerical_tier_value_t;
using ::crucible::fixy::is::opaque_lifetime_value_t;
using ::crucible::safety::extract::opaque_lifetime_value_t;
using ::crucible::fixy::is::owned_region_tag_t;
using ::crucible::safety::extract::owned_region_tag_t;
using ::crucible::fixy::is::owned_region_value_t;
using ::crucible::safety::extract::owned_region_value_t;
using ::crucible::fixy::is::permission_tag_t;
using ::crucible::safety::extract::permission_tag_t;
using ::crucible::fixy::is::producer_handle_value_t;
using ::crucible::safety::extract::producer_handle_value_t;
using ::crucible::fixy::is::progress_value_t;
using ::crucible::safety::extract::progress_value_t;
using ::crucible::fixy::is::recipe_spec_value_t;
using ::crucible::safety::extract::recipe_spec_value_t;
using ::crucible::fixy::is::reduce_into_accumulator_t;
using ::crucible::safety::extract::reduce_into_accumulator_t;
using ::crucible::fixy::is::reduce_into_reducer_t;
using ::crucible::safety::extract::reduce_into_reducer_t;
using ::crucible::fixy::is::refined_is_sealed_v;
using ::crucible::safety::extract::refined_is_sealed_v;
using ::crucible::fixy::is::refined_predicate_type_t;
using ::crucible::safety::extract::refined_predicate_type_t;
using ::crucible::fixy::is::refined_value_t;
using ::crucible::safety::extract::refined_value_t;
using ::crucible::fixy::is::residency_heat_value_t;
using ::crucible::safety::extract::residency_heat_value_t;
using ::crucible::fixy::is::sched_class_value_t;
using ::crucible::safety::extract::sched_class_value_t;
using ::crucible::fixy::is::scoped_fence_value_t;
using ::crucible::safety::extract::scoped_fence_value_t;
using ::crucible::fixy::is::secret_value_t;
using ::crucible::safety::extract::secret_value_t;
using ::crucible::fixy::is::session_handle_proto_t;
using ::crucible::safety::extract::session_handle_proto_t;
using ::crucible::fixy::is::shared_permission_tag_t;
using ::crucible::safety::extract::shared_permission_tag_t;
using ::crucible::fixy::is::simd_width_pinned_value_t;
using ::crucible::safety::extract::simd_width_pinned_value_t;
using ::crucible::fixy::is::stale_semiring_t;
using ::crucible::safety::extract::stale_semiring_t;
using ::crucible::fixy::is::stale_staleness_t;
using ::crucible::safety::extract::stale_staleness_t;
using ::crucible::fixy::is::stale_value_t;
using ::crucible::safety::extract::stale_value_t;
using ::crucible::fixy::is::suspend_behavior_value_t;
using ::crucible::safety::extract::suspend_behavior_value_t;
using ::crucible::fixy::is::swmr_reader_value_t;
using ::crucible::safety::extract::swmr_reader_value_t;
using ::crucible::fixy::is::swmr_writer_value_t;
using ::crucible::safety::extract::swmr_writer_value_t;
using ::crucible::fixy::is::tagged_tag_t;
using ::crucible::safety::extract::tagged_tag_t;
using ::crucible::fixy::is::tagged_value_t;
using ::crucible::safety::extract::tagged_value_t;
using ::crucible::fixy::is::vendor_value_t;
using ::crucible::safety::extract::vendor_value_t;
using ::crucible::fixy::is::wait_value_t;
using ::crucible::safety::extract::wait_value_t;
}  // namespace isx_alias_reach

// Every claim above is settled at compile time.  main() exists only so
// that the runner can link the TU as a stand-alone executable.
int main() { return 0; }
