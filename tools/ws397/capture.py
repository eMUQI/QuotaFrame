"""Capture WS397 serial diagnostics or a packed framebuffer without resetting the board."""
import argparse
import re
import time
from pathlib import Path
import serial
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--command',default='');p.add_argument('--seconds',type=float,default=12);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
port=serial.Serial(port=None,baudrate=115200,timeout=.2)
port.dtr=False;port.rts=False;port.port=a.port
with port as s:
    if a.command:s.write(a.command.encode('ascii'))
    data=bytearray();deadline=time.monotonic()+a.seconds
    while time.monotonic()<deadline:
        data.extend(s.read(s.in_waiting or 1))
        if b'FRAME_END' in data:break
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
    if b'FRAME_BEGIN' in data:
        body=data.split(b'FRAME_BEGIN',1)[1].split(b'FRAME_END',1)[0]
        lines=[x.strip() for x in body.splitlines() if re.fullmatch(rb'[0-9a-f]{128}',x.strip())]
        frame=bytes.fromhex(b''.join(lines).decode())
        if len(frame)!=96000:raise RuntimeError(f'Incomplete frame: {len(frame)} bytes')
        a.output.with_suffix('.bin').write_bytes(frame)
        from PIL import Image
        pixels = bytes(((value >> shift) & 3) * 85 for value in frame for shift in (6, 4, 2, 0))
        image_path = a.output.with_suffix('.png')
        Image.frombytes('L', (800, 480), pixels).save(image_path)
        print(f'Captured {len(frame)} framebuffer bytes; expected native image: {image_path}')
    else:print(data.decode('utf-8',errors='replace'))
