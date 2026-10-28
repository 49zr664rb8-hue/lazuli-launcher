# -*- coding: utf-8 -*-
"""生成琉璃启动器图标（纯 Python，无第三方依赖）"""
import zlib, struct, math, os

def png_bytes(w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def lerp(a, b, t):
    return a + (b - a) * t

def render(size, ss=3):
    S = size * ss
    buf = bytearray(S * S * 4)
    rad = S * 0.22
    # 背景渐变
    c1 = (86, 170, 255)
    c2 = (20, 92, 214)
    for y in range(S):
        t = y / (S - 1)
        r = lerp(c1[0], c2[0], t); g = lerp(c1[1], c2[1], t); b = lerp(c1[2], c2[2], t)
        for x in range(S):
            i = (y * S + x) * 4
            buf[i] = int(r); buf[i+1] = int(g); buf[i+2] = int(b); buf[i+3] = 255

    def round_mask(px, py):
        # 圆角矩形遮罩（含 1px 抗锯齿）
        cx = min(max(px, rad), S - rad)
        cy = min(max(py, rad), S - rad)
        d = math.hypot(px - cx, py - cy)
        return max(0.0, min(1.0, rad - d + 0.5))

    # 立方体
    cx, cy = S * 0.5, S * 0.52
    s = S * 0.30
    k = 0.866
    top = [(cx, cy - s), (cx + s*k, cy - s*0.5), (cx, cy), (cx - s*k, cy - s*0.5)]
    left = [(cx - s*k, cy - s*0.5), (cx, cy), (cx, cy + s), (cx - s*k, cy + s*0.5)]
    right = [(cx + s*k, cy - s*0.5), (cx, cy), (cx, cy + s), (cx + s*k, cy + s*0.5)]

    def in_poly(px, py, poly):
        inside = False
        n = len(poly)
        for i in range(n):
            x1, y1 = poly[i]; x2, y2 = poly[(i+1) % n]
            if ((y1 > py) != (y2 > py)) and (px < (x2-x1)*(py-y1)/(y2-y1+1e-9)+x1):
                inside = not inside
        return inside

    faces = [(top, (186, 228, 255)), (left, (108, 178, 250)), (right, (58, 132, 226))]
    for y in range(S):
        for x in range(S):
            i = (y*S + x)*4
            px, py = x + 0.5, y + 0.5
            a = round_mask(px, py)
            if a <= 0:
                buf[i+3] = 0
                continue
            # 高光
            hl = max(0.0, 1.0 - math.hypot(px - S*0.28, py - S*0.18) / (S*0.55)) * 0.30
            for poly, col in faces:
                if in_poly(px, py, poly):
                    buf[i] = min(255, int(col[0] + 255*hl*0.5))
                    buf[i+1] = min(255, int(col[1] + 255*hl*0.5))
                    buf[i+2] = min(255, int(col[2] + 255*hl*0.5))
                    break
            else:
                buf[i] = min(255, int(buf[i] + 255*hl))
                buf[i+1] = min(255, int(buf[i+1] + 255*hl))
                buf[i+2] = min(255, int(buf[i+2] + 255*hl))
            buf[i+3] = int(255 * a)

    # 下采样
    out = bytearray(size*size*4)
    for y in range(size):
        for x in range(size):
            r = g = b = a = 0
            for dy in range(ss):
                for dx in range(ss):
                    i = ((y*ss+dy)*S + (x*ss+dx))*4
                    al = buf[i+3]
                    r += buf[i]*al; g += buf[i+1]*al; b += buf[i+2]*al; a += al
            n = ss*ss
            o = (y*size + x)*4
            if a > 0:
                out[o] = min(255, r//a); out[o+1] = min(255, g//a); out[o+2] = min(255, b//a)
            out[o+3] = a // n
    return out

def main():
    sizes = [16, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        px = render(s, 3 if s <= 64 else 2)
        images.append((s, png_bytes(s, s, px)))
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b''
    data = b''
    for s, png in images:
        entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
        data += png
    with open(os.path.join(os.path.dirname(__file__), '..', 'res', 'app.ico'), 'wb') as f:
        f.write(header + entries + data)
    print('ok')

main()
