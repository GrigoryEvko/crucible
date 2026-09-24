// The word of a choice is a read too.  A read of the word that returns
// the word itself waits inside its own call, where the watch does not see
// it, so branch refuses it.

#include <fixy/session/Handle.h>

#include <cstddef>
#include <utility>

namespace s = ::fixy::session;

namespace neg_sess_branch_read_waits_in_secret_types {
struct Wire {
    std::size_t word = 0;
};
using Choice = s::Offer<s::End, s::End>;
}  // namespace neg_sess_branch_read_waits_in_secret_types

using namespace neg_sess_branch_read_waits_in_secret_types;

int main() {
    auto head = s::mint_session_handle<Choice>(Wire{});
    std::move(head).branch([](Wire& wire) noexcept { return wire.word; },
                           [](auto at_end) noexcept { static_cast<void>(std::move(at_end).close()); });
    return 0;
}
