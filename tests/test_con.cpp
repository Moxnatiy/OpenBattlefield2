#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/con/interpreter.h"
#include "obf2/con/lexer.h"

using namespace obf2::con;

namespace {

// A map-backed provider — the tests must not depend on the game being on disk.
class MemoryFiles : public FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    if (it == files.end()) return std::nullopt;
    return it->second;
  }
};

struct Result {
  std::vector<Command> commands;
  std::vector<Diagnostic> diagnostics;
};

Result run(MemoryFiles& files, std::string_view entry) {
  Result r;
  Interpreter interp(
      files, [&](const Command& c) { r.commands.push_back(c); },
      [&](const Diagnostic& d) { r.diagnostics.push_back(d); });
  interp.runFile(entry);
  return r;
}

}  // namespace

static void testTokenizer() {
  const auto t = tokenizeLine("ObjectTemplate.weaponHud.hudName\t \"WEAPON_NAME_ammobag\"  \r");
  CHECK_EQ(t.size(), std::size_t(2));
  CHECK_EQ(t[0], std::string("ObjectTemplate.weaponHud.hudName"));
  CHECK_EQ(t[1], std::string("WEAPON_NAME_ammobag"));

  // A backslash inside quotes is a path, not an escape.
  const auto p = tokenizeLine(R"(ObjectTemplate.icon "Ingame\Kits\kit.tga")");
  CHECK_EQ(p[1], std::string(R"(Ingame\Kits\kit.tga)"));

  CHECK(tokenizeLine("   \t  ").empty());

  const auto path = splitCommandPath("ObjectTemplate.fire.addFireRate");
  CHECK_EQ(path.size(), std::size_t(3));
  CHECK_EQ(path[2], std::string("addFireRate"));
}

static void testBasicCommands() {
  MemoryFiles files;
  files.files["a.con"] =
      "rem *** Generated with Bf2Editor.exe\r\n"
      "ObjectTemplate.create GenericFireArm ammokit\r\n"
      "\r\n"
      "ObjectTemplate.fire.projectileStartPosition 0.06/-0.12/0\r\n";

  const Result r = run(files, "a.con");
  CHECK_EQ(r.diagnostics.size(), std::size_t(0));
  CHECK_EQ(r.commands.size(), std::size_t(2));
  CHECK_EQ(r.commands[0].lowerPath, std::string("objecttemplate.create"));
  CHECK_EQ(r.commands[0].argStr(1), std::string_view("ammokit"));
  CHECK_EQ(r.commands[0].line, 2);

  const auto vec = r.commands[1].argVec3(0);
  CHECK(vec.has_value());
  CHECK(vec && vec->y < -0.11f && vec->y > -0.13f);
}

static void testBlockComments() {
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.a 1\n"
      "beginRem\n"
      "ObjectTemplate.b 2\n"
      "beginRem\n"
      "ObjectTemplate.c 3\n"
      "endRem\n"
      "ObjectTemplate.d 4\n"
      "endRem\n"
      "ObjectTemplate.e 5\n";

  const Result r = run(files, "a.con");
  CHECK_EQ(r.diagnostics.size(), std::size_t(0));
  CHECK_EQ(r.commands.size(), std::size_t(2));
  CHECK_EQ(r.commands[1].lowerPath, std::string("objecttemplate.e"));
}

static void testVarsAndIf() {
  MemoryFiles files;
  // An exact sample from the game: Kits/US/US_Common.con plus the BF2Editor check.
  files.files["a.con"] =
      "var v_dist = 20\n"
      "GeometryTemplate.setSubGeometryLodDistance 0 0 v_dist\n"
      "if v_arg1 == BF2Editor\n"
      "ObjectTemplate.editorOnly 1\n"
      "endIf\n"
      "if v_arg1 != BF2Editor\n"
      "ObjectTemplate.gameOnly 1\n"
      "endIf\n";

  Result r;
  Interpreter interp(
      files, [&](const Command& c) { r.commands.push_back(c); },
      [&](const Diagnostic& d) { r.diagnostics.push_back(d); });
  interp.runFile("a.con", {"BF2Editor"});

  CHECK_EQ(r.diagnostics.size(), std::size_t(0));
  CHECK_EQ(r.commands.size(), std::size_t(2));
  CHECK_EQ(r.commands[0].argStr(2), std::string_view("20"));
  CHECK_EQ(r.commands[1].lowerPath, std::string("objecttemplate.editoronly"));
}

static void testIncludeAndRun() {
  MemoryFiles files;
  files.files["weapons/handheld/ammokit/ammokit.con"] =
      "ObjectTemplate.create GenericFireArm ammokit\n"
      "include ammokit.tweak\n";
  files.files["weapons/handheld/ammokit/ammokit.tweak"] =
      "ObjectTemplate.castsDynamicShadow 1\n"
      "run ../shared/lod.con 42\n";
  files.files["weapons/handheld/shared/lod.con"] =
      "GeometryTemplate.lodDistance v_arg1\n";

  const Result r = run(files, "Weapons\\Handheld\\ammokit\\ammokit.con");
  CHECK_EQ(r.diagnostics.size(), std::size_t(0));
  CHECK_EQ(r.commands.size(), std::size_t(3));
  CHECK_EQ(r.commands[2].argStr(0), std::string_view("42"));
  // A call's arguments stay local to the called file.
  CHECK_EQ(r.commands[2].file, std::string("weapons/handheld/shared/lod.con"));
}

static void testErrors() {
  MemoryFiles files;
  files.files["a.con"] = "include nema.con\nendIf\nbeginRem\n";

  const Result r = run(files, "a.con");
  CHECK_EQ(r.diagnostics.size(), std::size_t(3));
  // A missing include is only a warning: that is how Refractor 2 itself behaves.
  CHECK_EQ(r.diagnostics[0].severity, Severity::Warning);
  CHECK(r.diagnostics[1].isError());  // endIf without if
  CHECK(r.diagnostics[2].isError());  // unclosed beginRem
}

static void testIncludeCycle() {
  MemoryFiles files;
  files.files["a.con"] = "include b.con\n";
  files.files["b.con"] = "include a.con\n";

  const Result r = run(files, "a.con");
  CHECK(!r.diagnostics.empty());
  CHECK_EQ(r.diagnostics.back().message, std::string("cyclic include"));
}

TEST_MAIN({
  testTokenizer();
  testBasicCommands();
  testBlockComments();
  testVarsAndIf();
  testIncludeAndRun();
  testErrors();
  testIncludeCycle();
})
