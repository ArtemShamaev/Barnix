#!/usr/bin/env bash
# Собирает компилятор Nova в один самодостаточный jar: build/nova.jar
# Нужен JDK 21+ (сгенерированные программы работают на BJVM/Java 8+). ObjectWeb ASM берётся из lib/ или скачивается с Maven Central.
set -euo pipefail
cd "$(dirname "$0")"

ASM_VERSION=9.7.1
ASM_JAR="${ASM_JAR:-lib/asm-$ASM_VERSION.jar}"
if [ ! -f "$ASM_JAR" ]; then
  mkdir -p lib
  echo "скачиваю ASM $ASM_VERSION..."
  curl -fsSL -o "$ASM_JAR" "https://repo1.maven.org/maven2/org/ow2/asm/asm/$ASM_VERSION/asm-$ASM_VERSION.jar"
fi

rm -rf build/classes && mkdir -p build/classes
javac --release 21 -encoding UTF-8 -cp "$ASM_JAR" -d build/classes $(find src/main/java -name '*.java')

# вкладываем ASM внутрь, чтобы nova.jar запускался без зависимостей
ASM_ABS="$(readlink -f "$ASM_JAR")"
(cd build/classes && jar xf "$ASM_ABS" org && rm -rf META-INF)
printf 'Main-Class: nova.Main\n' > build/manifest.txt
jar cfm build/nova.jar build/manifest.txt -C build/classes .
echo "готово: build/nova.jar"
