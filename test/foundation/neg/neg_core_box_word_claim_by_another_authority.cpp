// A word names the authority that made it.  A second authority builds its own
// key, and that key claims no word of the first authority.

#include <foundation/core/Ref.h>

struct Channel {};

class Thief {
public:
    static int steal(::foundation::core::BoxWord<int, Channel> word) {
        ::foundation::core::Box<int> const stolen =
            ::foundation::core::claim_box(word, ::foundation::core::box_word_key<Thief>{});
        return *stolen;
    }
};

int main() { return Thief::steal(::foundation::core::BoxWord<int, Channel>{}); }
