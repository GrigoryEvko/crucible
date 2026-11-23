// A pop of an MPSC channel is a merge: the order of the items of many
// producers depends on the schedule.  So the pop takes the bare tag of
// Nondet, and a pop with no tag has no overload.  A consumer handle of a
// frame alone is no evidence that the code that holds it may read a merge.

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel = fixy::concurrent::PermissionedChannel<fixy::concurrent::ChannelForm::merge, int, 8, UserTag, UserBrand>;

[[maybe_unused]] auto attempt(Channel& channel,
                              foundation::permissions::Permission<Channel::consumer_tag, UserBrand>&& token) noexcept {
    return fixy::concurrent::with_consumer(channel, std::move(token), [](auto consumer) noexcept {
        [[maybe_unused]] auto item = consumer.try_pop();
        return std::move(consumer).release();
    });
}

}  // namespace

int main() { return 0; }
