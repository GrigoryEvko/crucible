// A reset of the flag is correct only when neither side can reach it,
// and the compiler cannot check that.  So the reset takes a proof of
// quiescence that each call site must spell.  A call with no proof is
// refused.

#include <fixy/handle/OneShotFlag.h>

int main() {
    fixy::handle::OneShotFlag flag;
    flag.reset_in_quiescent_context();
    return 0;
}
