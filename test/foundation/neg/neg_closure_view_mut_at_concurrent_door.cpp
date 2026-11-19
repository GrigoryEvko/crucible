// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A door whose bodies run at the same time on other threads refuses a body
// that reads a ViewMut of int through a reference to const.  A const ViewMut
// can still write its run, so two threads that hold one race unless they can
// write its element (foundation/Transfer.h).  The message names the field,
// its type and the first part that refuses.

#include <foundation/Closure.h>
#include <foundation/core/Region.h>

namespace {

inline constexpr ::foundation::closure::DoorPolicy concurrent_policy{::foundation::closure::Crossing::ConcurrentThreads,
                                                                     ::foundation::closure::QttGrade::One};

struct RunBrand {};

struct ReadsTheRun {
    ::foundation::core::ViewMut<int, RunBrand> const& run;
    void operator()() const noexcept { static_cast<void>(run.size()); }
};

constexpr ::foundation::closure::ClosureVerdict verdict =
    ::foundation::closure::check_closure(^^ReadsTheRun, concurrent_policy, "run_concurrently");
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
