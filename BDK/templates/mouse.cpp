#define BARNIX_APP_NAME "app.elf"
#include <barnix_api>
using namespace Barnix;

int main() {
    Console::Clear();
    Console::WriteLine("BDK mouse demo: move/click; Q or Escape exits.");
    for (;;) {
        unsigned int key = Input::Poll(); // Services USB/PS2 without losing keyboard input.
        if (key == 'q' || key == 'Q' || key == KEY_ESCAPE) break;
        MouseState mouse = Mouse::GetState();
        Console::SetCursorPosition(0, 2);
        if (mouse.available) {
            Console::Write("X: "); Console::Write(mouse.x);
            Console::Write(" Y: "); Console::Write(mouse.y);
            Console::Write(" Buttons: "); Console::Write((int)mouse.buttons);
            Console::Write("       ");
            Console::SetCursorPosition(0, 3);
            Console::Write(Mouse::IsDown(mouse, Mouse::Left) ? "Left pressed    " : "Left released   ");
        } else Console::Write("No mouse detected. Q exits.");
        Thread::Sleep(10);
    }
    Console::Clear();
    return 0;
}
