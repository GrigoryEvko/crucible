// Sentinel TU: compiles the alias headers under the project warning flags so
// their static_asserts run.

#include <crucible/fixy/_Mach.h>
#include <crucible/fixy/Safety.h>

#include <type_traits>
#include <utility>

namespace fmach = ::crucible::fixy::mach;
namespace fsaf = ::crucible::fixy::safety;
namespace saf = ::crucible::safety;

static_assert(std::is_same_v<fmach::Machine<int>, saf::Machine<int>>,
              "fixy::mach::Machine must alias safety::Machine.");

static_assert(std::is_same_v<fsaf::Linear<int>, saf::Linear<int>>, "fixy::safety::Linear must alias safety::Linear.");

static_assert(std::is_same_v<fsaf::Secret<int>, saf::Secret<int>>, "fixy::safety::Secret must alias safety::Secret.");

int main() {
    auto m = fmach::mint_machine<int>(42);
    auto l = fsaf::mint_linear<int>(7);
    auto s = fsaf::mint_secret<int>(9);

    auto m2 = fmach::transition_to(std::move(m), int{99});
    (void)m2;
    fsaf::drop(std::move(l));
    (void)s;
    return 0;
}
