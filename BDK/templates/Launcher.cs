using System;
using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
using System.Text.Json;

// This host-side launcher builds the native C/C++/C# application. It is not shipped to Barnix.
internal static class Launcher
{
    static int Main()
    {
        string project = AppContext.BaseDirectory;
        while (!File.Exists(Path.Combine(project, "bdk", "tools", "build.py")))
        {
            var parent = Directory.GetParent(project);
            if (parent == null) { Console.Error.WriteLine("BDK project directory not found."); return 1; }
            project = parent.FullName;
        }
        var config = new Dictionary<string, string>();
        if (OperatingSystem.IsWindows())
        {
            string file = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                       "Barnino", "BDK", "toolchain.json");
            if (File.Exists(file))
            {
                try { config = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(file))!; }
                catch (Exception error) { Console.Error.WriteLine("BDK toolchain configuration: " + error.Message); return 1; }
            }
        }
        string python = Environment.GetEnvironmentVariable("BDK_PYTHON")
            ?? (config.TryGetValue("Python", out var configuredPython) ? configuredPython
                : OperatingSystem.IsWindows() ? "python" : "python3");
        var start = new ProcessStartInfo(python)
        { UseShellExecute = false, WorkingDirectory = project };
        foreach (string variable in new[] { "CC", "CXX", "BDK_LD" })
            if (Environment.GetEnvironmentVariable(variable) == null && config.TryGetValue(variable, out var value))
                start.Environment[variable] = value;
        start.ArgumentList.Add(Path.Combine(project, "bdk", "tools", "build.py"));
        start.ArgumentList.Add(project);
        try
        {
            using var process = Process.Start(start)!;
            process.WaitForExit();
            return process.ExitCode;
        }
        catch (Exception error) { Console.Error.WriteLine("BDK: " + error.Message); return 1; }
    }
}
