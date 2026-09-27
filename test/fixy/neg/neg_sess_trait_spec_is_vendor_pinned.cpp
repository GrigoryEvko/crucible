// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_vendor_pinned is an alias template that reads the head of the
// protocol, so no user specialization gives a portable protocol a vendor.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_vendor_pinned<Send<int, End>> : std::true_type {
    using protocol = End;
    static constexpr VendorBackend vendor_backend = VendorBackend::NV;
};
}  // namespace fixy::session

int main() { return 0; }
