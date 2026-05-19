// ── test_fixy_bridge_fwddecl — Bridge.h include-discipline witness ─
//
// FIXY-AUDIT-C5 + fixy-H-23 (#1483): documents the include-discipline
// in fixy/Bridge.h.  PRE-fixy-H-23 this TU witnessed that
// `<crucible/Vigil.h>` was structurally REQUIRED because
// `mint_vigil_mode_bridge` returned the nested type
// `Vigil::ModeSessionHandle`.  POST-fixy-H-23 the mode machinery
// lives in `<crucible/bridges/VigilModeHandle.h>` as the standalone
// `crucible::vigil_mode::` namespace, and Bridge.h pulls only that
// small header — Vigil.h is NO LONGER part of Bridge.h's transitive
// include closure.
//
// This TU includes ONLY `crucible/fixy/Bridge.h` (no manual Vigil.h
// pull) and witnesses:
//
//   1. The fixy::bridge umbrella exposes mint_vigil_mode_bridge as
//      the `vigil_mode::ModeCell`-taking primary surface.
//   2. The return type `vigil_mode::ModeSessionHandle` is reachable
//      without including Vigil.h.
//   3. `crucible::Vigil` itself is NOT defined here — proves Bridge.h
//      no longer transitively drags in the 1.1 KLoC Vigil hub.
//   4. The four bridges/* mint factories are reachable from the
//      same single-include surface (no transitive-pull-in fragility).
//
// HS14: this TU is positive-sentinel-only.  No new mint factories are
// introduced — H-23 is a header-relocation task.  The substrate mint
// factories already ship their own HS14 floors next to their
// definition headers (fixy-M-29 + fixy-HS14-* series).

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb     = ::crucible::fixy::bridge;
namespace cproto = ::crucible::safety::proto;
namespace cb     = ::crucible::bridges;
namespace cvm    = ::crucible::vigil_mode;

// ─── 1. mint_vigil_mode_bridge surfaced under fixy::bridge:: ──────
//
// Function-pointer identity proves the substrate symbol is reachable
// through the fixy::bridge:: re-export with no decay.  After
// fixy-H-23 the substrate signature is
// `mint_vigil_mode_bridge(const ModeCell&)` — Bridge.h's
// `using ::crucible::mint_vigil_mode_bridge;` pulls in this overload
// (the Vigil-taking convenience overload lives behind Vigil.h and is
// NOT reachable from this TU).
//
// decltype identity proves fb::mint_vigil_mode_bridge has the exact
// substrate signature.  Stronger than address-equality (the compiler
// flags &fb::X == &crucible::X as tautological precisely because the
// using-declaration's name lookup resolves to the same entity — that
// IS the witness we want).

static_assert(std::is_same_v<
    decltype(fb::mint_vigil_mode_bridge),
    decltype(::crucible::mint_vigil_mode_bridge)>,
    "fixy::bridge::mint_vigil_mode_bridge must be the substrate "
    "ModeCell-taking mint after the using-declaration.");

// Concrete pointer-to-function disambiguation: the substrate primary
// surface accepts `const vigil_mode::ModeCell&` and returns
// `vigil_mode::ModeSessionHandle`.  This static_assert pins the exact
// signature in case overload sets grow in the future.
static_assert(std::is_same_v<
    decltype(&::crucible::mint_vigil_mode_bridge),
    cvm::ModeSessionHandle (*)(const cvm::ModeCell&) noexcept>,
    "post-fixy-H-23: the umbrella-reachable mint_vigil_mode_bridge "
    "is the ModeCell-taking primary surface.");

// ─── 2. vigil_mode::ModeSessionHandle is name-reachable ───────────
//
// Witnesses that Bridge.h's `#include <crucible/bridges/VigilModeHandle.h>`
// makes the standalone type definition available to fixy:: consumers
// without them having to pull Vigil.h themselves.

static_assert(std::is_same_v<
    typename cvm::ModeSessionHandle::resource_type,
    const cvm::ModeCell*>,
    "vigil_mode::ModeSessionHandle nested resource_type must be "
    "reachable through fixy/Bridge.h alone.");

static_assert(sizeof(cvm::ModeCell)
              == sizeof(std::atomic<cvm::Mode>),
    "vigil_mode::ModeCell must be sizeof(std::atomic<Mode>) — the "
    "atomic IS the channel identity.");

// ─── 3. crucible::Vigil is NOT defined in this TU ──────────────────
//
// fixy-H-23 promise: Bridge.h no longer transitively pulls Vigil.h.
// The witness uses an incomplete-type SFINAE probe: if Vigil.h had
// been pulled (directly or transitively), `class crucible::Vigil`
// would be a complete type and `sizeof` would succeed.  A benign
// forward declaration introduces the name; the requires-clause
// probes completeness.
//
// If this static_assert fires, a recent edit re-introduced the
// dependency (check Bridge.h's include block) or Vigil.h leaked
// through another header in the chain (CrashTransport / EndpointMint
// / RecordingSessionHandle / SessionPersistence / Cipher /
// VigilModeHandle).

namespace crucible { class Vigil; }  // benign forward decl

namespace fixy_h23_witness {

template <typename T>
concept has_complete_size = requires { sizeof(T); };

static_assert(!has_complete_size<::crucible::Vigil>,
    "fixy-H-23: Bridge.h must not transitively pull Vigil.h.");

}  // namespace fixy_h23_witness

// ─── 4. Other Bridge.h mint factories reachable via fixy:: ────────
//
// Cross-check that the four bridges/* mint factories remain
// reachable from the single-include fixy::bridge:: surface — proves
// the include-discipline doesn't leave any factory orphaned behind
// a transitive-pull boundary.

namespace test_fixy_bridge_fwddecl {
using SendInt = cproto::Send<int, cproto::End>;
struct DummyRes {};
}

// Recording mint reachable.
static_assert(std::is_same_v<
    fb::RecordingSessionHandle<
        test_fixy_bridge_fwddecl::SendInt,
        test_fixy_bridge_fwddecl::DummyRes,
        void>,
    cproto::RecordingSessionHandle<
        test_fixy_bridge_fwddecl::SendInt,
        test_fixy_bridge_fwddecl::DummyRes,
        void>>);

// Endpoint mint factories reachable — name-lookup witness.  Each
// using-declaration would fail at parse time if the substrate name
// were not surfaced under fixy::bridge::.

namespace test_fixy_bridge_fwddecl_lookup {
using fb::mint_recording_endpoint;
using fb::mint_crash_watched_endpoint;
using fb::mint_recording_session;
using fb::mint_crash_watched_session;
using fb::mint_persisted_session;
using fb::mint_vigil_mode_bridge;
}

int main() { return 0; }
