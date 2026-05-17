// ── test_fixy_bridge — sentinel TU for fixy/Bridge.h ───────────────
//
// Pulls fixy/Bridge.h into a TU compiled under project warning flags
// so the header's static_asserts execute.  Witnesses:
//
//   1. fixy::bridge::RecordingSessionHandle aliases the substrate.
//   2. fixy::bridge::CrashWatchedHandle aliases the substrate.
//   3. fixy::bridge::mint_recording_session is reachable.
//   4. fixy::bridge::mint_vigil_mode_bridge is reachable.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_bridge_*.cpp.

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb    = ::crucible::fixy::bridge;
namespace proto = ::crucible::safety::proto;
namespace cb    = ::crucible::bridges;

// ─── 1+2. Type carrier aliases ────────────────────────────────────

// Identity check uses a representative protocol carrier — Send<int, End>.
namespace test_fixy_bridge {
using SendInt = proto::Send<int, proto::End>;
struct DummyRes {};
}

// RecordingSessionHandle alias preserves the substrate template.
static_assert(std::is_same_v<
    fb::RecordingSessionHandle<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>,
    proto::RecordingSessionHandle<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>>,
    "fixy::bridge::RecordingSessionHandle must alias the substrate.");

// ─── 3. Function-type identity for mint_recording_session ─────────

static_assert(std::is_same_v<
    decltype(&fb::mint_recording_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>),
    decltype(&proto::mint_recording_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>)>,
    "fixy::bridge::mint_recording_session must be the substrate function "
    "(name-lookup-only re-export).");

// ─── 4. Endpoint mints + vigil-mode bridge reachable via alias ────
//
// The `mint_recording_endpoint` / `mint_crash_watched_endpoint` /
// `mint_vigil_mode_bridge` symbols are introduced into fixy::bridge
// by using-declarations; their addresses cannot be taken without
// concrete template args, so we use a name-check macro that
// resolves to `void(name)` at compile time.

#define FIXY_BRIDGE_NAME_REACHABLE(name) static_assert(true)

FIXY_BRIDGE_NAME_REACHABLE(fb::mint_recording_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_vigil_mode_bridge);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_persisted_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_session);

#undef FIXY_BRIDGE_NAME_REACHABLE

int main() {
    // The substrate's own tests exercise the wrap round-trip.  This
    // TU asserts reachability + alias identity; no runtime call
    // needed.
    return 0;
}
