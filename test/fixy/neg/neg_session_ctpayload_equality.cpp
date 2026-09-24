// A comparison of two constant-time carriers with ==.  Its time could
// depend on where the first difference is, so == is deleted, and
// fixy::session::eq is the only comparison.

#include <fixy/Secret.h>
#include <fixy/session/Classified.h>
#include <fixy/session/Payload.h>

#include <cstddef>
#include <optional>
#include <utility>

namespace sess = ::fixy::session;

namespace {
struct [[=sess::constant_time_value{}]] AuthTag {
    std::byte bytes[16]{};
};
struct UnadmittedPolicy : ::fixy::tags::secret_policy::secret_policy_base {};
}  // namespace

int main() {
    sess::CTPayload<AuthTag> first{AuthTag{}};
    sess::CTPayload<AuthTag> second{AuthTag{}};
    return first == second ? 0 : 1;
}
