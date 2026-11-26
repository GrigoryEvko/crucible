// A record holds a StaticText and a TextView of a local FixedText.  The
// StaticText states refers_static{}, so it holds no borrow, but the TextView
// borrows the local run.  So the record is still a borrow, and the rule
// view-escape refuses its read after the end of the scope of the run.

#include <foundation/core/StaticText.h>
#include <foundation/core/Text.h>

namespace {

struct Labelled {
    ::foundation::core::StaticText label = "label";
    ::foundation::core::TextView body{};
};

}  // namespace

int main() {
    Labelled kept{};
    {
        ::foundation::core::FixedText<8> const local{"body"};
        kept = Labelled{"label", local.view()};
    }
    return static_cast<int>(kept.body.size());
}
