// The door of <sys/socket.h> in the test table.  This file includes the header,
// and the include is no finding.

#include <sys/socket.h>

namespace probe {

int socket_family() { return AF_UNIX; }

}  // namespace probe
