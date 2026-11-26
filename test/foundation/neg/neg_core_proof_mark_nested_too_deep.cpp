// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// it nests classes five levels deep.  The check reads the doors of each
// nested class to four levels, and a nested class deeper than that can have a
// door that the check does not read.  So the check fails closed: it stops the
// build and names the class and the nested class that it cannot read.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>

namespace neg_core_proof_mark_nested_too_deep_types {
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

    struct First {
        struct Second {
            struct Third {
                struct Fourth {
                    struct Fifth {};
                };
            };
        };
    };

    Stash() = default;
    Stash(Stash const&) = delete("a stash of two threads does not copy");
    Stash& operator=(Stash const&) = delete("a stash of two threads does not copy");

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
}  // namespace neg_core_proof_mark_nested_too_deep_types

static_assert(::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_nested_too_deep_types::CapturesStash>);

int main() { return 0; }
