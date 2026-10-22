#!/usr/bin/env python3
"""Keep a TCP listener alive while the native runtime selects another port."""
import socket
import subprocess
import sys

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as occupied:
    occupied.bind(('127.0.0.1', 0))
    occupied.listen()
    subprocess.run([sys.argv[1], '--busy-web-port', str(occupied.getsockname()[1])],
                   check=True, timeout=25)
