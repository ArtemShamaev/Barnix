# Nova

Статически типизированный язык для JVM. Синтаксис — среднее между C# и Python:
блоки задаются отступами, объявления пишутся как в C# (`int x = 5`, `var y = ...`),
есть классы с наследованием. Компилятор на Java + [ObjectWeb ASM](https://asm.ow2.io/)
генерирует байткод напрямую, а на выходе получается обычный запускаемый `.jar`,
которому нужна JVM с поддержкой Java 8, включая Barnix BJVM.

```
int fib(int n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

void main():
    var name = "World"
    print("Hello, " + name + "!")
    for i in 0..10:
        print("fib(" + i + ") = " + fib(i))
```

```bash
./build.sh                                   # собрать компилятор -> build/nova.jar
./nova build examples/hello.nova -o hello.jar
java -jar hello.jar                          # работает в Barnix BJVM и обычной JVM 8+
./nova run examples/shapes.nova              # скомпилировать в памяти и сразу запустить
./nova check file.nova                       # только проверить типы
tests/run.sh                                 # регрессионные тесты
```

Для сборки компилятора нужен JDK 21+. Сгенерированные программы используют байткод Java 8.
На Barnix запускайте готовый JAR командой `java -jar hello.jar`; компиляция `.nova`
выполняется на машине разработки, поскольку сам компилятор требует JDK 21 и ASM.

Из корня проекта Barnix: `make nova` собирает компилятор и четыре примера,
`make nova-check` запускает тесты компилятора, а `make nova-image` помещает
исходники и JAR-примеры в `/usr/share/nova/examples` образа `live-ext2.img`.
В Barnix пример запускается командой
`java -jar /usr/share/nova/examples/hello.jar`.
Можно собрать и через Maven: `mvn package` даёт `target/nova.jar`.

## Язык

### Типы

| Nova     | JVM                 | Примечание |
|----------|---------------------|------------|
| `int`    | `int`               | 32 бита |
| `double` | `double`            | `int` автоматически расширяется до `double` |
| `bool`   | `boolean`           | `true` / `false` |
| `string` | `java.lang.String`  | `==` сравнивает **значения**, а не ссылки |
| `T[]`    | массив              | `new int[10]`, `[1, 2, 3]`, `a.length` |
| `Класс`  | класс JVM           | `null` допустим только для ссылочных типов |
| `void`   | `void`              | только тип возврата |

Неявные преобразования есть только `int → double` и «подкласс → базовый класс».
`double → int` выполняется явно через `toInt(x)`.

### Переменные

```
var count = 0           // тип выводится: int
string title = "Nova"   // явный тип
double[] xs = [1, 2.5]  // массив double, 1 станет 1.0
Shape s = null
int x                   // без инициализатора — значение по умолчанию (0 / false / null)
```

Затенять переменную во вложенном блоке нельзя (как в C#). Локальная переменная
с именем поля разрешена, тогда поле доступно через `this.имя`.

### Управление

```
if x > 10:
    print("много")
elif x > 0:
    print("немного")
else:
    print("ничего")

while i < n:
    i += 1

for i in 0..n:          // 0, 1, ..., n-1
    ...
for item in array:      // по элементам массива
    ...

if n < 2: return n      // однострочные блоки тоже можно
```

`break`, `continue`, `pass`, `return`. Логические операторы: `and`, `or`, `not`
(с коротким замыканием). Цепочки вида `a < b < c` запрещены, вместо них используйте `and`.
Компилятор проверяет, что функция с непустым типом возврата возвращает значение во всех ветках.

### Функции

```
double avg(int[] xs):
    double sum = 0
    for x in xs:
        sum += x
    return sum / xs.length
```

Точка входа — `void main():` или `void main(string[] args):`. Перегрузки пока нет.

### Классы

```
class Shape:
    string name                 // поле (public)
    int sides = 0               // поле с инициализатором

    init(string name):          // конструктор
        this.name = name

    double area():              // методы виртуальные, как в Java
        return 0.0

    string toString():          // переопределяет Object.toString -> работает в print и "+"
        return name + ": " + area()

class Circle(Shape):            // наследование
    double r
    init(double r):
        super("круг")           // вызов базового конструктора — первой строкой
        this.r = r

    double area():
        return 3.14159 * r * r

void main():
    Shape s = new Circle(2)
    print(s)                    // круг: 12.56636
```

Внутри методов к полям и методам можно обращаться без `this.` (как в C#).
`super.метод(...)` вызывает реализацию базового класса. Переопределение обязано
совпадать с базовым методом по сигнатуре, иначе будет ошибка компиляции.

### Строки

Доступны все методы `java.lang.String`, компилятор находит их через рефлексию:

```
var t = "  hello, world ".trim()
t.toUpperCase()   t.length()   t.contains("lo")   t.substring(0, 5)
t.split(", ")     // -> string[]
t.charAt(0)       // char сразу превращается в string
"a" < "b"         // лексикографическое сравнение
s += "!"          // склейка с любым типом
```

Склейка компилируется через `StringBuilder`, доступный в BJVM.

### Встроенные функции

| Функция         | Что делает |
|-----------------|------------|
| `print(x)`      | печатает значение любого типа (массивы печатаются как `[1, 2, 3]`) |
| `input()`       | читает строку из stdin (`null` в конце ввода) |
| `str(x)`        | значение -> `string` |
| `toInt(x)`      | `string`/`double` -> `int` |
| `toDouble(x)`   | `string`/`int` -> `double` |

## Как устроен компилятор

```
исходник ─► Lexer ─► Parser ─► Checker ─► CodeGen (ASM) ─► .class ─► .jar
           токены,   AST       типы,      байткод,
           INDENT/             имена,     StackMapFrames
           DEDENT              слоты      (COMPUTE_FRAMES)
```

| Файл | Роль |
|------|------|
| `Lexer.java`    | токены; отступы превращаются в `INDENT`/`DEDENT`, переносы внутри `()`/`[]` игнорируются |
| `Parser.java`   | рекурсивный спуск -> `Ast` |
| `Checker.java`  | таблицы классов и функций, вывод типов, проверка присваиваний, return во всех ветках, слоты локальных переменных |
| `CodeGen.java`  | байткод: функции -> `static` методы главного класса, классы Nova -> классы JVM |
| `Main.java`     | CLI, запись `.jar` с `Main-Class` в манифесте, запуск в памяти |

Главный класс называется по имени файла: `hello.nova` -> `Hello`. В байткод пишутся
номера строк и таблица локальных переменных, поэтому стектрейсы указывают на строки
`.nova`-файла, а отладчик IDE показывает имена переменных:

```
ошибка выполнения: java.lang.NullPointerException: Cannot read field "x" because "p" is null
    at Runtime.main(runtime.nova:6)
```

## Что дальше

- Вызов любых Java-классов: `import java.util.ArrayList`, статические методы (`Math.sqrt`)
- Обобщённые коллекции `List<T>`, `Map<K, V>`
- Перегрузка методов, `static`, модификаторы доступа, интерфейсы
- Несколько файлов и модули
- Лямбды (через `invokedynamic` + `LambdaMetafactory`)
- Строковая интерполяция `$"x = {x}"`
- `long`, `char`, явные приведения `(int) x`
- Сбор всех ошибок за один проход вместо остановки на первой
