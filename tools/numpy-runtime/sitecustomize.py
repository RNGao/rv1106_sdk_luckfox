"""Route Python child processes through this runtime's musl launcher."""
import os
import sys

_runtime = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
_launcher = os.path.join(_runtime, 'bin', 'python3')
if os.path.isfile(_launcher):
    sys.executable = _launcher
    sys._base_executable = _launcher
