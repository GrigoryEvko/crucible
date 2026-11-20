// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A SoleWriter proves that its holder is the one thread that writes a cell.
// Two bodies of run_joined that read one writer through a reference to const
// would both write the cell of one writer, and load_owned would then read a
// value that the other thread stored.  A writer moves to another thread, but
// two threads do not share it, so it is Sendable and not Shareable
// (foundation/Transfer.h), and the door refuses the borrow
// (foundation/Closure.h, rule 6).
//
// Expected diagnostic: the verdict of the door names the field of the writer.

#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>

namespace {
struct CellRole {};
}  // namespace

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto writer = ::foundation::core::mint_sole_writer<CellRole>();
    using Writer = decltype(writer);
    ::foundation::core::Atomic<unsigned long, Writer> cell{writer};
    Writer const& proof = writer;
    auto first = [&cell, &proof]() noexcept { cell.store_release(proof, cell.load_owned(proof) + 1); };
    auto second = [&cell, &proof]() noexcept { cell.store_release(proof, cell.load_owned(proof) + 1); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, first, second);
    return static_cast<int>(cell.load_acquire());
}
