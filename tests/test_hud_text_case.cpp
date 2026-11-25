// The HUD's upper case, `BF2.exe` 0x4311b0 (docs/functions/squad-menu.md).
#include <string>

#include "check.h"
#include "obf2/hud/text_case.h"

using namespace obf2;

namespace {

void run() {
  CHECK(hud::upperCase("Alpha") == "ALPHA");
  CHECK(hud::upperCase("Xray 7") == "XRAY 7");
  // The five hand-mapped Latin-1 letters.
  CHECK(hud::upperCase("\xe5\xe4\xf6\xfc\xe9") == "\xc5\xc4\xd6\xdc\xc9");
  // Any other high byte is left alone ("C" locale toupper).
  CHECK(hud::upperCase("\xf1\xe0") == "\xf1\xe0");
  CHECK(hud::upperCase("") == "");
}

}  // namespace

TEST_MAIN({ run(); })
