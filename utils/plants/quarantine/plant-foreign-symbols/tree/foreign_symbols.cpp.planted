// The foreign symbols of a quarantined file: a function of C language linkage
// that the unit declares and does not define, an asm label and the attribute
// weakref.  The plugin of main found no system header behind these names, so
// each declaration and each call gave no finding.  A function of C language
// linkage that the unit defines, also after a call, is no foreign symbol.

extern "C" [[noreturn]] void abort() noexcept;
extern "C" long write(int descriptor, void const* bytes, unsigned long count);
void* foreign_copy(void* target, void const* source, unsigned long count) noexcept __asm__("memcpy");
static void foreign_exit(int status) __attribute__((weakref("exit")));
extern "C" int foreign_defined_later(int value);

[[noreturn]] void foreign_stop() { abort(); }
long foreign_write() { return write(1, "x", 1); }
void foreign_copy_int(int& target, int const& source) { foreign_copy(&target, &source, sizeof target); }
void foreign_leave() { foreign_exit(0); }
int foreign_call_later() { return foreign_defined_later(1); }

extern "C" int foreign_defined_later(int value) { return value; }

long foreign_register() {
    register long held __asm__("r12") = 0;
    return held;
}
