// A declassification on a channel under a policy that has no admitted
// edge.  The carrier takes only an admitted policy, as Secret::declassify
// does.

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

static_assert(sizeof(sess::DeclassifyOnSend<int, UnadmittedPolicy>) != 0);

int main() {
    return 0;
}
