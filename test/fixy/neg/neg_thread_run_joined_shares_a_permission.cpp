// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A Permission proves that its holder owns a region alone.  Two bodies of
// run_joined that read one token through a reference to const would each show
// the proof of the region on its own thread at the same time.  A token moves
// to another thread, but two threads do not share it, so it is Sendable and
// not Shareable (foundation/Transfer.h), and the door refuses the borrow
// (foundation/Closure.h, rule 6).
//
// Expected diagnostic: the verdict of the door names the field of the token.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// A door that takes the token of the region as the proof of its write.
template <class Token>
void write_region(Token const& /*proof*/, ::foundation::core::Atomic<int>& cell) noexcept {
    cell.store_release(cell.load_acquire() + 1);
}
}  // namespace

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto token = ::foundation::permissions::mint_permission_root<Region>();
    auto const& proof = token;
    ::foundation::core::Atomic<int> cell{0};
    auto first = [&cell, &proof]() noexcept { write_region(proof, cell); };
    auto second = [&cell, &proof]() noexcept { write_region(proof, cell); };
    [[maybe_unused]] auto started =
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second));
    ::foundation::linear::drop(static_cast<decltype(token)&&>(token));
    return cell.load_acquire();
}
