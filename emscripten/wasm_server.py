#!/usr/bin/env python3
"""
WASM HTTPS 开发服务器 — Python 生成自签名证书，自动添加 COOP/COEP 响应头，支持 pthreads SharedArrayBuffer。

用法:
    python script/wasm_server.py [端口号] [输出目录]

参数:
    端口号    可选，默认 5500
    输出目录  可选，指定服务目录（相对或绝对路径）
              也可通过环境变量 WASM_OUT_DIR 设置
              未指定时使用当前目录

示例:
    python script/wasm_server.py
    python script/wasm_server.py 5500
    python script/wasm_server.py 5500 out/emscripten/Debug
    set WASM_OUT_DIR=out/emscripten/Debug && python script/wasm_server.py

服务器会:
    1. 为所有 .html/.js/.wasm 等文件添加 COOP/COEP 头
    2. 切换到指定目录（默认当前目录）
    3. 显示访问地址
"""

import http.server
import os
import sys
import re
import mimetypes
import ssl
import socket
import datetime
import ipaddress

try:
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
except ImportError:
    print("❌ 需要安装 cryptography 库:")
    print("   pip install cryptography")
    sys.exit(1)

mimetypes.add_type('application/wasm', '.wasm')
mimetypes.add_type('application/javascript', '.js')
mimetypes.add_type('text/html', '.html')
mimetypes.add_type('application/octet-stream', '.data')

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5500

if len(sys.argv) > 2:
    OUT_DIR = sys.argv[2]
elif os.environ.get("WASM_OUT_DIR"):
    OUT_DIR = os.environ["WASM_OUT_DIR"]
else:
    OUT_DIR = os.getcwd()

OUT_DIR = os.path.abspath(OUT_DIR)
if not os.path.isdir(OUT_DIR):
    print(f"错误: 找不到目录 {OUT_DIR}")
    sys.exit(1)

os.chdir(OUT_DIR)
print(f"服务目录: {OUT_DIR}")


def generate_self_signed_cert(cert_file="cert.pem", key_file="key.pem"):
    """用 Python 生成自签名证书"""
    
    if os.path.exists(cert_file) and os.path.exists(key_file):
        print(f"✅ 证书已存在: {cert_file}, {key_file}")
        return cert_file, key_file
    
    print("📜 生成自签名证书...")
    
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )

    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "CN"),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "Hubei"),
        x509.NameAttribute(NameOID.LOCALITY_NAME, "Wuhan"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "WASM Dev"),
        x509.NameAttribute(NameOID.COMMON_NAME, "localhost"),
    ])

    cert = x509.CertificateBuilder() \
        .subject_name(subject) \
        .issuer_name(issuer) \
        .public_key(private_key.public_key()) \
        .serial_number(x509.random_serial_number()) \
        .not_valid_before(datetime.datetime.now(datetime.UTC)) \
        .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=365)) \
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName("localhost"),
                x509.DNSName("*.local"),
                x509.IPAddress(ipaddress.IPv4Address("127.0.0.1")),
                x509.IPAddress(ipaddress.IPv4Address("0.0.0.0")),
            ]),
            critical=False,
        ) \
        .sign(private_key, hashes.SHA256())

    with open(key_file, "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        ))

    with open(cert_file, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))

    print(f"✅ 证书生成成功: {cert_file}, {key_file}")
    return cert_file, key_file


class WASMHandler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        ext = os.path.splitext(path)[1].lower()
        if ext == '.wasm':
            return 'application/wasm'
        elif ext == '.js':
            return 'application/javascript'
        elif ext == '.html':
            return 'text/html'
        elif ext == '.data':
            return 'application/octet-stream'
        return super().guess_type(path)

    def do_GET(self):
        path = self.translate_path(self.path)
        if not os.path.exists(path):
            self.send_error(404)
            return
        if os.path.isdir(path):
            self.send_error(403)
            return

        stat = os.stat(path)
        file_size = stat.st_size

        range_header = self.headers.get('Range')
        if range_header:
            match = re.match(r'bytes=(\d+)-(\d*)$', range_header)
            if match:
                start = int(match.group(1))
                end = int(match.group(2)) if match.group(2) else file_size - 1
                if start >= file_size:
                    self.send_error(416)
                    return
                end = min(end, file_size - 1)
                length = end - start + 1

                self.send_response(206)
                self.send_header('Content-Type', self.guess_type(path))
                self.send_header('Content-Range', f'bytes {start}-{end}/{file_size}')
                self.send_header('Content-Length', str(length))
                self.send_header('Accept-Ranges', 'bytes')
                self.end_headers()

                with open(path, 'rb') as f:
                    f.seek(start)
                    self.wfile.write(f.read(length))
                return

        self.send_response(200)
        self.send_header('Content-Type', self.guess_type(path))
        self.send_header('Content-Length', str(file_size))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()

        with open(path, 'rb') as f:
            self.wfile.write(f.read())

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()

    def log_message(self, format, *args):
        msg = ' '.join(str(a) for a in args) if args else format
        print(f"[{self.log_date_time_string()}] {msg}")


if __name__ == '__main__':
    cert_file = "cert.pem"
    key_file = "key.pem"
    
    generate_self_signed_cert(cert_file, key_file)
    
    # ✅ 修复：用 SSLContext 替代 ssl.wrap_socket
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=cert_file, keyfile=key_file)
    
    server = http.server.HTTPServer(('0.0.0.0', PORT), WASMHandler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    
    print(f"\n🔒 WASM HTTPS 开发服务器启动:")
    print(f"   本地: https://localhost:{PORT}/index.html?game=data.xp3")
    print(f"   手机: https://{local_ip}:{PORT}/index.html?game=data.xp3")
    print(f"   (自签名证书，浏览器会提示不安全，点击'继续访问'即可)")
    print("COOP/COEP 头已启用 ✓")
    print("按 Ctrl+C 停止\n")
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止")
        server.server_close()