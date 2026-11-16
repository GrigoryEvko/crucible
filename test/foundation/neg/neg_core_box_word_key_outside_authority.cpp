// Only a member of the authority builds the key of its box words.  Code
// outside the authority builds no key, so it claims no word of the authority.

#include <foundation/core/Ref.h>

struct Channel {};

int main() {
    ::foundation::core::BoxWord<int, Channel> const word{};
    ::foundation::core::Box<int> const stolen =
        ::foundation::core::claim_box(word, ::foundation::core::box_word_key<Channel>{});
    return *stolen;
}
