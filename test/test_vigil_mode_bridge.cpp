#include <crucible/Vigil.h>

#include <cassert>
#include <atomic>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using Mode = crucible::Vigil::Mode;
using ModeCell = crucible::Vigil::ModeCell;
using ModeSession = crucible::Vigil::ModeSessionHandle;

static_assert(crucible::safety::AtomicMachineCell<ModeCell>);
static_assert(crucible::safety::proto::is_well_formed_v<
              crucible::Vigil::ModeProtocol>);
static_assert(std::is_same_v<typename ModeSession::resource_type,
                             const ModeCell*>);
static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));

int run_vigil_mode_bridge_observes_vigil_cell() {
    crucible::Vigil vigil{};
    assert(vigil.mode() == Mode::RECORDING);

    auto session = crucible::mint_vigil_mode_bridge(vigil);
    auto terminal = std::move(session).select_local<2>();
    const ModeCell* cell = std::move(terminal).close();
    assert(cell != nullptr);
    assert(crucible::safety::atomic_machine_state(*cell) == Mode::RECORDING);
    return 0;
}

int run_atomic_mode_session_lifecycle() {
    ModeCell cell{};
    assert(crucible::safety::atomic_machine_state(cell) == Mode::RECORDING);

    auto apply_transition =
        []<typename Transition>(ModeCell*& c, Transition&&) noexcept {
            static_assert(crucible::Vigil::mode_transition_allowed(
                Transition::from, Transition::to));
            assert(crucible::safety::atomic_machine_state(*c)
                   == Transition::from);
            crucible::safety::publish_atomic_machine_transition<
                Transition, ModeCell>(c, Transition{});
            assert(crucible::safety::atomic_machine_state(*c)
                   == Transition::to);
        };

    auto session =
        crucible::safety::atomic_session_from_machine<
            crucible::Vigil::ModeProtocol>(cell);
    auto publish_compiled = std::move(session).select_local<0>();
    auto after_compiled = std::move(publish_compiled).send(
        crucible::Vigil::ModeRecordingToCompiled{}, apply_transition);

    auto publish_recording = std::move(after_compiled).select_local<1>();
    auto after_recording = std::move(publish_recording).send(
        crucible::Vigil::ModeCompiledToRecording{}, apply_transition);

    auto terminal = std::move(after_recording).select_local<2>();
    ModeCell* recovered = std::move(terminal).close();
    assert(recovered == &cell);
    return 0;
}

}  // namespace

int main() {
    if (int rc = run_vigil_mode_bridge_observes_vigil_cell(); rc != 0) return rc;
    if (int rc = run_atomic_mode_session_lifecycle(); rc != 0) return 100 + rc;
    std::puts("vigil_mode_bridge: atomic MachineSessionBridge view OK");
    return 0;
}
