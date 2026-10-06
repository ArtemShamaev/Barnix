using Barnix;

static class Program
{
    const string AppName = "app.elf";

    static int Main()
    {
        Console.Clear();
        Console.WriteLine("BDK C# mouse demo: move/click; Q or Escape exits.");
        while (true)
        {
            uint key = Input.Poll();
            if (key == 113 || key == 81 || key == 27) break;
            MouseState mouse = Mouse.GetState();
            Console.SetCursorPosition(0, 2);
            if (mouse.available != 0)
            {
                Console.Write("X: "); Console.Write(mouse.x);
                Console.Write(" Y: "); Console.Write(mouse.y);
                Console.Write(" Buttons: "); Console.Write((int)mouse.buttons);
                Console.Write("       ");
                Console.SetCursorPosition(0, 3);
                Console.Write(Mouse.IsDown(mouse, Mouse.Left) ? "Left pressed    " : "Left released   ");
            }
            else Console.Write("No mouse detected. Q exits.");
            Thread.Sleep(10);
        }
        Console.Clear();
        return 0;
    }
}
