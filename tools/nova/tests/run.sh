#!/usr/bin/env bash
# Регрессионные тесты: примеры собираются в .jar и сверяются с эталонным выводом,
# а файлы из tests/errors обязаны падать с ожидаемой ошибкой компиляции.
set -uo pipefail
cd "$(dirname "$0")/.."
export LANG=C.UTF-8 LC_ALL=C.UTF-8
NOVA="java -jar build/nova.jar"
pass=0; fail=0
tmp=$(mktemp -d)

for f in examples/*.nova; do
  b=$(basename "$f" .nova)
  if $NOVA build "$f" -o "$tmp/$b.jar" >/dev/null 2>"$tmp/err" \
     && java -jar "$tmp/$b.jar" 2>&1 | grep -v '^Picked up' > "$tmp/$b.out" \
     && diff -u "tests/expected/$b.out" "$tmp/$b.out"; then
    pass=$((pass+1)); echo "ok    $b"
  else
    fail=$((fail+1)); echo "FAIL  $b"; cat "$tmp/err"
  fi
done

for f in tests/errors/*.nova; do
  b=$(basename "$f" .nova)
  want=$(head -1 "$f" | sed 's|^// ожидается: ||')
  if out=$($NOVA check "$f" 2>&1); then
    fail=$((fail+1)); echo "FAIL  errors/$b: скомпилировалось, а не должно было"
  elif grep -qF -- "$want" <<<"$out"; then
    pass=$((pass+1)); echo "ok    errors/$b"
  else
    fail=$((fail+1)); echo "FAIL  errors/$b: ждали «$want», получили:"; echo "$out"
  fi
done

rm -rf "$tmp"
echo "пройдено: $pass, упало: $fail"
[ "$fail" -eq 0 ]
