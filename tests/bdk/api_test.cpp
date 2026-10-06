#include <barnix_api>
#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
using namespace Barnix;

static char output[2048];
static unsigned int used, keys[32], key_index, polls;
static BarnixMouse pointer = {12,34,BARNIX_MOUSE_RIGHT,9,1};
static void print(int, const char *text) { while (*text) output[used++] = *text++; output[used] = 0; }
static void println(int color, const char *text) { print(color,text); print(color,"\n"); }
static unsigned int getkey() { assert(key_index < 32); return keys[key_index++]; }
static unsigned int poll() { ++polls; return 'x'; }
static void mouse(BarnixMouse *state) { *state = pointer; }
static const char *user() { return "tester"; }
static int write(const char *, const char *data, int size) { assert(size == 3 && data[0] == 'a'); return -7; }
static bool equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static BarnixAPI api = {};
extern "C" {
const BarnixAPI *barnix = &api;
int barnix_program_argc = 2;
static const char *arguments[] = {"app", "value"};
const char *const *barnix_program_argv = arguments;
}
int main() {
    api.print = print; api.println = println; api.getch = getkey;
    api.poll_key = poll; api.mouse = mouse; api.current_user = user; api.write = write;
    Console::WriteLine("hello"); Console::Write(INT_MIN); std::cout << ':' << 42 << std::endl;
    assert(equal(output, "hello\n-2147483648:42\n"));
    assert(Input::Poll() == 'x' && polls == 1);
    auto state = Mouse::GetState();
    assert(state.x == 12 && state.y == 34 && state.sequence == 9);
    assert(Mouse::IsDown(state, Mouse::Right) && !Mouse::IsDown(state, Mouse::Left));
    assert(polls == 1); // Getting a snapshot must not consume a pending key.
    assert(equal(Environment::UserName(), "tester"));
    assert(equal(Environment::Argument(1), "value"));
    assert(!Environment::Argument(-1) && !Environment::Argument(2));
    assert(File::Write("test", "abc", 3) == -7); // Preserve kernel errors.
    char text[8]; keys[0] = 'a'; keys[1] = 0x416; keys[2] = '\b'; keys[3] = 'b'; keys[4] = '\n';
    assert(Console::ReadLine(text) == 2 && equal(text, "ab"));
    key_index = 0; keys[0] = 'a'; keys[1] = 'b'; keys[2] = 'c'; keys[3] = '\n';
    char small[3]; assert(Console::ReadLine(small) == 2 && equal(small,"ab"));
    assert(Console::ReadLine(nullptr, 0) == -1);
    key_index = 0; keys[0] = KEY_ESCAPE; assert(Console::ReadLine(text) == -1);
    key_index = 0; keys[0] = 0x416; keys[1] = '\n';
    assert(Console::ReadLine(text) == 2 && equal(text, "\xd0\x96"));
    // C stdio declarations must have C linkage when included from C++.
    printf("%s %d", "link", 23);
}
