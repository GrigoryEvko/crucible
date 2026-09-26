// std::set gives no emplace_back, so the ordered log could not grow its
// tail.  The door refuses it as storage.

#include <fixy/Mutation.h>

#include <functional>
#include <set>

int main() {
    auto log = fixy::mint_ordered_append_only<int, std::identity, std::less<>, std::set>();
    return static_cast<int>(log.size());
}
