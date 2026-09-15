// The server's template numbers from the archives (template_numbers.h).
#include <string>
#include <vector>

#include "check.h"
#include "obf2/game/template_numbers.h"

using namespace obf2::game;

// Lower-case, component by component: '_' sorts before the letters, a
// directory's name is compared as a whole before what is inside it.
static void testFileOrder() {
  CHECK(templateFileLess("objects/staticobjects/_asia/a.con",
                         "objects/staticobjects/ambienteffects/a.con"));
  CHECK(templateFileLess("objects/vehicles/land/JEEP_FAAV/x.con",
                         "objects/vehicles/land/jep_mec_paratrooper/x.con"));
  // The directory "flag" sorts before the file "flag.con": "flag" < "flag.con".
  CHECK(templateFileLess("a/flag/x.con", "a/flag.con"));
  CHECK(!templateFileLess("a/b.con", "a/b/c.con"));
  CHECK(!templateFileLess("a/b.con", "a/b.con"));
}

// A name created again takes no new number.
static void testRepeatsTakeNoNumber() {
  TemplateNumbers numbers;
  numbers.add("us_heavy_soldier");
  numbers.add("UnlockUSAssault");
  numbers.add("unlockusassault");
  numbers.add("us_light_soldier");
  CHECK_EQ(numbers.size(), std::size_t(3));
  CHECK(numbers.numberOf("US_LIGHT_SOLDIER") && *numbers.numberOf("US_LIGHT_SOLDIER") == 2u);
  CHECK(numbers.nameOf(1) && *numbers.nameOf(1) == "UnlockUSAssault");
}

static void testArchivesFromCon() {
  const auto archives = archivesFromCon(
      "fileManager.mountArchive Objects_server.zip Objects\r\n"
      "rem nothing\n"
      "  fileManager.mountArchive Menu_server.zip Menu\n");
  CHECK_EQ(archives.size(), std::size_t(2));
  CHECK(archives[0] == "Objects_server.zip");
  CHECK(archives[1] == "Menu_server.zip");
}

TEST_MAIN({
  testFileOrder();
  testRepeatsTakeNoNumber();
  testArchivesFromCon();
})
