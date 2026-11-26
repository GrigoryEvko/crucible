// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled reaches a holder of the context and its key, as a
// fixture of a test or the replay state of a Vigil holds them, and deactivates
// the context.  The deactivation would end the pool and the engine that the
// scope lends.  The gate refuses a body that reaches a key of a context through
// a member of an object that it captures.

#include <crucible/CrucibleContext.h>

namespace {

struct Holder {
    crucible::CrucibleContext context;
    crucible::ContextKey key;
};

}  // namespace

int main() {
    auto const test = ::foundation::effects::testing::test();
    Holder holder{.context = crucible::CrucibleContext{test.alloc}, .key = crucible::mint_context_key(test)};
    holder.context.with_compiled(holder.key, [&holder](crucible::CompiledScope& scope) noexcept {
        static_cast<void>(scope);
        holder.context.deactivate(holder.key);
    });
    return 0;
}
