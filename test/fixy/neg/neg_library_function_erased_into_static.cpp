// The body of with_library puts a CallRef of a function of its library into a
// static local.  A CallRef erases the type of its callable, so the type of the
// static names no brand, and the rule brand-escape cannot see the scope brand
// of the library in it.  The next call of the door then calls through the
// static after the last scope closed the library.  A CallRef refuses each
// callable that holds a scope brand, as erase_brand refuses a scope brand.

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
                   auto read_id = ::fixy::os::find_function<int() noexcept>(ctx, library, "getpid",
                                                                            ::foundation::core::to(diagnostic))
                                      .expect("the C library has getpid");
                   static ::foundation::core::CallRef<int() noexcept> const kept{read_id};
                   return kept();
               })
        .expect("the test loads the C library");
}

}  // namespace
