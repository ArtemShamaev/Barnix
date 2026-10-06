using Barnix;
static class Program {
    const string AppName = "app.elf";
    static int Main() {
        string[] items = new string[] { "Добро пожаловать", "Barnix Graphic Library", "Mouse and keyboard", "Native C# ELF" };
        Graphics.Init("Barnix Executive", 64, 48, 512, 304);
        Graphics.List(1, 16, 40, 480, 176, items, 4);
        Graphics.Button(2, 16, 240, 152, "О программе");
        Graphics.Button(3, 336, 240, 160, "Выход");
        while (true) {
            GraphicEvent ev = Graphics.Poll();
            if (ev.type == Graphics.CloseEvent || (ev.type == Graphics.Click && ev.id == 3)) break;
            if (ev.type == Graphics.Click && ev.id == 2) Graphics.SetTitle("Barnix Graphic Library - BGL");
            Graphics.Draw();
            Thread.Sleep(10);
        }
        Graphics.Close();
        return 0;
    }
}
