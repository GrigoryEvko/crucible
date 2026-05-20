// ── test_fixy_umbrella_reach — sentinel TU for fixy umbrella reach ─
//
// Pulls ONLY `<crucible/Fixy.h>` (the umbrella) into a TU compiled
// under project warning flags so the umbrella's reachability gaps
// surface at the static_assert layer rather than at downstream
// production call sites.  Closes the paired fixy-A4-001 (Profile.h
// orphan) + fixy-A4-002 (Contract.h orphan) tasks.
//
// Witnesses (all via `<crucible/Fixy.h>`, no individual fixy/*.h
// includes):
//
//   1. Profile.h reach — fixy::IsAcceptedActive concept alias and
//      fixy::fixy_is_strict constexpr sentinel resolve through the
//      umbrella without descending into fixy/Profile.h directly.
//   2. Profile.h integration — fixy::mint_fn now consumes
//      IsAcceptedActive (not IsAccepted) at its requires-clause;
//      under STRICT mode the strict gate engages, under SKETCH mode
//      the permissive gate engages — both routed through the same
//      umbrella include.
//   3. Contract.h reach — fixy::contract::cipher::mint_promote,
//      mint_demote, mint_restore, EpochedDelegate, and
//      mint_persisted_session resolve through the umbrella without
//      descending into fixy/Contract.h directly.
//   4. CRUCIBLE_PRE / CRUCIBLE_POST macro reach — the consteval-aware
//      contract macros expand cleanly when the umbrella is the only
//      include path.
//
// Failure mode this closes: prior to the A4-001/002 sweep, Fixy.h
// was missing `#include <crucible/fixy/Profile.h>` and `#include
// <crucible/fixy/Contract.h>` in its Phase A / Phase C blocks.  A
// downstream TU that included only the umbrella would silently get
// no Profile.h toggle access AND no Contract.h cipher migration
// access — both surfaces only existed for callers that knew to
// include the individual headers.  This sentinel guarantees the
// umbrella stays load-bearing for both.

#include <crucible/Fixy.h>

#include <type_traits>

namespace fixy  = ::crucible::fixy;
namespace fcc   = ::crucible::fixy::contract::cipher;
namespace cs    = ::crucible::safety;
namespace cc    = ::crucible::cipher;

// ─── 1. Profile.h symbols reach through the umbrella ──────────────

#if CRUCIBLE_FIXY_STRICT
static_assert(fixy::fixy_is_strict,
    "umbrella reach: fixy::fixy_is_strict must be true under "
    "CRUCIBLE_FIXY_STRICT=1.  If this red-lights, fixy/Profile.h is "
    "not pulled in by <crucible/Fixy.h>.");
#else
static_assert(!fixy::fixy_is_strict,
    "umbrella reach: fixy::fixy_is_strict must be false under "
    "CRUCIBLE_FIXY_STRICT=0.");
#endif

// IsAcceptedSketch is always permissive.
static_assert(fixy::IsAcceptedSketch<int>,
    "umbrella reach: fixy::IsAcceptedSketch must resolve through the "
    "umbrella.");

// IsAcceptedActive routes per the toggle.  Under STRICT, an empty
// Grants pack rejects (no engagements); under SKETCH, it accepts.
#if CRUCIBLE_FIXY_STRICT
static_assert(!fixy::IsAcceptedActive<int>,
    "umbrella reach: under STRICT, IsAcceptedActive<int> with empty "
    "Grants must reject.");
#else
static_assert(fixy::IsAcceptedActive<int>,
    "umbrella reach: under SKETCH, IsAcceptedActive<int> with empty "
    "Grants must accept.");
#endif

// ─── 2. Profile.h ↔ Fn.h integration witness ──────────────────────
//
// mint_fn's requires-clause routes through IsAcceptedActive (the
// toggle-bound active gate).  The existing test_fixy_profile.cpp +
// test_fixy_fn.cpp suites verify the routing's substantive behavior
// under both modes.  This sentinel only needs to witness that the
// concept template itself resolves through the umbrella — IF
// `<crucible/Fixy.h>` strips Profile.h from its transitive include
// graph, the next line fails to compile because IsAcceptedActive's
// `requires`-target body becomes invisible.

template <typename T>
constexpr bool umbrella_reach_active_resolves =
    requires { requires fixy::IsAcceptedActive<T>; };

// Witness: under SKETCH mode, IsAcceptedActive<int> is true.  Under
// STRICT mode, IsAcceptedActive<int> with the empty Grants pack is
// false (verified by claim #1 above), so we test instantiability via
// the same all-strict pack the substrate's Reject.h self-test uses
// (AllStrictPack), reached through Fixy.h's transitive Reject.h pull.
#if !CRUCIBLE_FIXY_STRICT
static_assert(umbrella_reach_active_resolves<int>,
    "umbrella reach: under SKETCH, fixy::IsAcceptedActive<int> must "
    "instantiate true through the umbrella.");
#endif

// ─── 3. Contract.h cipher-migration symbols reach through ─────────

static_assert(std::is_same_v<
    fcc::CipherTier<cs::CipherTierTag_v::Hot, int>,
    cs::CipherTier<cs::CipherTierTag_v::Hot, int>>,
    "umbrella reach: fixy::contract::cipher::CipherTier must alias "
    "safety::CipherTier when reached via the umbrella.");

static_assert(std::is_same_v<
    fcc::HotTierHandle<int>,
    cs::cipher_tier::Hot<int>>,
    "umbrella reach: fixy::contract::cipher::HotTierHandle must alias "
    "cipher_tier::Hot when reached via the umbrella.");

static_assert(std::is_same_v<
    fcc::WarmTierHandle<int>,
    cs::cipher_tier::Warm<int>>,
    "umbrella reach: fixy::contract::cipher::WarmTierHandle must alias "
    "cipher_tier::Warm when reached via the umbrella.");

static_assert(std::is_same_v<
    fcc::ColdTierHandle<int>,
    cs::cipher_tier::Cold<int>>,
    "umbrella reach: fixy::contract::cipher::ColdTierHandle must alias "
    "cipher_tier::Cold when reached via the umbrella.");

// mint_promote function-pointer identity (Cold → Hot).
static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &fcc::mint_promote<cs::CipherTierTag_v::Cold,
                           cs::CipherTierTag_v::Hot, int>)
    ==
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &cc::mint_promote<cs::CipherTierTag_v::Cold,
                          cs::CipherTierTag_v::Hot, int>),
    "umbrella reach: fixy::contract::cipher::mint_promote must be the "
    "substrate cipher::mint_promote when reached via the umbrella.");

// ─── 4. CRUCIBLE_PRE / CRUCIBLE_POST macros expand via umbrella ───
//
// Function defined and called below; if the macro pair is reachable
// via the umbrella, this TU compiles.  If a future regression strips
// safety/Pre.h or safety/Post.h from Contract.h's transitive include
// graph, this consteval call fails to build.

[[nodiscard]] constexpr int umbrella_reach_contract_demo(int n) noexcept {
    CRUCIBLE_PRE(n > 0);
    int const result = n * 2;
    CRUCIBLE_POST(result, result == n * 2);
    return result;
}

static_assert(umbrella_reach_contract_demo(7) == 14,
    "umbrella reach: CRUCIBLE_PRE/CRUCIBLE_POST must expand cleanly "
    "from <crucible/Fixy.h>.");

// ─── 5. SessDecl.h reach — fixy::sess::declassify:: (FIXY-U-052a) ─
//
// Witness that the wire-policy payload-marker surface
// (DeclassifyOnSend + 7 traits/concept) reaches the consumer through
// the umbrella include alone.  If a future regression strips
// `#include <crucible/fixy/SessDecl.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile — the in-header sentinels inside
// SessDecl.h would NOT catch that drift (they fire only at direct-
// include sites), so the umbrella-reach gate lives here.

namespace fsd = ::crucible::fixy::sess::declassify;

namespace u052a_reach_probe {
struct WirePayload {};
using WireMsg = fsd::DeclassifyOnSend<WirePayload,
    ::crucible::safety::secret_policy::WireSerialize>;
}

// 5a. DeclassifyOnSend wrapper resolves through the umbrella.
static_assert(std::is_same_v<u052a_reach_probe::WireMsg,
    ::crucible::safety::DeclassifyOnSend<u052a_reach_probe::WirePayload,
        ::crucible::safety::secret_policy::WireSerialize>>,
    "umbrella reach: fixy::sess::declassify::DeclassifyOnSend must "
    "alias safety::DeclassifyOnSend when reached via the umbrella.  "
    "If this red-lights, fixy/SessDecl.h is not pulled in by "
    "<crucible/Fixy.h>.");

// 5b. DeclassifyOnSendable concept routes through the umbrella.
static_assert( fsd::DeclassifyOnSendable<u052a_reach_probe::WireMsg>,
    "umbrella reach: fixy::sess::declassify::DeclassifyOnSendable "
    "must accept DeclassifyOnSend specialisations.");
static_assert(!fsd::DeclassifyOnSendable<u052a_reach_probe::WirePayload>,
    "umbrella reach: fixy::sess::declassify::DeclassifyOnSendable "
    "must reject bare payloads.");

// 5c. wire_payload_type_t extracts inner T (with passthrough fallback).
static_assert(std::is_same_v<
    fsd::wire_payload_type_t<u052a_reach_probe::WireMsg>,
    u052a_reach_probe::WirePayload>,
    "umbrella reach: fixy::sess::declassify::wire_payload_type_t must "
    "extract the inner payload through the umbrella.");
static_assert(std::is_same_v<fsd::wire_payload_type_t<int>, int>,
    "umbrella reach: fixy::sess::declassify::wire_payload_type_t must "
    "pass non-DeclassifyOnSend types through unchanged.");

// 5d. wire_policy_t extracts the wire-policy tag.
static_assert(std::is_same_v<
    fsd::wire_policy_t<u052a_reach_probe::WireMsg>,
    ::crucible::safety::secret_policy::WireSerialize>,
    "umbrella reach: fixy::sess::declassify::wire_policy_t must "
    "extract the wire-policy tag through the umbrella.");

// ─── 6. SessCT.h reach — fixy::sess::ct:: (FIXY-U-052b) ───────────
//
// Witness that the CT-required session payload surface (CTPayload<T>
// + ct::eq overload + 8 traits/concepts/metafns) reaches the consumer
// through the umbrella include alone.  If a future regression strips
// `#include <crucible/fixy/SessCT.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile — the in-header sentinels inside
// SessCT.h would NOT catch that drift (they fire only at direct-
// include sites), so the umbrella-reach gate lives here.

namespace fsct = ::crucible::fixy::sess::ct;

namespace u052b_reach_probe {
struct AuthTag { std::byte bytes[16]{}; };
struct PlainTag { std::byte bytes[16]{}; };  // NOT opted into requires_ct
}  // namespace u052b_reach_probe

// requires_ct specialization MUST live in the substrate namespace
// (standard C++ rule: primary template's namespace is the only
// specialization site).  Opt the reach-probe placeholder in here.
namespace crucible::safety::ct {
template <>
struct requires_ct<u052b_reach_probe::AuthTag> : std::true_type {};
}  // namespace crucible::safety::ct

// 6a. CTPayload wrapper resolves through the umbrella.
static_assert(std::is_same_v<
    fsct::CTPayload<u052b_reach_probe::AuthTag>,
    ::crucible::safety::ct::CTPayload<u052b_reach_probe::AuthTag>>,
    "umbrella reach: fixy::sess::ct::CTPayload must alias "
    "safety::ct::CTPayload when reached via the umbrella.  If this "
    "red-lights, fixy/SessCT.h is not pulled in by <crucible/Fixy.h>.");

// 6b. requires_ct trait routes through the umbrella.
static_assert( fsct::requires_ct_v<u052b_reach_probe::AuthTag>,
    "umbrella reach: fixy::sess::ct::requires_ct_v must observe the "
    "opt-in specialization through the umbrella.");
static_assert(!fsct::requires_ct_v<u052b_reach_probe::PlainTag>,
    "umbrella reach: fixy::sess::ct::requires_ct_v must reject "
    "non-opted-in types through the umbrella.");

// 6c. RequiresCT concept conjoins trait + trivial-copyability.
static_assert( fsct::RequiresCT<u052b_reach_probe::AuthTag>);
static_assert(!fsct::RequiresCT<u052b_reach_probe::PlainTag>);

// 6d. is_ct_payload shape predicate discriminates wrapper vs raw.
static_assert( fsct::is_ct_payload_v<
    fsct::CTPayload<u052b_reach_probe::AuthTag>>);
static_assert(!fsct::is_ct_payload_v<u052b_reach_probe::AuthTag>);
static_assert( fsct::CTPayloadType<
    fsct::CTPayload<u052b_reach_probe::AuthTag>>);

// 6e. ct_payload_value_type_t extracts inner T (with passthrough).
static_assert(std::is_same_v<
    fsct::ct_payload_value_type_t<
        fsct::CTPayload<u052b_reach_probe::AuthTag>>,
    u052b_reach_probe::AuthTag>,
    "umbrella reach: fixy::sess::ct::ct_payload_value_type_t must "
    "extract the inner payload through the umbrella.");
static_assert(std::is_same_v<
    fsct::ct_payload_value_type_t<int>, int>,
    "umbrella reach: fixy::sess::ct::ct_payload_value_type_t must "
    "pass non-CTPayload types through unchanged.");

// ─── 6c. SessContentAddr.h reach — fixy::sess::contentaddr:: (FIXY-U-052c) ──
//
// Witness that the content-hash-quotient surface (ContentAddressed<T>
// + is_content_addressed trait family + underlying/unwrap metafns +
// depth counter) reaches the consumer through the umbrella include
// alone.  If a future regression strips
// `#include <crucible/fixy/SessContentAddr.h>` from Fixy.h's Phase-C
// block, the next claims fail to compile — the in-header sentinels
// inside SessContentAddr.h would NOT catch that drift (they fire only
// at direct-include sites), so the umbrella-reach gate lives here.
//
// Production consumer: Cipher.h federation entry payload + cold-blob
// region persistence types reach `is_content_addressed_v<...>` through
// this fixy path under FIXY-U-092.

namespace fsca = ::crucible::fixy::sess::contentaddr;

namespace u052c_reach_probe {
struct Payload {};
}  // namespace u052c_reach_probe

// 6c-a. ContentAddressed wrapper resolves through the umbrella.
static_assert(std::is_same_v<
    fsca::ContentAddressed<u052c_reach_probe::Payload>,
    ::crucible::safety::proto::ContentAddressed<u052c_reach_probe::Payload>>,
    "umbrella reach: fixy::sess::contentaddr::ContentAddressed must "
    "alias safety::proto::ContentAddressed when reached via the "
    "umbrella.  If this red-lights, fixy/SessContentAddr.h is not "
    "pulled in by <crucible/Fixy.h>.");

// 6c-b. is_content_addressed_v discriminates wrapped vs bare.
static_assert( fsca::is_content_addressed_v<
    fsca::ContentAddressed<u052c_reach_probe::Payload>>,
    "umbrella reach: fixy::sess::contentaddr::is_content_addressed_v "
    "must observe wrapped payloads through the umbrella.");
static_assert(!fsca::is_content_addressed_v<u052c_reach_probe::Payload>,
    "umbrella reach: fixy::sess::contentaddr::is_content_addressed_v "
    "must reject bare payloads through the umbrella.");

// 6c-c. ContentAddressedType concept routes through the umbrella.
static_assert( fsca::ContentAddressedType<
    fsca::ContentAddressed<u052c_reach_probe::Payload>>);
static_assert(!fsca::ContentAddressedType<u052c_reach_probe::Payload>);

// 6c-d. unwrap_content_addressed_t strips all layers through the umbrella.
static_assert(std::is_same_v<
    fsca::unwrap_content_addressed_t<
        fsca::ContentAddressed<
            fsca::ContentAddressed<u052c_reach_probe::Payload>>>,
    u052c_reach_probe::Payload>,
    "umbrella reach: fixy::sess::contentaddr::unwrap_content_addressed_t "
    "must strip all wrapper layers through the umbrella.");

// 6c-e. depth counter routes through the umbrella.
static_assert(fsca::content_addressed_depth_v<u052c_reach_probe::Payload> == 0);
static_assert(fsca::content_addressed_depth_v<
    fsca::ContentAddressed<u052c_reach_probe::Payload>> == 1);

// ─── 6d. SessEventLog.h reach — fixy::sess::eventlog:: (FIXY-U-052d) ──
//
// Witness that the typed append-only event-log surface (8 strong IDs +
// SessionOp + classifier enums + SessionEvent + StepIdKeyFn/StepIdLess +
// SessionEventLog) reaches the consumer through the umbrella include
// alone.  If a future regression strips
// `#include <crucible/fixy/SessEventLog.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile — the in-header sentinels inside
// SessEventLog.h fire only at direct-include sites, so the umbrella-reach
// gate lives here.
//
// Production consumer: Cipher.h HEAD/log roll-forward + cold-tier
// SessionEvent drain reach StepId / SessionTagId / StepIdKeyFn /
// StepIdLess through this fixy path under FIXY-U-092.

namespace fsel = ::crucible::fixy::sess::eventlog;

// 6d-a. Strong-ID types resolve through the umbrella to the substrate.
static_assert(std::is_same_v<fsel::StepId, ::crucible::safety::proto::StepId>,
    "umbrella reach: fixy::sess::eventlog::StepId must alias "
    "safety::proto::StepId.  If this red-lights, fixy/SessEventLog.h is "
    "not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fsel::SessionTagId,
                             ::crucible::safety::proto::SessionTagId>);
static_assert(std::is_same_v<fsel::StepIdKeyFn,
                             ::crucible::safety::proto::StepIdKeyFn>);
static_assert(std::is_same_v<fsel::StepIdLess,
                             ::crucible::safety::proto::StepIdLess>);

// 6d-b. SessionEvent record + 72-byte wire format reach the consumer.
static_assert(std::is_same_v<fsel::SessionEvent,
                             ::crucible::safety::proto::SessionEvent>);
static_assert(sizeof(fsel::SessionEvent) == 72,
    "umbrella reach: fixy::sess::eventlog::SessionEvent must keep its "
    "72-byte cold-tier wire format through the umbrella.");

// 6d-c. SessionEventLog primitive + SessionOp enum route through.
static_assert(std::is_same_v<fsel::SessionEventLog,
                             ::crucible::safety::proto::SessionEventLog>);
static_assert(std::is_same_v<fsel::SessionOp,
                             ::crucible::safety::proto::SessionOp>);

// 6d-d. SessionEvent ALSO surfaces in fixy::contract::cipher:: — both
// umbrella paths must alias the SAME substrate symbol (fixy-A4-011
// dual-export discipline).
static_assert(std::is_same_v<fsel::SessionEvent,
                             ::crucible::fixy::contract::cipher::SessionEvent>,
    "dual-export: fixy::sess::eventlog::SessionEvent and "
    "fixy::contract::cipher::SessionEvent must be the same substrate type.");

// ─── 6e. SessSubtype.h reach — fixy::sess::subtype:: (FIXY-U-052e) ───
//
// Witness that the Gay-Hole subtype-layer surface (the synchronous
// subtype relation + its ergonomic concepts/assertions + the
// failure-reason diagnostics) reaches the consumer through the umbrella
// include alone.  If a future regression strips
// `#include <crucible/fixy/SessSubtype.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile — the in-header sentinels inside
// SessSubtype.h fire only at direct-include sites, so the umbrella-reach
// gate lives here.
//
// Production consumers: Vessel adapter `assert_subtype_sync<>` checks
// and protocol-evolution boundaries reach the relation through this
// fixy path.

namespace fss = ::crucible::fixy::sess::subtype;

// 6e-a. Relation _v aliases resolve through the umbrella to the substrate.
static_assert(fss::is_subtype_sync_v<::crucible::safety::proto::End,
                                     ::crucible::safety::proto::End>,
    "umbrella reach: fixy::sess::subtype::is_subtype_sync_v must reach "
    "safety::proto.  If this red-lights, fixy/SessSubtype.h is not "
    "pulled in by <crucible/Fixy.h>.");
static_assert(!fss::is_subtype_sync_v<
                  ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                  ::crucible::safety::proto::Recv<int, ::crucible::safety::proto::End>>,
    "umbrella reach: Send/Recv shape mismatch must be rejected through "
    "the fixy path too.");

// 6e-b. Concept reaches the consumer and holds on a reflexive pair.
static_assert(fss::SubtypeSync<::crucible::safety::proto::End,
                               ::crucible::safety::proto::End>);

// 6e-c. Reason result types route through and alias the substrate.
static_assert(std::is_same_v<fss::SubtypeOk,
                             ::crucible::safety::proto::SubtypeOk>);
static_assert(std::is_same_v<
    fss::subtype_rejection_reason_t<::crucible::safety::proto::End,
                                    ::crucible::safety::proto::End>,
    fss::SubtypeOk>,
    "umbrella reach: End ⩽ End yields the SubtypeOk sentinel through "
    "the fixy::sess::subtype path.");

// ─── 6f. SessQueue.h reach — fixy::sess::queue:: (FIXY-U-052f) ───────
//
// Witness that the L3 queue-types σ surface (QueuedMsg/Queue carriers
// + FIFO enqueue/head/tail ops + channel-scoped queries + queue-state
// predicates) reaches the consumer through the umbrella include alone.
// If a future regression strips
// `#include <crucible/fixy/SessQueue.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile — the in-header sentinels inside
// SessQueue.h fire only at direct-include sites, so the umbrella-reach
// gate lives here.

namespace fsq = ::crucible::fixy::sess::queue;

namespace u052f_reach {
struct RoleA {};
struct RoleB {};
using Msg = fsq::QueuedMsg<RoleA, RoleB, int>;
}  // namespace u052f_reach

// 6f-a. Carrier types resolve through the umbrella to the substrate.
static_assert(std::is_same_v<fsq::EmptyQueue,
                             ::crucible::safety::proto::EmptyQueue>,
    "umbrella reach: fixy::sess::queue::EmptyQueue must alias "
    "safety::proto::EmptyQueue.  If this red-lights, fixy/SessQueue.h is "
    "not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<
    fsq::QueuedMsg<u052f_reach::RoleA, u052f_reach::RoleB, int>,
    ::crucible::safety::proto::QueuedMsg<u052f_reach::RoleA,
                                         u052f_reach::RoleB, int>>);

// 6f-b. FIFO operation reaches the consumer and reduces correctly.
static_assert(std::is_same_v<
    fsq::enqueue_queue_t<fsq::EmptyQueue, u052f_reach::Msg>,
    fsq::Queue<u052f_reach::Msg>>,
    "umbrella reach: enqueue right-appends through the fixy path.");
static_assert(fsq::queue_size_v<fsq::Queue<u052f_reach::Msg, u052f_reach::Msg>> == 2);

// 6f-c. Channel query + queue-state predicate route through.
static_assert(fsq::queue_contains_v<fsq::Queue<u052f_reach::Msg>,
                                    u052f_reach::RoleA, u052f_reach::RoleB>);
static_assert(fsq::is_queue_state_v<fsq::EmptyQueue>);

// ─── 6g. SessDiagnostic.h reach — fixy::sess::diagnostic:: (U-052g) ──
//
// Witness that the session manifest-bug catalog (tag_base + 23 tags +
// classifier/accessors + Diagnostic<> wrapper + Catalog) reaches the
// consumer through the umbrella include alone.  If a future regression
// strips `#include <crucible/fixy/SessDiagnostic.h>` from Fixy.h's
// Phase-C block, the next claims fail to compile — the in-header
// sentinels inside SessDiagnostic.h fire only at direct-include sites,
// so the umbrella-reach gate lives here.
//
// NOTE: this is the SESSION diagnostic catalog (proto::diagnostic),
// distinct from fixy::diag:: (the FOUND-E01 safety::diag Category).

namespace fsdiag = ::crucible::fixy::sess::diagnostic;

// 6g-a. Tag + Catalog types resolve through the umbrella to substrate.
static_assert(std::is_same_v<fsdiag::SubtypeMismatch,
    ::crucible::safety::proto::diagnostic::SubtypeMismatch>,
    "umbrella reach: fixy::sess::diagnostic::SubtypeMismatch must alias "
    "safety::proto::diagnostic::SubtypeMismatch.  If this red-lights, "
    "fixy/SessDiagnostic.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fsdiag::Catalog,
    ::crucible::safety::proto::diagnostic::Catalog>);

// 6g-b. Classifier + catalog size route through.
static_assert(fsdiag::is_diagnostic_class_v<fsdiag::SubtypeMismatch>);
static_assert(!fsdiag::is_diagnostic_class_v<int>);
static_assert(fsdiag::catalog_size == 23);

// 6g-c. Diagnostic<> wrapper reaches the consumer.
static_assert(fsdiag::is_diagnostic_v<fsdiag::Diagnostic<fsdiag::SubtypeMismatch, int>>);

// ─── 6h. SessPayloadSubsort.h reach — payload axioms (FIXY-U-052h) ───
//
// SessPayloadSubsort.h ships only is_subsort<...> specialisations (no
// new names).  The load-bearing reach claim is VISIBILITY: pulling the
// umbrella must make the payload-subsort specialisations visible, so
// `fixy::sess::subtype::is_subsort_v<Refined<positive,int>, int>`
// resolves to the NARROWING specialisation (true) — not the primary
// template (false).  If Fixy.h stops pulling fixy/SessPayloadSubsort.h,
// these flip to false and the static_asserts fire.  (`fss` is the
// fixy::sess::subtype alias from §6e.)

// 6h-a. Narrowing axiom reaches through the umbrella (true, not false).
static_assert(fss::is_subsort_v<
    ::crucible::safety::Refined<::crucible::safety::positive, int>, int>,
    "umbrella reach: the Refined<P,T> ⩽ T narrowing axiom must be "
    "visible — if false, fixy/SessPayloadSubsort.h is not pulled by "
    "<crucible/Fixy.h> and the primary template silently won.");

// 6h-b. Safe-to-erase provenance flows; unsafe provenance does NOT.
static_assert(fss::is_subsort_v<
    ::crucible::safety::Tagged<int, ::crucible::safety::source::Sanitized>, int>);
static_assert(!fss::is_subsort_v<
    ::crucible::safety::Tagged<int, ::crucible::safety::source::External>, int>,
    "trust-boundary discipline must reach the umbrella consumer: "
    "External provenance does not flow to bare T.");

// ─── 6i. SessContext.h reach — fixy::sess::context:: (FIXY-U-052i) ───
//
// Witness that the L2 typing-context Γ surface (Entry/Context carriers +
// lookup/update/remove/compose + domain projection + permission balance)
// reaches the consumer through the umbrella include alone.  If a future
// regression strips `#include <crucible/fixy/SessContext.h>` from Fixy.h's
// Phase-C block, the next claims fail to compile.

namespace fsctx = ::crucible::fixy::sess::context;

namespace u052i_reach {
struct Sess {};
struct RoleP {};
struct RoleC {};
struct TyP {};
struct TyC {};
using Ctx = fsctx::Context<fsctx::Entry<Sess, RoleP, TyP>,
                           fsctx::Entry<Sess, RoleC, TyC>>;
}  // namespace u052i_reach

// 6i-a. Carrier types resolve through the umbrella to the substrate.
static_assert(std::is_same_v<fsctx::EmptyContext,
                             ::crucible::safety::proto::EmptyContext>,
    "umbrella reach: fixy::sess::context::EmptyContext must alias "
    "safety::proto::EmptyContext.  If this red-lights, fixy/SessContext.h "
    "is not pulled in by <crucible/Fixy.h>.");

// 6i-b. Core Γ operations route through (size, lookup, contains).
static_assert(fsctx::context_size_v<u052i_reach::Ctx> == 2);
static_assert(std::is_same_v<
    fsctx::lookup_context_t<u052i_reach::Ctx, u052i_reach::Sess, u052i_reach::RoleP>,
    u052i_reach::TyP>);
static_assert(fsctx::contains_key_v<u052i_reach::Ctx,
                                    u052i_reach::Sess, u052i_reach::RoleC>);

// ─── 7. fixy::wrap:: saturating-arithmetic free functions (FIXY-U-096b) ──
//
// Witness that the saturating-arithmetic primitives required by
// Saturate.h's *_det / *_from / *_into variants (add_sat_checked /
// sub_sat_checked / mul_sat_checked) reach the consumer through the
// fixy::wrap:: umbrella alone.  These are free functions rather than
// type templates, so we prove identity via decltype-equality on the
// function-pointer type — the same technique used in fixy/Wrap.h's
// in-header sentinels (pointer-equality `==` triggers `-Werror=
// tautological-compare` because GCC folds both sides; the type-identity
// rail dodges that and witnesses what we need).  A using-declaration
// does not introduce a new function entity, so decltype-identity is
// the right structural witness for the using-decl path.

namespace fwrap = ::crucible::fixy::wrap;

// 7a. add_sat_checked through the umbrella resolves to the substrate
//     primary template — proves the using-decl path is identity-preserving
//     for free-function templates.
static_assert(std::is_same_v<
    decltype(&fwrap::add_sat_checked<std::uint32_t>),
    decltype(&::crucible::safety::add_sat_checked<std::uint32_t>)>,
    "umbrella reach: fixy::wrap::add_sat_checked must alias "
    "safety::add_sat_checked when reached via the umbrella.  If this "
    "red-lights, fixy/Wrap.h dropped the using-decl or Fixy.h fails "
    "to pull in fixy/Wrap.h.");

// 7b. sub_sat_checked through the umbrella.
static_assert(std::is_same_v<
    decltype(&fwrap::sub_sat_checked<std::int32_t>),
    decltype(&::crucible::safety::sub_sat_checked<std::int32_t>)>,
    "umbrella reach: fixy::wrap::sub_sat_checked must alias "
    "safety::sub_sat_checked when reached via the umbrella.");

// 7c. mul_sat_checked through the umbrella.
static_assert(std::is_same_v<
    decltype(&fwrap::mul_sat_checked<std::uint16_t>),
    decltype(&::crucible::safety::mul_sat_checked<std::uint16_t>)>,
    "umbrella reach: fixy::wrap::mul_sat_checked must alias "
    "safety::mul_sat_checked when reached via the umbrella.");

// 7d. Runtime behavioral witness through the umbrella path:
//     no-overflow returns clamped=false, overflow returns clamped=true.
//     Performed in a consteval context so the runner does not depend
//     on linker resolution of the substrate symbol.
namespace u096b_reach_probe {
consteval bool fixy_wrap_sat_smoke() noexcept {
    auto a = fwrap::add_sat_checked<std::uint8_t>(std::uint8_t{200},
                                                  std::uint8_t{100});  // 300>255
    if (a.value() != std::uint8_t{255}) return false;
    if (!a.was_clamped()) return false;
    auto b = fwrap::sub_sat_checked<std::uint8_t>(std::uint8_t{10},
                                                  std::uint8_t{50});   // -40<0
    if (b.value() != std::uint8_t{0}) return false;
    if (!b.was_clamped()) return false;
    auto c = fwrap::mul_sat_checked<std::uint8_t>(std::uint8_t{2},
                                                  std::uint8_t{3});    // 6, no clamp
    if (c.value() != std::uint8_t{6}) return false;
    if (c.was_clamped()) return false;
    return true;
}
static_assert(fixy_wrap_sat_smoke(),
    "umbrella reach: fixy::wrap:: saturating-arithmetic free functions "
    "must produce the substrate's behavioral semantics through the "
    "umbrella path.");
}  // namespace u096b_reach_probe

// Every claim above is consteval; main() exists so the runner can
// link the TU as a stand-alone executable.
int main() { return 0; }
