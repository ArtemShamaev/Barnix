#define BARNIX_APP_NAME "app.elf"
#include <barnix_api>
using namespace Barnix;

int main() {
    Console::WriteLine("Hello from Barnino Systems BDK!");
    Console::Write("User: ");
    Console::WriteLine(Environment::UserName());
    return 0;
}
