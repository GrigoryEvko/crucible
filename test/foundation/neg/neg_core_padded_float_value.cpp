// A Padded takes a TextView or an integer.  A double needs a count of
// decimals for a text of a fixed form, so it goes through Fixed, which has a
// width of its own, and the deleted constructor refuses it at the call.

#include <foundation/core/Report.h>

int main() {
    double const ratio = 0.25;
    ::foundation::core::report(::foundation::core::Sink::Out, "the ratio is {}\n",
                               ::foundation::core::Padded<8>{ratio});
}
