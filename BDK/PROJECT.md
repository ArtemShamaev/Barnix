# Barnino Systems BDK — проект Barnix

Это нативная программа Barnix на C/C++ или ограниченном подмножестве C#.

```sh
dotnet run
```

Результат: `elf/app.elf`. Для C# см. раздел ниже. Для C/C++ изменяйте `src/main.cpp` (или `src/main.c`).
Для другого имени измените `#define BARNIX_APP_NAME "app.elf"` в этом файле.
Имя должно содержать не более 23 символов вместе с `.elf`, без пути.
Все `.c`, `.cpp`, `.cc`, `.cxx` внутри `src/` собираются автоматически.
`dotnet run` собирает ELF; запускать его нужно внутри Barnix.
`dotnet build` собирает только служебный .NET launcher.
Прямой вызов сборщика: `python3 bdk/tools/build.py .`. Для C# ему нужен .NET SDK.

Для одного исходника используйте BCC: `bcc main.c`. Результатом будет
`./main.elf`; путь можно изменить через `-o`. BCC добавляет `BARNIX_APP_NAME`, если макрос не указан, и
вызывает тот же freestanding-конвейер BDK. Проверочный пример BGL находится в
`bdk/examples/bccdemo.c`.

Для проверки stdio соберите `bdk/examples/bccstdio.c`; он использует
`printf`, `puts` и `snprintf` без host libc. Заголовок `<stdio.h>` также
предоставляет базовые `FILE`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`,
`ftell`, `remove` и `rename` поверх Barnix файлового ABI.

Установщики BDK настраивают зависимости автоматически: на Linux x86_64 —
.NET SDK 10, Python 3.9+, GCC/G++ и binutils; на Windows x64 — .NET SDK 10,
Python и LLVM Clang/LLD. Windows-сборка создаёт ELF напрямую, без WSL. Загрузка NuGet-зависимостей не требуется.
При необходимости задайте `CC`, `CXX`, `BDK_PYTHON` путями к инструментам.
SDK хранится в проекте: путь к исходному репозиторию Barnix не нужен.

```cpp
#include <barnix_api>
using namespace Barnix;
// Внутри main():
// Console::WriteLine("Hello!");
// char name[80]; Console::ReadLine(name);
// Console::WriteLine(name);
```

В C используйте `<barnix_api.h>` и `barnix_console_write_line("Hello!")`.
`<stdio.h>` работает в обоих языках; `<iostream>` предоставляет небольшой
`std::cout` для строк, char, int и `std::endl`, а не полную libstdc++.

Для мыши: один `Input::Poll()` за итерацию, обработайте возвращённую клавишу,
затем `Mouse::GetState()`. `KEY_MOUSE` — событие мыши, не символ.
Координаты 640×400; Left/Right/Middle — битовая маска; `sequence` меняется при
движении/смене кнопок. `available` означает обнаружение после инициализации,
а не текущее число подключённых устройств. USB: HID boot mouse на прямом UHCI;
PS/2 поддерживается тоже. Колесо, USB-хабы и xHCI пока не поддерживаются.

Скопируйте ELF на доступный Barnix носитель и установите в `/bin` от root:
`cp /mnt/app.elf /bin/myapp`, затем `myapp`. Путь носителя может отличаться.
Barnix разрешает запуск нативного ABI только из `/bin`; используется ABI 9.

Ограничения: нет host libc/libstdc++, исключений, RTTI, потоков, динамического
выделения памяти и загрузчика библиотек. Используйте фиксированные буферы.
Глобальные конструкторы/деструкторы и деструкторы локальных static-объектов
поддерживаются; таблица atexit ограничена 64 обработчиками.
Ошибки компиляции/линковки возвращают ненулевой код; последний успешно
собранный ELF сохраняется, поэтому после ошибки его нельзя считать новым.

## C# → ELF

Создание: `dotnet new bca_cs -o MyApp` или `dotnet new bcawms_cs -o MouseApp`.
Сборка: `dotnet run`. Редактируйте `src/main.cs`; имя ELF задаёт
`const string AppName = "app.elf"` внутри `static class Program`.
Точка входа — `static int Main()`. Дополнительные статические методы можно
размещать в `partial class Program` в других `.cs` файлах внутри `src/`.

Roslyn из установленного .NET SDK проверяет синтаксис и типы. Компилятор BDK
переводит поддерживаемые конструкции в C++, затем собирает ELF32 с ABI Barnix.
CLR и .NET runtime на Barnix не нужны; пакеты NuGet не скачиваются.
Поддержаны `int`, `uint`, `bool`, строковые литералы, `var`, `MouseState`,
статические методы, локальные переменные, арифметика, `if`, `while`, `for`,
`break`, `continue`, `return`. API: `Barnix.Console`, `Input`, `Mouse`, `Thread`,
`Environment` (см. исходники шаблонов и `bdk/tools/csharp/Api.cs.txt`).
`string` хранит неизменяемую строку UTF-8; операции над строками не поддержаны.
Нет классов с экземплярами, `new`, массивов, GC, исключений, async, LINQ,
System.* и стандартной библиотеки .NET. Неподдерживаемый синтаксис останавливает
сборку. Смешивание C# с C/C++ в одном проекте не поддержано.


## Barnix Graphic Library (BGL)

```sh
dotnet new bga -o GraphicCpp
# C: dotnet new bga --c -o GraphicC
# C#: dotnet new bga_cs -o GraphicCS
cd GraphicCpp
dotnet run
```

Результат — `elf/app.elf` для Barnix ABI 9. BGL рисует окно на холсте
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
