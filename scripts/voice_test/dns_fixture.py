#!/usr/bin/env python3
"""Bounded DNS fault fixture; localhost host tests or the board's isolated peer."""
import argparse
import socket
import socketserver
import struct
import subprocess
import threading
import time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--bind",choices=("127.0.0.1","192.168.137.1"),default="127.0.0.1")
p.add_argument("--port",type=int,default=0)
p.add_argument("--seconds",type=int,default=20)
p.add_argument("--run")
a=p.parse_args()
if not 1 <= a.seconds <= 900 or not 0 <= a.port <= 65535: p.error("invalid bounds")
slots=threading.BoundedSemaphore(4)
lock=threading.Lock()
requests=0

def reply(data,tcp=False):
    global requests
    with lock:
        requests+=1
        if requests>200: return None,None
    if not 12 <= len(data) <= 512: return None,None
    labels=[]
    pos=12
    while pos<len(data) and data[pos]:
        size=data[pos]
        if size>63 or pos+1+size>=len(data): return None,None
        labels.append(data[pos+1:pos+1+size].decode("ascii",errors="replace"))
        pos+=size+1
    if pos+5>len(data): return None,None
    name=".".join(labels)
    question=data[12:pos+5]
    if name=="stall.test": return None,name
    if name=="short.test": return b"bad",name
    if name in ("tcp.test","tcpstall.test","half.test") and not tcp:
        return struct.pack("!HHHHHH",int.from_bytes(data[:2],"big"),0x8380,1,0,0,0)+question,name
    ident=int.from_bytes(data[:2],"big") ^ (1 if name=="wrongid.test" else 0)
    answer=b"\xc0\x0c"+struct.pack("!HHIH",1,1,60,4)+socket.inet_aton("127.0.0.1")
    return struct.pack("!HHHHHH",ident,0x8180,1,1,0,0)+question+answer,name

class Bounded(socketserver.ThreadingMixIn):
    daemon_threads=True
    allow_reuse_address=True
    def process_request(self,request,address):
        if not slots.acquire(False):
            self.shutdown_request(request)
            return
        try: super().process_request(request,address)
        except BaseException:
            slots.release()
            raise
    def process_request_thread(self,request,address):
        try: super().process_request_thread(request,address)
        finally: slots.release()
class UDP(Bounded,socketserver.UDPServer): pass
class TCP(Bounded,socketserver.TCPServer): pass
class Datagram(socketserver.BaseRequestHandler):
    def handle(self):
        data,sock=self.request
        response,_=reply(data)
        if response is not None: sock.sendto(response,self.client_address)
class Stream(socketserver.BaseRequestHandler):
    def handle(self):
        sock=self.request
        sock.settimeout(.5)
        def readn(n):
            result=b""
            while len(result)<n:
                part=sock.recv(n-len(result))
                if not part: raise EOFError
                result+=part
            return result
        try:
            length=int.from_bytes(readn(2),"big")
            if length>512: return
            data=readn(length)
            response,name=reply(data,True)
            if response is None: return
            prefix=len(response).to_bytes(2,"big")
            if name in ("tcpstall.test","half.test"):
                sock.sendall(prefix[:1] if name=="half.test" else prefix)
                while sock.recv(64): pass
                return
            # Force split length and body reads through the production parser.
            for part in (prefix[:1],prefix[1:]):
                sock.sendall(part)
                time.sleep(.002)
            for off in range(0,len(response),7):
                sock.sendall(response[off:off+7])
                time.sleep(.002)
        except (OSError,EOFError): pass

with UDP((a.bind,a.port),Datagram) as udp:
    port=udp.server_address[1]
    with TCP((a.bind,port),Stream) as tcp:
        threads=[threading.Thread(target=s.serve_forever,kwargs={"poll_interval":.05},daemon=True) for s in (udp,tcp)]
        for thread in threads: thread.start()
        print(f"DNS fixture {a.bind}:{port} udp/tcp max_workers=4 max_requests=200",flush=True)
        try:
            if a.run:
                subprocess.run([str(Path(a.run).resolve()),str(port)],check=True,timeout=a.seconds)
            else:
                end=time.monotonic()+a.seconds
                while time.monotonic()<end:
                    time.sleep(.1)
        finally:
            for server in (udp,tcp): server.shutdown()
            for thread in threads: thread.join()
            print(f"DNS fixture stopped requests={requests}",flush=True)
