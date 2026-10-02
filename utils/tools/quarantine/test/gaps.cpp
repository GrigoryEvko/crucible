// The paths around the rules of the plugin: a typedef of fixy that names a
// library enumeration, a global variable of the C library, a macro of a system
// header, builtins that the code calls, a builtin that the front end calls,
// va_arg, an asm statement and assert.  check_plugin.py gives the finding of
// each line in LIBRARY_NAMES and the lines with no finding in
// LIBRARY_NAMES_ABSENT, and it compiles the file with and without NDEBUG.

#include <cerrno>
#include <cstdarg>
#include <unistd.h>

#include <fixy/GapsAlias.h>

fixy::GapsByte gaps_byte{};
int gaps_option = optind;

int gaps_error() { return errno; }

void gaps_trap() { __builtin_trap(); }

void gaps_copy(int& to, const int& from) { __builtin_memcpy(&to, &from, sizeof to); }

int gaps_load(int& value) { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }

unsigned gaps_bits(float value) { return __builtin_bit_cast(unsigned, value); }

int gaps_next();
int gaps_counter() {
    static int counter = gaps_next();
    return counter;
}

int gaps_sum(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int sum = va_arg(arguments, int);
    va_end(arguments);
    return sum;
}

void gaps_fence() { asm volatile("" ::: "memory"); }

#include <cassert>

void gaps_check(int value) { assert(value > 0); }
