// A unit of the base that is not a mint or a read of foundation/core/Text.h
// calls the door of those mints to make a TextView of any pointer and any
// count.  The members of the door are private, so the call is refused.

#include <foundation/core/Text.h>

int main() {
    foundation::core::TextView const text =
        foundation::core::detail::TextPartsDoor::text_(static_cast<char const*>(nullptr), 4);
    return static_cast<int>(text.size());
}
