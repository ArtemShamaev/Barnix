#!/usr/bin/env python3
"""Draw the Happy Gouse logo and application icons; write apps/gouse/gouse_assets.h.

Everything is described as vector shapes and rasterized with 4x4 supersampling,
so the header needs no image tools. `--png DIR` also saves logo.png and an icon
sheet for documentation.
"""
import math
import pathlib
import struct
import sys
import zlib

SAMPLES = 4
INK = 0x23232f


def inside(shape, x, y):
    kind = shape[0]
    if kind == 'rect':
        _, x0, y0, x1, y1 = shape[:5]
        return x0 <= x < x1 and y0 <= y < y1
    if kind == 'rrect':
        _, x0, y0, x1, y1, r = shape[:6]
        if not (x0 <= x < x1 and y0 <= y < y1):
            return False
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    if kind == 'ellipse':
        _, cx, cy, rx, ry = shape[:5]
        return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1
    if kind == 'poly':
        points, result = shape[1], False
        for i in range(len(points)):
            (ax, ay), (bx, by) = points[i], points[i - 1]
            if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                result = not result
        return result
    if kind == 'line':
        _, ax, ay, bx, by, width = shape[:6]
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1)))
        return (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= width * width / 4
    if kind == 'ring':
        _, cx, cy, r, width = shape[:5]
        d = math.hypot(x - cx, y - cy)
        return r - width / 2 <= d <= r + width / 2
    if kind == 'arc':
        _, cx, cy, r, width, a0, a1 = shape[:7]
        d = math.hypot(x - cx, y - cy)
        if not r - width / 2 <= d <= r + width / 2:
            return False
        a = math.degrees(math.atan2(y - cy, x - cx)) % 360
        return (a0 <= a <= a1) if a0 <= a1 else (a >= a0 or a <= a1)
    raise ValueError(kind)


def bounds(shape):
    kind = shape[0]
    if kind in ('rect', 'rrect'):
        return shape[1:5]
    if kind == 'ellipse':
        return shape[1] - shape[3], shape[2] - shape[4], shape[1] + shape[3], shape[2] + shape[4]
    if kind == 'poly':
        xs, ys = [p[0] for p in shape[1]], [p[1] for p in shape[1]]
        return min(xs), min(ys), max(xs), max(ys)
    if kind == 'line':
        w = shape[5] / 2
        return min(shape[1], shape[3]) - w, min(shape[2], shape[4]) - w, max(shape[1], shape[3]) + w, max(shape[2], shape[4]) + w
    r = shape[3] + shape[4] / 2
    return shape[1] - r, shape[2] - r, shape[1] + r, shape[2] + r


def color_of(shape):
    value = shape[-1]
    return (value, 255) if isinstance(value, int) else value


def render(shapes, design, size):
    """shapes in a design x design space; returns size*size 0xAARRGGBB."""
    scale = size / design
    acc = [[0.0, 0.0, 0.0, 0.0] for _ in range(size * size)]   # premultiplied r, g, b, a
    for item in shapes:
        clip = None
        if item[0] == 'clip':
            clip, group = item[1], item[2]
        else:
            group = [item]
        for shape in group:
            rgb, alpha = color_of(shape)
            x0, y0, x1, y1 = bounds(shape)
            px0, py0 = max(0, int(x0 * scale) - 1), max(0, int(y0 * scale) - 1)
            px1, py1 = min(size, int(x1 * scale) + 2), min(size, int(y1 * scale) + 2)
            cr, cg, cb = rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255
            for py in range(py0, py1):
                for px in range(px0, px1):
                    hits = 0
                    for sy in range(SAMPLES):
                        y = (py + (sy + 0.5) / SAMPLES) / scale
                        for sx in range(SAMPLES):
                            x = (px + (sx + 0.5) / SAMPLES) / scale
                            if inside(shape, x, y) and (clip is None or inside(clip, x, y)):
                                hits += 1
                    if not hits:
                        continue
                    a = alpha / 255 * hits / (SAMPLES * SAMPLES)
                    p = acc[py * size + px]
                    p[0] = cr * a + p[0] * (1 - a)
                    p[1] = cg * a + p[1] * (1 - a)
                    p[2] = cb * a + p[2] * (1 - a)
                    p[3] = a + p[3] * (1 - a)
    out = []
    for r, g, b, a in acc:
        if a <= 0:
            out.append(0)
            continue
        out.append(round(a * 255) << 24 | min(255, round(r / a)) << 16 | min(255, round(g / a)) << 8 | min(255, round(b / a)))
    return out


def outlined(shape, grow, color=INK):
    """The same shape, larger, as an outline drawn underneath."""
    kind = shape[0]
    if kind == 'ellipse':
        return ('ellipse', shape[1], shape[2], shape[3] + grow, shape[4] + grow, color)
    if kind == 'line':
        return ('line', *shape[1:5], shape[5] + 2 * grow, color)
    if kind in ('rect', 'rrect'):
        rest = (shape[5] + grow,) if kind == 'rrect' else ()
        return (kind, shape[1] - grow, shape[2] - grow, shape[3] + grow, shape[4] + grow, *rest, color)
    raise ValueError(kind)


def with_ink(*shapes, grow=1.4):
    result = []
    for shape in shapes:
        result.append(outlined(shape, grow))
    return result + list(shapes)


# ---------------------------------------------------------------- the logo
def logo():
    tile = ('rrect', 2, 2, 62, 62, 13)
    sky = [('rect', 0, y, 64, y + 4.5, (0x2f8fe8 + (y // 4) * 0x030301) & 0xffffff) for y in range(0, 64, 4)] + [
           ('ellipse', 50, 13, 7, 7, 0xffd84a), ('ellipse', 50, 13, 9.5, 9.5, (0xffd84a, 70)),
           ('ellipse', 14, 14, 6, 3.2, 0xffffff), ('ellipse', 20, 12.5, 5, 3.6, 0xffffff),
           ('ellipse', 66, 74, 46, 24, 0x4fbf5a), ('ellipse', -4, 76, 40, 22, 0x63cf63)]
    goose = []
    goose += [('poly', [(14, 37), (5.5, 31), (4, 35.5), (7, 42), (14, 47)], INK), ('poly', [(14, 38.5), (6.3, 33), (5.6, 35.8), (8.2, 41), (14, 45.5)], 0xffffff)]
    body, neck, head = ('ellipse', 28, 44, 17, 11, 0xffffff), ('line', 38, 41, 42, 22, 8.5, 0xffffff), ('ellipse', 42, 19, 8.5, 8.5, 0xffffff)
    goose += [outlined(body, 1.4), outlined(neck, 1.4), outlined(head, 1.4), body, neck, head]
    goose += [('ellipse', 24, 43.5, 9.5, 6, 0xdfe5ef), ('arc', 24, 41.5, 8, 1.2, 20, 160, INK)]
    goose += [('line', 35.5, 31.5, 45, 32.5, 4.6, INK), ('line', 35.5, 31.5, 45, 32.5, 3, 0xe23d48),
              ('poly', [(37, 32), (33, 40), (36.5, 40.5), (39.5, 33)], INK),
              ('poly', [(37.5, 32.5), (34.2, 39.3), (36.3, 39.6), (39, 33.2)], 0xe23d48)]
    goose += [('poly', [(48.5, 15.5), (60, 19.5), (48.5, 24.5)], INK),
              ('poly', [(49.5, 16.8), (58, 19.8), (49.5, 23.3)], 0xff9a1f),
              ('poly', [(49.5, 20.5), (57, 20.5), (49.5, 23.3)], 0xe07400),
              ('arc', 45, 17.5, 6.5, 1.1, 40, 95, INK)]
    goose += [('ellipse', 44, 16, 2.1, 2.3, INK), ('ellipse', 44.8, 15.2, 0.75, 0.75, 0xffffff),
              ('ellipse', 45.5, 22, 2.3, 1.4, (0xff7aa8, 200))]
    goose += [('line', 24, 54, 23, 58.5, 2.2, 0xf08a10), ('line', 32, 54, 33, 58.5, 2.2, 0xf08a10),
              ('poly', [(19.5, 58), (26, 58), (23, 60.5)], 0xf08a10), ('poly', [(30, 58), (36.5, 58), (33, 60.5)], 0xf08a10)]
    return [('clip', tile, sky + goose)]


# ---------------------------------------------------------------- icons (32 units)
def page(x0, y0, x1, y1, fold=5):
    return [('poly', [(x0 - 1, y0 - 1), (x1 - fold + 1, y0 - 1), (x1 + 1, y0 + fold - 1), (x1 + 1, y1 + 1), (x0 - 1, y1 + 1)], INK),
            ('poly', [(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)], 0xffffff),
            ('poly', [(x1 - fold, y0), (x1, y0 + fold), (x1 - fold, y0 + fold)], 0xc9d2de)]


ICONS = {}


def icon(name):
    def register(function):
        ICONS[name] = function
        return function
    return register


@icon('executive')
def _():
    return [('rrect', 2, 8, 30, 27, 2, INK), ('poly', [(3, 9), (12, 9), (14, 6), (3, 6)], INK), ('rect', 3, 7, 13, 11, 0xe6a817),
            ('rrect', 3, 9, 29, 26, 1.5, 0xf5c242), ('rect', 3, 13, 29, 26, 0xffd65c), ('rect', 3, 13, 29, 14, 0xe6a817)]


@icon('notepad')
def _():
    return page(6, 3, 26, 29) + [('rect', 6, 3, 21, 7, 0x3b7ddd)] + [('rect', 9, y, 23, y + 1.3, 0x8090a8) for y in (11, 15, 19, 23)]


@icon('calculator')
def _():
    shapes = [('rrect', 5, 2, 27, 30, 3, INK), ('rrect', 6, 3, 26, 29, 2.5, 0xb8bfcc), ('rect', 8, 5, 24, 11, INK), ('rect', 9, 6, 23, 10, 0x9fe39b)]
    for row in range(3):
        for col in range(3):
            x, y = 8.5 + col * 5.5, 13.5 + row * 5
            shapes.append(('rrect', x, y, x + 4.3, y + 3.8, 1, 0xf04f4f if (row, col) == (2, 2) else 0xffffff))
    return shapes


@icon('clock')
def _():
    shapes = [('ellipse', 16, 16, 14, 14, INK), ('ellipse', 16, 16, 12.5, 12.5, 0xffffff), ('ring', 16, 16, 12.5, 1.5, 0x3b7ddd)]
    for k in range(12):
        a = math.radians(k * 30)
        shapes.append(('line', 16 + 9.5 * math.sin(a), 16 - 9.5 * math.cos(a), 16 + 11 * math.sin(a), 16 - 11 * math.cos(a), 1.2, INK))
    return shapes + [('line', 16, 16, 16, 7.5, 2, INK), ('line', 16, 16, 22, 19, 2, INK), ('ellipse', 16, 16, 1.6, 1.6, 0xe23d48)]


@icon('calendar')
def _():
    shapes = [('rrect', 4, 5, 28, 29, 2, INK), ('rrect', 5, 6, 27, 28, 1.5, 0xffffff), ('rect', 5, 6, 27, 12, 0xe23d48),
              ('rrect', 9, 3, 11.5, 9, 1, INK), ('rrect', 20.5, 3, 23, 9, 1, INK)]
    for row in range(3):
        for col in range(4):
            x, y = 7 + col * 5, 14 + row * 4.5
            shapes.append(('rect', x, y, x + 3.2, y + 2.8, 0x3b7ddd if (row, col) == (1, 2) else 0xb0b8c6))
    return shapes


@icon('cardfile')
def _():
    shapes = []
    for k, color in enumerate((0xc9d2de, 0xe6ecf4, 0xffffff)):
        x, y = 3 + k * 3, 4 + k * 4
        shapes += [('rect', x - 1, y - 1, x + 21, y + 15, INK), ('rect', x, y, x + 20, y + 14, color), ('rect', x, y, x + 20, y + 3, 0xe23d48 if k == 2 else 0x9aa6b8)]
    return shapes + [('rect', 11, 21, 25, 22.2, 0x8090a8), ('rect', 11, 24, 21, 25.2, 0x8090a8)]


@icon('paint')
def _():
    shapes = with_ink(('ellipse', 15, 17, 12.5, 10.5, 0xf4e2c0), grow=1.2) + [('ellipse', 20, 22, 3, 2.4, 0xffffff)]
    for x, y, c in ((9, 13, 0xe23d48), (14, 9.5, 0xffd84a), (20, 11, 0x4fbf5a), (9, 20, 0x3b7ddd)):
        shapes.append(('ellipse', x, y, 2.4, 2.4, c))
    return shapes + [('line', 21, 26, 30, 3, 3.6, INK), ('line', 21, 26, 30, 3, 2, 0xb5651d), ('line', 20, 28.5, 22.5, 22.5, 3.4, 0x3b3b4b)]


@icon('reversi')
def _():
    shapes = [('rect', 2, 2, 30, 30, INK), ('rect', 3, 3, 29, 29, 0x2f9e4f)]
    shapes += [('rect', 3 + 6.5 * k, 3, 3.6 + 6.5 * k, 29, 0x1d6e35) for k in range(1, 4)]
    shapes += [('rect', 3, 3 + 6.5 * k, 29, 3.6 + 6.5 * k, 0x1d6e35) for k in range(1, 4)]
    for (x, y, white) in ((12.8, 12.8, 1), (19.3, 12.8, 0), (12.8, 19.3, 0), (19.3, 19.3, 1)):
        shapes += [('ellipse', x, y, 2.9, 2.9, INK), ('ellipse', x, y, 2.4, 2.4, 0xffffff if white else 0x15151a)]
    return shapes


@icon('barnocraft')
def _():
    top = [(16, 3), (29, 9.5), (16, 16), (3, 9.5)]
    left = [(3, 9.5), (16, 16), (16, 30), (3, 23.5)]
    right = [(16, 16), (29, 9.5), (29, 23.5), (16, 30)]
    shapes = [('poly', [(16, 1.5), (30.5, 9), (30.5, 24.3), (16, 31.5), (1.5, 24.3), (1.5, 9)], INK),
              ('poly', top, 0x6cc24a), ('poly', left, 0x8a5a33), ('poly', right, 0x6e4526),
              ('poly', [(3, 9.5), (16, 16), (16, 19.5), (3, 13)], 0x5aa83b), ('poly', [(16, 16), (29, 9.5), (29, 13), (16, 19.5)], 0x4a8f30)]
    for x, y in ((7, 18), (11, 22), (21, 20), (25, 17), (19, 25)):
        shapes.append(('rect', x, y, x + 1.6, y + 1.6, 0x5a3a1e))
    return shapes


@icon('control')
def _():
    shapes = [('rrect', 3, 4, 29, 28, 3, INK), ('rrect', 4, 5, 28, 27, 2.5, 0xd8dee8)]
    for k, (pos, color) in enumerate(((10, 0xe23d48), (20, 0x4fbf5a), (14, 0x3b7ddd))):
        y = 10 + k * 6
        shapes += [('rect', 7, y - 0.6, 25, y + 0.6, 0x606878), ('rrect', pos - 2, y - 2.4, pos + 2, y + 2.4, 1, INK), ('rrect', pos - 1.3, y - 1.7, pos + 1.3, y + 1.7, 0.8, color)]
    return shapes


@icon('clipboard')
def _():
    return [('rrect', 5, 4, 27, 30, 2.5, INK), ('rrect', 6, 5, 26, 29, 2, 0xb5793d), ('rect', 8.5, 8, 23.5, 27, 0xffffff),
            ('rrect', 11, 2, 21, 8, 2, INK), ('rrect', 12, 3, 20, 7, 1.5, 0xc0c6d0)] + [('rect', 11, y, 21, y + 1.2, 0x8090a8) for y in (12, 16, 20)]


@icon('terminal')
def _():
    return [('rrect', 2, 4, 30, 28, 2.5, INK), ('rect', 3.5, 8, 28.5, 26.5, 0x10141c), ('rect', 3.5, 5.5, 28.5, 8, 0x9aa6b8),
            ('poly', [(7, 12), (13, 16), (7, 20), (7, 18), (10.5, 16), (7, 14)], 0x5cf07a), ('rect', 14, 19, 22, 20.8, 0x5cf07a)]


@icon('mines')
def _():
    shapes = [('rect', 2, 2, 30, 30, 0xffffff), ('rect', 4, 4, 30, 30, 0x7a8294), ('rect', 4, 4, 28, 28, 0xc4cad6)]
    for k in range(4):
        a = math.radians(k * 45)
        shapes.append(('line', 16 - 10 * math.cos(a), 16 - 10 * math.sin(a), 16 + 10 * math.cos(a), 16 + 10 * math.sin(a), 2, INK))
    return shapes + [('ellipse', 16, 16, 7, 7, INK), ('ellipse', 13.8, 13.8, 2, 2, 0xffffff)]


@icon('puzzle')
def _():
    shapes = [('rect', 2, 2, 30, 30, INK)]
    colors = (0xe23d48, 0xffd84a, 0x3b7ddd, 0x4fbf5a)
    for k in range(4):
        x, y = 3 + (k % 2) * 13.5, 3 + (k // 2) * 13.5
        if k != 3:
            shapes += [('rrect', x, y, x + 12.5, y + 12.5, 2, colors[k]), ('rrect', x + 4, y + 4, x + 8.5, y + 8.5, 1, (0xffffff, 150))]
    return shapes


@icon('about')
def _():
    return [('ellipse', 16, 16, 14, 14, INK), ('ellipse', 16, 16, 12.5, 12.5, 0x3b7ddd), ('ellipse', 16, 9.5, 2.3, 2.3, 0xffffff),
            ('rrect', 13.8, 13.5, 18.2, 24.5, 1, 0xffffff)]


@icon('program')
def _():
    return [('rect', 3, 5, 29, 27, INK), ('rect', 4, 6, 28, 26, 0xffffff), ('rect', 4, 6, 28, 10, 0x3b7ddd),
            ('rect', 7, 13, 16, 22, 0xc9d2de), ('rect', 18, 13, 25, 15, 0x8090a8), ('rect', 18, 17, 25, 19, 0x8090a8)]


@icon('shutdown')
def _():
    return [('ellipse', 16, 16, 14, 14, INK), ('ellipse', 16, 16, 12.5, 12.5, 0xe23d48),
            ('arc', 16, 17, 7, 2.6, 300, 240, 0xffffff), ('line', 16, 7, 16, 16, 2.8, 0xffffff)]


@icon('folder_open')
def _():
    return [('rrect', 2, 8, 30, 27, 2, INK), ('rect', 3, 9, 29, 26, 0xe6a817), ('poly', [(1, 14), (31, 14), (28, 27), (4, 27)], INK),
            ('poly', [(2.5, 15), (29.5, 15), (27, 26), (5, 26)], 0xffd65c)]


@icon('document')
def _():
    return page(7, 3, 25, 29) + [('rect', 10, y, 22, y + 1.2, 0x8090a8) for y in (12, 16, 20, 24)]


@icon('piano')
def _():
    shapes = [('rrect', 1.5, 6, 30.5, 27, 2.5, INK), ('rrect', 2.5, 7, 29.5, 26, 2, 0x8a4a22), ('rect', 3.5, 8, 28.5, 11, 0xb86a33)]
    shapes += [('rect', 3.5, 12, 28.5, 25, INK)]
    for k in range(5):
        x = 4 + k * 4.9
        shapes.append(('rect', x, 12.5, x + 4.3, 24.5, 0xffffff))
    for x in (7.3, 12.2, 22, 26.9):
        shapes.append(('rect', x - 1.3, 12.5, x + 1.3, 19.5, 0x15151a))
    return shapes


@icon('media')
def _():
    shapes = [('rrect', 2, 3, 30, 29, 3, INK), ('rrect', 3, 4, 29, 28, 2.5, 0x3a4458)]
    shapes += [('poly', [(7, 12.5), (11, 12.5), (16, 8), (16, 24), (11, 19.5), (7, 19.5)], 0xffffff)]
    shapes += [('arc', 15, 16, 5.5, 1.8, 300, 60, 0xffd84a), ('arc', 15, 16, 9.5, 1.8, 305, 55, 0xffd84a)]
    return shapes


@icon('browser')
def _():
    shapes = [('ellipse', 16, 16, 14, 14, INK), ('ellipse', 16, 16, 12.6, 12.6, 0x2f8fdd)]
    land = [('poly', [(8, 9), (14, 7), (17, 11), (13, 15), (14, 20), (10, 22), (7, 16)], 0x4fbf5a),
            ('poly', [(19, 16), (25, 14), (27, 19), (22, 25), (19, 22)], 0x4fbf5a)]
    shapes.append(('clip', ('ellipse', 16, 16, 12.6, 12.6), land))
    shapes += [('ring', 16, 16, 12.6, 1.4, (0xffffff, 120)), ('line', 3.5, 16, 28.5, 16, 1, (0xffffff, 90)),
               ('ellipse', 11, 10, 3, 2, (0xffffff, 110))]
    return shapes


@icon('link')
def _():
    return [('rrect', 1, 1, 31, 31, 4, INK), ('rrect', 2.2, 2.2, 29.8, 29.8, 3, 0xffffff),
            ('poly', [(9, 23), (19, 13), (19, 18), (23, 18), (23, 9), (14, 9), (14, 13), (19, 13), (9, 23)], INK),
            ('line', 9, 23, 19.5, 12.5, 3.4, INK)]


@icon('store')
def _():
    # a shopping bag with a package on it: BarnStore
    shapes = [('arc', 16, 11, 5.5, 2.4, 180, 360, INK), ('arc', 16, 11, 5.5, 1.0, 180, 360, 0xffd84a)]
    shapes += [('poly', [(4, 10), (28, 10), (29.5, 30), (2.5, 30)], INK),
               ('poly', [(5.2, 11.2), (26.8, 11.2), (28.2, 28.8), (3.8, 28.8)], 0x2f8fdd)]
    shapes += [('rrect', 10, 15, 22, 26, 1.5, INK), ('rect', 11, 16, 21, 25, 0xc98a45),
               ('rect', 15.2, 16, 16.8, 25, 0xf3d29a), ('line', 11, 19.5, 21, 19.5, 1, 0x8a5a26)]
    return shapes


@icon('code')
def _():
    # the IDE: a window with </> in it
    shapes = [('rrect', 1.5, 3, 30.5, 29, 3, INK), ('rrect', 2.7, 4.2, 29.3, 27.8, 2.2, 0x1d2330),
              ('rect', 2.7, 4.2, 29.3, 9, 0x4a8ee6)]
    shapes += [('line', 11, 13, 6, 18.5, 2.2, 0x7ee787), ('line', 6, 18.5, 11, 24, 2.2, 0x7ee787),
               ('line', 21, 13, 26, 18.5, 2.2, 0x7ee787), ('line', 26, 18.5, 21, 24, 2.2, 0x7ee787),
               ('line', 18, 12, 14, 25, 2, 0xffd84a)]
    return shapes


@icon('alpha')
def _():
    # the Alpha launcher: a dirt block with grass and a pickaxe over it
    shapes = [('rect', 4, 10, 26, 30, INK), ('rect', 5, 11, 25, 29, 0x8a5a33), ('rect', 5, 11, 25, 16, 0x5fb33a),
              ('rect', 8, 16, 10, 18, 0x5fb33a), ('rect', 15, 16, 17, 19, 0x5fb33a), ('rect', 20, 16, 22, 18, 0x5fb33a),
              ('rect', 9, 21, 11, 23, 0x6b4426), ('rect', 17, 24, 19, 26, 0x6b4426)]
    shapes += [('line', 13, 22, 28, 7, 3.2, INK), ('line', 13, 22, 28, 7, 1.6, 0xa8743a),
               ('arc', 21, 14, 10, 3.6, 248, 22, INK), ('arc', 21, 14, 10, 2, 250, 20, 0x8fd8ff)]
    return shapes


def write_png(path, width, height, pixels):
    raw = b''.join(b'\0' + b''.join(struct.pack('>I', ((p & 0xffffff) << 8) | (p >> 24)) for p in pixels[y * width:(y + 1) * width])
                   for y in range(height))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    output = pathlib.Path(sys.argv[1])
    png = pathlib.Path(sys.argv[sys.argv.index('--png') + 1]) if '--png' in sys.argv else None
    names = list(ICONS)
    logo_shapes = logo()
    logo128, logo48, logo16 = render(logo_shapes, 64, 128), render(logo_shapes, 64, 48), render(logo_shapes, 64, 16)
    icons32 = [render(ICONS[n](), 32, 32) for n in names]
    icons16 = [render(ICONS[n](), 32, 16) for n in names]

    def table(name, values, per_line=8):
        lines = [f'static const unsigned int {name}[{len(values)}] = {{']
        for i in range(0, len(values), per_line):
            lines.append('    ' + ','.join(f'0x{v:08x}u' for v in values[i:i + per_line]) + ',')
        return lines + ['};']
    lines = ['/* Generated by scripts/gouse_assets.py; do not edit. */', '#ifndef GOUSE_ASSETS_H', '#define GOUSE_ASSETS_H',
             'enum {' + ', '.join(f'ICON_{n.upper()}' for n in names) + ', ICON_COUNT};']
    # The pixels are stored deflated (bnet_inflate unpacks them at start-up):
    # Happy Gouse has to fit the 640 KiB file limit of Barnix.
    pixels = logo128 + logo48 + logo16 + [v for i in icons32 for v in i] + [v for i in icons16 for v in i]
    packed = zlib.compress(struct.pack(f'<{len(pixels)}I', *pixels), 9)
    lines.append(f'#define GOUSE_LOGO128 0\n#define GOUSE_LOGO48 {len(logo128)}\n#define GOUSE_LOGO16 {len(logo128) + len(logo48)}')
    lines.append(f'#define GOUSE_ICONS32 {len(logo128) + len(logo48) + len(logo16)}\n#define GOUSE_ICONS16 {len(logo128) + len(logo48) + len(logo16) + 1024 * len(names)}')
    lines.append(f'#define GOUSE_PIXELS {len(pixels)}')
    lines.append(f'static const unsigned char gouse_packed[{len(packed)}] = {{')
    for i in range(0, len(packed), 24):
        lines.append('    ' + ','.join(str(b) for b in packed[i:i + 24]) + ',')
    lines.append('};')
    lines.append('static const char *const gouse_icon_names[ICON_COUNT] = {' + ', '.join(f'"{n}"' for n in names) + '};')
    lines.append('static const int gouse_sin60[60] = {' + ','.join(str(round(math.sin(math.radians(i * 6)) * 65536)) for i in range(60)) + '};')
    lines.append('#endif')
    output.write_text('\n'.join(lines) + '\n')
    if png:
        png.mkdir(parents=True, exist_ok=True)
        write_png(png / 'logo.png', 128, 128, logo128)
        sheet_w = len(names) * 36
        sheet = [0] * (sheet_w * 56)
        for k, (big, small) in enumerate(zip(icons32, icons16)):
            for y in range(32):
                for x in range(32):
                    sheet[(2 + y) * sheet_w + k * 36 + 2 + x] = big[y * 32 + x]
            for y in range(16):
                for x in range(16):
                    sheet[(38 + y) * sheet_w + k * 36 + 10 + x] = small[y * 16 + x]
        write_png(png / 'icons.png', sheet_w, 56, sheet)


if __name__ == '__main__':
    main()
