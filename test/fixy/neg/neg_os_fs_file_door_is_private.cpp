// A public static factory on a Handle would open a descriptor with no
// context and no gate: an unsanitized path, raw O_* flags, a socket of
// any triple.  The calls live in door classes whose members are private,
// and only the gated mints are friends.  A direct call to the file door
// is refused.

#include <fixy/os/Fs.h>

#include <fcntl.h>

int main() {
    [[maybe_unused]] auto opened = fixy::fs::FileDoor::open_file_<fixy::fs::FileOf<fixy::fs::read_only>, O_RDONLY>(
        "/etc/hostname", fixy::fs::FileMode::owner_read);
    return 0;
}
