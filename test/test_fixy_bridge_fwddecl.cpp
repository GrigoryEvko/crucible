// ── test_fixy_bridge_fwddecl — Bridge.h include-discipline witness ─
//
// FIXY-AUDIT-C5: documents the include-discipline decision in
// fixy/Bridge.h that `<crucible/Vigil.h>` is STRUCTURALLY required
// because `mint_vigil_mode_bridge` returns the nested type
// `Vigil::ModeSessionHandle` — a `class Vigil;` forward declaration
// is insufficient.
//
// This TU includes ONLY `crucible/fixy/Bridge.h` (no manual Vigil.h
// pull) and witnesses:
//
//   1. The fixy::bridge umbrella exposes mint_vigil_mode_bridge.
//   2. Its return type is reachable as Vigil::ModeSessionHandle.
//   3. The four bridges/* mint factories are reachable from the
//      same single-include surface (no transitive-pull-in fragility).
//
// HS14: this TU is positive-sentinel-only.  No new mint factories are
// introduced — C5 is a documentation/structural-discipline task.
// The substrate mint factories already ship their own HS14 floors
// next to their definition headers.

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb     = ::crucible::fixy::bridge;
namespace cproto = ::crucible::safety::proto;
namespace cb     = ::crucible::bridges;

// ─── 1. mint_vigil_mode_bridge surfaced under fixy::bridge:: ──────
//
// Function-pointer identity proves the substrate symbol is reachable
// through the fixy::bridge:: re-export with no decay.

// decltype identity proves fb::mint_vigil_mode_bridge has the exact
// substrate signature.  Stronger than address-equality (the compiler
// flags &fb::X == &crucible::X as tautological precisely because the
// using-declaration's name lookup resolves to the same entity — that
// IS the witness we want).

static_assert(std::is_same_v<
    decltype(fb::mint_vigil_mode_bridge),
    decltype(::crucible::mint_vigil_mode_bridge)>,
    "fixy::bridge::mint_vigil_mode_bridge must be the substrate "
    "mint after the using-declaration.");

// ─── 2. Vigil::ModeSessionHandle is name-reachable ────────────────
//
// Witnesses that Bridge.h's `#include <crucible/Vigil.h>` makes the
// nested type definition available to fixy:: consumers without them
// having to pull Vigil.h themselves.

static_assert(std::is_same_v<
    typename ::crucible::Vigil::ModeSessionHandle::resource_type,
    const ::crucible::Vigil::ModeCell*>,
    "Vigil::ModeSessionHandle nested resource_type must be reachable "
    "through fixy/Bridge.h alone.");

// ─── 3. Other Bridge.h mint factories reachable via fixy:: ────────
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
}

int main() { return 0; }
