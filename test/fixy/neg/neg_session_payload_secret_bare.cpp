// A classified value on a channel with no named policy.  A fixy::Secret
// inside a pair leaves classification at the transport, so the walk
// refuses it outside DeclassifyOnSend.

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
    using Delta = sess::payload_perm_delta<std::pair<::fixy::Secret<int>, int>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
