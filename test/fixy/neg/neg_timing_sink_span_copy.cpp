// A span of the observation sink ends one time, in the scope of its start.
// A copy would add the count of its life a second time, so a span has no
// copy.
//
// Expected diagnostic: the deleted copy of the span, with its reason.

#include <fixy/Core.h>
#include <fixy/os/TimingSink.h>

int main() {
    auto sink = ::fixy::observe::mint_timing_sink<2>();
    auto const span = sink.span(::fixy::index<0>());
    [[maybe_unused]] auto const copy = span;
    return 0;
}
