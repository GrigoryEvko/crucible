// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-080: DIVERGED is a replay status, not a persistent Vigil mode.
// Instantiating a direct persistent transition to it must fail before
// any runtime state or transport code exists.

#include <crucible/Vigil.h>

using BadTransition = crucible::Vigil::ModeTransition<
    crucible::Vigil::Mode::RECORDING,
    crucible::Vigil::Mode::DIVERGED>;

int main() {
    [[maybe_unused]] BadTransition transition{};
    return 0;
}
