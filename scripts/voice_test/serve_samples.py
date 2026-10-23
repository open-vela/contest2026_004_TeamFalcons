#!/usr/bin/env python3
"""Bounded, local-only HTTP fixture serving generated WAVs, no directory listing."""
import argparse
import functools
import http.server
import re
import time
import urllib.parse
p=argparse.ArgumentParser()
p.add_argument("directory")
p.add_argument("--seconds",type=int,default=600)
a=p.parse_args()
if not 1 <= a.seconds <= 900: p.error("seconds must be 1..900")
class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if not re.fullmatch(r"/[a-z]+[0-9]{2}\.wav", urllib.parse.urlsplit(self.path).path):
            self.send_error(404)
            return
        super().do_GET()
server=http.server.HTTPServer(("192.168.137.1",8080),functools.partial(Handler,directory=a.directory))
server.timeout=1
end=time.monotonic()+a.seconds
print("fixture ready 192.168.137.1:8080",flush=True)
try:
    for _ in range(1000):
        if time.monotonic() >= end: break
        server.handle_request()
finally:
    server.server_close()
    print("fixture stopped",flush=True)
