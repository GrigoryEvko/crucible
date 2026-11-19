// The relaxed read of the commit count is the read of its one writer.  A
// stage that is not the publisher tries it, so it would read a count that
// another thread stores with no order, and conclude that the writes of the
// publisher are visible.

#include <fixy/handle/PublishCommit.h>

#include <cstdint>

struct CommitTag {};
struct Publisher {};
struct OtherStage {
    template <class Cell>
    static std::uint64_t peek(Cell const& cell) noexcept {
        return cell.peek_relaxed();
    }
};

int main() {
    fixy::handle::PublishCommitCell<CommitTag, Publisher> cell;
    return OtherStage::peek(cell) == 0 ? 0 : 1;
}
