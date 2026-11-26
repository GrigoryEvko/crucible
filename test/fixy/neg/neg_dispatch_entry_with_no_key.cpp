// A body of the dispatch door opens a frame of a keyed owner of a dispatch
// brand, and the frame body names the entry of the dispatch door with no
// window, for a second key of the same brand.  The second frame of the array
// would then open inside the first one on one thread.  The entry takes the key
// of the frame doors first, which only the dispatch door gets after it opens
// its window, so a call with no key does not compile.

#include <fixy/concurrent/Dispatch.h>
#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace {

struct[[= ::foundation::core::dispatch_brand{}]] LogBrand final {};

struct Recorder {
    ::foundation::core::FixedArray<int, 4, LogBrand> log{};
};

using DispatchCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Recorder>, ::foundation::effects::Row<>>;

}  // namespace

int main() {
    DispatchCtx const ctx = ::foundation::effects::testing::foreground<Recorder>();
    Recorder recorder{};
    return ::fixy::dispatch::run_dispatch(
        ctx, [&recorder](DispatchCtx const&, ::foundation::core::LoanKey<LogBrand>& outer) noexcept {
            recorder.log.with_mut(outer, [&recorder](auto run) noexcept {
                ::foundation::core::detail::run_dispatch_body_<0>(
                    [&recorder](::foundation::core::LoanKey<LogBrand>& inner) noexcept {
                        recorder.log.with_mut(inner, [](auto second) noexcept {
                            auto element = second.get_mut(0).expect("the array holds the element");
                            *element = 1;
                        });
                        return 0;
                    });
                auto element = run.get_mut(0).expect("the array holds the element");
                *element += 1;
            });
            return recorder.log.at(::foundation::core::index<0>());
        });
}
