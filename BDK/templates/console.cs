using Barnix;

static class Program
{
    const string AppName = "app.elf";

    static int Main()
    {
        Console.WriteLine("Hello from Barnino Systems BDK C#!");
        Console.Write("User: ");
        Console.WriteLine(Environment.UserName());
        return 0;
    }
}
