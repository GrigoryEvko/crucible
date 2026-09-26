// std::set gives no emplace_back, so the log could not grow its tail.  The
// door refuses it as storage.

#include <fixy/Mutation.h>

#include <set>

int main() {
    auto log = fixy::mint_append_only<int, std::set>();
    return static_cast<int>(log.size());
}
