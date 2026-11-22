// Each write of the cell takes the token of the publishing stage, and only
// that stage builds the token.  A stage that is not the publisher builds a
// token to bump the counter, so a reader could be released before the side
// effects the count announces.

#include <fixy/handle/PublishCommit.h>
#include <foundation/Transfer.h>

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
    static void bump(Cell& cell) noexcept {
        (void)cell.bump(Publisher{});
    }
};

int main() {
    fixy::handle::PublishCommitCell<CommitTag, Publisher> cell;
    OtherStage::bump(cell);
    return 0;
}
