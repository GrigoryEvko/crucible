// A parallel pointwise walk over two frames of one FixedArray with no key.  The
// output is the run [0, kCount - 1) of the inner frame and the input is the run
// [1, kCount) of the outer frame.  The two brands would prove that the runs do
// not overlap, so one shard would read the element that another shard writes.
// An array with no key opens no frame.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelPointwise.h>
#include <foundation/core/Pointwise.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

constexpr std::size_t kCount = 4096;

struct AddOne final {
    template <class X>
    constexpr X operator()(X value) const noexcept {
        return static_cast<X>(value + X{1});
    }
};

// Opens a frame of the array.  The body of the last one walks the two runs.
template <class... Outer>
bool shift_through_frames(BgCtx const& ctx, ::fixy::FixedArray<std::uint32_t, kCount>& array,
                          Outer&... outer) noexcept {
    return array.with_mut([&](auto inner) noexcept -> bool {
        if constexpr (sizeof...(Outer) == 0) {
            auto const open_inner = [&]() noexcept { return shift_through_frames(ctx, array, inner); };
            return open_inner();
        } else {
            ::fixy::FixedArray<std::size_t, 1> const sizes{kCount - 1};
            ::fixy::FixedArray<std::size_t, 1> const strides{1};
            auto out = ::fixy::mint_strided_view_mut(std::move(inner), sizes.view(), strides.view(), 0)
                           .expect("the output fits the run");
            auto const in = ::fixy::mint_strided_view_mut(std::move(outer...[0]), sizes.view(), strides.view(), 1)
                                .expect("the input fits the run");
            return ::fixy::concurrent::write_pointwise_in_parallel<::fixy::spawn::BodyCoupling::WaitOnEachOther, 2>(
                       ctx, out, AddOne{}, in)
                .is_ok();
        }
    });
}

// Names the first frame, so the compiler reads the template.
[[maybe_unused]] bool shift_by_one(BgCtx const& ctx, ::fixy::FixedArray<std::uint32_t, kCount>& array) noexcept {
    return shift_through_frames(ctx, array);
}

}  // namespace

int main() { return 0; }
