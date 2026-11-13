// The control of the import check of fixy::copy.
//
// The unit copies a run whose count is a value at run time, so each build
// calls the library memmove.  The test copy_imports (test/fixy/CMakeLists.txt)
// requires the import of memmove in this object.  The control shows that the
// check reads the object and that its pattern finds an import.

#include <fixy/Core.h>

#include <cstddef>

namespace {

[[gnu::used]] bool copy_run(fixy::View<std::byte> target, fixy::View<std::byte const> source) noexcept {
    return fixy::copy(target, source).is_ok();
}

}  // namespace
