// format() writes its arguments with the formatter of the Report family,
// which writes no long double: one value would have two texts, because a
// long double has 80 value bits on x86_64 and 128 on aarch64.  So format()
// refuses a long double argument at the call.

#include <foundation/core/Format.h>

int main() {
    ::foundation::core::FixedText<32> text;
    auto const result = ::foundation::core::format(text, "the ratio is {}", 0.5L);
    return result.is_ok() ? 0 : 1;
}
