// The word of a choice is a write too.  A write of the word that returns
// void can wait inside its own call, where the watch does not see it, so
// select refuses it.

#include <fixy/session/Handle.h>

#include <cstddef>
#include <utility>

namespace s = ::fixy::session;

namespace neg_sess_select_write_waits_in_secret_types {
struct Wire {
    std::size_t word = 0;
};
using Choice = s::Select<s::End, s::End>;
}  // namespace neg_sess_select_write_waits_in_secret_types

using namespace neg_sess_select_write_waits_in_secret_types;

int main() {
    auto head = s::mint_session_handle<Choice>(Wire{});
    auto at_end = std::move(head).template select<0>([](Wire& wire, std::size_t word) noexcept { wire.word = word; });
    static_cast<void>(std::move(at_end).close());
    return 0;
}
