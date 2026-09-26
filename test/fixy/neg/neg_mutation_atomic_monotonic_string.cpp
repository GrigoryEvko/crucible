// A std::string is not trivially copyable, so no std::atomic holds it
// lock-free and no compare-exchange replaces it in one step.  The door
// refuses it as the carrier of an atomic counter.

#include <fixy/Mutation.h>

#include <string>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::string>(std::string{"a"});
    return static_cast<int>(counter.get().size());
}
