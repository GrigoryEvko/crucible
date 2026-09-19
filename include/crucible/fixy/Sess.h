#pragma once

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/permissions/_PermSet.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Diagnostic.h>
#include <crucible/safety/diag/RowMismatch.h>
#include <crucible/sessions/FederationProtocol.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPatterns.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionPhi.h>
#include <crucible/sessions/SessionView.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::fixy::sess {

using ::crucible::safety::proto::Send;
using ::crucible::safety::proto::Recv;
using ::crucible::safety::proto::Select;
using ::crucible::safety::proto::Offer;
using ::crucible::safety::proto::Sender;
using ::crucible::safety::proto::AnonymousPeer;
using ::crucible::safety::proto::Loop;
using ::crucible::safety::proto::Continue;
using ::crucible::safety::proto::End;
using ::crucible::safety::proto::VendorPinned;

using ::crucible::safety::proto::Stop_g;
using ::crucible::safety::proto::Stop;
using ::crucible::safety::proto::CrashClass;

using ::crucible::safety::proto::Delegate;
using ::crucible::safety::proto::Accept;
using ::crucible::safety::proto::EpochedDelegate;
using ::crucible::safety::proto::EpochedAccept;

using ::crucible::safety::proto::EpochCtx;

using ::crucible::safety::proto::CheckpointedSession;

}  // namespace crucible::fixy::sess

#include <crucible/fixy/SessShape.h>

namespace crucible::fixy::sess {

using ::crucible::fixy::sess::shape::is_send_v;
using ::crucible::fixy::sess::shape::is_recv_v;
using ::crucible::fixy::sess::shape::is_select_v;
using ::crucible::fixy::sess::shape::is_offer_v;
using ::crucible::fixy::sess::shape::is_loop_v;
using ::crucible::fixy::sess::shape::is_head_v;

using ::crucible::safety::proto::PermSet;
using ::crucible::safety::proto::EmptyPermSet;
using ::crucible::safety::proto::perm_set_contains_v;
using ::crucible::safety::proto::perm_set_insert;
using ::crucible::safety::proto::perm_set_insert_t;
using ::crucible::safety::proto::perm_set_remove;
using ::crucible::safety::proto::perm_set_remove_t;
using ::crucible::safety::proto::perm_set_subset_v;
using ::crucible::safety::proto::perm_set_disjoint_v;

using ::crucible::safety::proto::Transferable;
using ::crucible::safety::proto::Borrowed;
using ::crucible::safety::proto::Returned;
using ::crucible::safety::proto::DelegatedSession;

using ::crucible::safety::proto::is_transferable_v;
using ::crucible::safety::proto::is_borrowed_v;
using ::crucible::safety::proto::is_returned_v;
using ::crucible::safety::proto::is_delegated_session_v;

// Each of these forwards to a predicate that decides its property by
// structural refutation: it looks for a deadlock witness, a non-terminating
// loop body, a dead-branch payload duplicate.  So each one is sound and
// conservatively incomplete.  A `false` here means the protocol was not
// shown to have the property, not that it lacks it.

template <typename P>
inline constexpr bool phi_safe_v = ::crucible::safety::proto::phi_safe_v<P>;

template <typename P>
inline constexpr bool phi_df_v = ::crucible::safety::proto::phi_df_v<P>;

template <typename P>
inline constexpr bool phi_term_v = ::crucible::safety::proto::phi_term_v<P>;

template <typename P>
inline constexpr bool phi_nterm_v = ::crucible::safety::proto::phi_nterm_v<P>;

template <typename P>
inline constexpr bool phi_live_v = ::crucible::safety::proto::phi_live_v<P>;

template <typename P>
inline constexpr bool phi_live_plus_v = ::crucible::safety::proto::phi_live_plus_v<P>;

template <typename P>
inline constexpr bool phi_live_pp_v = ::crucible::safety::proto::phi_live_pp_v<P>;

using ::crucible::safety::proto::is_well_formed_v;
using ::crucible::safety::proto::is_terminal_state_v;
using ::crucible::safety::proto::is_dual_v;

// These five are the gates the mint factories below check before returning a
// handle.  They are re-exported so a caller can name the gate it failed in
// its own `static_assert`, in the same namespace it reached the factory
// through, rather than past this one.

using ::crucible::safety::proto::ProtocolVendorAdmittedByLoopCtx;
using ::crucible::safety::proto::ProtocolEpochAdmittedByLoopCtx;
using ::crucible::safety::proto::ProtocolPermissionedRunnable;
using ::crucible::safety::proto::CtxFitsPermissionedProtocol;
using ::crucible::safety::proto::CtxFitsChannel;

// The cells below instantiate each concept rather than merely naming it.  A
// name that resolves proves nothing: only evaluating the predicate body
// shows the re-export reaches the substrate concept and not some fresh
// same-named one.

namespace fixy_v168_sentinel_probes {
struct NonExecCtxProbe {};
}  // namespace fixy_v168_sentinel_probes

static_assert(!CtxFitsPermissionedProtocol<::crucible::safety::proto::End, fixy_v168_sentinel_probes::NonExecCtxProbe,
                                           EmptyPermSet>,
              "FIXY-V-168: fixy::sess::CtxFitsPermissionedProtocol must "
              "reject a non-IsExecCtx Ctx argument through the umbrella.  If "
              "this red-lights, the using-decl above is either missing or "
              "resolves to a different substrate concept.");

static_assert(!CtxFitsChannel<::crucible::safety::proto::End, fixy_v168_sentinel_probes::NonExecCtxProbe,
                              fixy_v168_sentinel_probes::NonExecCtxProbe>,
              "FIXY-V-168: fixy::sess::CtxFitsChannel must reject "
              "non-IsExecCtx CtxA/CtxB through the umbrella.");

namespace fixy_v168_sentinel_probes {
struct PayloadProbe {};
using SendEndProbe = ::crucible::safety::proto::Send<PayloadProbe, ::crucible::safety::proto::End>;
}  // namespace fixy_v168_sentinel_probes

static_assert(ProtocolPermissionedRunnable<::crucible::safety::proto::End>,
              "FIXY-V-168: fixy::sess::ProtocolPermissionedRunnable must "
              "admit the End terminal protocol through the umbrella.");
static_assert(ProtocolPermissionedRunnable<fixy_v168_sentinel_probes::SendEndProbe>,
              "FIXY-V-168: fixy::sess::ProtocolPermissionedRunnable must "
              "admit Send<Probe, End> through the umbrella.");

// `void` is the LoopCtx a protocol carries when it declares no vendor or
// epoch position, so it exercises these two concept bodies without a
// fully-formed context fixture.
static_assert(ProtocolVendorAdmittedByLoopCtx<::crucible::safety::proto::End, void>,
              "FIXY-V-168: fixy::sess::ProtocolVendorAdmittedByLoopCtx must "
              "admit End under the no-LoopCtx (void) sentinel through the "
              "umbrella.");
static_assert(ProtocolEpochAdmittedByLoopCtx<::crucible::safety::proto::End, void>,
              "FIXY-V-168: fixy::sess::ProtocolEpochAdmittedByLoopCtx must "
              "admit End under the no-LoopCtx (void) sentinel through the "
              "umbrella.");

using ::crucible::safety::proto::RecordingSessionHandle;
using ::crucible::safety::proto::CrashWatchedHandle;

// The mint factories below return these by value, so a caller that wants to
// name a return type reaches the carrier here rather than past this
// namespace.

using ::crucible::safety::proto::SessionHandleBase;
using ::crucible::safety::proto::SessionHandle;
using ::crucible::safety::proto::PermissionedSessionHandle;

template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>
using PSH = ::crucible::safety::proto::PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>;

// The message a `= delete("...")` declaration emits is unclassified: it does
// not satisfy the diagnostic-class trait and no name lookup reaches it.  This
// tag gives the deletion below that structured surface.  It stays out of the
// closed foundation catalog, which is append-only and coordinated, and is a
// local tag instead.

namespace diag {

struct FixyMintSessionRemoved final : ::crucible::safety::diag::tag_base {
    static constexpr ::std::string_view name = "FixyMintSessionRemoved";
    static constexpr ::std::string_view description = "Bare `mint_session<Proto>(ctx, resource)` and "
                                                      "`mint_session<Proto>(resource)` are `= delete`d in "
                                                      "sessions/SessionMint.h.  Production code constructs typed "
                                                      "session handles via `mint_permissioned_session<Proto>(ctx, "
                                                      "resource, perms...)`, which threads CSL Permission tokens "
                                                      "through the protocol's position so the local row-flow "
                                                      "closure check fires per FOUND-C v1.  The bare mint_session "
                                                      "spelling was structurally unable to carry the permission "
                                                      "evolution and was removed.";
    static constexpr ::std::string_view remediation = "Replace the call site with "
                                                      "`mint_permissioned_session<Proto>(ctx, resource, perms...)`.  "
                                                      "For protocols that do not transfer wire-level permissions, "
                                                      "spell the call as "
                                                      "`mint_permissioned_session<Proto>(ctx, resource)` — the "
                                                      "perms pack is variadic and the empty-PermSet shim is the "
                                                      "current default surface (sessions/SessionMint.h:935).";
};

static_assert(::crucible::safety::diag::is_diagnostic_class_v<FixyMintSessionRemoved>,
              "FixyMintSessionRemoved must inherit safety::diag::tag_base.");

}  // namespace diag

// Both `mint_session` overloads are deleted at the definition site.  The
// deleted declarations are re-exported anyway so that a stale call site
// reaching them through this namespace gets the deletion diagnostic rather
// than a name-lookup failure.  A match on `mint_session` here is therefore a
// pointer to the deletion, never an authorization point.

using ::crucible::safety::proto::mint_session;
using ::crucible::safety::proto::mint_permissioned_session;
using ::crucible::safety::proto::mint_channel;
using ::crucible::safety::proto::mint_session_handle;
using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::mint_crash_watched_session;
// The companion probe concept for this mint is test-only and deliberately
// not re-exported.  A caller asking whether a handle can be viewed writes
// `requires { mint_session_view<Tag>(h); }` at the call site instead.
using ::crucible::safety::proto::mint_session_view;

// This mint returns a session handle, so it is reachable here as well as
// from the substrate-side namespace that holds the per-substrate mint
// families.  Both spellings resolve to one symbol.
using ::crucible::concurrent::mint_substrate_session;

}  // namespace crucible::fixy::sess

#include <crucible/fixy/SessFederation.h>

namespace crucible::fixy::sess {

// A namespace alias rather than per-name using-declarations, so the set of
// reachable patterns tracks the substrate without an edit here.  The
// crash-aware refinements are not reachable this way: they live in the
// substrate's own self-test scope and are not user-facing.

namespace pattern = ::crucible::safety::proto::pattern;

namespace self_test {

static_assert(std::is_same_v<Send<int, End>, ::crucible::safety::proto::Send<int, End>>,
              "fixy::sess::Send alias must be identical to safety::proto::Send.");

static_assert(std::is_same_v<Recv<int, End>, ::crucible::safety::proto::Recv<int, End>>,
              "fixy::sess::Recv alias must be identical to safety::proto::Recv.");

static_assert(std::is_same_v<Loop<End>, ::crucible::safety::proto::Loop<End>>,
              "fixy::sess::Loop alias must be identical to safety::proto::Loop.");

static_assert(std::is_same_v<Stop, ::crucible::safety::proto::Stop>,
              "fixy::sess::Stop alias must be identical to safety::proto::Stop.");

static_assert(std::is_same_v<End, ::crucible::safety::proto::End>,
              "fixy::sess::End alias must be identical to safety::proto::End.");

static_assert(std::is_same_v<Continue, ::crucible::safety::proto::Continue>,
              "fixy::sess::Continue alias must be identical to safety::proto::Continue.");

static_assert(
    std::is_same_v<
        EpochedDelegate<Send<int, End>, End, 0, 0>,
        ::crucible::safety::proto::EpochedDelegate<::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                                                   ::crucible::safety::proto::End, 0, 0>>,
    "fixy::sess::EpochedDelegate alias must be identical to "
    "safety::proto::EpochedDelegate.");

// Every re-export above gets an identity assertion here, so a substrate-side
// rename breaks the build at this header rather than in some consumer.

static_assert(std::is_same_v<EpochCtx<5, 3>, ::crucible::safety::proto::EpochCtx<5, 3>>,
              "fixy::sess::EpochCtx alias must be identical to safety::proto::EpochCtx.");

static_assert(
    is_send_v<Send<int, End>>
        == ::crucible::safety::proto::is_send_v<::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>>,
    "fixy::sess::is_send_v must mirror safety::proto::is_send_v.");

static_assert(
    is_recv_v<Recv<int, End>>
        == ::crucible::safety::proto::is_recv_v<::crucible::safety::proto::Recv<int, ::crucible::safety::proto::End>>,
    "fixy::sess::is_recv_v must mirror safety::proto::is_recv_v.");

static_assert(
    is_loop_v<Loop<End>>
        == ::crucible::safety::proto::is_loop_v<::crucible::safety::proto::Loop<::crucible::safety::proto::End>>,
    "fixy::sess::is_loop_v must mirror safety::proto::is_loop_v.");

static_assert(std::is_same_v<EmptyPermSet, ::crucible::safety::proto::EmptyPermSet>,
              "fixy::sess::EmptyPermSet alias must be identical to "
              "safety::proto::EmptyPermSet.");

namespace u012_permset_witness {
struct TagA {};
struct TagB {};
using PS = PermSet<TagA>;
using PS2 = perm_set_insert_t<PS, TagB>;
using PS3 = perm_set_remove_t<PS2, TagA>;
static_assert(perm_set_contains_v<PS, TagA>, "fixy::sess::perm_set_contains_v must agree with substrate.");
static_assert(!perm_set_contains_v<PS, TagB>, "fixy::sess::perm_set_contains_v must reject missing tag.");
static_assert(perm_set_subset_v<PS, PS2>, "fixy::sess::perm_set_subset_v must accept PS ⊆ PS ∪ {TagB}.");
static_assert(perm_set_disjoint_v<PermSet<TagA>, PermSet<TagB>>,
              "fixy::sess::perm_set_disjoint_v must accept disjoint PermSets.");
static_assert(std::is_same_v<PS3, PermSet<TagB>>, "fixy::sess::perm_set_remove_t<PS+TagB, TagA> must yield {TagB}.");
}  // namespace u012_permset_witness

static_assert(std::is_same_v<Transferable<int, u012_permset_witness::TagA>,
                             ::crucible::safety::proto::Transferable<int, u012_permset_witness::TagA>>,
              "fixy::sess::Transferable alias must be identical to "
              "safety::proto::Transferable.");

static_assert(std::is_same_v<Borrowed<int, u012_permset_witness::TagA>,
                             ::crucible::safety::proto::Borrowed<int, u012_permset_witness::TagA>>,
              "fixy::sess::Borrowed alias must be identical to "
              "safety::proto::Borrowed.");

static_assert(std::is_same_v<Returned<int, u012_permset_witness::TagA>,
                             ::crucible::safety::proto::Returned<int, u012_permset_witness::TagA>>,
              "fixy::sess::Returned alias must be identical to "
              "safety::proto::Returned.");

static_assert(is_transferable_v<Transferable<int, u012_permset_witness::TagA>>,
              "fixy::sess::is_transferable_v must accept Transferable<T, Tag>.");
static_assert(is_borrowed_v<Borrowed<int, u012_permset_witness::TagA>>,
              "fixy::sess::is_borrowed_v must accept Borrowed<T, Tag>.");
static_assert(is_returned_v<Returned<int, u012_permset_witness::TagA>>,
              "fixy::sess::is_returned_v must accept Returned<T, Tag>.");
static_assert(!is_transferable_v<int>, "fixy::sess::is_transferable_v must reject plain T.");

static_assert(std::is_same_v<SessionHandleBase<Send<int, End>>,
                             ::crucible::safety::proto::SessionHandleBase<
                                 ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>>>,
              "fixy::sess::SessionHandleBase alias must be identical to "
              "safety::proto::SessionHandleBase.");

static_assert(std::is_same_v<PSH<Send<int, End>, EmptyPermSet, int>,
                             ::crucible::safety::proto::PermissionedSessionHandle<
                                 ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                                 ::crucible::safety::proto::EmptyPermSet, int, void>>,
              "fixy::sess::PSH alias must yield "
              "safety::proto::PermissionedSessionHandle<Proto, PS, Resource, void>.");

}  // namespace self_test

// A function body, not more namespace-scope assertions, because the compiler
// must walk every using-declaration to compile one.  Namespace-scope
// assertions alone take a path that leaves most of them uninstantiated.

inline void runtime_smoke_test() noexcept {
    struct WitnessTag {};
    using PS0 = EmptyPermSet;
    using PS1 = perm_set_insert_t<PS0, WitnessTag>;
    using PS2 = perm_set_remove_t<PS1, WitnessTag>;
    static_assert(std::is_same_v<PS0, PS2>, "fixy::sess::runtime_smoke_test: insert+remove round-trips.");
    static_assert(perm_set_contains_v<PS1, WitnessTag>, "fixy::sess::runtime_smoke_test: contains after insert.");
    static_assert(!perm_set_contains_v<PS0, WitnessTag>,
                  "fixy::sess::runtime_smoke_test: empty PermSet contains nothing.");

    // The payload markers carry a non-default-constructible token field, so
    // the predicates are exercised instead of an instance.
    static_assert(is_transferable_v<Transferable<int, WitnessTag>>,
                  "fixy::sess::runtime_smoke_test: is_transferable_v accepts marker.");
    static_assert(is_borrowed_v<Borrowed<int, WitnessTag>>,
                  "fixy::sess::runtime_smoke_test: is_borrowed_v accepts marker.");
    static_assert(is_returned_v<Returned<int, WitnessTag>>,
                  "fixy::sess::runtime_smoke_test: is_returned_v accepts marker.");
    static_assert(!is_transferable_v<int>, "fixy::sess::runtime_smoke_test: is_transferable_v rejects plain T.");

    static_assert(is_send_v<Send<int, End>>, "fixy::sess::runtime_smoke_test: is_send_v identifies Send head.");
    static_assert(!is_send_v<End>, "fixy::sess::runtime_smoke_test: is_send_v rejects End.");
    static_assert(is_loop_v<Loop<End>>, "fixy::sess::runtime_smoke_test: is_loop_v identifies Loop head.");
}

}  // namespace crucible::fixy::sess
