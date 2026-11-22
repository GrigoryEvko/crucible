// A Ref of a LoanKey would keep a borrow of the key in a field that outlives
// the frame that lent the key, such as a member of an owner that a dispatch
// body reaches.  Ref refuses a key of each brand as its element.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

int main() {
    auto minted = ::foundation::core::mint_fixed_array<int, 4>();
    auto& key = minted.at(::foundation::core::index<1>());
    auto const kept = ::foundation::core::to(key);
    static_cast<void>(kept);
    return 0;
}
