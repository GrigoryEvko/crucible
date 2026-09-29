#include <crucible/cntp/_wip/QuicTransport.h>

#include <array>
#include <cstddef>

// QuicResumptionToken::from is the only writer of a token length, so view()
// never reads past the array.  A caller that could store a length of its own
// could make view() read far past the end, and the length is private.

int main() {
    namespace cntp = crucible::cntp::_wip;

    std::array<std::byte, 4> bytes{std::byte{1}};
    auto token = cntp::QuicResumptionToken::from(bytes).value();
    token.size_ = 1000;
    (void)token;
    return 0;
}
