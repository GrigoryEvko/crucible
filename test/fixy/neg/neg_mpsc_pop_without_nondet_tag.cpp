// A pop of an MPSC channel is a merge: the order of the items of many
// producers depends on the schedule.  So the pop takes the bare tag of
// Nondet, and a pop with no tag has no overload.  A consumer handle alone
// is no evidence that the code that holds it may read a merge.

#include <fixy/concurrent/PermissionedChannel.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel = fixy::concurrent::PermissionedChannel<fixy::concurrent::ChannelForm::merge, int, 8, UserTag, UserBrand>;

[[maybe_unused]] void attempt(Channel::ConsumerHandle& consumer) { [[maybe_unused]] auto item = consumer.try_pop(); }

}  // namespace

int main() { return 0; }
