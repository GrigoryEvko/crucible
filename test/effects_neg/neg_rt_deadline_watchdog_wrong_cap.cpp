// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004g (#1283): rt::DeadlineWatchdog construction takes an
// `effects::Init` capability tag by value as its third parameter.
// Same gate as Senses::load_all / SchedSwitch::load / etc — Bg /
// Init / Test are distinct 1-byte structs with no implicit
// conversion between them, so a hot-path or background frame that
// holds only `Bg` cannot accidentally construct a watchdog.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// Expected diagnostic regex: "could not convert|no matching function|
// cannot convert|expected.*Init".

#include <crucible/effects/Capabilities.h>
#include <crucible/rt/DeadlineWatchdog.h>
#include <crucible/rt/Policy.h>

int main() {
    crucible::rt::Policy policy = crucible::rt::Policy::production();
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile
    crucible::rt::DeadlineWatchdog wd{nullptr, policy, bg_cap};

    (void)wd;
    return 0;
}
