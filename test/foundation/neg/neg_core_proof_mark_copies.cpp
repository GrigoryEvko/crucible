// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// it copies.  The copy constructor reads a second object of the class, whose
// borrow slot held_ names long, the target of the slot of the first object,
// so the copy can keep a borrow of the storage of its caller.  The check of
// the mark stops the build and names the class, the copy constructor and its
// parameter.

#include <foundation/core/Array.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>

namespace neg_core_proof_mark_copies_types {
namespace core = ::foundation::core;
namespace transfer = ::foundation::transfer;

class[[= transfer::synchronized{}]][[= core::synchronized_by_proof{}]] Stash {
public:
    Stash() = default;
    Stash(Stash const&) = default;
    Stash& operator=(Stash const&) = delete("a stash of two threads takes no second stash in place");

    [[nodiscard]] int read() const noexcept { return count_; }

private:
    int count_ = 0;
    core::Option<core::View<long>> held_{};
};

struct CapturesStash {
    Stash& stash;
};
}  // namespace neg_core_proof_mark_copies_types

static_assert(::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_copies_types::CapturesStash>);

int main() { return 0; }
