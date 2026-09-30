// The field audit of fixy/ScopedView.h asks IsScopedView whether a class
// stores a view.  This file tries to hide a stored view from the audit: it
// writes an explicit specialization of IsScopedView for one view type, as
// it would for a variable template.  IsScopedView is a concept, and the
// template-id of a concept declares nothing.

#include <fixy/ScopedView.h>

namespace {

struct Carrier {};
struct Ready {};
struct ViewBrand {};

}  // namespace

template <>
inline constexpr bool fixy::IsScopedView<fixy::ScopedView<Carrier, Ready, ViewBrand>> = false;

int main() { return 0; }
