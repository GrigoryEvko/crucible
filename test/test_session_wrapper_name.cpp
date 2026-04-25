// Compile-time + runtime witnesses for #429: per-derived-class
// abandonment diagnostic in SessionHandleBase.
//
// SessionHandleBase<Proto, Derived> uses C++26 P2996 reflection
// (std::meta::identifier_of(template_of(^^Derived))) to spell the
// DERIVED wrapper class's bare template name in its destructor abort
// message.  Without #429 the diagnostic printed only Proto, leaving
// the developer unable to tell SessionHandle's abort from
// CrashWatchedHandle's abort from RecordingSessionHandle's abort when
// they share the same Proto template argument — the exact ambiguity
// that cost real debugging cycles in #400.
//
// This test verifies the new public `wrapper_name()` static accessor
// returns the EXPECTED bare template name for each wrapper type.
// Coverage:
//   * SessionHandle — the canonical wrapper
//   * CrashWatchedHandle — #400's crash-aware wrapper
//   * RecordingSessionHandle — #404's event-log wrapper
//
// Each check is a static_assert (zero runtime cost) AND a runtime
// print (so the test harness records "PASSED" with visible evidence
// of the rendered names).

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/Session.h>

#include <cstdio>
#include <string_view>

namespace {

using namespace crucible::safety::proto;

struct FakeRes {};
struct ServerPeer {};

// ── 1. Bare SessionHandle spells "SessionHandle" ─────────────────

using HEnd = SessionHandle<End, FakeRes>;
static_assert(HEnd::wrapper_name() == "SessionHandle",
    "SessionHandle<End, FakeRes>::wrapper_name() must spell "
    "\"SessionHandle\" — the bare template name extracted via "
    "std::meta::identifier_of(template_of(^^Derived)).");

using HSend = SessionHandle<Send<int, End>, FakeRes>;
static_assert(HSend::wrapper_name() == "SessionHandle");

using HRecv = SessionHandle<Recv<int, End>, FakeRes>;
static_assert(HRecv::wrapper_name() == "SessionHandle");

using HSelect = SessionHandle<Select<Send<int, End>, Send<float, End>>, FakeRes>;
static_assert(HSelect::wrapper_name() == "SessionHandle");

using HOffer = SessionHandle<Offer<Recv<int, End>, Recv<float, End>>, FakeRes>;
static_assert(HOffer::wrapper_name() == "SessionHandle");

// ── 2. CrashWatchedHandle spells "CrashWatchedHandle" ────────────
//
// THE load-bearing case.  Pre-#429, CrashWatchedHandle<Recv<int, End>,
// FakeRes, ServerPeer> aborting at non-terminal state printed an abort
// message indistinguishable from SessionHandle<Recv<int, End>, FakeRes>
// aborting at non-terminal state — both inherit SessionHandleBase
// <Recv<int, End>>.  The reflection-based wrapper_name() tells them
// apart at compile time AND at the abort message text.

using CWEnd = CrashWatchedHandle<End, FakeRes, ServerPeer>;
static_assert(CWEnd::wrapper_name() == "CrashWatchedHandle",
    "CrashWatchedHandle<End, FakeRes, ServerPeer>::wrapper_name() "
    "must spell \"CrashWatchedHandle\" — distinct from "
    "SessionHandle<End, FakeRes>::wrapper_name() == \"SessionHandle\" "
    "even though both inherit SessionHandleBase<End>.  This is the "
    "exact disambiguation #429 was added to provide.");

using CWSend = CrashWatchedHandle<Send<int, End>, FakeRes, ServerPeer>;
static_assert(CWSend::wrapper_name() == "CrashWatchedHandle");

using CWRecv = CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>;
static_assert(CWRecv::wrapper_name() == "CrashWatchedHandle");

using CWSelect = CrashWatchedHandle<Select<Send<int, End>, Send<float, End>>,
                                    FakeRes, ServerPeer>;
static_assert(CWSelect::wrapper_name() == "CrashWatchedHandle");

using CWOffer = CrashWatchedHandle<Offer<Recv<int, End>, Recv<float, End>>,
                                   FakeRes, ServerPeer>;
static_assert(CWOffer::wrapper_name() == "CrashWatchedHandle");

// ── 3. RecordingSessionHandle spells "RecordingSessionHandle" ────

using REnd = RecordingSessionHandle<End, FakeRes>;
static_assert(REnd::wrapper_name() == "RecordingSessionHandle",
    "RecordingSessionHandle<End, FakeRes>::wrapper_name() must spell "
    "\"RecordingSessionHandle\" — distinct from SessionHandle and "
    "CrashWatchedHandle even on the same Proto.");

using RSend = RecordingSessionHandle<Send<int, End>, FakeRes>;
static_assert(RSend::wrapper_name() == "RecordingSessionHandle");

using RRecv = RecordingSessionHandle<Recv<int, End>, FakeRes>;
static_assert(RRecv::wrapper_name() == "RecordingSessionHandle");

using RSelect = RecordingSessionHandle<Select<Send<int, End>, Send<float, End>>, FakeRes>;
static_assert(RSelect::wrapper_name() == "RecordingSessionHandle");

using ROffer = RecordingSessionHandle<Offer<Recv<int, End>, Recv<float, End>>, FakeRes>;
static_assert(ROffer::wrapper_name() == "RecordingSessionHandle");

// ── 4. The same-Proto disambiguation property ────────────────────
//
// All three wrappers can wrap the SAME protocol; pre-#429 their
// abandonment diagnostics rendered identically.  The compile-time
// witness below proves the new wrapper_name() distinguishes them.

static_assert(SessionHandle<Recv<int, End>, FakeRes>::wrapper_name()
              != CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>::wrapper_name(),
    "Same Proto + different wrapper class → wrapper_name() MUST "
    "differ.  This was the exact ambiguity #429 fixed.");

static_assert(SessionHandle<Recv<int, End>, FakeRes>::wrapper_name()
              != RecordingSessionHandle<Recv<int, End>, FakeRes>::wrapper_name(),
    "Same Proto + different wrapper class → wrapper_name() MUST "
    "differ.");

static_assert(CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>::wrapper_name()
              != RecordingSessionHandle<Recv<int, End>, FakeRes>::wrapper_name(),
    "Same Proto + different wrapper class → wrapper_name() MUST "
    "differ.");

// ── 5. protocol_name() still works alongside wrapper_name() ──────
//
// The existing #379 protocol_name() accessor is preserved.  Both can
// be queried independently — wrapper_name() spells the wrapper class,
// protocol_name() spells the Proto template argument.

static_assert(HEnd::protocol_name().find("End") != std::string_view::npos,
    "protocol_name() must still render the Proto's shape.");

static_assert(CWRecv::protocol_name().find("Recv") != std::string_view::npos,
    "protocol_name() works identically across wrapper classes.");

// ── 6. next_method_hint() — protocol-head-driven action suggestion (B)
//
// Each non-terminal protocol head maps to a verb-phrase naming the
// &&-qualified consumer method that would have advanced the protocol.
// The destructor uses this to surface "Expected: call .recv()" /
// "Expected: call .send(value)" / etc.  User code can also query
// directly for runtime crash-handler messages.

static_assert(HSend::next_method_hint().find("send") != std::string_view::npos,
    "Send<T, K>::next_method_hint() must mention 'send'.");

static_assert(HRecv::next_method_hint().find("recv") != std::string_view::npos,
    "Recv<T, K>::next_method_hint() must mention 'recv'.");

static_assert(HSelect::next_method_hint().find("pick") != std::string_view::npos,
    "Select<Bs...>::next_method_hint() must mention 'pick'.");

static_assert(HOffer::next_method_hint().find("branch") != std::string_view::npos
              || HOffer::next_method_hint().find("pick") != std::string_view::npos,
    "Offer<Bs...>::next_method_hint() must mention 'branch' or 'pick'.");

// CrashWatchedHandle inherits the hint via SessionHandleBase — same
// Proto, same hint.
static_assert(CWRecv::next_method_hint() == HRecv::next_method_hint(),
    "Hints depend on Proto only, not on the wrapper class.");

// ── 7. full_handle_type_name() — full Derived spelling (A) ───────
//
// Returns the COMPLETE template instantiation including Resource,
// PeerTag, LoopCtx, etc.  This is what the destructor uses for the
// "Full handle type:" line — distinguishes two CrashWatchedHandles
// for different PeerTags wrapping the same Proto.

static_assert(CWRecv::full_handle_type_name().find("CrashWatchedHandle")
              != std::string_view::npos,
    "Full type spelling must contain the wrapper template name.");

static_assert(CWRecv::full_handle_type_name().find("ServerPeer")
              != std::string_view::npos,
    "Full type spelling MUST carry PeerTag — that is the load-bearing "
    "info that distinguishes two CrashWatchedHandles for different "
    "peers wrapping the same Proto.  Pre-#429 improvement A this was "
    "missing from the abandonment diagnostic.");

static_assert(REnd::full_handle_type_name().find("RecordingSessionHandle")
              != std::string_view::npos,
    "Full type spelling must distinguish RecordingSessionHandle from "
    "SessionHandle, even on the same Proto.");

}  // anonymous namespace

int main() {
    // Print the rendered wrapper + protocol names so a human reading
    // the test harness output sees the diagnostic strings the
    // destructor would print on abandonment.

    // Print every diagnostic field for one example of each wrapper
    // class, in the same shape as the destructor's structured message.

    auto dump = [](std::string_view label,
                   std::string_view wrapper,
                   std::string_view full,
                   std::string_view proto,
                   std::string_view hint) {
        std::printf("session_wrapper_name: %.*s\n",
                    static_cast<int>(label.size()), label.data());
        std::printf("    Wrapper class:    %.*s\n",
                    static_cast<int>(wrapper.size()), wrapper.data());
        std::printf("    Full handle type: %.*s\n",
                    static_cast<int>(full.size()), full.data());
        std::printf("    Protocol head:    %.*s\n",
                    static_cast<int>(proto.size()), proto.data());
        std::printf("    Expected action:  call .%.*s\n",
                    static_cast<int>(hint.size()), hint.data());
    };

    dump("SessionHandle<End>",
         HEnd::wrapper_name(),
         HEnd::full_handle_type_name(),
         HEnd::protocol_name(),
         HEnd::next_method_hint());

    dump("CrashWatchedHandle<Recv<int, End>, MyChannel, ServerPeer>",
         CWRecv::wrapper_name(),
         CWRecv::full_handle_type_name(),
         CWRecv::protocol_name(),
         CWRecv::next_method_hint());

    dump("RecordingSessionHandle<Send<int, End>>",
         RSend::wrapper_name(),
         RSend::full_handle_type_name(),
         RSend::protocol_name(),
         RSend::next_method_hint());

    std::puts("session_wrapper_name: per-derived-class wrapper-name "
              "reflection + next-method hint + full type rendering (#429) "
              "— every diagnostic field correctly populated OK");
    return 0;
}
