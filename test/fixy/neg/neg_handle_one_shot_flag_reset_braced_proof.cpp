// The proof of quiescence has an explicit constructor, so an empty brace
// does not make one.  Each reset site must spell the name of the proof,
// and a search for that name finds every reset.

#include <fixy/handle/OneShotFlag.h>

int main() {
    fixy::handle::OneShotFlag flag;
    flag.reset_in_quiescent_context({});
    return 0;
}
