// A foreign text came from outside the process, so it takes only the tag of
// a boundary that the input sanitizer discharges (BoundaryTag).  A
// FromInternal tag on it would claim that the program made the characters,
// and the provenance door refuses that tag at its constraint.  The generic
// factory refuses a ForeignText for each tag.

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

// A text that mint_foreign_text gave at a foreign ABI.  The fixture never
// links, so the function has no body.
::fixy::ForeignText foreign_name() noexcept;

int main() {
    auto claimed = ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(foreign_name());
    return static_cast<int>(claimed.value().size());
}
