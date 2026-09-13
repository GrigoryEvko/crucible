// This file includes the bridge umbrella and nothing else, on purpose. It
// witnesses that the umbrella surfaces every bridge mint on its own and
// that the heavy vigil hub stays outside its transitive include closure.
// Adding any other include destroys both claims.

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb = ::crucible::fixy::bridge;
namespace cproto = ::crucible::safety::proto;
namespace cb = ::crucible::bridges;
namespace cvm = ::crucible::vigil_mode;

// The using-declaration resolves to the same entity, so an address
// comparison is tautological and witnesses nothing. Pinning the decltype
// fixes the exact signature against an overload set that can still grow.
// The mint is a constrained template, so the pin names one concrete cell.
static_assert(std::is_same_v<decltype(&fb::mint_vigil_mode_bridge<cvm::ModeCell>),
                             decltype(&::crucible::mint_vigil_mode_bridge<cvm::ModeCell>)>,
              "fixy::bridge::mint_vigil_mode_bridge must be the substrate "
              "ModeCell-taking mint after the using-declaration.");

static_assert(std::is_same_v<decltype(&::crucible::mint_vigil_mode_bridge<cvm::ModeCell>),
                             cvm::ModeSessionHandle (*)(const cvm::ModeCell&) noexcept>,
              "the umbrella-reachable mint_vigil_mode_bridge is the "
              "ModeCell-taking primary surface.");

static_assert(std::is_same_v<typename cvm::ModeSessionHandle::resource_type, const cvm::ModeCell*>,
              "vigil_mode::ModeSessionHandle nested resource_type must be "
              "reachable through fixy/Bridge.h alone.");

static_assert(sizeof(cvm::ModeCell) == sizeof(std::atomic<cvm::Mode>),
              "vigil_mode::ModeCell must be sizeof(std::atomic<Mode>) — the "
              "atomic IS the channel identity.");

// The forward declaration introduces the name without defining it, and the
// requires-clause probes completeness. Had the vigil header been pulled in
// along any path, the class would be complete and sizeof would succeed.

namespace crucible {
class Vigil;
}  // namespace crucible

namespace fixy_h23_witness {

template <typename T>
concept has_complete_size = requires { sizeof(T); };

static_assert(!has_complete_size<::crucible::Vigil>,
              "the bridge umbrella must not transitively pull the vigil header.");

}  // namespace fixy_h23_witness

namespace test_fixy_bridge_fwddecl {
using SendInt = cproto::Send<int, cproto::End>;
struct DummyRes {};
}  // namespace test_fixy_bridge_fwddecl

static_assert(
    std::is_same_v<
        fb::RecordingSessionHandle<test_fixy_bridge_fwddecl::SendInt, test_fixy_bridge_fwddecl::DummyRes, void>,
        cproto::RecordingSessionHandle<test_fixy_bridge_fwddecl::SendInt, test_fixy_bridge_fwddecl::DummyRes, void>>);

// These using-declarations are the test. Each one fails at parse time if
// the substrate name is not surfaced under the umbrella namespace, which
// would leave that factory orphaned behind a transitive-include boundary.

namespace test_fixy_bridge_fwddecl_lookup {
using fb::mint_recording_endpoint;
using fb::mint_crash_watched_endpoint;
using fb::mint_recording_session;
using fb::mint_crash_watched_session;
using fb::mint_persisted_session;
using fb::mint_vigil_mode_bridge;
}  // namespace test_fixy_bridge_fwddecl_lookup

int main() { return 0; }
