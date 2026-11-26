// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// it states no synchronized{} of foundation/Transfer.h.  The mark asks the
// check of synchronized{}, so that two threads write an object of the class
// only through a proof.  The check of the mark stops the build and names the
// class.

#include <foundation/core/Array.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>

namespace neg_core_proof_mark_without_synchronized_types {
namespace core = ::foundation::core;

class[[= core::synchronized_by_proof{}]] Stash {
public:
    Stash() = default;
    Stash(Stash const&) = delete("a stash of two threads does not copy");
    Stash& operator=(Stash const&) = delete("a stash of two threads does not copy");

    [[nodiscard]] int read() const noexcept { return count_; }

private:
    int count_ = 0;
    core::Option<core::View<long>> held_{};
};

struct CapturesStash {
    Stash& stash;
};
}  // namespace neg_core_proof_mark_without_synchronized_types

static_assert(::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_without_synchronized_types::CapturesStash>);

int main() { return 0; }
