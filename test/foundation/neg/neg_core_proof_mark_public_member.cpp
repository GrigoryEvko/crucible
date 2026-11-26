// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// its member limit is public.  The check of synchronized{} admits a public
// const member that two threads can read, but the mark asks that each data
// member is private, so that only a door writes an object of the class.  The
// check of the mark stops the build and names the class and the member.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>

namespace neg_core_proof_mark_public_member_types {
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

    [[nodiscard]] Key claim() noexcept {
        if (is_key_claimed_.exchange_acq_rel(true)) core::fatal("a second claim of the key of a stash");
        return Key{};
    }

    int const limit = 8;

private:
    core::Atomic<bool> is_key_claimed_{false};
    core::Option<core::View<long>> held_{};
};

struct CapturesStash {
    Stash& stash;
};
}  // namespace neg_core_proof_mark_public_member_types

static_assert(::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_public_member_types::CapturesStash>);

int main() { return 0; }
