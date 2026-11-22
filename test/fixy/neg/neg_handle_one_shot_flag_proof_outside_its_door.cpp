// Negative compile test.  This file must fail to compile.
//
// A proof of quiescence says that no thread of an owner runs.  Only the
// OwnerThread of the owner gives one, while it holds no thread.  A copy of a
// proof would be a second proof that no door gave, so the proof does not
// copy.  (The witness fixture of the proof refuses its private constructor.)
//
// Expected diagnostic: the copy constructor of the proof is deleted.

#include <fixy/handle/OneShotFlag.h>
#include <fixy/os/OwnedThread.h>

namespace {
struct Owner {
    Owner() = default;
    Owner(Owner const&) = delete("the thread borrows its owner at one address");
    Owner(Owner&&) = delete("the thread borrows its owner at one address");
    Owner& operator=(Owner const&) = delete("the thread borrows its owner at one address");
    Owner& operator=(Owner&&) = delete("the thread borrows its owner at one address");
    ~Owner() = default;

    ::fixy::handle::OneShotFlag stop;
    ::fixy::thread::OwnerThread<Owner> thread_{};
};
}  // namespace

int main() {
    Owner owner{};
    ::fixy::thread::QuiescenceProof<Owner> const given = owner.thread_.quiescence(owner);
    ::fixy::thread::QuiescenceProof<Owner> const copied{given};
    owner.stop.reset_in_quiescent_context(copied);
    return 0;
}
