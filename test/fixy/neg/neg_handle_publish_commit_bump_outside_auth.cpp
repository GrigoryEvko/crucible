// The friend list of the cell is the whole gate: only WriteAuth bumps
// the counter.  A stage that is not the publisher tries to bump it, so a
// reader could be released before the side effects the count announces.

#include <fixy/handle/PublishCommit.h>

struct CommitTag {};
struct Publisher {};
struct OtherStage {
    template <class Cell>
    static void bump(Cell& cell) noexcept {
        (void)cell.bump();
    }
};

int main() {
    fixy::handle::PublishCommitCell<CommitTag, Publisher> cell;
    OtherStage::bump(cell);
    return 0;
}
