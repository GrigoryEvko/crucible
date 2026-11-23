// A helper that takes each text and gives it a tag that a caller names does
// not open a route around the door of a foreign text.  The helper deduces
// the type of its text, so a foreign text reaches the provenance door as a
// ForeignText, and the door refuses the FromInternal tag at its constraint.

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

// A text that mint_foreign_text gave at a foreign ABI.  The fixture never
// links, so the function has no body.
::fixy::ForeignText foreign_name() noexcept;

namespace {

template <class Tag, class Text>
auto tag_text(Text text) {
    return ::fixy::mint_tagged<Tag>(text);
}

}  // namespace

int main() {
    auto claimed = tag_text<::fixy::tags::source::FromInternal>(foreign_name());
    return static_cast<int>(claimed.value().size());
}
