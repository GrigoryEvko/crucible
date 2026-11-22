// Negative-compile test. This file must fail to compile.
//
// A ChannelBinding refers to a channel that the handles on several threads
// use at once.  Its constructor takes the key of the channel, and only the
// channel builds that key (foundation/ChannelBinding.h).  So code that is
// not the channel cannot bind an object, here a plain struct that two
// threads write with no order.

#include <foundation/ChannelBinding.h>

namespace outside_key {

struct Plain {
    int value = 0;
};

}  // namespace outside_key

int main() {
    outside_key::Plain plain{};
    ::foundation::ChannelBinding<outside_key::Plain> binding{plain, ::foundation::BindingKey<outside_key::Plain>{}};
    return binding.is_bound() ? 0 : 1;
}
