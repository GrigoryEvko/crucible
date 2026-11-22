// The relaxed read of the commit count is the read of its one writer, and it
// takes the token of the publishing stage.  A stage that is not the publisher
// builds a token to read, so it would read a count that another thread stores
// with no order, and conclude that the writes of the publisher are visible.

#include <fixy/handle/PublishCommit.h>
#include <foundation/Transfer.h>

#include <cstdint>

struct CommitTag {};
struct PublishingStage;

class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] Publisher final {
    Publisher() noexcept {}
    friend struct PublishingStage;

public:
    Publisher(Publisher const&) = delete("a publisher token names the one writer of the cell");
};

struct OtherStage {
    template <class Cell>
    static std::uint64_t peek(Cell const& cell) noexcept {
        return cell.peek_relaxed(Publisher{});
    }
};

int main() {
    fixy::handle::PublishCommitCell<CommitTag, Publisher> cell;
    return OtherStage::peek(cell) == 0 ? 0 : 1;
}
