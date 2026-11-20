// The proof of quiescence names its owner, and only the OwnerThread of the
// owner gives one.  An empty brace names no owner and makes no proof, so a
// reset with it is refused.

#include <fixy/handle/OneShotFlag.h>

int main() {
    fixy::handle::OneShotFlag flag;
    flag.reset_in_quiescent_context({});
    return 0;
}
