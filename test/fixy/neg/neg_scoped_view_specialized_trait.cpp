// The field audit of fixy/ScopedView.h refuses a class that stores a
// view.  This file tries to hide a stored view from the audit.  It
// specializes the class template that the audit once read, so a view
// would read as no view.  The audit reads a concept over the instance
// query, so the specialization has nothing to name.

#include <fixy/ScopedView.h>

#include <type_traits>

namespace {

struct Carrier {};
struct Ready {};
struct ViewBrand {};

}  // namespace

template <>
struct fixy::is_scoped_view<fixy::ScopedView<Carrier, Ready, ViewBrand>> : std::false_type {};

int main() { return 0; }
