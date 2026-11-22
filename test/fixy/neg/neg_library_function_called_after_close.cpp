// The body of with_library gives a closure that holds a function of its
// library, and the caller calls the closure after the scope closed the
// library.  The type of a closure has no template argument, so the concept
// of the door does not see the brand in it.  The rule brand-escape refuses
// each result of a scope mint that holds its own scope brand, also in a
// capture.

#include <fixy/os/Loader.h>

#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

struct KeepsFunction final {
    IoBlockCtx context;

    template <class Brand>
    auto operator()(::fixy::os::Library<Brand> const& library) const noexcept {
        ::fixy::os::LoaderDiagnostic diagnostic{};
        auto const read_id =
            ::fixy::os::find_function<int() noexcept>(context, library, "getpid", ::foundation::core::to(diagnostic))
                .expect("the C library has getpid");
        return [read_id]() noexcept { return read_id(); };
    }
};

[[maybe_unused]] int calls_after_close(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    auto const later =
        ::fixy::os::with_library(ctx, "libc.so.6", ::foundation::core::to(diagnostic), KeepsFunction{ctx})
            .expect("the test loads the C library");
    return later();
}

}  // namespace
