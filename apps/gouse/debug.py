"""Barnix Code's Python debugger (a bdb front end for an 80x25 console).

    python3 debug.py [--breaks 3,10] [--state FILE] SCRIPT [ARGS...]

The screen shows the source around the current line, the breakpoints, the
variables (and watches), the call stack and the last lines the program
printed. Commands: n next line, s step into, o step out, c continue,
b [N] toggle a breakpoint, p EXPR evaluate, w EXPR watch, q quit; Enter
repeats the last command. The breakpoints are written back to --state.
"""
import bdb
import builtins
import linecache
import os
import reprlib
import sys
import traceback

ESC = "\x1b["
# The last column stays empty: Barnix wraps right after it, unlike a VT100.
WIDTH, HEIGHT = 79, 25
SOURCE_ROWS = 13


def cut(text, width):
    text = text.replace("\t", "    ").replace("\n", " ")
    return text if len(text) <= width else text[:width - 1] + "~"


class Output:
    """The program's prints, kept for the OUTPUT lines of the screen."""

    def __init__(self):
        self.lines = [""]

    def write(self, text):
        for character in text:
            if character == "\n":
                self.lines.append("")
            else:
                self.lines[-1] += character
        del self.lines[:-200]
        return len(text)

    def flush(self):
        pass


class Debugger(bdb.Bdb):
    def __init__(self, script, breaks, state):
        super().__init__()
        self.script = self.canonic(script)
        self.state = state
        self.out = Output()
        self.watches = []
        self.message = "Stopped at the start. n: next line, c: continue to a breakpoint."
        self.last = "n"
        self.repr = reprlib.Repr()
        self.repr.maxstring, self.repr.maxother, self.repr.maxlist, self.repr.maxdict = 40, 40, 6, 4
        for line in breaks:
            self.set_break(self.script, line)
        self.finished = False

    # ---------------------------------------------------------------- screen
    def draw(self, frame, prompt="debug> "):
        lines = []
        filename = self.canonic(frame.f_code.co_filename)
        current = frame.f_lineno
        title = " Barnix Debug   %s:%d   in %s()" % (os.path.basename(filename), current, frame.f_code.co_name)
        lines.append(ESC + "30;47m" + cut(title, WIDTH).ljust(WIDTH) + ESC + "0m")
        first = max(1, current - SOURCE_ROWS // 2)
        breaks = set(self.get_file_breaks(filename))
        for number in range(first, first + SOURCE_ROWS):
            text = linecache.getline(filename, number).rstrip("\n")
            mark = ESC + "91m*" + ESC + "0m" if number in breaks else " "
            body = "%4d  %s" % (number, cut(text, WIDTH - 8))
            if number == current:
                lines.append(ESC + "30;43m>" + ESC + "0m" + mark + ESC + "30;43m" + body.ljust(WIDTH - 2) + ESC + "0m")
            else:
                lines.append(" " + mark + ESC + "90m" + body[:6] + ESC + "0m" + body[6:])
        lines.append(ESC + "36m" + "-- VARIABLES ".ljust(46, "-") + " CALL STACK ".ljust(WIDTH - 46, "-") + ESC + "0m")
        variables = []
        for expression in self.watches:
            try:
                value = self.repr.repr(eval(expression, frame.f_globals, frame.f_locals))
            except Exception as error:
                value = "<%s>" % type(error).__name__
            variables.append(ESC + "93m" + cut("%s = %s" % (expression, value), 45) + ESC + "0m")
        for name, value in frame.f_locals.items():
            if name.startswith("__") or type(value).__name__ in ("module", "function", "type", "builtin_function_or_method"):
                continue
            variables.append(cut("%s = %s" % (name, self.repr.repr(value)), 45))
        stack = []
        entry = frame
        while entry is not None and len(stack) < 6:
            if self.canonic(entry.f_code.co_filename) == self.script or entry is frame:
                stack.append(cut("%s:%d %s" % (os.path.basename(entry.f_code.co_filename), entry.f_lineno, entry.f_code.co_name), 33))
            entry = entry.f_back
        for row in range(6):
            left = variables[row] if row < len(variables) else ""
            right = stack[row] if row < len(stack) else ""
            visible = len(left) - (len(ESC + "93m") + len(ESC + "0m") if left.startswith(ESC) else 0)
            lines.append(left + " " * (46 - visible) + ESC + "96m" + right + ESC + "0m")
        output = [line for line in self.out.lines if line][-2:]
        lines.append(ESC + "36m" + "-- OUTPUT ".ljust(WIDTH, "-") + ESC + "0m")
        for row in range(2):
            lines.append(cut(output[row], WIDTH) if row < len(output) else "")
        lines.append(ESC + "97m" + cut(self.message, WIDTH) + ESC + "0m")
        hint = "n next  s step  o out  c continue  b N break  p EXPR  w EXPR watch  q quit"
        screen = ESC + "2J" + ESC + "H" + "\n".join(line + ESC + "K" for line in lines[:HEIGHT - 1])
        screen += ESC + "%d;1H" % HEIGHT + ESC + "90m" + hint[: WIDTH - len(prompt) - 1].rjust(WIDTH - 1) + ESC + "0m"
        screen += ESC + "%d;1H" % HEIGHT + ESC + "97m" + prompt + ESC + "0m"
        sys.__stdout__.write(screen)
        sys.__stdout__.flush()

    # ---------------------------------------------------------------- bdb
    def user_line(self, frame):
        if self.canonic(frame.f_code.co_filename) != self.script:
            self.set_step()   # library code: through it, back to the script
            return
        self.interact(frame)

    def user_exception(self, frame, info):
        kind, value, _ = info
        if self.canonic(frame.f_code.co_filename) != self.script:
            return
        self.message = "%s: %s" % (kind.__name__, value)
        self.interact(frame)

    def interact(self, frame):
        while True:
            self.draw(frame)
            try:
                command = sys.__stdin__.readline()
            except KeyboardInterrupt:
                command = "q"
            if not command:
                command = "q"
            command = command.strip() or self.last
            word, _, rest = command.partition(" ")
            if word in ("n", "next", "s", "step", "o", "out", "c", "continue", "q", "quit"):
                self.last = word
                self.message = ""
                if word in ("n", "next"):
                    self.set_next(frame)
                elif word in ("s", "step"):
                    self.set_step()
                elif word in ("o", "out"):
                    self.set_return(frame)
                elif word in ("c", "continue"):
                    self.set_continue()
                else:
                    self.set_quit()
                return
            if word in ("b", "break"):
                line = int(rest) if rest.strip().isdigit() else frame.f_lineno
                if line in self.get_file_breaks(self.script):
                    self.clear_break(self.script, line)
                    self.message = "Breakpoint at line %d removed." % line
                elif linecache.getline(self.script, line).strip():
                    self.set_break(self.script, line)
                    self.message = "Breakpoint at line %d." % line
                else:
                    self.message = "Line %d is empty." % line
            elif word in ("p", "print") and rest:
                try:
                    self.message = "%s = %s" % (rest, self.repr.repr(eval(rest, frame.f_globals, frame.f_locals)))
                except Exception as error:
                    self.message = "%s: %s" % (type(error).__name__, error)
            elif word in ("w", "watch") and rest:
                self.watches.append(rest)
                self.message = "Watching %s" % rest
            else:
                self.message = "Unknown command: %s (n s o c b p w q)" % command

    # ---------------------------------------------------------------- the program
    def run_script(self, argv):
        sys.argv = argv
        with open(self.script, "rb") as source:
            code = compile(source.read(), self.script, "exec")
        globs = {"__name__": "__main__", "__file__": self.script, "__builtins__": builtins}

        def debug_input(prompt=""):
            self.out.write(str(prompt))
            frame = sys._getframe(1)
            self.message = "The program asks for input:"
            self.draw(frame, prompt=cut(str(prompt), 40) or "input> ")
            line = sys.__stdin__.readline()
            self.out.write(line)
            return line.rstrip("\n")
        builtins.input = debug_input
        sys.stdout = self.out
        status = "The program finished."
        try:
            if not self.get_all_breaks():
                self.set_step()   # no breakpoints: stop at the first line
            self.run(code, globs, globs)
        except bdb.BdbQuit:
            status = "Debugging stopped."
        except SystemExit as exit:
            status = "The program exited (%s)." % exit.code
        except Exception:
            text = traceback.format_exc().rstrip().splitlines()
            self.out.write("\n".join(text) + "\n")
            status = "The program stopped with an error: " + text[-1]
        finally:
            sys.stdout = sys.__stdout__
        self.save()
        sys.__stdout__.write(ESC + "0m" + ESC + "2J" + ESC + "H")
        for line in [line for line in self.out.lines][-20:]:
            sys.__stdout__.write(cut(line, WIDTH) + "\n")
        sys.__stdout__.write(ESC + "97m" + status + " Enter: back to the editor." + ESC + "0m")
        sys.__stdout__.flush()
        sys.__stdin__.readline()

    def save(self):
        if self.state:
            try:
                with open(self.state, "w") as out:
                    out.write("breaks=%s\n" % ",".join(str(line) for line in sorted(self.get_file_breaks(self.script))))
            except OSError:
                pass


def main():
    arguments = sys.argv[1:]
    breaks, state = [], None
    while arguments and arguments[0].startswith("--"):
        option = arguments.pop(0)
        if option == "--breaks" and arguments:
            breaks = [int(n) for n in arguments.pop(0).split(",") if n.strip().isdigit()]
        elif option == "--state" and arguments:
            state = arguments.pop(0)
    if not arguments:
        print(__doc__)
        return 2
    debugger = Debugger(os.path.abspath(arguments[0]), breaks, state)
    debugger.run_script(arguments)
    return 0


if __name__ == "__main__":
    sys.exit(main())
