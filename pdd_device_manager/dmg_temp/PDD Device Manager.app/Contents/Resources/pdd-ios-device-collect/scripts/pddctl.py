#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys


RUNTIME_DIR = Path(__file__).resolve().parents[1] / "runtime"
if str(RUNTIME_DIR) not in sys.path:
    sys.path.insert(0, str(RUNTIME_DIR))

from pddctl_app.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
