// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The brand of a ReadLoan names the parked token.  Only an empty class
// type can be a brand.  A loan branded with int would carry a value where
// the brand must carry only an identity.
//
// Expected diagnostic: the static_assert in ReadLoan that asks for
// IsBrand<Brand>.

#include <foundation/permissions/ReadView.h>

namespace neg_read_loan_brand_not_empty {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_read_loan_brand_not_empty

int main() {
    using neg_read_loan_brand_not_empty::Region;
    return static_cast<int>(sizeof(::foundation::permissions::ReadLoan<Region, int>));
}
