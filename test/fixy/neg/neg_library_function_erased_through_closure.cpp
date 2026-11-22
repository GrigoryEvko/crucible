// The body of with_library puts a CallRef of a closure into a static local,
// and the closure captures a function of the library by reference.  The type
// of a closure has no template argument, so only the walk of its captures
// finds the scope brand of the library.  A CallRef refuses each callable that
// holds a scope brand at a depth of that walk.

#include <fixy/os/Loader.h>

#include <foundation/core/CallRef.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

[[maybe_unused]] int calls_after_close(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    return ::fixy::os::with_library(
               ctx, "libc.so.6", ::foundation::core::to(diagnostic),
               [&ctx, &diagnostic]<class Brand>(::fixy::os::Library<Brand> const& library) noexcept {
                   auto const read_id = ::fixy::os::find_function<int() noexcept>(ctx, library, "getpid",
                                                                                  ::foundation::core::to(diagnostic))
                                            .expect("the C library has getpid");
                   auto call = [&read_id]() noexcept { return read_id(); };
                   static ::foundation::core::CallRef<int() noexcept> const kept{call};
                   return kept();
               })
        .expect("the test loads the C library");
}

}  // namespace
