"""Bounded real-board numerical and runtime checks (no persistent test data)."""
import ctypes
import io
import json
import platform
import resource
import sqlite3
import ssl
import sys
import time
import zlib

start = time.perf_counter()
import numpy as np

checks = []


def check(name, fn):
    begin = time.perf_counter()
    fn()
    checks.append({'name': name, 'seconds': round(time.perf_counter() - begin, 4)})


def arrays():
    a = np.arange(12, dtype=np.float32).reshape(3, 4)
    np.testing.assert_array_equal(a.sum(axis=0), [12, 15, 18, 21])
    np.testing.assert_array_equal(a + np.arange(4),
                                  [[0, 2, 4, 6], [4, 6, 8, 10], [8, 10, 12, 14]])
    with np.errstate(divide='raise', invalid='raise'):
        try:
            np.ones(1) / np.zeros(1)
        except FloatingPointError:
            pass
        else:
            raise AssertionError('Floating-point exception handling failed')


def linalg():
    a = np.array([[4., 1., 2.], [1., 3., 0.], [2., 0., 5.]])
    b = np.array([2., 3., 4.])
    np.testing.assert_allclose(a @ np.linalg.solve(a, b), b, rtol=1e-12)
    u, s, vh = np.linalg.svd(a)
    np.testing.assert_allclose((u * s) @ vh, a, atol=1e-12)
    np.testing.assert_allclose(a @ np.linalg.inv(a), np.eye(3), atol=1e-12)


def fft():
    a = np.sin(2 * np.pi * np.arange(1024) / 32)
    spectrum = np.fft.rfft(a)
    assert int(np.argmax(np.abs(spectrum))) == 32
    np.testing.assert_allclose(np.fft.irfft(spectrum), a, atol=1e-12)


def matrix():
    rng = np.random.default_rng(42)
    a = rng.normal(size=(256, 256)).astype(np.float32)
    b = rng.normal(size=(256, 256)).astype(np.float32)
    c = a @ b
    for i, j in [(0, 0), (17, 39), (255, 255)]:
        reference = sum(float(a[i, k]) * float(b[k, j]) for k in range(256))
        np.testing.assert_allclose(c[i, j], reference, rtol=2e-5, atol=2e-5)


def image():
    # Example 720p RGB workload, approximately 2.6 MiB input.
    a = np.full((720, 1280, 3), 128, dtype=np.uint8)
    grey = (a[..., 0].astype(np.float32) * .299
            + a[..., 1] * .587 + a[..., 2] * .114).astype(np.uint8)
    assert grey.shape == (720, 1280)
    assert np.all(np.abs(grey.astype(np.int16) - 128) <= 1)


def serialization():
    b = io.BytesIO()
    a = np.arange(100, dtype=np.int32)
    np.save(b, a, allow_pickle=False)
    b.seek(0)
    np.testing.assert_array_equal(np.load(b, allow_pickle=False), a)
    assert zlib.decompress(zlib.compress(b'hello')) == b'hello'
    with sqlite3.connect(':memory:') as db:
        assert db.execute('SELECT 1 + 1').fetchone()[0] == 2
    assert ctypes.sizeof(ctypes.c_void_p) == 4


for name, fn in [('arrays_and_fenv', arrays), ('solve_svd_inverse', linalg),
                 ('fft_1024', fft), ('float32_matmul_256', matrix),
                 ('rgb_720p', image), ('serialization_and_stdlib', serialization)]:
    check(name, fn)

with open('/proc/self/maps') as f:
    libraries = sorted({line.split()[-1] for line in f if '.so' in line})
assert not any('uclibc' in path.lower() for path in libraries), libraries
report = {'status': 'PASS', 'python': sys.version, 'numpy': np.__version__,
          'architecture': platform.machine(), 'openssl': ssl.OPENSSL_VERSION,
          'checks': checks, 'elapsed_seconds': round(time.perf_counter() - start, 4),
          'peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
          'loaded_libraries': libraries}
print(json.dumps(report, indent=2))
