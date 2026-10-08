"""A planted module that reads the symbols, the relocations and a disassembly of an archive as the text of tools."""

from assay.core import proc


def undefined_lines(archive):
    """Return the lines of nm for the undefined symbols of an archive."""
    return proc.run(proc.Command(["nm", "-A", "--undefined-only", archive], timeout_s=60.0)).out.splitlines()


def local_function_lines(archive):
    """Return the lines of readelf for the symbols of an archive."""
    return proc.run(proc.Command(["readelf", "-sW", archive], timeout_s=60.0)).out.splitlines()


def demangled(names):
    """Return the text of c++filt for each name."""
    text = "".join(f"{name}\n" for name in names).encode()
    return proc.run(proc.Command(["c++filt"], timeout_s=60.0, stdin=text)).out.splitlines()


def disassembly_lines(path):
    """Return the lines of objdump for the code of an object."""
    return proc.run(proc.Command(["objdump", "-dr", path], timeout_s=60.0)).out.splitlines()
