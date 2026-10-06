"""Launch the private-prefix compositor with its library paths scoped to this process."""

import os
import sys
from pathlib import Path

from weston_environment import environment

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    executable = root / "build/weston-install/bin/weston"
    os.execve(executable, [str(executable), *sys.argv[1:]], environment(root))
