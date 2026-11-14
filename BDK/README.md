# Barnix Developer Kit (Barnino Systems BDK)

BDK также содержит BCC (Barnino C Compiler) — драйвер для сборки одного
freestanding C-файла в нативный ELF Barnix.

В самой ОС доступна self-hosted команда `/bin/bcc`. Она работает без GCC и
умеет компилировать bootstrap-подмножество C прямо в запущенном Barnix:

```c
#include <stdio.h>
int main(void) { printf("Hello, World"); return 0; }
```

```sh
bcc main.c
./main.elf
```

Текущий bootstrap-компилятор поддерживает один `main`, строковый `puts` или
`printf` без аргументов формата и `return` (возвращаемое значение резервируется
для следующего этапа). Он генерирует настоящий ELF Barnix из встроенного
freestanding-шаблона; это первый self-hosted этап, а не замена полному GCC.

```sh
bcc main.c
./main.elf

# Explicit output path is also supported.
bcc bdk/examples/bccdemo.c -o bccdemo.elf
```

`bccdemo.c` проверяет BGL, а `bccstdio.c` — совместимый слой `stdio.h/.c`
(`printf`, `puts`, `snprintf`, `fopen`, `fread`, `fwrite`, `fseek` и базовые
файловые операции). Это freestanding-реализация поверх Barnix ABI, поэтому
она сохраняет Linux-подобные имена и сигнатуры, но не обещает host libc,
сокеты, процессы или POSIX-дескрипторы.

BDK упрощает создание нативных программ Barnix на C, C++ и ограниченном подмножестве C#:

- `barnix_api.h` — C API для GCC;
- `barnix_api` — C++ API для G++, `using namespace Barnix;`;
- `bca` — **Barnix Console App**;
- `bcawms` — **Barnx Console App with Mouse Support** (написание названия сохранено);
- `bca_cs`, `bcawms_cs` — C#-варианты консольного и мышиного шаблонов;
- `dotnet run` → `elf/app.elf`, имя задаётся в исходнике.

## Установка одной командой (Linux x86_64)

В репозитории:

```sh
bash BDK/setup.sh
```

В распакованном `Barnino.Systems.BDK-0.1.0.zip`:

```sh
bash setup.sh
```

Сетаппер сам собирает комплект, если запущен из репозитория, проверяет Python,
GCC/G++, binutils и .NET SDK 10, устанавливает недостающие зависимости,
копирует SDK, регистрирует четыре шаблона и собирает тестовые C/C++/C# приложения.
Запускайте от обычного пользователя: `sudo` запрашивается только для пакетов ОС.
Поддержаны apt (Debian/Ubuntu), dnf (Fedora), pacman (Arch), zypper (openSUSE).
Нужен интернет для отсутствующих зависимостей. .NET при необходимости
устанавливается [официальным скриптом Microsoft](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-install-script)
в пользовательский каталог BDK.

По умолчанию: `~/.local/share/barnino-bdk`; окружение добавляется в
`~/.profile` и `~/.bashrc`. Откройте новый терминал или выполните команду
`source .../env.sh`, которую напечатает установщик.

Параметры: `--prefix PATH`, `--no-deps` (готовые зависимости, без загрузок),
`--no-profile`, `--no-smoke-test`. `--help` показывает описание.
Повторный запуск обновляет регистрацию шаблонов; проекты сохраняются.

## Windows x64 — Inno Setup

Запустите **`Barnino.Systems.BDK-0.1.0-win64-setup.exe`** из `BDK/dist/`.
Мастер на русском/английском устанавливает BDK для текущего пользователя,
добавляет пункты документации, восстановления и удаления в меню «Пуск».
Настройка сама устанавливает через WinGet .NET SDK 10, Python и LLVM,
регистрирует `bca`, `bcawms`, `bca_cs`, `bcawms_cs`, `bga`, `bga_cs` и проверяет их сборку.
Если WinGet отсутствует, используется [официальный модуль Microsoft.WinGet.Client](https://learn.microsoft.com/en-us/windows/package-manager/winget/).
Для зависимостей Windows может запросить права администратора.
Нужны Windows 10 x64 1809+ / Windows 11, интернет и доступ к WinGet/PSGallery.

На Windows используется Clang/LLD с явной целью i386 ELF; GCC/G++ остаются
стандартом Linux. WSL не требуется. C/C++ исходники и интерфейс BDK общие.
После установки откройте новый терминал и используйте обычные `dotnet new`
и `dotnet run`. Пути инструментов записываются в
`%LOCALAPPDATA%\Barnino\BDK\toolchain.json`; глобальные `CC`/`CXX` не меняются.
Журнал — `setup.log` в том же каталоге. При ошибке настройка не сообщает об
успехе; пункт **Repair BDK setup** повторяет её после устранения причины.
Удаление BDK убирает его регистрацию/настройки, оставляя проекты и общие зависимости.

Пересборка Windows-установщика: сначала `make bdk`, затем на Windows с
Inno Setup 6.7+:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File BDK\windows\build-installer.ps1
```

Для распакованного ZIP путь — `windows\build-installer.ps1`.
Файл `windows/bdk.iss` также можно открыть и скомпилировать в Inno Setup GUI.
Готовый EXE — сетевой установщик зависимостей, не полный офлайн-образ toolchain.

## Ручная установка / упаковка

`make bdk` создаёт переносимый ZIP с SDK, Bash-сетаппером и исходниками
Windows-установщика, а также `.nupkg`. `python3 BDK/dist/install.py` остаётся
вариантом для подготовленной среды; `--headers-only` копирует SDK без регистрации.
Удалить только шаблоны: `dotnet new uninstall Barnino.Systems.BDK.Templates`.

## Первый проект

```sh
dotnet new bca -o HelloBarnix
cd HelloBarnix
dotnet run
```

Редактируйте `src/main.cpp`:

```cpp
#define BARNIX_APP_NAME "hello.elf"
#include <barnix_api>
using namespace Barnix;

int main() {
    Console::WriteLine("Hello, Barnix!");
    Console::WriteLine(Environment::UserName());
    return 0;
}
```

После `dotnet run` получится `elf/hello.elf`.
Для C: `dotnet new bca --c -o HelloC`; для мыши:
`dotnet new bcawms -o MouseApp` (или добавьте `--c`).
Шаблон мыши показывает координаты и кнопки; Q/Escape завершают программу.

## Библиотека и заголовки

SDK хранит `barnix_api.h`, `barnix_api`, `stdio.h`, `iostream` в общей
стандартной для BDK папке `sdk/include`. Созданный проект содержит свой SDK
в `bdk/`, автоматически подключённый сборщиком: ручные include-пути не нужны.
Системные `stdio.h`/`iostream` Linux несовместимы с ядром Barnix и не заменяются.
Если нужно также сделать API-заголовки видимыми системному GCC/G++ без `-I`,
передайте установщику `--include-dir /usr/local/include` с правами на запись
в этот каталог. Он добавляет только API-заголовки и каталог `barnix/`.
Это не делает обычный host ELF программой Barnix: используйте сборщик BDK.

| C++ | C | Назначение |
| --- | --- | --- |
| `Console::Write/WriteLine` | `barnix_console_write/write_line` | Текст |
| `Console::ReadKey/ReadLine` | `barnix_console_read_key/read_line` | Ввод |
| `Console::Clear/SetCursorPosition` | `barnix_console_clear/position` | Экран |
| `Input::Poll` | `barnix_input_poll` | Неблокирующий ввод |
| `Mouse::GetState/IsDown` | `barnix_mouse_get_state/is_down` | Мышь |
| `Thread::Sleep` | `barnix_sleep` | Задержка |
| `File::Size/Read/Write/Append` | `barnix_file_size/read/write/append` | Файлы |
| `Environment::UserName` | `barnix_current_user` | Пользователь |
| `Screen::Show` | `barnix_screen_show` | 80×25 текстовых ячеек |

`Console::ReadLine` принимает буфер и размер (для массива C++ размер выводится
автоматически), возвращает число UTF-8 байтов или -1 при Escape/неверном буфере.
Файловые методы сохраняют коды возврата Barnix: Read/Size — число байтов или
отрицательная ошибка, Write/Append — 0 при успехе. Права пользователя соблюдаются.
Полный низкоуровневый API доступен через `barnix->...`.

Подробнее о загрузке в Barnix, мыши и ограничениях C++: [PROJECT.md](PROJECT.md).
`dotnet` здесь является средством создания проекта и запуска сборщика,
не реализацией C#/.NET внутри Barnix. `dotnet build` собирает launcher;
`dotnet run` собирает нативное приложение, но не запускает QEMU.

Шаблоны используют стандартный формат
[.NET custom templates](https://learn.microsoft.com/en-us/dotnet/core/tools/custom-templates).

## Проверки

`make test-bdk-setup` проверяет установку из ZIP, четыре smoke build и повторную
установку в изолированный prefix/hive без изменений пользовательских профилей.
`tests/bdk_windows_test.py --llvm PATH --output PATH` запускается Windows Python
(в том числе под Wine), собирает пять ELF настоящим Windows LLVM.
Эти файлы можно проверить `python3 tests/bdk_qemu_test.py PATH/artifacts`.
Полный цикл WinGet/UAC требует настоящей Windows; Wine его не заменяет.


`make test-bdk` проверяет API с подставным BarnixAPI, устанавливает пакет в
изолированный template hive, создаёт и собирает консольные и мышиные проекты на C/C++/C#,
проверяет ELF загрузчиком Barnix, переименование, ошибки и автономность SDK.
`make test-bdk-qemu` дополнительно запускает полученные ELF в Barnix/QEMU,
включая USB-мышь (нужны QEMU и утилиты ext2).

## C#

```sh
dotnet new bca_cs -o HelloCSharp
cd HelloCSharp
dotnet run
```

Для мыши: `dotnet new bcawms_cs -o MouseCSharp`. Результат — `elf/app.elf`.
Имя задаётся через `const string AppName` в `src/main.cs`.
Поддерживается ограниченное нативное подмножество C#, без CLR и библиотеки .NET.
Roslyn проверяет C# и BDK переводит его в C++ для сборки ELF32.
Поддержанные конструкции и ограничения описаны в [PROJECT.md](PROJECT.md#c--elf).


## Barnix Graphic Library (BGL)

```sh
dotnet new bga -o GraphicCpp
# C: dotnet new bga --c -o GraphicC
# C#: dotnet new bga_cs -o GraphicCS
cd GraphicCpp
dotnet run
```

Результат — `elf/app.elf` для Barnix ABI 11. BGL рисует окно на холсте
640×400 с 16 цветами; без framebuffer использует текстовый экран 80×25.
Поддерживаются UTF-8, кнопки, списки с прокруткой, перетаскивание окна,
мышь, Tab, стрелки, Enter и Esc. Одновременно доступно до 32 виджетов.

C: подключите `<barnix_graphics.h>` и создайте **static BglApp**.
C++: подключите `<barnix_graphics>`, используйте **static GraphicApplication**.
Холст слишком велик для стека приложения. Строки и массив элементов списка
должны жить до закрытия окна. Координаты виджетов задаются относительно окна;
область содержимого начинается с y=24. Функции Button/List возвращают 0 при
успехе и -1 при неверных размерах, повторном ID или исчерпании мест.

C#: `using Barnix;`, затем `Graphics.Init(title,x,y,width,height)`,
`Graphics.Button(id,x,y,width,text)` и
`Graphics.List(id,x,y,width,height,items,count)`. `Graphics` хранит один
статический холст. Для списка поддерживается локальное объявление
`string[] items = new string[] { "Первый", "Второй" };`: непустой массив
строковых литералов, без изменения элементов и без динамического выделения.
Количество count не должно превышать длину массива. Общая поддержка массивов
и CLR не добавляется.

Цикл приложения вызывает `Graphics.Poll()`, обрабатывает `GraphicEvent`
(поля `type`, `id`, `value`, `key`), затем `Graphics.Draw()` и `Thread.Sleep(10)`.
Типы событий: `Graphics.None`, `Graphics.Click`, `Graphics.CloseEvent`,
`Graphics.Select`. Для списка value — индекс выбранного элемента с нуля.
`Graphics.SetTitle(text)` меняет заголовок; перед выходом вызовите
`Graphics.Close()`, чтобы вернуть консоль. Полный пример находится в
`src/main.cs` проекта `bga_cs`.

## Barnix Graphics (BGX): полноцветный холст для игр

`barnix_gfx.h` — библиотека, а не рабочий стол: программа занимает весь экран,
рисует в свой буфер `0x00RRGGBB` и показывает его через `bgx_present`.
На UEFI кадр идёт в GOP framebuffer, на Legacy BIOS — в Bochs/QEMU VBE или
VGA 320×200×256; текстовая консоль восстанавливается при `bgx_close` и выходе.

```c
#include <barnix_gfx.h>
static unsigned int pixels[640 * 400];
int main(void) {
    BgxCanvas screen;
    if (bgx_open(&screen, pixels, 640, 400)) return 1;
    for (;;) {
        BgxEvent event;
        while (bgx_poll(&event))
            if (event.type == BGX_PRESS && event.code == BGX_KEY_ESC) { bgx_close(); return 0; }
        BgxMouse mouse; bgx_mouse(&mouse);          /* x, y, dx, dy, wheel, pressed */
        bgx_clear(&screen, BGX_RGB(30, 40, 60));
        bgx_fill(&screen, mouse.x, mouse.y, 20, 20, bgx_key_held(BGX_KEY_SPACE) ? 0xffff55 : 0x55ff55);
        bgx_text_shadow(&screen, 8, 8, "Hello, BGX", BGX_WHITE, 2);
        bgx_present(&screen);
    }
}
```

Есть заливка, полупрозрачность, рамки, ARGB-изображения с масштабом,
увеличение холста, UTF-8 текст системным шрифтом 8×16, курсор и `bgx_millis`.
События клавиш содержат нажатие, автоповтор и отпускание (set-1 коды
`BGX_KEY_*`), поэтому удержание клавиш работает без чтения портов.

Все ядра процессора (ABI 11): `bgx_parallel(job, argument, count)` вызывает
`job(argument, i)` для каждого `i` от 0 до `count - 1` на всех ядрах и
возвращается, когда всё готово; `bgx_cpus()` — число ядер. Задания только
считают (без вызовов `bgx_*` и `barnix->`); делите кадр на полосы по 8 строк,
чтобы быстрые ядра брали больше. На старых ядрах Barnix задания идут по очереди.

## Barnix OpenGL (BXGL): настоящий OpenGL 1.1

`<barnix_opengl.h>` подключает стандартные `<GL/gl.h>`, `<GL/glu.h>` и
`<math.h>`. Это программная реализация OpenGL 1.1 (как Mesa/TinyGL):
геометрия считается в регистрах SSE2, а треугольники растеризуются полосами
сразу на всех ядрах процессора (`bgx_parallel`). Обычный код OpenGL
переносится почти без изменений — заменяется только создание окна.

```c
#include <barnix_opengl.h>
int main(void) {
    if (bxgl_open(0, 0)) return 1;              /* весь экран, до 1024x768 */
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    for (;;) {
        BgxEvent e;
        while (bgx_poll(&e)) if (e.type == BGX_PRESS && e.code == BGX_KEY_ESC) { bxgl_close(); return 0; }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        gluPerspective(60, (double)bxgl_width() / bxgl_height(), 0.5, 50);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(0, 0, -5); glRotatef(bgx_millis() * 0.05f, 1, 1, 0);
        /* ... glBegin/glVertex/glEnd, glDrawArrays, glCallList ... */
        bxgl_swap();
    }
}
```

Поддерживается: `glBegin/glEnd` со всеми 10 типами примитивов, матричные
стеки, `glOrtho/glFrustum`, отсечение по пирамиде видимости, буфер глубины,
отсечение граней, `glPolygonMode` (каркас/точки), плоское и Гуро-затенение,
освещение (8 источников: направленные, точечные, прожекторы, затухание,
блики, `GL_COLOR_MATERIAL`), туман (linear/exp/exp2), текстуры RGB/RGBA/
luminance с перспективной коррекцией, фильтрами nearest/linear и
мип-уровнями, режимы `GL_MODULATE/REPLACE/DECAL/BLEND/ADD`, смешивание,
alpha test, scissor, `glColorMask`, `glPolygonOffset`, массивы вершин
(`glDrawArrays/glDrawElements/glInterleavedArrays`), дисплейные списки,
`glPushAttrib`, `glReadPixels`, `glCopyTexImage2D`, `glGet*`. GLU:
`gluPerspective`, `gluLookAt`, `gluOrtho2D`, `gluProject/UnProject`,
`gluBuild2DMipmaps`, `gluSphere/Cylinder/Disk/PartialDisk`.

Нет: шейдеров (это OpenGL 1.1, а не 2.0+), трафарета, альфа-канала в буфере
кадра, сглаживания линий, `glTexGen`. Мип-уровень выбирается один на
треугольник.

Контекст забирает расширенную память программы (`barnix->memory`): в
OpenGL-программе выделяйте память через `bxgl_alloc/bxgl_free`. Без
расширенной памяти контекст работает в 320×200. Для 2D-надписей поверх
3D: `glFinish()`, затем `bgx_text(bxgl_canvas(), ...)`, затем `bxgl_swap()`.
`bxgl_context` рисует во внешние буферы без экрана. Пример —
`examples/glcube.c`; в Barnix есть демо `gldemo`.

BDK компилирует `float` и `double` в инструкции SSE2 (`-march=pentium4
-mfpmath=sse`, как требует и Windows 10): нужен процессор Pentium 4 / Athlon 64
или новее. `start.c` включает FPU и SSE перед `main`. `<math.h>` даёт `sinf/cosf/sqrtf/powf`
и остальные функции в вариантах float и double.

## Barnix Sound (BSND): звук

`barnix_sound.h` (ABI 11): ядро смешивает до 16 клипов на 48 кГц и выводит их
через Intel HD Audio или AC'97; без звуковой карты остаётся PC speaker.
`bsnd_device` возвращает `BARNIX_SOUND_CARD`, `BARNIX_SOUND_SPEAKER` или 0 и
название устройства. Сэмплы клипа живут в памяти программы, пока он звучит.

```c
#include <barnix_sound.h>
static short note[48000];
int main(void) {
    if (bsnd_device(0, 0) != BARNIX_SOUND_CARD) { bsnd_beep(880, 200); return 0; }
    BsndTone tone = {BSND_PLUCK, bsnd_note_hz(69), 0, 800, 3, 300, 220};  /* A4 */
    unsigned int frames = bsnd_synth(note, 48000, 32000, &tone);
    int voice = bsnd_play(note, frames, 32000, 256, 0);                  /* громкость, панорама */
    while (bsnd_playing(voice)) barnix->sleep_ms(20);
    return 0;
}
```

Синтезатор умеет square, triangle, saw, sine, noise, organ и щипок струны
(`BSND_PLUCK`) со сдвигом высоты, атакой и затуханием; `bsnd_mix` складывает
ноты в один клип, `bsnd_wav` разбирает WAV (PCM 8/16 бит, моно/стерео) для
`bsnd_play_clip`, `bsnd_tone`/`bsnd_beep` звучат на PC speaker.
`bsnd_ulaw` распаковывает 8-битный mu-law (G.711) в 16-битные сэмплы: так
`scripts/sound_assets.py` встраивает WAV-файлы в программу вдвое компактнее.

## Barnix Net (BNET): HTTP и HTTPS

ABI 12. Ядро даёт TCP (`barnix->net_open/net_send/net_recv/net_close`),
библиотека добавляет HTTP/1.1 и TLS 1.2/1.3 с проверкой сертификатов по
корням из `/etc/ssl/roots`. Подключите `runtime/net.c http.c tls.c x509.c
pk.c crypto.c` (и `inflate.c` для `bnet_inflate`).

```c
#include <barnix_net.h>

static int save(void *context, const void *data, unsigned int length)
{ (void)context; barnix->append("page.html", data, (int)length); return 0; }

BnetRequest request = {0};
request.data = save;
if (bnet_request("https://example.com/", &request))
    barnix->println(4, request.error);
```

`bnet_request` следует редиректам, понимает chunked-ответы и POST
(`method`, `content_type`, `body`). Для своих протоколов есть
`bnet_tcp_open/send/recv/close`, для zlib/deflate — `bnet_inflate`.

## Размер и совместимость

Сборщик компонует программы с `--gc-sections`: неиспользуемый код
библиотек (например, TLS из BNET) выбрасывается, игра весит около 30 КиБ.
Программа запускается на ядре с тем же или более новым ABI.
