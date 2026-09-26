// An assignment from a fresh mint would drop every element of the log.
// AppendOnly deletes both assignments, so the move assignment below names
// a deleted function.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_append_only<int>();
    log.append(1);
    log.append(2);
    log = fixy::mint_append_only<int>();
    return static_cast<int>(log.size());
}
