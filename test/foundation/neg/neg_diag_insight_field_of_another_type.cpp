// The reader of an insight field takes a TextView.  This tag declares
// why_this_matters as a FixedText, a text of the base that is no TextView, so
// the reader of foundation/diag/Insights.h stops the build with the name of
// the field.  A reader that gave such a field as empty would hide the prose
// of the tag.
#include <foundation/core/Text.h>
#include <foundation/diag/Insights.h>

namespace {
struct text_of_another_type_tag : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "TextOfAnotherTypeTag";
    static constexpr ::foundation::diag::tag_text_literal description = "fixture";
    static constexpr ::foundation::diag::tag_text_literal remediation = "fixture";
    static constexpr ::foundation::core::FixedText<4> why_this_matters{"why"};
};
}  // namespace

int main() {
    return static_cast<int>(::foundation::diag::insight_provider<text_of_another_type_tag>::why_this_matters.size());
}
