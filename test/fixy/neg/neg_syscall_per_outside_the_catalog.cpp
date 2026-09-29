// A value of SyscallId made by a cast names no call of the catalog, so it
// holds no row of the family table.  The per atom refuses it rather than
// giving it the family of the value after the lookup loop.

#include <fixy/atoms/Syscall.h>

int main() {
    return sizeof(::fixy::atom::syscall::per<static_cast<::fixy::atom::syscall::SyscallId>(99)>) == 1 ? 0 : 1;
}
