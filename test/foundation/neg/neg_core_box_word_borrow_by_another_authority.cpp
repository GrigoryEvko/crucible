// A word names the authority that made it.  A second authority builds its own
// key, and that key borrows no word of the first authority.

#include <foundation/core/Ref.h>

struct Channel {};

class Thief {
public:
    static int peek(::foundation::core::BoxWord<int, Channel> word) {
        return *word.borrow(::foundation::core::box_word_key<Thief>{});
    }
};

int main() { return Thief::peek(::foundation::core::BoxWord<int, Channel>{}); }
