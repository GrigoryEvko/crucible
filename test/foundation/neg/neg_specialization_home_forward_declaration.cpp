// The rule specialization-home of the quarantine plugin refuses a variable
// template that a file declares with extern before the header that defines
// it.  GCC keeps the place of the first declaration of a variable template,
// so the home of is_graded_v moves to this file, and a forged specialization
// here is at home.  The plugin makes the declaration a compile error in each
// build.  The template is a template of the base, so the declaration is also
// the first declaration of a base template outside the base, and the plugin
// gives that error at the same place.

namespace foundation::algebra {
template <typename T>
extern const bool is_graded_v;
}  // namespace foundation::algebra

#include <foundation/algebra/Graded.h>

namespace {
struct Fake final {};
}  // namespace

template <>
inline constexpr bool foundation::algebra::is_graded_v<Fake> = true;

int main() { return foundation::algebra::is_graded_v<Fake> ? 0 : 1; }
