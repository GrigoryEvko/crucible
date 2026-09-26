// An assignment from a fresh mint would drop every element of the log,
// and the next append would then accept a key below the dropped ones.
// OrderedAppendOnly deletes both assignments, so the move assignment
// below names a deleted function.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_ordered_append_only<int>();
    log.append(5);
    log = fixy::mint_ordered_append_only<int>();
    log.append(1);
    return static_cast<int>(log.size());
}
