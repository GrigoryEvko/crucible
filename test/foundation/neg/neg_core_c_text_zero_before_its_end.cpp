// A zero byte inside a CText cuts the text for the kernel, which reads up to
// the first zero, so a literal that holds one stops the build at the call.

#include <foundation/core/Text.h>

int main() {
    constexpr ::foundation::core::CText path{"/proc/self\0/status"};
    return static_cast<int>(path.size());
}
