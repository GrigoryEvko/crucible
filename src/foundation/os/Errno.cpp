// The check of the error numbers of foundation/os/Errno.h against the kernel
// header that gives them.  This file is the one includer of <asm/errno.h>,
// and it compiles to no code.

#include <foundation/os/Errno.h>

#include <asm/errno.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <utility>

namespace foundation::os {

namespace {

// One name of the enumeration, and the value of its macro.  The two fields
// have 16 bits each, so a row has no padding.
struct KernelErrno {
    Errno name;
    std::uint16_t value;
};

constexpr KernelErrno kKernelErrnos[] = {
    {Errno::eperm, EPERM},
    {Errno::enoent, ENOENT},
    {Errno::esrch, ESRCH},
    {Errno::eintr, EINTR},
    {Errno::eio, EIO},
    {Errno::enxio, ENXIO},
    {Errno::e2big, E2BIG},
    {Errno::enoexec, ENOEXEC},
    {Errno::ebadf, EBADF},
    {Errno::echild, ECHILD},
    {Errno::eagain, EAGAIN},
    {Errno::enomem, ENOMEM},
    {Errno::eacces, EACCES},
    {Errno::efault, EFAULT},
    {Errno::enotblk, ENOTBLK},
    {Errno::ebusy, EBUSY},
    {Errno::eexist, EEXIST},
    {Errno::exdev, EXDEV},
    {Errno::enodev, ENODEV},
    {Errno::enotdir, ENOTDIR},
    {Errno::eisdir, EISDIR},
    {Errno::einval, EINVAL},
    {Errno::enfile, ENFILE},
    {Errno::emfile, EMFILE},
    {Errno::enotty, ENOTTY},
    {Errno::etxtbsy, ETXTBSY},
    {Errno::efbig, EFBIG},
    {Errno::enospc, ENOSPC},
    {Errno::espipe, ESPIPE},
    {Errno::erofs, EROFS},
    {Errno::emlink, EMLINK},
    {Errno::epipe, EPIPE},
    {Errno::edom, EDOM},
    {Errno::erange, ERANGE},
    {Errno::edeadlk, EDEADLK},
    {Errno::enametoolong, ENAMETOOLONG},
    {Errno::enolck, ENOLCK},
    {Errno::enosys, ENOSYS},
    {Errno::enotempty, ENOTEMPTY},
    {Errno::eloop, ELOOP},
    {Errno::enomsg, ENOMSG},
    {Errno::eidrm, EIDRM},
    {Errno::echrng, ECHRNG},
    {Errno::el2nsync, EL2NSYNC},
    {Errno::el3hlt, EL3HLT},
    {Errno::el3rst, EL3RST},
    {Errno::elnrng, ELNRNG},
    {Errno::eunatch, EUNATCH},
    {Errno::enocsi, ENOCSI},
    {Errno::el2hlt, EL2HLT},
    {Errno::ebade, EBADE},
    {Errno::ebadr, EBADR},
    {Errno::exfull, EXFULL},
    {Errno::enoano, ENOANO},
    {Errno::ebadrqc, EBADRQC},
    {Errno::ebadslt, EBADSLT},
    {Errno::ebfont, EBFONT},
    {Errno::enostr, ENOSTR},
    {Errno::enodata, ENODATA},
    {Errno::etime, ETIME},
    {Errno::enosr, ENOSR},
    {Errno::enonet, ENONET},
    {Errno::enopkg, ENOPKG},
    {Errno::eremote, EREMOTE},
    {Errno::enolink, ENOLINK},
    {Errno::eadv, EADV},
    {Errno::esrmnt, ESRMNT},
    {Errno::ecomm, ECOMM},
    {Errno::eproto, EPROTO},
    {Errno::emultihop, EMULTIHOP},
    {Errno::edotdot, EDOTDOT},
    {Errno::ebadmsg, EBADMSG},
    {Errno::eoverflow, EOVERFLOW},
    {Errno::enotuniq, ENOTUNIQ},
    {Errno::ebadfd, EBADFD},
    {Errno::eremchg, EREMCHG},
    {Errno::elibacc, ELIBACC},
    {Errno::elibbad, ELIBBAD},
    {Errno::elibscn, ELIBSCN},
    {Errno::elibmax, ELIBMAX},
    {Errno::elibexec, ELIBEXEC},
    {Errno::eilseq, EILSEQ},
    {Errno::erestart, ERESTART},
    {Errno::estrpipe, ESTRPIPE},
    {Errno::eusers, EUSERS},
    {Errno::enotsock, ENOTSOCK},
    {Errno::edestaddrreq, EDESTADDRREQ},
    {Errno::emsgsize, EMSGSIZE},
    {Errno::eprototype, EPROTOTYPE},
    {Errno::enoprotoopt, ENOPROTOOPT},
    {Errno::eprotonosupport, EPROTONOSUPPORT},
    {Errno::esocktnosupport, ESOCKTNOSUPPORT},
    {Errno::eopnotsupp, EOPNOTSUPP},
    {Errno::epfnosupport, EPFNOSUPPORT},
    {Errno::eafnosupport, EAFNOSUPPORT},
    {Errno::eaddrinuse, EADDRINUSE},
    {Errno::eaddrnotavail, EADDRNOTAVAIL},
    {Errno::enetdown, ENETDOWN},
    {Errno::enetunreach, ENETUNREACH},
    {Errno::enetreset, ENETRESET},
    {Errno::econnaborted, ECONNABORTED},
    {Errno::econnreset, ECONNRESET},
    {Errno::enobufs, ENOBUFS},
    {Errno::eisconn, EISCONN},
    {Errno::enotconn, ENOTCONN},
    {Errno::eshutdown, ESHUTDOWN},
    {Errno::etoomanyrefs, ETOOMANYREFS},
    {Errno::etimedout, ETIMEDOUT},
    {Errno::econnrefused, ECONNREFUSED},
    {Errno::ehostdown, EHOSTDOWN},
    {Errno::ehostunreach, EHOSTUNREACH},
    {Errno::ealready, EALREADY},
    {Errno::einprogress, EINPROGRESS},
    {Errno::estale, ESTALE},
    {Errno::euclean, EUCLEAN},
    {Errno::enotnam, ENOTNAM},
    {Errno::enavail, ENAVAIL},
    {Errno::eisnam, EISNAM},
    {Errno::eremoteio, EREMOTEIO},
    {Errno::edquot, EDQUOT},
    {Errno::enomedium, ENOMEDIUM},
    {Errno::emediumtype, EMEDIUMTYPE},
    {Errno::ecanceled, ECANCELED},
    {Errno::enokey, ENOKEY},
    {Errno::ekeyexpired, EKEYEXPIRED},
    {Errno::ekeyrevoked, EKEYREVOKED},
    {Errno::ekeyrejected, EKEYREJECTED},
    {Errno::eownerdead, EOWNERDEAD},
    {Errno::enotrecoverable, ENOTRECOVERABLE},
    {Errno::erfkill, ERFKILL},
    {Errno::ehwpoison, EHWPOISON},
};

// True when each row names the value of its macro.
[[nodiscard]] consteval bool each_value_agrees() noexcept {
    for (KernelErrno const& row : kKernelErrnos) {
        if (std::to_underlying(row.name) != row.value) {
            return false;
        }
    }
    return true;
}

// True when the table holds each enumerator of Errno one time, so a new name
// gets a row of its macro.
[[nodiscard]] consteval bool each_name_has_a_row() noexcept {
    std::size_t names = 0;
    for (std::meta::info const enumerator : std::meta::enumerators_of(^^Errno)) {
        Errno const name = std::meta::extract<Errno>(enumerator);
        std::size_t rows = 0;
        for (KernelErrno const& row : kKernelErrnos) {
            rows += row.name == name ? 1U : 0U;
        }
        if (rows != 1U) {
            return false;
        }
        ++names;
    }
    return names == sizeof kKernelErrnos / sizeof kKernelErrnos[0];
}

static_assert(each_value_agrees(), "a name of foundation::os::Errno has a value that <asm/errno.h> does not give it");
static_assert(each_name_has_a_row(), "a name of foundation::os::Errno has no row of its macro in this file");

}  // namespace

}  // namespace foundation::os
