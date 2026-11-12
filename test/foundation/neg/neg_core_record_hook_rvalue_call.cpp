// A direct call of the hook get<I> of a structured binding on an rvalue moves
// one part out and drops the other parts.  The rule binding-hook of the
// plugin refuses each call of the hook outside the initializer of a binding.

#include <foundation/core/Record.h>

#include <utility>

int main() {
    ::foundation::core::Record<int, long> pair{1, 2};
    return static_cast<int>(get<1>(std::move(pair)));
}
