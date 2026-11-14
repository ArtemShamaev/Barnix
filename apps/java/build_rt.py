#!/usr/bin/env python3
"""Compile the BJVM class library (apps/java/lib) into rt.jar (stored entries)."""
import os, subprocess, sys, zipfile, shutil
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, 'lib')
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'obj', 'rt.jar')
classes = os.path.join(os.path.dirname(out), 'rt-classes')
shutil.rmtree(classes, ignore_errors=True)
os.makedirs(classes)
files = sorted(os.path.join(d, f) for d, _, fs in os.walk(src) for f in fs if f.endswith('.java'))
listing = os.path.join(os.path.dirname(out), 'rt-sources.txt')
open(listing, 'w').write('\n'.join(files) + '\n')
subprocess.run(['javac', '-source', '8', '-target', '8', '-bootclasspath', classes, '-sourcepath', src,
                '-encoding', 'UTF-8', '-nowarn', '-Xlint:-options', '-XDignore.symbol.file', '-implicit:class',
                '-d', classes, '@' + listing], check=True)
with zipfile.ZipFile(out + '.tmp', 'w', zipfile.ZIP_STORED) as z:
    for d, _, fs in sorted(os.walk(classes)):
        for f in sorted(fs):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, classes))
os.replace(out + '.tmp', out)
print('rt.jar: %d classes, %d bytes' % (sum(len(fs) for _, _, fs in os.walk(classes)), os.path.getsize(out)))
