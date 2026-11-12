// A direct call of the hook get<I> of a structured binding on an lvalue is a
// second form of the access to a part.  The rule binding-hook of the plugin
// refuses each call of the hook outside the initializer of a binding.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, long> pair{1, 2};
    return get<0>(pair);
}
