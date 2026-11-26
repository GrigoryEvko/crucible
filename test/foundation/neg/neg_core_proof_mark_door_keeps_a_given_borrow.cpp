// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// its door keep takes a View of long, the target of its borrow slot held_.  So
// the door can keep a borrow of the storage of its caller in the object, and a
// body that captures the object by reference could keep a View of its frame
// there.  The first walk that reads the class runs the check of the mark,
// which stops the build and names the class, the door and the parameter.  The
// check file of foundation/core/CaptureStore.h pins the same class with no
// such door (ProvenStash), which passes.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>

namespace neg_core_proof_mark_door_keeps_a_given_borrow_types {
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

    void keep(Key& /*key*/, core::View<long> view) noexcept { held_ = core::some(view); }
    [[nodiscard]] Key claim() noexcept {
        if (is_key_claimed_.exchange_acq_rel(true)) core::fatal("a second claim of the key of a stash");
        return Key{};
    }

private:
    core::Atomic<bool> is_key_claimed_{false};
    core::Option<core::View<long>> held_{};
};

struct CapturesStash {
    Stash& stash;
};
}  // namespace neg_core_proof_mark_door_keeps_a_given_borrow_types

static_assert(
    ::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_door_keeps_a_given_borrow_types::CapturesStash>);

int main() { return 0; }
