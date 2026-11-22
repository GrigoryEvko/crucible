// The loop over a niche Option gives its body a reference to the payload.
// The body assigns the Option, and the reference then names the empty value
// of the niche, a Ref that holds the null pointer.  The rule borrow-match
// reads the body of the loop as an arm and refuses the assignment.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

namespace fc = ::foundation::core;

namespace {

int read_after_reset(fc::Option<fc::Ref<int>>& held) {
    int sum = 0;
    for (fc::Ref<int> const& ref : held) {
        held = fc::none;
        sum += *ref;
    }
    return sum;
}

}  // namespace

int main() {
    int object = 7;
    fc::Option<fc::Ref<int>> held = fc::Option<fc::Ref<int>>::some(fc::erase_brand(fc::to(object)));
    return read_after_reset(held);
}
