// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// its door template visit takes a body.  The check reads a door template
// through its specialization for the probe body of foundation/Transfer.h,
// which stands for each type that a caller can give, so the target of the
// body is erased and meets the target long of the borrow slot held_.  A class
// with a borrow slot and a door that takes a body therefore cannot take the
// mark.  The check of the mark stops the build and names the class, the door
// and the parameter.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>

namespace neg_core_proof_mark_body_door_types {
namespace core = ::foundation::core;
namespace transfer = ::foundation::transfer;

class[[= transfer::synchronized{}]][[= core::synchronized_by_proof{}]] Stash {
public:
    class[[= transfer::thread_bound{}]][[= transfer::not_shareable{}]] Key final {
        Key() noexcept {}
        friend class Stash;

    public:
        Key(Key const&) = delete("a key names the one holder of the stash");
        Key& operator=(Key const&) = delete("a key names the one holder of the stash");
        ~Key() = default;
    };

    Stash() = default;
    Stash(Stash const&) = delete("a stash of two threads does not copy");
    Stash& operator=(Stash const&) = delete("a stash of two threads does not copy");

    template <class Body>
    void visit(Key& /*key*/, Body&& body) noexcept {
        body(count_);
    }
    [[nodiscard]] Key claim() noexcept {
        if (is_key_claimed_.exchange_acq_rel(true)) core::fatal("a second claim of the key of a stash");
        return Key{};
    }

private:
    core::Atomic<bool> is_key_claimed_{false};
    int count_ = 0;
    core::Option<core::View<long>> held_{};
};

struct CapturesStash {
    Stash& stash;
};
}  // namespace neg_core_proof_mark_body_door_types

static_assert(::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_body_door_types::CapturesStash>);

int main() { return 0; }
