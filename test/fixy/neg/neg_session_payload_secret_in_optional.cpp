// A classified value in a std::optional on a channel.  The union storage
// of the optional does not hide it from the walk.

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
    using Delta = sess::payload_perm_delta<std::optional<::fixy::Secret<int>>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
