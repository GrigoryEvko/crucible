// A quarantined file that replaces the global operator new and declares an
// allocation function of a class.  With the plugin of main, neither gave a
// finding, and each new-expression of the program can call the replacement.
// A deleted allocation function allocates nothing.

#include <cstddef>

void* operator new(std::size_t size) { return __builtin_malloc(size); }

struct AllocationOwned {
    static void* operator new(std::size_t size) = delete;
    static void operator delete(void* pointer) noexcept;
};
