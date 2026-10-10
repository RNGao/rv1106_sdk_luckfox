# 开发板 HTTPS 流式测速（C / OpenSSL）

板子运行 C HTTPS 服务，Ubuntu 用 curl 上传及下载。每次固定传输 64 MiB，
使用 64 KiB 缓冲区，不在板子保存大文件。上传边接收边计算 SHA-256；
下载重复发送 block.bin 中的 64 KiB 数据。没有启用 HTTP 压缩。
它测试当前 Wi-Fi、TLS、程序与 Ubuntu 虚拟机路径的综合速度，不代表射频极限。

## 编译

SDK 根目录执行（依赖本 SDK 已构建的 Buildroot OpenSSL）：

```sh
./tools/https-bench/build.sh
adb push output/https-bench/https_bench /tmp/luckfox-https-test/
```

## 板子启动

本次测试已在两端 `/tmp/luckfox-https-test/` 准备证书、令牌及数据。
证书有效期两天；板子重启会丢失临时文件。IP 改变时须重新生成包含正确 IP SAN 的证书。

```sh
/tmp/luckfox-https-test/https_bench 192.168.10.87 \
  /tmp/luckfox-https-test/server.crt /tmp/luckfox-https-test/server.key \
  /tmp/luckfox-https-test/token /tmp/luckfox-https-test/block.bin
```

前台运行，Ctrl+C 停止。服务只绑定指定 Wi-Fi IP 的 8443 端口，
最低 TLS 1.2，Bearer 令牌认证，单连接处理，仅用于局域网测速。

## Ubuntu 测试

```sh
cd /tmp/luckfox-https-test
# Ubuntu -> 板子；响应包含接收字节数和 SHA-256
curl --fail --noproxy '*' --cacert server.crt --config curl.conf \
  --connect-timeout 5 --max-time 180 -H 'Expect:' \
  -X POST --upload-file payload-64MiB.bin https://192.168.10.87:8443/upload \
  -w '\nseconds=%{time_total} bytes/s=%{speed_upload}\n'
# 板子 -> Ubuntu
curl --fail --noproxy '*' --cacert server.crt --config curl.conf \
  --connect-timeout 5 --max-time 180 https://192.168.10.87:8443/download \
  -o c-download-64MiB.bin \
  -w '\nseconds=%{time_total} bytes/s=%{speed_download}\n'
sha256sum payload-64MiB.bin c-download-64MiB.bin
```

MB/s = bytes/s ÷ 1,000,000；Mbps = bytes/s × 8 ÷ 1,000,000。
本程序不会配置 Wi-Fi、安装开机服务或改变 DHCP 设置。

## 本次实测（2026-09-29）

每方向单次 67,108,864 字节，TLS 1.3，验证测试证书，不跳过验证。

| 板子服务实现 | Ubuntu → 板子 | 板子 → Ubuntu |
|---|---:|---:|
| Python + OpenSSL | 17.279 秒，3.88 MB/s（31.1 Mbps） | 78.645 秒，0.85 MB/s（6.83 Mbps） |
| C + OpenSSL | 14.340 秒，4.68 MB/s（37.4 Mbps） | 67.968 秒，0.99 MB/s（7.90 Mbps） |

四次传输均 HTTP 200；接收 SHA-256 与源数据一致：
`7d53af12cf46263683329de7450eb3a45d98e7e300bc7d3b4f1bc4518d8bca6e`。

这是按顺序进行的单次测试，Wi-Fi 状态会波动，不能把全部差异归因于语言。
Ubuntu 经虚拟机 NAT 访问板子，板子走 wlan0；板子存在两个 DHCP 客户端，
本次使用可正常通信的 192.168.10.87。板子发送明显慢于接收，换 C 后仍然如此，
需要进一步测 TCP、无线重传及驱动/省电状态才能定位瓶颈。
