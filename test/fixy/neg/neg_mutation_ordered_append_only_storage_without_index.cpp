// std::list gives no operator[], which the ordered log forwards to, so the
// door refuses it as storage.

#include <fixy/Mutation.h>

#include <functional>
#include <list>

int main() {
    auto log = fixy::mint_ordered_append_only<int, std::identity, std::less<>, std::list>();
    return static_cast<int>(log.size());
}
