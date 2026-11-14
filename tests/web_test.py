"""Gouse Web engine on the host (tests/web_render): layout facts of the test
pages, URL resolution, form submission, and pages cut or mangled anywhere
must not crash the engine."""
import os
os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
import random
import re
import subprocess
import sys
import tempfile

render, font = sys.argv[1], sys.argv[2]
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'web')
failures = 0


def check(name, condition, detail=''):
    global failures
    if not condition:
        failures += 1
        print('FAIL', name, str(detail)[:400])


def run(page, width=800, dump=True):
    with tempfile.TemporaryDirectory() as t:
        out = subprocess.run([render, font, page, str(width), os.path.join(t, 'out.ppm'), '600'] + (['dump'] if dump else []),
                             capture_output=True, text=True, timeout=60)
    if out.returncode:
        return None, out.stderr
    items = []
    for line in out.stdout.splitlines():
        m = re.match(r'(\d+) kind=(\d+) x=(-?\d+) y=(-?\d+) w=(-?\d+) h=(-?\d+) color=(\w+) fixed=(\d) ?(.*?)( \[link\])?$', line)
        if m:
            items.append({'kind': int(m[2]), 'x': int(m[3]), 'y': int(m[4]), 'w': int(m[5]), 'h': int(m[6]), 'color': m[7],
                          'fixed': int(m[8]), 'text': m[9], 'link': bool(m[10])})
    return items, out.stdout


def text(items, word):
    return [i for i in items if i['kind'] == 3 and i['text'] == word]


# ------------------------------------------------------------ features.html
items, out = run(os.path.join(here, 'features.html'))
check('features renders', items is not None, out)
if items:
    check('title', 'title: Gouse Web — проверка' in out, out[:200])
    one = [i for i in text(items, '1') if i['h'] == 16]
    row = one[0]['y'] if one else -1
    two = [i for i in text(items, '2') if i['y'] == row]
    four = [i for i in text(items, '4') if i['h'] == 16 and i['y'] > row]
    check('grid: 1 and 2 share a row', one and two and one[0]['y'] == two[0]['y'] and two[0]['x'] > one[0]['x'] + 150, (one, two))
    check('grid: 4 starts the next row', one and four and four[0]['y'] > one[0]['y'] and abs(four[0]['x'] - one[0]['x']) < 3, (one, four))
    link = text(items, 'ссылка')
    check('links are links', link and link[0]['link'], link)
    box = [i for i in items if i['kind'] == 1 and i['color'] == 'ffffd9a0']
    beside = [i for i in items if box and i['kind'] == 3 and box[0]['y'] < i['y'] - 4 < box[0]['y'] + box[0]['h'] and i['x'] < box[0]['x']]
    after = [i for i in items if box and i['kind'] == 3 and i['y'] - 16 > box[0]['y'] + box[0]['h'] + 8 and i['y'] < box[0]['y'] + 200]
    check('text flows left of the float', box and beside and max(i['x'] + i['w'] for i in beside) <= box[0]['x'], (box, beside[-3:]))
    check('and under it afterwards', after and max(i['x'] + i['w'] for i in after) > box[0]['x'], after[-3:])
    c, python = text(items, 'C'), text(items, 'Python')
    years = text(items, '1972') + text(items, '1991')
    check('table columns line up', len(years) == 2 and years[0]['x'] == years[1]['x'], years)
    check('table rows in order', c and python and python[0]['y'] > c[0]['y'], (c, python))
    marker = [i for i in items if i['kind'] == 9]
    check('list markers', len(marker) >= 8, len(marker))
    check('ordered list numbers', any(i['text'] == '10. ' for i in marker) and any(i['text'] == '11. ' for i in marker), [i['text'] for i in marker])
    check('::before content', text(items, '★'), [i['text'] for i in items if i['kind'] == 3][:40])
    check('images', any(i['kind'] == 5 and i['w'] == 64 for i in items) and any(i['kind'] == 5 and i['w'] == 32 for i in items))
    check('form submits GET', 'submit 0 ' in out and 'search.html?q=' in out and 'r=1' in out and 's=' in out, [l for l in out.splitlines() if l.startswith('submit')])
    check('header gradient', any(i['kind'] == 1 and i['w'] == 800 and i['y'] == 0 for i in items))

# ------------------------------------------------------------ layout1251.html
items, out = run(os.path.join(here, 'layout1251.html'))
check('layout renders', items is not None, out)
if items:
    check('windows-1251', text(items, 'Раскладка'), [i['text'] for i in items if i['kind'] == 3][:10])
    check('display: none', not text(items, 'must'))
    check('visibility: hidden keeps space', not text(items, 'invisible') and text(items, 'after.') and text(items, 'after.')[0]['x'] - text(items, 'Before')[0]['x'] > 60)
    fixed = [i for i in items if i['fixed'] == 1 and i['kind'] == 3]
    check('position: fixed', any(i['text'] == 'Fixed' for i in fixed), fixed[:3])
    absolute = text(items, 'absolute,')
    check('position: absolute (right, bottom)', absolute and absolute[0]['x'] > 600, absolute)
    centered = text(items, 'Centered')
    check('flex centering', centered and 280 < centered[0]['x'] < 400, centered)
    code = [i for i in items if i['kind'] == 3 and i['text'].startswith('printf')]
    check('pre keeps indentation', code and code[0]['x'] > 50, code)
    check('entities', text(items, '©') and text(items, '€') and text(items, '—'))
    words = [i for i in items if i['kind'] == 3 and i['text'].startswith('Averyvery')]
    check('long words break', words and words[0]['w'] <= 160, words)

# ------------------------------------------------------------ URLs
cases = [
    ('https://a.b/c/d.html', 'e.html', 'https://a.b/c/e.html'),
    ('https://a.b/c/d.html', '../e', 'https://a.b/e'),
    ('https://a.b/c/d.html', '/x/./y/../z', 'https://a.b/x/z'),
    ('https://a.b/c/d.html?q=1', '?r=2', 'https://a.b/c/d.html?r=2'),
    ('https://a.b/c/d.html', '#top', 'https://a.b/c/d.html#top'),
    ('https://a.b/c/', '//cdn.c/s.css', 'https://cdn.c/s.css'),
    ('https://a.b', 'p', 'https://a.b/p'),
    ('http://a.b/c/d', 'mailto:x@y', 'mailto:x@y'),
    ('file:///home/root/a.html', 'b/c.html', 'file:///home/root/b/c.html'),
    ('https://a.b/c/d', 'e f', 'https://a.b/c/e%20f'),
]
for base, href, want in cases:
    got = subprocess.run([render, '--url', base, href], capture_output=True, text=True).stdout.strip()
    check(f'url {base} + {href}', got == want, got)

# ------------------------------------------------------------ robustness
rng = random.Random(1)
source = open(os.path.join(here, 'features.html'), encoding='utf-8').read()
pieces = ['<div>', '</div>', '<table><tr><td>', '</td></tr>', '<p>', '<li>', '<b><i>', '</b>', '<!--', '-->', '<style>a{', '}</style>',
          '&amp', '&#xffff;', '<img src=x>', '<input>', '<svg><path/>', '"\'', '<script>', '</script>', 'display:flex;', '{{', '}}',
          'calc(1px +', 'var(--x', '@media (', '<select><option>', '</table>', '<pre>', '\x00', '\xff', '<a href=', '::before{content:"', ')']
with tempfile.TemporaryDirectory() as t:
    for k in range(60):
        text_ = source
        for _ in range(rng.randrange(5, 40)):
            if not text_:
                break
            at = rng.randrange(len(text_))
            choice = rng.random()
            if choice < 0.4:
                text_ = text_[:at] + rng.choice(pieces) + text_[at:]
            elif choice < 0.7:
                text_ = text_[:at] + text_[at + rng.randrange(1, 200):]
            else:
                text_ = text_[:at]
        page = os.path.join(t, f'fuzz{k}.html')
        open(page, 'w', encoding='utf-8', errors='replace').write(text_)
        result = subprocess.run([render, font, page, str(rng.choice([320, 800, 1200])), os.path.join(t, 'o.ppm'), '600'],
                                capture_output=True, text=True, timeout=60)
        check(f'mangled page {k} does not crash', result.returncode == 0, result.stderr[-300:])
        if failures > 5:
            break

if failures:
    sys.exit(f'{failures} Gouse Web check(s) failed')
print('Gouse Web: layout, fixed/absolute/flex/grid/float/table, forms, encodings, URLs and 60 mangled pages passed')
