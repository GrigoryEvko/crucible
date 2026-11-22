// The body of with_library gives a copy of the reference to its library to a
// closure, and the closure ends the library with an explicit destructor call.
// The body then calls a function of the library, whose code the end of the
// library unmapped.  The destructor of a Library is private to the loader
// door, so the closure cannot name it either.

#include <fixy/os/Loader.h>

#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

[[maybe_unused]] int calls_after_end(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    return ::fixy::os::with_library(
               ctx, "libc.so.6", ::foundation::core::to(diagnostic),
               [&ctx, &diagnostic]<class Brand>(::fixy::os::Library<Brand> const& library) noexcept {
                   auto const read_id = ::fixy::os::find_function<int() noexcept>(ctx, library, "getpid",
                                                                                  ::foundation::core::to(diagnostic))
                                            .expect("the C library has getpid");
                   auto const end_library = [&library]() noexcept { library.~Library(); };
                   end_library();
                   return read_id();
               })
        .expect("the test loads the C library");
}

}  // namespace
