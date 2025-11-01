#include <cassert>
#include <string>

#include "../src/utils/Utils.h"

int main() {
    const std::string  original  = u8"áéíóúñ ISO";
    const std::wstring wide      = Utils::utf8_to_wstring(original);
    const std::string  roundTrip = Utils::wstring_to_utf8(wide);
    assert(roundTrip == original);

    const std::string ansi     = "Prueba ANSI";
    const std::string ansiUtf8 = Utils::ansi_to_utf8(ansi);
    assert(!ansiUtf8.empty());

    const std::string exeDir = Utils::getExeDirectory();
    assert(!exeDir.empty());

    return 0;
}
