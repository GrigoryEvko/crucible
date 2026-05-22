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
#include <utility>

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
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 23`) lives in fixy/SessDiagnostic.h:183 colocated with the
// source-of-truth Catalog tuple; THIS TU only holds the FLOOR pin
// (`>= 23`) catching the inverse direction — an accidental REMOVAL
// of a session-diagnostic Catalog entry.
static_assert(fsdiag::is_diagnostic_class_v<fsdiag::SubtypeMismatch>);
static_assert(!fsdiag::is_diagnostic_class_v<int>);
static_assert(fsdiag::catalog_size >= 23,
    "floor: fixy::sess::diagnostic::catalog_size regressed below 23 "
    "— a session-diagnostic Catalog entry was removed without "
    "updating both SessDiagnostic.h's colocated ceiling pin AND this "
    "floor witness.");

// 6g-c. Diagnostic<> wrapper reaches the consumer.
static_assert(fsdiag::is_diagnostic_v<fsdiag::Diagnostic<fsdiag::SubtypeMismatch, int>>);

// ─── 6h. Payload-subsort axiom reach — merged into SessSubtype.h (V-067) ──
//
// History: through 2026-05-22 the SessionPayloadSubsort.h visibility
// include lived in a dedicated fixy/SessPayloadSubsort.h slice with its
// own `fixy::sess::payloadsubsort::` sub-namespace.  V-067 consolidated
// that slice into fixy/SessSubtype.h (payload-subsort axioms are a
// proper extension of the subtype layer's `is_subsort` relation) and
// deleted the dedicated header.  The reach claims below STILL hold —
// the visibility now travels through SessSubtype.h — so this cell is
// kept as the load-bearing witness that V-067 didn't drop the umbrella
// visibility for the payload-subsort specialisations.  If a future
// regression strips `#include <crucible/sessions/SessionPayloadSubsort.h>`
// from fixy/SessSubtype.h, the next claims fail to compile.
// (`fss` is the fixy::sess::subtype alias from §6e.)

// 6h-a. Narrowing axiom reaches through the umbrella (true, not false).
static_assert(fss::is_subsort_v<
    ::crucible::safety::Refined<::crucible::safety::positive, int>, int>,
    "umbrella reach: the Refined<P,T> ⩽ T narrowing axiom must be "
    "visible — if false, fixy/SessSubtype.h dropped its include of "
    "<crucible/sessions/SessionPayloadSubsort.h> and the primary template "
    "silently won.");

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

// ─── 6j. SessGrade.h reach — fixy::sess::grade:: (FIXY-U-052j) ───────
//
// Witness that the Graded-product grade-extraction surface (axis tags +
// protocol_grade folds + per-axis projections + aggregate satisfaction)
// reaches the consumer through the umbrella include alone.  Closing
// slice of the U-052 umbrella (#1742).  If a future regression strips
// `#include <crucible/fixy/SessGrade.h>` from Fixy.h's Phase-C block,
// the next claims fail to compile.

namespace fsg = ::crucible::fixy::sess::grade;

namespace u052j_reach {
struct Payload {};
using P = ::crucible::safety::proto::Send<
    ::crucible::safety::NumericalTier<
        ::crucible::safety::proto::Tolerance::BITEXACT, Payload>,
    ::crucible::safety::proto::End>;
}  // namespace u052j_reach

// 6j-a. Axis tag resolves through the umbrella to the substrate.
static_assert(std::is_same_v<fsg::axis::NumericalTier,
                             ::crucible::safety::proto::axis::NumericalTier>,
    "umbrella reach: fixy::sess::grade::axis::NumericalTier must alias "
    "safety::proto::axis::NumericalTier.  If this red-lights, "
    "fixy/SessGrade.h is not pulled in by <crucible/Fixy.h>.");

// 6j-b. Grade extraction + per-axis projection route through.
static_assert(fsg::protocol_grade_numerical_tier_v<u052j_reach::P>
                  == ::crucible::safety::proto::Tolerance::BITEXACT);
static_assert(fsg::grade_for_axis_v<fsg::axis::NumericalTier,
    ::crucible::safety::NumericalTier<
        ::crucible::safety::proto::Tolerance::BITEXACT, u052j_reach::Payload>>
                  == ::crucible::safety::proto::Tolerance::BITEXACT);

// 6j-c. Aggregate self-satisfaction routes through.
static_assert(fsg::protocol_grade_aggregate_satisfies_v<u052j_reach::P,
                                                        u052j_reach::P>);

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

// ─── 6k. SessAssoc.h reach — fixy::sess::assoc:: (FIXY-V-059) ────────
//
// Witness that the L5 association invariant surface (HYK24 Δ ⊑_s G —
// domain_matches_v + all_entries_refine_projection_v + is_associated_v
// + AssociatedWith + assert_associated + projected_context_t + role-
// list set helpers) reaches the consumer through the umbrella include
// alone.  If a future regression strips `#include <crucible/fixy/
// SessAssoc.h>` from Fixy.h's Phase-C block, the claims below fail to
// compile.

namespace fsassoc = ::crucible::fixy::sess::assoc;

namespace v059_reach {
struct SessZ {};
struct RoleA {};
struct RoleB {};
struct Msg   {};
using G = ::crucible::safety::proto::Transmission<RoleA, RoleB, Msg,
                                                  ::crucible::safety::proto::End_G>;
using Gamma = fsassoc::projected_context_t<G, SessZ>;
}  // namespace v059_reach

// 6k-a. projected_context_t resolves through the umbrella to the substrate.
static_assert(std::is_same_v<
    v059_reach::Gamma,
    ::crucible::safety::proto::projected_context_t<v059_reach::G, v059_reach::SessZ>>,
    "umbrella reach: fixy::sess::assoc::projected_context_t must alias "
    "safety::proto::projected_context_t.  If this red-lights, "
    "fixy/SessAssoc.h is not pulled in by <crucible/Fixy.h>.");

// 6k-b. Core association traits route through (domain + refine + assoc).
static_assert(fsassoc::domain_matches_v<v059_reach::Gamma,
                                        v059_reach::G,
                                        v059_reach::SessZ>);
static_assert(fsassoc::all_entries_refine_projection_v<v059_reach::Gamma,
                                                      v059_reach::G,
                                                      v059_reach::SessZ>);
static_assert(fsassoc::is_associated_v<v059_reach::Gamma,
                                       v059_reach::G,
                                       v059_reach::SessZ>);

// ─── 6l. SessDelegate.h reach — fixy::sess::delegate:: (FIXY-V-060) ──
//
// Witness that the L4 delegation surface (Delegate / Accept primary
// pair, Epoched-NTTP variants, crash propagation triple, sugar aliases,
// discriminators, concepts, transport concepts, intent-revealing
// consteval asserts) reaches the consumer through the umbrella include
// alone.  If a future regression strips `#include <crucible/fixy/
// SessDelegate.h>` from Fixy.h's Phase-C block, the claims below fail
// to compile.

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

// 6l-a. Core combinators alias through the umbrella to the substrate.
static_assert(std::is_same_v<
    v060_reach::D,
    ::crucible::safety::proto::Delegate<v060_reach::T, v060_reach::K>>,
    "umbrella reach: fixy::sess::delegate::Delegate must alias "
    "safety::proto::Delegate.  If this red-lights, fixy/SessDelegate.h "
    "is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<
    v060_reach::A,
    ::crucible::safety::proto::Accept<v060_reach::T, v060_reach::K>>);

// 6l-b. Epoched variants preserve their NTTPs through the umbrella.
static_assert(v060_reach::ED::min_epoch      == 11u);
static_assert(v060_reach::ED::min_generation == 13u);
static_assert(v060_reach::EA::min_epoch      == 11u);
static_assert(v060_reach::EA::min_generation == 13u);

// 6l-c. Discriminators reach (is_delegate_v / is_accept_v /
// is_delegation_head_v).
static_assert(fsdelegate::is_delegate_v<v060_reach::D>);
static_assert(fsdelegate::is_accept_v<v060_reach::A>);
static_assert(fsdelegate::is_delegation_head_v<v060_reach::ED>);
static_assert(fsdelegate::is_delegation_head_v<v060_reach::EA>);

// 6l-d. Sugar combinator (Delegate_seq / Redelegate) expansions reach.
static_assert(std::is_same_v<
    fsdelegate::Delegate_seq<v060_reach::Req, v060_reach::Ack, v060_reach::K>,
    ::crucible::safety::proto::Delegate<v060_reach::Req,
        ::crucible::safety::proto::Delegate<v060_reach::Ack, v060_reach::K>>>);
static_assert(std::is_same_v<
    fsdelegate::Redelegate<v060_reach::T, v060_reach::K>,
    ::crucible::safety::proto::Accept<v060_reach::T,
        ::crucible::safety::proto::Delegate<v060_reach::T, v060_reach::K>>>);

// 6l-e. Concepts reach — CanDelegate / DelegatesTo / AcceptsFrom.
template <typename P, typename R>
    requires fsdelegate::CanDelegate<P, R>
consteval bool v060_can_delegate_witness() { return true; }
static_assert(v060_can_delegate_witness<v060_reach::T, v060_reach::CrashTag>());

template <typename C, typename Td>
    requires fsdelegate::DelegatesTo<C, Td>
consteval bool v060_delegates_to_witness() { return true; }
static_assert(v060_delegates_to_witness<v060_reach::D, v060_reach::T>());

// 6l-f. Intent-revealing consteval assertions reach.
consteval bool v060_assert_delegates_to_witness() {
    fsdelegate::assert_delegates_to<v060_reach::D, v060_reach::T>();
    return true;
}
static_assert(v060_assert_delegates_to_witness());

// ─── 6m. SessCheckpoint.h reach — fixy::sess::checkpoint:: (V-061) ───
//
// Witness that the L8 checkpoint-pair surface (CheckpointedSession +
// shape traits + branch extractors + Checkpointed concept +
// assert_checkpointed_matches consteval helper) reaches the consumer
// through the umbrella include alone.  If a future regression strips
// `#include <crucible/fixy/SessCheckpoint.h>` from Fixy.h's Phase-C
// block, the claims below fail to compile.

namespace fscheckpoint = ::crucible::fixy::sess::checkpoint;

namespace v061_reach {
struct Req {};
struct Resp {};
struct Err  {};
using CommitPath   = ::crucible::safety::proto::Send<Req,
                       ::crucible::safety::proto::Recv<Resp,
                         ::crucible::safety::proto::End>>;
using RollbackPath = ::crucible::safety::proto::Send<Req,
                       ::crucible::safety::proto::Recv<Err,
                         ::crucible::safety::proto::End>>;
using Ckpt = fscheckpoint::CheckpointedSession<CommitPath, RollbackPath>;
using Plain = ::crucible::safety::proto::Send<Req,
                ::crucible::safety::proto::End>;
}  // namespace v061_reach

// 6m-a. CheckpointedSession aliases through the umbrella to the
// substrate.
static_assert(std::is_same_v<
    v061_reach::Ckpt,
    ::crucible::safety::proto::CheckpointedSession<
        v061_reach::CommitPath, v061_reach::RollbackPath>>,
    "umbrella reach: fixy::sess::checkpoint::CheckpointedSession must "
    "alias safety::proto::CheckpointedSession.  If this red-lights, "
    "fixy/SessCheckpoint.h is not pulled in by <crucible/Fixy.h>.");

// 6m-b. Shape traits reach — accepts CkptSession, rejects plain.
static_assert(fscheckpoint::is_checkpointed_session_v<v061_reach::Ckpt>);
static_assert(!fscheckpoint::is_checkpointed_session_v<v061_reach::Plain>);

// 6m-c. Branch extractors recover the (base, rollback) pair.
static_assert(std::is_same_v<
    fscheckpoint::checkpoint_base_t<v061_reach::Ckpt>,
    v061_reach::CommitPath>);
static_assert(std::is_same_v<
    fscheckpoint::checkpoint_rollback_t<v061_reach::Ckpt>,
    v061_reach::RollbackPath>);

// 6m-d. Checkpointed concept admits CkptSession.
template <typename P>
    requires fscheckpoint::Checkpointed<P>
consteval bool v061_checkpointed_witness() { return true; }
static_assert(v061_checkpointed_witness<v061_reach::Ckpt>());

// 6m-e. assert_checkpointed_matches reach (consteval call site).
consteval bool v061_assert_matches_witness() {
    fscheckpoint::assert_checkpointed_matches<v061_reach::Ckpt,
        v061_reach::CommitPath, v061_reach::RollbackPath>();
    return true;
}
static_assert(v061_assert_matches_witness());

// ─── 6n. SessRowExtraction.h reach — fixy::sess::row:: (V-062) ───────
//
// Witness that the payload-row projection surface (carrier
// NumericalPayloadRow + payload_row + payload_row_effect +
// payload_effect_row_t + protocol_effect_row + row_union_pack)
// reaches the consumer through the umbrella include alone.  If a
// future regression strips `#include <crucible/fixy/
// SessRowExtraction.h>` from Fixy.h's Phase-C block, the claims
// below fail to compile.

namespace fsrow = ::crucible::fixy::sess::row;

namespace v062_reach {
using IoComp = ::crucible::effects::Computation<
    ::crucible::effects::Row<::crucible::effects::Effect::IO>, int>;
using SendIo = ::crucible::safety::proto::Send<IoComp,
                  ::crucible::safety::proto::End>;
}  // namespace v062_reach

// 6n-a. NumericalPayloadRow aliases through the umbrella.
static_assert(std::is_same_v<
    fsrow::NumericalPayloadRow<::crucible::safety::Tolerance::BITEXACT,
                               ::crucible::effects::Row<::crucible::effects::Effect::IO>>,
    ::crucible::safety::proto::NumericalPayloadRow<
        ::crucible::safety::Tolerance::BITEXACT,
        ::crucible::effects::Row<::crucible::effects::Effect::IO>>>,
    "umbrella reach: fixy::sess::row::NumericalPayloadRow must alias "
    "safety::proto::NumericalPayloadRow.  If this red-lights, "
    "fixy/SessRowExtraction.h is not pulled in by <crucible/Fixy.h>.");

// 6n-b. payload_row_t extracts the Computation row.
static_assert(std::is_same_v<
    fsrow::payload_row_t<v062_reach::IoComp>,
    ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

// 6n-c. payload_effect_row_t composed.
static_assert(std::is_same_v<
    fsrow::payload_effect_row_t<v062_reach::IoComp>,
    ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

// 6n-d. protocol_effect_row_t walks Send and recovers payload row.
static_assert(std::is_same_v<
    fsrow::protocol_effect_row_t<v062_reach::SendIo>,
    ::crucible::effects::Row<::crucible::effects::Effect::IO>>);

// ─── 6o. SessView.h reach — fixy::sess::view:: (V-063) ──────────────
//
// Witness that the non-consuming inspection surface (10 position
// tags + handle_is_at trait family + view_ok + mint_session_view +
// session_view_protocol_name + session_view_message_type +
// session_view_branch_count) reaches the consumer through the
// umbrella include alone.  If a future regression strips
// `#include <crucible/fixy/SessView.h>` from Fixy.h's Phase-C
// block, the claims below fail to compile.

namespace fsview = ::crucible::fixy::sess::view;

namespace v063_reach {
struct FakeResource {};
struct Msg {};
using SendProto = ::crucible::safety::proto::Send<Msg,
                    ::crucible::safety::proto::End>;
using SendHandle = ::crucible::safety::proto::SessionHandle<
                    SendProto, FakeResource, void>;
}  // namespace v063_reach

// 6o-a. AtSend position tag aliases through the umbrella.
static_assert(std::is_same_v<fsview::AtSend,
    ::crucible::safety::proto::AtSend>,
    "umbrella reach: fixy::sess::view::AtSend must alias "
    "safety::proto::AtSend.  If this red-lights, "
    "fixy/SessView.h is not pulled in by <crucible/Fixy.h>.");

// 6o-b. AtTerminal covers End ∪ Stop (umbrella witnesses the
// destruction-safe category).
static_assert(std::is_same_v<fsview::AtTerminal,
    ::crucible::safety::proto::AtTerminal>);

// 6o-c. handle_is_at_v admits matching tag (positive case).
static_assert(fsview::handle_is_at_v<v063_reach::SendHandle,
                                      fsview::AtSend>);

// 6o-d. HandleIsAt concept admits matching pair AND rejects
// mismatch (negative case).
static_assert( fsview::HandleIsAt<v063_reach::SendHandle, fsview::AtSend>);
static_assert(!fsview::HandleIsAt<v063_reach::SendHandle, fsview::AtRecv>);

// 6o-e. mint_session_view produces ScopedView<Handle, Tag>.
// Pure type-level (Send-state handle has abandonment-check destructor
// per substrate doc-block; substrate self-test uses the same idiom).
using V063MintedView = decltype(
    fsview::mint_session_view<fsview::AtSend>(
        std::declval<v063_reach::SendHandle const&>()));
static_assert(std::is_same_v<V063MintedView,
    ::crucible::safety::ScopedView<v063_reach::SendHandle, fsview::AtSend>>);

// 6o-f. session_view_message_type_t extracts the AtSend view payload.
static_assert(std::is_same_v<
    fsview::session_view_message_type_t<V063MintedView>,
    v063_reach::Msg>);

// ─── 6p. SessCrash.h reach — fixy::sess::crash:: (V-064) ────────────
//
// Witness that the BSYZ22/BHYZ23 crash-stop surface (5 stop combinator
// re-exports + 3 crash payload re-exports + UnavailableQueue + 4
// reliability-set entries + 3 per-Offer crash-branch entries + 2 per-
// tree walker entries + 1 synthesis concept = 19 entries) reaches the
// consumer through the umbrella include alone.  Cells below test
// (a) substrate identity preservation, (b) the per-Offer / per-tree
// gates' positive-vs-negative behaviour, and (c) the CrashAwareForTransport
// synthesis concept's two-part discipline.  If a future regression
// strips `#include <crucible/fixy/SessCrash.h>` from Fixy.h's Phase-C
// block, the claims below fail to compile.

namespace fscrash = ::crucible::fixy::sess::crash;

namespace v064_reach {
struct Alice {};
struct Bob   {};
struct Msg   {};
struct Ack   {};

namespace proto = ::crucible::safety::proto;

using EndProto  = proto::End;
using StopProto = fscrash::Stop;

using AliceCrashOffer = proto::Offer<
    proto::Recv<Msg,                  EndProto>,
    proto::Recv<fscrash::Crash<Alice>, EndProto>>;
using NormalOffer = proto::Offer<
    proto::Recv<Msg, EndProto>,
    proto::Recv<Ack, EndProto>>;

using CrashAwareClient = proto::Send<Msg, AliceCrashOffer>;
using CrashOblivClient = proto::Send<Msg, NormalOffer>;
}  // namespace v064_reach

// 6p-a. Stop combinator + CrashClass alias through the umbrella.
static_assert(std::is_same_v<fscrash::Stop,
    ::crucible::safety::proto::Stop>,
    "umbrella reach: fixy::sess::crash::Stop must alias "
    "safety::proto::Stop.  If this red-lights, "
    "fixy/SessCrash.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<fscrash::CrashClass,
    ::crucible::safety::proto::CrashClass>);
static_assert(std::is_same_v<
    fscrash::Stop_g<fscrash::CrashClass::NoThrow>,
    ::crucible::safety::proto::Stop_g<
        ::crucible::safety::proto::CrashClass::NoThrow>>);

// 6p-b. Crash payload + reliability-set reach.
static_assert(std::is_same_v<fscrash::Crash<v064_reach::Alice>,
    ::crucible::safety::proto::Crash<v064_reach::Alice>>);
static_assert( fscrash::is_crash_v<fscrash::Crash<v064_reach::Alice>>);
static_assert(!fscrash::is_crash_v<v064_reach::Msg>);
static_assert(std::is_same_v<fscrash::UnreliableAll,
    fscrash::ReliableSet<>>);
static_assert( fscrash::is_reliable_v<
    fscrash::ReliableSet<v064_reach::Alice>, v064_reach::Alice>);
static_assert(!fscrash::is_reliable_v<
    fscrash::ReliableSet<v064_reach::Alice>, v064_reach::Bob>);

// 6p-c. Per-Offer crash-branch predicate reach.
static_assert(!fscrash::has_crash_branch_for_peer_v<
    v064_reach::NormalOffer, v064_reach::Alice>);
static_assert( fscrash::has_crash_branch_for_peer_v<
    v064_reach::AliceCrashOffer, v064_reach::Alice>);

// 6p-d. Per-tree crash-branch walker reach (positive + negative).
static_assert( fscrash::every_offer_has_crash_branch_for_peer_v<
    v064_reach::CrashAwareClient, v064_reach::Alice>);
static_assert(!fscrash::every_offer_has_crash_branch_for_peer_v<
    v064_reach::CrashOblivClient, v064_reach::Alice>);

// 6p-e. CrashAwareForTransport synthesis concept reach.
// The concept's two-part gate fires through the umbrella include:
// crash-aware client satisfies; crash-oblivious client rejects.
static_assert( fscrash::CrashAwareForTransport<
    v064_reach::CrashAwareClient, v064_reach::Alice>,
    "CrashAwareForTransport must admit a well-formed client whose "
    "every Offer<> has a Recv<Crash<Alice>, _> branch.");
static_assert(!fscrash::CrashAwareForTransport<
    v064_reach::CrashOblivClient, v064_reach::Alice>,
    "CrashAwareForTransport must REJECT a well-formed but crash-"
    "oblivious client — well-formedness alone is insufficient.");

// 6p-f. End / Stop are vacuously crash-aware (no Offer<> on the tree).
static_assert(fscrash::CrashAwareForTransport<
    v064_reach::EndProto, v064_reach::Alice>);
static_assert(fscrash::CrashAwareForTransport<
    v064_reach::StopProto, v064_reach::Alice>);

// ─── 6q. SessFederation.h reach — fixy::sess::federation:: (V-065) ──
//
// Witness that the federation 3-role projection surface (namespace
// alias + 4 per-role + admittance mints + row gate using-decls + 1
// fixy wrapper = 8 fixy-side entries reaching 23 transitive substrate
// symbols) reaches the consumer through the umbrella include alone.
// Cells below test (a) substrate-identity preservation through the
// namespace alias for representative symbols across all 7 substrate
// categories, (b) `fixy::sess::mint_federation_channel` reachability
// at the fixy:: level (the explicit alias that disambiguates from
// the session-protocol `mint_channel`), and (c) the `CtxFitsFederation`
// row gate. If a future regression strips `#include <crucible/fixy/
// SessFederation.h>` from Fixy.h's Phase-C block, the claims below
// fail to compile.

namespace ffed = ::crucible::fixy::sess::federation;
namespace pfed = ::crucible::safety::proto::federation;

namespace v065_reach {
struct ReachKey {};
}  // namespace v065_reach

// 6q-a. Role tag identity through the namespace alias.
static_assert(std::is_same_v<ffed::SenderRole, pfed::SenderRole>,
    "umbrella reach: fixy::sess::federation::SenderRole must alias "
    "safety::proto::federation::SenderRole.  If this red-lights, "
    "fixy/SessFederation.h is not pulled in by <crucible/Fixy.h>.");
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole,    pfed::CoordRole>);
static_assert(std::is_same_v<ffed::AnyFederationKey,
                             pfed::AnyFederationKey>);

// 6q-b. Per-role projection protocols reach (parameterized + default).
static_assert(std::is_same_v<
    ffed::SenderProto<v065_reach::ReachKey>,
    pfed::SenderProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<
    ffed::ReceiverProto<v065_reach::ReachKey>,
    pfed::ReceiverProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<
    ffed::CoordProto<v065_reach::ReachKey>,
    pfed::CoordProto<v065_reach::ReachKey>>);

// 6q-c. Expected canonical forms reach.
static_assert(std::is_same_v<
    ffed::ExpectedSenderProto<v065_reach::ReachKey>,
    pfed::ExpectedSenderProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<
    ffed::ExpectedReceiverProto<v065_reach::ReachKey>,
    pfed::ExpectedReceiverProto<v065_reach::ReachKey>>);
static_assert(std::is_same_v<
    ffed::ExpectedCoordProto<v065_reach::ReachKey>,
    pfed::ExpectedCoordProto<v065_reach::ReachKey>>);

// 6q-d. Global protocol + KeyTag-indexed alias reach.
static_assert(std::is_same_v<ffed::FederationProtocol,
                             pfed::FederationProtocol>);
static_assert(std::is_same_v<
    ffed::FederationProtocolFor<v065_reach::ReachKey>,
    pfed::FederationProtocolFor<v065_reach::ReachKey>>);

// 6q-e. Payload types reach.
static_assert(std::is_same_v<ffed::Ack<v065_reach::ReachKey>,
                             pfed::Ack<v065_reach::ReachKey>>);
static_assert(std::is_same_v<ffed::PullRequest<v065_reach::ReachKey>,
                             pfed::PullRequest<v065_reach::ReachKey>>);
static_assert(std::is_same_v<
    ffed::FederationEntryPayload<v065_reach::ReachKey>,
    pfed::FederationEntryPayload<v065_reach::ReachKey>>);

// 6q-f. Verifier trait reach — positive + negative.
static_assert(ffed::role_protocol_matches_v<
                  pfed::SenderRole,
                  ffed::SenderProto<v065_reach::ReachKey>,
                  v065_reach::ReachKey>,
    "umbrella reach: role_protocol_matches_v admits matching "
    "(role, proto) pair through fixy::sess::federation::.");
static_assert(!ffed::role_protocol_matches_v<
                  pfed::SenderRole,
                  ffed::ReceiverProto<v065_reach::ReachKey>,
                  v065_reach::ReachKey>,
    "umbrella reach: role_protocol_matches_v rejects mismatched pair.");

// 6q-g. Row gate using-decls reach at fixy::sess:: level.
namespace fsess = ::crucible::fixy::sess;
static_assert(std::is_same_v<
    decltype(fsess::federation_required_row{}),
    decltype(pfed::federation_required_row{})>,
    "umbrella reach: fixy::sess::federation_required_row must alias "
    "the substrate row.");

// CtxFitsFederation concept reach — instantiate over a non-IsExecCtx
// fixture and witness rejection.  We cannot positively-witness without
// a fitting Ctx fixture; rejection of a plain struct suffices to prove
// the concept reaches and evaluates through the umbrella.
struct NonExecCtxProbe {};
static_assert(!fsess::CtxFitsFederation<NonExecCtxProbe>,
    "umbrella reach: fixy::sess::CtxFitsFederation must reach AND "
    "reject a non-IsExecCtx argument (clause 1 of the packaged gate).");

// ─── 6r. SessShape.h reach — fixy::sess::shape:: (FIXY-V-066) ────────
//
// Witness that the 6 protocol-shape predicates (`is_send_v` /
// `is_recv_v` / `is_select_v` / `is_offer_v` / `is_loop_v` /
// `is_head_v`) reach the consumer through the umbrella alone after
// V-066's carve-out from fixy/Sess.h to fixy/SessShape.h.  Cells below
// test (a) sub-namespace reach `fixy::sess::shape::is_<shape>_v`
// (canonical home) AND (b) umbrella-level reach `fixy::sess::is_<shape>_v`
// (backward-compat using-decl that Sess.h preserves).  If a future
// regression strips `#include <crucible/fixy/SessShape.h>` from Fixy.h's
// Phase-C block OR removes the umbrella using-decls from Sess.h, the
// claims below fail to compile.

namespace fshape = ::crucible::fixy::sess::shape;
namespace fsess_umbrella = ::crucible::fixy::sess;
namespace pproto = ::crucible::safety::proto;

namespace v066_reach {
struct Probe {};
using SendP   = pproto::Send<Probe, pproto::End>;
using RecvP   = pproto::Recv<Probe, pproto::End>;
using SelectP = pproto::Select<SendP>;
using OfferP  = pproto::Offer<RecvP>;
using LoopP   = pproto::Loop<pproto::End>;
using EndP    = pproto::End;
}  // namespace v066_reach

// 6r-a. Sub-namespace canonical reach — is_send_v / is_recv_v.
static_assert(fshape::is_send_v<v066_reach::SendP>,
    "umbrella reach: fixy::sess::shape::is_send_v must admit Send<T, K> "
    "through the umbrella.  If this red-lights, fixy/SessShape.h is not "
    "pulled in by <crucible/Fixy.h> OR the substrate predicate moved.");
static_assert(!fshape::is_send_v<v066_reach::RecvP>);
static_assert(fshape::is_recv_v<v066_reach::RecvP>);
static_assert(!fshape::is_recv_v<v066_reach::SendP>);

// 6r-b. Sub-namespace canonical reach — is_select_v / is_offer_v.
static_assert(fshape::is_select_v<v066_reach::SelectP>);
static_assert(!fshape::is_select_v<v066_reach::OfferP>);
static_assert(fshape::is_offer_v<v066_reach::OfferP>);
static_assert(!fshape::is_offer_v<v066_reach::SelectP>);

// 6r-c. Sub-namespace canonical reach — is_loop_v / is_head_v.
static_assert(fshape::is_loop_v<v066_reach::LoopP>);
static_assert(!fshape::is_loop_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::EndP>);
static_assert(!fshape::is_head_v<v066_reach::LoopP>,
    "umbrella reach: is_head_v = !is_loop_v by substrate definition; "
    "Loop must classify as non-head.");

// 6r-d. Umbrella-level backward-compat reach — Sess.h's using-decls
// must continue to expose `fixy::sess::is_<shape>_v` at the same
// umbrella level the V-065-and-prior code expected.
static_assert(fsess_umbrella::is_send_v<v066_reach::SendP>,
    "umbrella reach: fixy::sess::is_send_v must reach through Sess.h's "
    "backward-compat using-decl after V-066 carve-out.  If this "
    "red-lights, Sess.h's umbrella using-decls were dropped — restore "
    "them or expect every historical call site to break.");
static_assert(fsess_umbrella::is_recv_v<v066_reach::RecvP>);
static_assert(fsess_umbrella::is_select_v<v066_reach::SelectP>);
static_assert(fsess_umbrella::is_offer_v<v066_reach::OfferP>);
static_assert(fsess_umbrella::is_loop_v<v066_reach::LoopP>);
static_assert(fsess_umbrella::is_head_v<v066_reach::SendP>);

// 6r-e. Sub-namespace AND umbrella point at the SAME predicate value.
// If Sess.h ever rewrites the backward-compat using-decls to reach
// substrate directly (bypassing fixy::sess::shape::), the values
// remain identical (substrate is the single source of truth), but
// the structural fact "shape:: is canonical" weakens to "shape:: and
// umbrella both exist".  The static_assert below confirms equivalence
// of resolved values regardless of routing path.
static_assert(fshape::is_send_v<v066_reach::SendP> ==
              fsess_umbrella::is_send_v<v066_reach::SendP>);
static_assert(fshape::is_head_v<v066_reach::LoopP> ==
              fsess_umbrella::is_head_v<v066_reach::LoopP>);

// ─── 6s. SessGlobal.h reach — fixy::sess::mpst:: (FIXY-V-068) ────────
//
// Witness that the MPST global-types surface (End_G / Var_G /
// Transmission / BranchG / Choice / Rec_G / StopG + form predicates +
// role machinery + projection + plain_merge_t) reaches the consumer
// through the umbrella alone after V-068's rename of fixy/Mpst.h →
// fixy/SessGlobal.h (canonical) + thin shim fixy/Mpst.h re-include.
//
// Cells below test:
//   6s-a. Sub-namespace canonical reach via fixy::sess::mpst:: —
//         the namespace IS preserved across the rename (the task is
//         "pure cosmetic file rename" per FIXY-V-068).
//   6s-b. Form predicates reach through the umbrella.
//   6s-c. Cross-binding sentinel: the substrate type fed by the OLD
//         shim path (`<crucible/fixy/Mpst.h>`) is THE SAME type fed
//         by the NEW canonical path (`<crucible/fixy/SessGlobal.h>`);
//         this is the load-bearing rename premise that namespace
//         identity holds.
//   6s-d. Projection metafunction reaches through the umbrella —
//         producing the expected binary-session local type.
//   6s-e. Role-list machinery reaches through the umbrella.
//
// If a future regression strips `#include <crucible/fixy/SessGlobal.h>`
// or `#include <crucible/fixy/Mpst.h>` from Fixy.h's Phase-C block OR
// replaces the Mpst.h shim with a non-re-include form (e.g., a
// duplicated using-decl block, or an inline-namespace alias), one of
// the claims below fails to compile.

namespace fmpst = ::crucible::fixy::sess::mpst;
namespace pproto_v068 = ::crucible::safety::proto;

namespace v068_reach {
struct Alice {};
struct Bob   {};
struct Carol {};
struct Ping  {};
}  // namespace v068_reach

// 6s-a. Sub-namespace canonical reach — constructors land at
//       fixy::sess::mpst::* (preserved namespace identity).
using EndG_via_fixy = fmpst::End_G;
using VarG_via_fixy = fmpst::Var_G;
using Trans_via_fixy =
    fmpst::Transmission<v068_reach::Alice, v068_reach::Bob,
                        v068_reach::Ping, fmpst::End_G>;
using Choice_via_fixy =
    fmpst::Choice<v068_reach::Alice, v068_reach::Bob,
                  fmpst::BranchG<v068_reach::Ping, fmpst::End_G>>;
using RecG_via_fixy = fmpst::Rec_G<fmpst::End_G>;
using StopG_via_fixy = fmpst::StopG<v068_reach::Alice>;

// 6s-b. Form predicates reach through the umbrella.  Variable
//       templates resolve against the umbrella-projected surface
//       exactly as they would against the substrate.
static_assert(fmpst::is_end_g_v<EndG_via_fixy>,
    "umbrella reach: fixy::sess::mpst::is_end_g_v must admit End_G "
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

// 6s-c. Cross-binding sentinel — the load-bearing rename premise.
//
// `<crucible/Fixy.h>` includes BOTH `<crucible/fixy/SessGlobal.h>`
// AND `<crucible/fixy/Mpst.h>` (the shim) in this TU.  The types
// resolved through `fixy::sess::mpst::End_G` MUST be one-and-the-same
// substrate type whether the user spells the include as the
// pre-V-068 path or the post-V-068 path.  If the shim were rewritten
// to redeclare types instead of re-include them, this static_assert
// would red-light.  The check uses substrate-side identity because
// both fixy paths route to the same substrate::safety::proto namespace.
static_assert(std::is_same_v<EndG_via_fixy, pproto_v068::End_G>,
    "umbrella reach: V-068 rename premise broken — fixy::sess::mpst::"
    "End_G must resolve to safety::proto::End_G regardless of whether "
    "the consumer reaches the surface through fixy/SessGlobal.h "
    "(canonical) or fixy/Mpst.h (shim).  If this red-lights, the "
    "rename's 'namespace identity preserved' premise was violated — "
    "audit the shim header and the canonical header for divergent "
    "type declarations.");
static_assert(std::is_same_v<VarG_via_fixy, pproto_v068::Var_G>);
static_assert(std::is_same_v<Trans_via_fixy,
    pproto_v068::Transmission<v068_reach::Alice, v068_reach::Bob,
                              v068_reach::Ping,
                              pproto_v068::End_G>>);

// 6s-d. Projection — fixy::sess::mpst::project_t reaches through the
//       umbrella and lands the binary-session local type at the
//       expected substrate namespace.
using G_AB_v068 =
    fmpst::Transmission<v068_reach::Alice, v068_reach::Bob,
                        v068_reach::Ping, fmpst::End_G>;
using L_Alice_v068 = fmpst::project_t<G_AB_v068, v068_reach::Alice>;
using L_Bob_v068   = fmpst::project_t<G_AB_v068, v068_reach::Bob>;
using L_Carol_v068 = fmpst::project_t<G_AB_v068, v068_reach::Carol>;
static_assert(std::is_same_v<L_Alice_v068,
    pproto_v068::Send<v068_reach::Ping, pproto_v068::End>>,
    "umbrella reach: project_t<G_AB, Alice> must yield Send<Ping, End> "
    "via the umbrella.  If this red-lights, the projection's substrate "
    "wiring is severed at the fixy layer.");
static_assert(std::is_same_v<L_Bob_v068,
    pproto_v068::Recv<v068_reach::Ping, pproto_v068::End>>);
static_assert(std::is_same_v<L_Carol_v068, pproto_v068::End>);

// 6s-e. Role machinery — RoleList + insert_unique_t + roles_of_t
//       reach through the umbrella.
using RL_AB_v068 = fmpst::RoleList<v068_reach::Alice, v068_reach::Bob>;
static_assert(std::is_same_v<
    fmpst::insert_unique_t<v068_reach::Carol, RL_AB_v068>,
    fmpst::RoleList<v068_reach::Carol, v068_reach::Alice, v068_reach::Bob>>,
    "umbrella reach: insert_unique_t must reach + behave identically "
    "through the fixy umbrella.");
static_assert(std::is_same_v<fmpst::EmptyRoleList, fmpst::RoleList<>>);

// 6s-f. has_interaction_between_v reach — fixy::sess::mpst:: (V-169).
//
// `has_interaction_between_v<G, A, B>` was previously buried at
// `safety::proto::detail::global::has_interaction_between_v` — V-169
// lifts it to the substrate-public `safety::proto::` level (parallel
// to plain_merge_t / roles_of_t / project_t) AND re-exports through
// `fixy::sess::mpst::` so production callers (the StopG<Peer, C>
// projection rule and any user of MPST crash-aware projection) reach
// the predicate through the same umbrella as project_t.
//
// Three cells exercise the V-169 lift:
//   (a) positive: Transmission<Alice, Bob, Ping, End> contains an
//       interaction between {Alice, Bob} → predicate yields true.
//   (b) negative: same protocol queried for {Alice, Carol} → predicate
//       yields false (Carol does not appear in any Transmission/Choice).
//   (c) symmetry: query is symmetric over (A, B), so swapping the
//       arguments must yield the same value — the same_unordered_pair
//       discipline at the substrate is the load-bearing invariant.
//   (d) cross-binding: umbrella value == substrate-public value ==
//       substrate-detail value for every queried triple, proving the
//       lift is a pure pass-through (no fresh predicate forked).

using G_AB_Ping_v169 =
    fmpst::Transmission<v068_reach::Alice, v068_reach::Bob,
                        v068_reach::Ping, fmpst::End_G>;

static_assert(fmpst::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>,
    "FIXY-V-169 reach: fixy::sess::mpst::has_interaction_between_v "
    "must admit the (Alice, Bob) pair on Transmission<Alice, Bob, "
    "Ping, End> through the umbrella.  If this red-lights, V-169's "
    "lift dropped or the using-decl in SessGlobal.h was rewritten.");
static_assert(!fmpst::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Carol>,
    "FIXY-V-169 reach: fixy::sess::mpst::has_interaction_between_v "
    "must reject (Alice, Carol) on Transmission<Alice, Bob, Ping, "
    "End> through the umbrella — Carol does not participate.");
// Symmetry — (Alice, Bob) and (Bob, Alice) yield the same value.  The
// substrate's `same_unordered_pair_v` discipline is the single point
// of truth (sessions/SessionGlobal.h:764-767); this cell is the
// umbrella-side regression witness.
static_assert(fmpst::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Bob, v068_reach::Alice>,
    "FIXY-V-169 reach: predicate must be symmetric over (A, B) — "
    "(Bob, Alice) must admit the same protocol that (Alice, Bob) "
    "admits.  If this red-lights, same_unordered_pair_v drifted.");
// Cross-binding — umbrella reach AND substrate-public path AND
// substrate-detail::global:: path must all yield the same value on
// the same query, proving the lift is a pure pass-through chain.
static_assert(fmpst::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>
              == pproto_v068::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>);
static_assert(pproto_v068::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>
              == pproto_v068::detail::global::has_interaction_between_v<
                  G_AB_Ping_v169, v068_reach::Alice, v068_reach::Bob>,
    "FIXY-V-169 cross-binding: substrate-public alias must forward "
    "verbatim to detail::global:: — if this red-lights, the lift "
    "introduced a fresh predicate rather than a pass-through.");
// End / Var_G terminate the walk → no interactions for any (A, B).
static_assert(!fmpst::has_interaction_between_v<
                  fmpst::End_G, v068_reach::Alice, v068_reach::Bob>,
    "FIXY-V-169 reach: End_G admits no interactions through the "
    "umbrella for any role pair.");
static_assert(!fmpst::has_interaction_between_v<
                  fmpst::Var_G, v068_reach::Alice, v068_reach::Bob>,
    "FIXY-V-169 reach: Var_G admits no interactions through the "
    "umbrella for any role pair.");

// ─── 6t. Ctx-fit concept reach — fixy::sess:: (FIXY-V-168) ───────────
//
// Witness that the 5 Ctx-fit concepts moved into fixy::sess:: in V-168
// reach through the umbrella AND evaluate identically to their
// substrate-side counterparts.  Five concepts:
//
//   ProtocolVendorAdmittedByLoopCtx<Proto, LoopCtx>
//   ProtocolEpochAdmittedByLoopCtx<Proto, LoopCtx>
//   ProtocolPermissionedRunnable<Proto>
//   CtxFitsPermissionedProtocol<Proto, Ctx, InitialPS, LoopCtx = ...>
//   CtxFitsChannel<Proto, CtxA, CtxB>
//
// Coverage matrix per concept:
//   (a) substrate-side baseline — a concrete value that the substrate
//       admits (positive) and one it rejects (negative) under the
//       same arguments.
//   (b) umbrella-side reach — the SAME arguments evaluated through
//       `fixy::sess::*`, witnessing that the using-decl forwards
//       structurally.
//   (c) cross-binding sentinel — `fixy::sess::Concept<args>` value
//       equals `safety::proto::Concept<args>` value for both the
//       admitted and rejected cell.  Failure here means the using-
//       decl was inadvertently rewritten to introduce a fresh
//       concept (e.g. accidental renaming during a future carve-out).
//
// Pre-V-168, every cell that resolved through fixy::sess::CtxFits*
// failed to compile because the symbol did not exist at the umbrella
// level — production callers had to reach `::crucible::safety::proto::`
// directly, breaking §XVI's "fixy::sess:: is structurally complete"
// promise.  Post-V-168 the cells pass and the umbrella covers the
// concept gate at every mint-factory entry point.

namespace v168_reach {
struct PayloadProbe {};
using EndP   = pproto::End;
using SendP  = pproto::Send<PayloadProbe, EndP>;
// Empty-Offer is structurally not runnable per substrate's fixy-CR-15
// specialization — the canonical negative witness for
// ProtocolPermissionedRunnable.
using EmptyOfferP = pproto::Offer<>;
// Non-IsExecCtx probe — drives every CtxFits* rejection cell.
struct NonExecCtxProbe {};
// Witness Ctx — TestRunnerCtx carries Row<Test, Alloc, IO, Block>
// which is a superrow of Row<> (the row engaged by End).
using FittingCtx = ::crucible::effects::TestRunnerCtx;
}  // namespace v168_reach

namespace fsess_v168       = ::crucible::fixy::sess;
namespace pproto_v168      = ::crucible::safety::proto;

// 6t-a. ProtocolVendorAdmittedByLoopCtx — End admits, umbrella reach,
//       cross-binding sentinel.
static_assert(pproto_v168::ProtocolVendorAdmittedByLoopCtx<
                  v168_reach::EndP, void>,
    "FIXY-V-168 baseline: substrate ProtocolVendorAdmittedByLoopCtx "
    "must admit End under the no-LoopCtx (void) sentinel.");
static_assert(fsess_v168::ProtocolVendorAdmittedByLoopCtx<
                  v168_reach::EndP, void>,
    "FIXY-V-168 reach: fixy::sess::ProtocolVendorAdmittedByLoopCtx "
    "must admit the same baseline through the umbrella.  If this "
    "red-lights, the using-decl in fixy/Sess.h was dropped.");
static_assert(fsess_v168::ProtocolVendorAdmittedByLoopCtx<
                  v168_reach::EndP, void>
              == pproto_v168::ProtocolVendorAdmittedByLoopCtx<
                  v168_reach::EndP, void>,
    "FIXY-V-168 cross-binding: umbrella concept value must equal "
    "substrate concept value on identical arguments.");

// 6t-b. ProtocolEpochAdmittedByLoopCtx — same triplet, End admits.
static_assert(pproto_v168::ProtocolEpochAdmittedByLoopCtx<
                  v168_reach::EndP, void>);
static_assert(fsess_v168::ProtocolEpochAdmittedByLoopCtx<
                  v168_reach::EndP, void>,
    "FIXY-V-168 reach: fixy::sess::ProtocolEpochAdmittedByLoopCtx "
    "must admit End under the no-LoopCtx (void) sentinel through "
    "the umbrella.");
static_assert(fsess_v168::ProtocolEpochAdmittedByLoopCtx<
                  v168_reach::EndP, void>
              == pproto_v168::ProtocolEpochAdmittedByLoopCtx<
                  v168_reach::EndP, void>);

// 6t-c. ProtocolPermissionedRunnable — End admits, Send<Probe, End>
//       admits (recurses into K), Offer<> rejects (fixy-CR-15).
static_assert(pproto_v168::ProtocolPermissionedRunnable<
                  v168_reach::EndP>,
    "FIXY-V-168 baseline: substrate ProtocolPermissionedRunnable "
    "must admit End.");
static_assert(pproto_v168::ProtocolPermissionedRunnable<
                  v168_reach::SendP>,
    "FIXY-V-168 baseline: substrate ProtocolPermissionedRunnable "
    "must admit Send<Probe, End> via tail recursion.");
static_assert(!pproto_v168::ProtocolPermissionedRunnable<
                  v168_reach::EmptyOfferP>,
    "FIXY-V-168 baseline: substrate ProtocolPermissionedRunnable "
    "must reject empty Offer<> (fixy-CR-15: empty branch packs are "
    "structurally not runnable — guards against vacuous-truth admission "
    "of unrunnable protocols at the channel mint boundary).");
static_assert(fsess_v168::ProtocolPermissionedRunnable<
                  v168_reach::EndP>,
    "FIXY-V-168 reach: fixy::sess::ProtocolPermissionedRunnable "
    "must admit End through the umbrella.");
static_assert(fsess_v168::ProtocolPermissionedRunnable<
                  v168_reach::SendP>,
    "FIXY-V-168 reach: fixy::sess::ProtocolPermissionedRunnable "
    "must admit Send<Probe, End> through the umbrella.");
static_assert(!fsess_v168::ProtocolPermissionedRunnable<
                  v168_reach::EmptyOfferP>,
    "FIXY-V-168 reach: fixy::sess::ProtocolPermissionedRunnable "
    "must reject empty Offer<> through the umbrella.");
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::EndP>
              == pproto_v168::ProtocolPermissionedRunnable<v168_reach::EndP>);
static_assert(fsess_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>
              == pproto_v168::ProtocolPermissionedRunnable<v168_reach::EmptyOfferP>);

// 6t-d. CtxFitsPermissionedProtocol — fitting Ctx + EmptyPermSet
//       admits End; non-IsExecCtx Ctx rejects.
static_assert(pproto_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  fsess_v168::EmptyPermSet>,
    "FIXY-V-168 baseline: substrate CtxFitsPermissionedProtocol "
    "must admit End under TestRunnerCtx + EmptyPermSet — the row "
    "engaged by End is Row<> ⊆ Row<Test, Alloc, IO, Block>, and the "
    "empty PermSet trivially closes.");
static_assert(!pproto_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  fsess_v168::EmptyPermSet>,
    "FIXY-V-168 baseline: substrate CtxFitsPermissionedProtocol "
    "must reject a non-IsExecCtx Ctx argument (CtxFitsProtocol's "
    "IsExecCtx clause must fire first, before any row check).");
static_assert(fsess_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  fsess_v168::EmptyPermSet>,
    "FIXY-V-168 reach: fixy::sess::CtxFitsPermissionedProtocol "
    "must admit the same fitting (End, TestRunnerCtx, EmptyPermSet) "
    "triple through the umbrella.");
static_assert(!fsess_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  fsess_v168::EmptyPermSet>,
    "FIXY-V-168 reach: fixy::sess::CtxFitsPermissionedProtocol "
    "must reject the same non-IsExecCtx Ctx through the umbrella.");
static_assert(fsess_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  fsess_v168::EmptyPermSet>
              == pproto_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  fsess_v168::EmptyPermSet>,
    "FIXY-V-168 cross-binding: umbrella + substrate must agree on "
    "the fitting cell.");
static_assert(fsess_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  fsess_v168::EmptyPermSet>
              == pproto_v168::CtxFitsPermissionedProtocol<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  fsess_v168::EmptyPermSet>);

// 6t-e. CtxFitsChannel — both endpoints fitting; non-IsExecCtx
//       rejects.  End is self-dual at the structural level under
//       sessions/SessionMint.h's dual_of_t<End> = End, so both
//       endpoints can carry End under TestRunnerCtx.
static_assert(pproto_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  v168_reach::FittingCtx>,
    "FIXY-V-168 baseline: substrate CtxFitsChannel must admit "
    "(End, TestRunnerCtx, TestRunnerCtx) — End is self-dual, "
    "EmptyPermSet closes on both endpoints.");
static_assert(!pproto_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  v168_reach::FittingCtx>,
    "FIXY-V-168 baseline: substrate CtxFitsChannel must reject "
    "when either endpoint's Ctx is not an IsExecCtx (the per-endpoint "
    "CtxFitsPermissionedProtocol gate fires).");
static_assert(fsess_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  v168_reach::FittingCtx>,
    "FIXY-V-168 reach: fixy::sess::CtxFitsChannel must admit the "
    "same baseline through the umbrella.");
static_assert(!fsess_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::NonExecCtxProbe,
                  v168_reach::FittingCtx>,
    "FIXY-V-168 reach: fixy::sess::CtxFitsChannel must reject the "
    "same non-IsExecCtx endpoint through the umbrella.");
static_assert(fsess_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  v168_reach::FittingCtx>
              == pproto_v168::CtxFitsChannel<
                  v168_reach::EndP,
                  v168_reach::FittingCtx,
                  v168_reach::FittingCtx>);

// Every claim above is consteval; main() exists so the runner can
// link the TU as a stand-alone executable.
int main() { return 0; }
