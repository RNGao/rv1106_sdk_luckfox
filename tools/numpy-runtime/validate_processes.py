"""Check the isolated loader also works for Python child processes."""
import json
import multiprocessing as mp
import os
import subprocess
import sys


def worker(send):
    import numpy as np
    send.send(int(np.arange(10).sum()))
    send.close()


if __name__ == '__main__':
    assert sys.executable == '/opt/python-numpy/bin/python3', sys.executable
    out = subprocess.check_output(
        [sys.executable, '-c', 'import numpy as np; print(int(np.arange(10).sum()))'],
        text=True, timeout=60)
    assert out.strip() == '45', out
    assert subprocess.check_output(['/bin/sh', '-c', 'echo ok'], text=True).strip() == 'ok'
    ctx = mp.get_context('spawn')
    receive, send = ctx.Pipe(duplex=False)
    p = ctx.Process(target=worker, args=(send,))
    p.start()
    send.close()
    try:
        assert receive.poll(60), 'spawn timed out'
        assert receive.recv() == 45
        p.join(10)
        assert p.exitcode == 0, p.exitcode
    finally:
        if p.is_alive():
            p.terminate()
            p.join()
        receive.close()
    import numpy as np
    assert len(os.listdir('/proc/self/task')) == 1
    print(json.dumps({'status': 'PASS', 'checks': ['python_subprocess',
          'system_shell_subprocess', 'multiprocessing_spawn_numpy', 'single_thread'],
          'executable': sys.executable}))
