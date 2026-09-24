// A value of SyscallFamily made by a cast names no family of the chain, so
// it has no row to lift to.  The family atom refuses it at its constraint,
// before the incomplete row type could fail somewhere later.

#include <fixy/atoms/Syscall.h>

int main() {
    return sizeof(::fixy::atom::syscall::family<static_cast<::fixy::atom::syscall::SyscallFamily>(9)>) == 1 ? 0 : 1;
}
