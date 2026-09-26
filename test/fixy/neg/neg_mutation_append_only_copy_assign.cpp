// A copy assignment from a shorter log would drop elements.  AppendOnly
// deletes both assignments, so the copy assignment below names a deleted
// function.

#include <fixy/Mutation.h>

int main() {
    auto shorter = fixy::mint_append_only<int>();
    auto log = fixy::mint_append_only<int>();
    log.append(1);
    log = shorter;
    return static_cast<int>(log.size());
}
