// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-031 fixture: Cipher::open() refuses bare std::string at the
// trust boundary.  Pre-V-031, Cipher's signature was
//     static Cipher open(const std::string& root);
// which made the operator's untrusted bytes invisible to the type
// system.  V-031 changed the signature to
//     static Cipher open(crucible::fixy::wrap::Path<
//                            crucible::fixy::tags::source::External> root);
// so every call site MUST mint an External-tagged Path explicitly.
//
// `Tagged<T, Source>`'s constructor is `explicit` — there is no
// implicit conversion from `const char*` / `std::string` / a path
// literal into `Tagged<std::filesystem::path, External>`.  Passing
// a bare string therefore fails substitution at the call site.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "could not convert" / "cannot convert"
//   "explicit" / "candidate constructor not viable".

#include <crucible/Cipher.h>

int main() {
    // Should FAIL: bare const-char* literal cannot implicitly convert
    // to Path<External>; the explicit Tagged ctor refuses the bridge.
    [[maybe_unused]] auto cipher =
        ::crucible::Cipher::open("/tmp/crucible_neg_v031_bare_string");
    return 0;
}
