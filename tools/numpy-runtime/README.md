# Luckfox ARMv7 独立 NumPy 环境

部署目录：`/opt/python-numpy`。保留 Buildroot/uClibc 和系统 Python 3.11.6。
本环境由 Alpine v3.23 官方 armv7 APK 组装，使用 musl、Python 3.12.14、
NumPy 2.3.5、OpenBLAS 0.3.30；使用官方预编译包，没有从源码重新编译。

## 使用

在板子终端执行：

```sh
/opt/python-numpy/bin/python3 -c 'import numpy as np; print(np.__version__); print(np.arange(10).sum())'
/opt/python-numpy/bin/python3 /路径/你的脚本.py
```

从电脑执行：

```sh
adb -s f6d5349ae3693d4d shell '/opt/python-numpy/bin/python3 /opt/python-numpy/validate.py'
adb -s f6d5349ae3693d4d shell '/opt/python-numpy/bin/python3 /opt/python-numpy/validate_processes.py'
```

必须使用 `bin/python3` 启动器。它显式调用自带的 musl 加载器，设置独立
Python 路径，并将 OpenBLAS/OMP 线程数固定为 1。环境变量只作用于该进程
及其子进程，不修改 shell 配置。`sitecustomize.py` 将 `sys.executable`
和 `sys._base_executable` 指向启动器，让 Python 子进程和 multiprocessing
spawn 也经过加载器。使用 `-S` 会跳过这一适配，此时应显式指定启动器。

## 重建部署包

在 SDK 根目录执行：

```sh
python3 tools/numpy-runtime/assemble.py
```

输出：

- `output/numpy-runtime/python-numpy/`：完整运行目录和包清单 `manifest.json`。
- `output/numpy-runtime/python-numpy-armv7.tar.gz`：可部署压缩包，约 21 MB。
- `output/numpy-runtime/downloads/`：官方索引和 APK 缓存，保留以复现版本。

脚本针对本环境解析固定分支的依赖闭包，不是通用 APK 包管理器，不执行
APK 安装脚本。通过 HTTPS 下载，核对索引中的 APK 大小和 control 校验值、
control 中的 SHA-256 datahash，并记录每个 APK 的 SHA-256。未另外执行
Alpine RSA 签名验证。删除索引缓存后重新下载可能选中该分支的新版本。

首次部署到没有 `/opt/python-numpy` 的板子：

```sh
adb -s f6d5349ae3693d4d push output/numpy-runtime/python-numpy-armv7.tar.gz /opt/python-numpy-armv7.tar.gz
adb -s f6d5349ae3693d4d shell "python3 -B -c 'import os, tarfile; assert not os.path.exists(\"/opt/python-numpy\"); tarfile.open(\"/opt/python-numpy-armv7.tar.gz\", \"r:gz\").extractall(\"/opt\")'"
```

板子的 BusyBox tar 不支持 `-z`，所以用已有系统 Python 解压。
不要向已存在的其他环境直接覆盖解包。

## 实机验证与边界

RV1106 Cortex-A7，Linux 5.10.160，系统可见内存约 181 MiB。
数组/广播、浮点异常、线性方程求解、SVD、求逆、FFT、256×256 float32
矩阵乘法、720p RGB 转灰度、NumPy 内存序列化、SQLite/zlib/ctypes 均通过。
首次整套数值检查峰值 RSS 为 55132 KiB（约 53.8 MiB），这是包含测试模块
和图像处理中间数组的进程峰值，不代表所有应用都只需这么多内存。
运行目录板上占用约 51 MiB。数值检查会输出动态库路径以核对 libc 隔离。

不包含 pip。现有 uClibc/Python 3.11 原生扩展不能直接复制进本环境，
摄像头/RKNN 等厂商接口尚未集成验证。系统 Python 保持原有用途。
启动器设置的 PYTHONHOME 会传给子进程；如果需要在子进程里调用原系统
Python，请用 `/usr/bin/python3 -E` 忽略这些 Python 环境变量。

没有更改开机启动项或全局链接；文件在 ext4 根分区中持久保存，尚未重启
板子验证。停止使用该环境的进程后，可删除 `/opt/python-numpy` 回退。
