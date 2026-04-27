// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A19 — PermissionedChaseLevDeque::ThiefHandle exposes only
// try_steal.  Calling try_push must be a hard compile error — only
// the OWNER may push to bottom (CL's single-owner contract on
// push_bottom is enforced structurally by the type system).

#include <crucible/concurrent/PermissionedChaseLevDeque.h>

namespace {

struct BadDeque {};

void exercise_thief_try_push() {
    crucible::concurrent::PermissionedChaseLevDeque<int, 64, BadDeque> deque;
    auto t_opt = deque.thief();
    if (!t_opt) return;
    auto thief = std::move(*t_opt);
    thief.try_push(42);
}

}  // namespace

int main() { exercise_thief_try_push(); return 0; }
