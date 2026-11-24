// The body of with_library keeps a text in a variable that it captures by
// reference.  A function of the library can give the address of a text in the
// memory of the library, such as the version text of the C library.  The end
// of the scope closes the library, and the last close unmaps that memory, so
// a read of the kept text after the scope would read unmapped memory.  The
// store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the body, whatever the source of
// the text.

#include <fixy/os/Loader.h>

#include <foundation/core/Ref.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

[[maybe_unused]] bool keeps_text(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    ::foundation::core::TextView kept{};
    auto const loaded =
        ::fixy::os::with_library(ctx, "libc.so.6", ::foundation::core::to(diagnostic),
                                 [&kept](auto const&) noexcept { kept = ::foundation::core::TextView{"2.42"}; });
    return loaded.is_ok() && kept.size() != 0;
}

}  // namespace

int main() { return 0; }
