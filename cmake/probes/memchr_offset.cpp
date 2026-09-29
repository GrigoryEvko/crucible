// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// The constant evaluator of GCC 10.1 through 16.2 counts the offset of the
// first argument two times when it evaluates memchr, strchr, strrchr or
// strstr on a pointer into a string.  toolchain/gcc/patches/0002-*.patch has
// the fix.  A compiler with the fix accepts this file.  A compiler without
// the fix refuses each static_assert.
constexpr long memchr_offset(const char* text, unsigned long count) {
    const void* hit = __builtin_memchr(text, ',', count);
    return hit ? static_cast<const char*>(hit) - text : -1;
}

constexpr long strchr_offset(const char* text) { return __builtin_strchr(text, ',') - text; }

constexpr long strrchr_offset(const char* text) { return __builtin_strrchr(text, ',') - text; }

constexpr long strstr_offset(const char* text) { return __builtin_strstr(text, ",2") - text; }

static_assert(memchr_offset("0,1,2,3" + 2, 5) == 1);
static_assert(strchr_offset("0,1,2,3" + 2) == 1);
static_assert(strrchr_offset("0,1,2,3" + 2) == 3);
static_assert(strstr_offset("0,1,2,3" + 2) == 1);

int main() { return 0; }
