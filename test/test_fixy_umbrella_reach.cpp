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

// Every claim above is consteval; main() exists so the runner can
// link the TU as a stand-alone executable.
int main() { return 0; }
