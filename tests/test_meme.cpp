// Читач `MemeFile 2.0` — графа, яким рушій анімує інтерфейс.
//
// Перевіряємо на справжньому `Menu/Ingame` з інсталяції користувача:
// синтетичний зразок тут нічого не вартий, бо саме на справжньому файлі
// й видно, чи сходиться розкладка класів із бібліотеки.
//
// Мірило те саме, що в `tools/meme_read.py --check`: файл має
// прочитатися **цілком**, без залишку.
#include <filesystem>

#include "obf2/meme/file.h"
#include "obf2/meme/graph.h"
#include "obf2/core/path.h"
#include "obf2/vfs/filesystem.h"
#include "check.h"

using namespace obf2;

namespace {

std::filesystem::path menuArchive() {
  return std::filesystem::path(OBF2_GAME_FILES) / "mods" / "bf2" / "Menu_client.zip";
}

// Порахувати вузли заданого класу — так само, як це робить
// `meme_read.py Ingame | grep -c`.
int count(const meme::File& file, std::string_view type) {
  int found = 0;
  for (const meme::Object& object : file.objects()) {
    if (object.type() == type) ++found;
  }
  return found;
}

}  // namespace

static void testIngameParsesWhole() {
  if (!std::filesystem::exists(menuArchive())) {
    std::printf("  немає %s — перевірку пропущено\n", menuArchive().string().c_str());
    return;
  }
  auto archive = ZipArchive::open(menuArchive());
  CHECK(archive != nullptr);
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  CHECK(data.has_value());
  if (!data) return;

  meme::File file;
  std::string error;
  const bool ok = file.load(*data, &error);
  if (!ok) std::printf("  %s\n", error.c_str());
  CHECK(ok);
  CHECK_EQ(file.version(), std::string("MemeFile 2.0"));
  for (const std::string& name : file.unknownClasses()) {
    std::printf("  класу немає в таблиці: %s\n", name.c_str());
  }
  CHECK(file.unknownClasses().empty());

  // Ті самі числа, що друкує `tools/meme_read.py Ingame`. Якщо розкладка
  // якогось класу поїде, поїдуть і вони.
  CHECK_EQ(count(file, "FloatData"), 58);
  CHECK_EQ(count(file, "BoolData"), 25);
  CHECK_EQ(count(file, "SplitNode"), 19);
  CHECK_EQ(count(file, "CullNode"), 13);
  CHECK_EQ(count(file, "VariableColorEffect"), 11);
  CHECK_EQ(count(file, "PictureNode"), 11);
  CHECK_EQ(count(file, "TransformNode"), 9);
  CHECK_EQ(count(file, "CullVariableActionNode"), 5);
  CHECK_EQ(count(file, "BfTransformNode"), 2);
}

// Головне, заради чого це й читається: **шар і його положення лежать у
// файлі**, а не в нашому коді. Ліва кутова ділянка — це `BfTransformNode`
// 400x64, чий X прив'язаний до змінної `BottomLeft/BottomLeft_XPos`.
static void testLayerPositionComesFromTheFile() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::File file;
  if (!file.load(*data)) return;

  const meme::Object* layer = nullptr;
  for (const meme::Object& object : file.objects()) {
    if (object.type() != "BfTransformNode") continue;
    const meme::Object* x = file.child(object, "X");
    if (x != nullptr && x->name == "BottomLeft/BottomLeft_XPos") layer = &object;
  }
  CHECK(layer != nullptr);
  if (layer == nullptr) return;

  const meme::Value* width = meme::File::field(*layer, "Width");
  CHECK(width != nullptr);
  if (width != nullptr) CHECK(std::abs(width->number - 400.0f) < 0.01f);

  // І початкове значення тієї змінної — теж звідти: -295, сховане
  // положення лівої ділянки.
  const meme::Object* x = file.child(*layer, "X");
  CHECK(x != nullptr);
  if (x != nullptr) {
    const meme::Value* value = meme::File::field(*x, "Value <do not edit>");
    CHECK(value != nullptr);
    if (value != nullptr) CHECK(std::abs(value->number + 295.0f) < 0.01f);
  }
}

// Решта файлів HUD теж має читатися цілком — їх дванадцять.
static void testEveryHudMemeParses() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;

  int whole = 0;
  for (const char* entry : {"BottomLeftAnimate", "BottomLeftStatic", "BottomRightAnimate",
                            "BottomRightStatic", "Global", "Ingame", "Left", "PreGlobal", "Top",
                            "TopLayer", "TopLeft", "TopRight"}) {
    const auto data = archive->read(normalizeAssetPath(entry));
    if (!data) continue;
    meme::File file;
    std::string error;
    if (file.load(*data, &error)) {
      ++whole;
    } else {
      std::printf("  %s: %s\n", entry, error.c_str());
    }
  }
  CHECK_EQ(whole, 12);
}

// Головне: граф **виконується**. Ставимо ціль лівої ділянки й дивимося,
// чи він сам довозить її туди — рівно на 600 одиницях за секунду, як
// каже `SetVariableSineAction {Speed 600}` у файлі.
static void testGraphMovesTheCornerPanel() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::Graph graph;
  CHECK(graph.load(*data));

  // Початкові значення — з файлу: ділянка схована на -295.
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 295.0f) < 0.01f);
  // `AniPos` теж звідти, і саме він відмикає гілку з рухом.
  CHECK(std::abs(graph.variables().get("AniPos") - 1.0f) < 0.01f);

  graph.variables().set("BottomLeft/BottomLeft_nextXPos", -137.0f);
  graph.update(1.0f / 30.0f);
  CHECK(graph.lastActions() > 0);
  // 600 за секунду -> 20 за такт.
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 275.0f) < 0.01f);

  for (int i = 0; i < 60; ++i) graph.update(1.0f / 30.0f);
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 137.0f) < 0.01f);
}

// Прозорості веде інша дія — `SetVariableSoftAction {Speed 10}`, і теж
// сама, без нашої допомоги.
static void testGraphMovesTheAlpha() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::Graph graph;
  if (!graph.load(*data)) return;

  graph.variables().set("BottomLeft/Alpha/BottomLeft_nextAlpha1", 1.0f);
  for (int i = 0; i < 60; ++i) graph.update(1.0f / 30.0f);
  CHECK(std::abs(graph.variables().get("BottomLeft/Alpha/BottomLeft_alpha1") - 1.0f) < 0.01f);
}

TEST_MAIN({
  testIngameParsesWhole();
  testGraphMovesTheCornerPanel();
  testGraphMovesTheAlpha();
  testLayerPositionComesFromTheFile();
  testEveryHudMemeParses();
})
