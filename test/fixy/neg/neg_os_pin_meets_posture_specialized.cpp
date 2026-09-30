// The clock reader asks pin_meets_posture whether a pin is explicit.
// This file tries to make each pin meet each posture: it writes an
// explicit specialization of pin_meets_posture.  pin_meets_posture is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/os/CpuPinned.h>

#include <meta>

template <>
consteval bool fixy::pin_meets_posture(std::meta::info, fixy::PinningPosture) {
    return true;
}

int main() { return 0; }
