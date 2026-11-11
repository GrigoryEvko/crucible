"""A planted rule module: reads of the state of the host, through a call and through a file of the kernel."""

import os
from pathlib import Path


def cpus() -> int:
    """Return the number of the CPUs of the host."""
    return os.cpu_count() or 1


def load() -> float:
    """Return the load of the host in the last minute."""
    return os.getloadavg()[0]


def free_memory() -> str:
    """Return the memory file of the kernel."""
    return Path("/proc/meminfo").read_text(encoding="utf-8")


def status(process: int) -> str:
    """Return the status file of a process."""
    return Path(f"/proc/{process}/status").read_text(encoding="utf-8")
