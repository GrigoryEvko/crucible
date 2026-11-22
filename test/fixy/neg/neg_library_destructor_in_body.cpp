// The body of with_library ends its library through the const reference with
// an explicit destructor call, and then calls a function of the library.  A
// destructor call on a const object is valid C++, and the end of the library
// unmaps the code of the function.  The destructor of a Library is private to
// the loader door, so no body names it.

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
                   library.~Library();
                   return read_id();
               })
        .expect("the test loads the C library");
}

}  // namespace
