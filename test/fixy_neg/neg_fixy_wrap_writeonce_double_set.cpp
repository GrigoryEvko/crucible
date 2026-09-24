// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #6: WriteOnce<T>::set rejects double-set at
// consteval.
//
// Violation: WriteOnce::set fires `pre(!value_.has_value())` —
// calling set() a second time on the same slot is the contract
// violation that WriteOnce exists to prevent.  Surfaced through
// the fixy::wrap alias must reject the consteval evaluation as
// non-constant.
//
// Expected diagnostic: substring "contract" / "not a constant
// expression" / "pre".

#include <crucible/fixy/Wrap.h>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapWriteOnceDoubleSet {};

consteval int probe() {
    fw::WriteOnce<int> wo{};
    wo.set(1);
    wo.set(2);  // double-set — fires pre(!has_value())
    return wo.get();
}

constexpr int kProbed = probe();

int main() {
    (void)kProbed;
    return 0;
}
