// std::list gives no operator[], which the log forwards to, so the door
// refuses it as storage.

#include <fixy/Mutation.h>

#include <list>

int main() {
    auto log = fixy::mint_append_only<int, std::list>();
    return static_cast<int>(log.size());
}
