// The `MemeFile 2.0` reader — the graph the engine animates the interface with.
//
// We check against a real `Menu/Ingame` from the user's installation: a synthetic
// sample is worth nothing here, because it is precisely on a real file that one
// sees whether the classes' layout from the library adds up.
//
// The measure is the same as in `tools/meme_read.py --check`: the file has to read
// **completely**, with nothing left over.
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

// Count the nodes of a given class — the same way
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
    std::printf("  no %s — the check was skipped\n", menuArchive().string().c_str());
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
    std::printf("  the class is not in the table: %s\n", name.c_str());
  }
  CHECK(file.unknownClasses().empty());

  // The same numbers `tools/meme_read.py Ingame` prints. If some class's layout
  // slides, they will slide too.
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

// The main thing this is read for: **a layer and its position lie in the file**,
// not in our code. The left corner region is a `BfTransformNode` 400x64 whose X is
// bound to the variable `BottomLeft/BottomLeft_XPos`.
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

  // And that variable's initial value comes from there too: -295, the left region's
  // hidden position.
  const meme::Object* x = file.child(*layer, "X");
  CHECK(x != nullptr);
  if (x != nullptr) {
    const meme::Value* value = meme::File::field(*x, "Value <do not edit>");
    CHECK(value != nullptr);
    if (value != nullptr) CHECK(std::abs(value->number + 295.0f) < 0.01f);
  }
}

// The other HUD files have to read completely too — there are twelve of them.
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

// The main thing: the graph **executes**. We set the left region's target and see
// whether it drives it there itself — at exactly 600 units per second, as
// `SetVariableSineAction {Speed 600}` in the file says.
static void testGraphMovesTheCornerPanel() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::Graph graph;
  CHECK(graph.load(*data));

  // The initial values come from the file: the region is hidden at -295.
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 295.0f) < 0.01f);
  // `AniPos` comes from there too, and it is what unlocks the branch with the movement.
  CHECK(std::abs(graph.variables().get("AniPos") - 1.0f) < 0.01f);

  graph.variables().set("BottomLeft/BottomLeft_nextXPos", -137.0f);
  graph.update(1.0f / 30.0f);
  CHECK(graph.lastActions() > 0);
  // 600 per second -> 20 per tick.
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 275.0f) < 0.01f);

  for (int i = 0; i < 60; ++i) graph.update(1.0f / 30.0f);
  CHECK(std::abs(graph.variables().get("BottomLeft/BottomLeft_XPos") + 137.0f) < 0.01f);
}

// The alphas are driven by a different action — `SetVariableSoftAction {Speed 10}`,
// and by itself too, without our help.
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

// The HUD's four corner regions also come from the file, together with the binding
// of X to a variable. Until now these numbers stood in main.cpp by hand.
static void testLayersComeFromTheFile() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::Graph graph;
  if (!graph.load(*data)) return;

  const auto layers = graph.layers();
  CHECK_EQ(layers.size(), std::size_t(2));
  if (layers.size() != 2) return;

  CHECK_EQ(layers[0].variable, std::string("BottomLeft/BottomLeft_XPos"));
  CHECK(std::abs(layers[0].x + 295.0f) < 0.01f);
  CHECK(std::abs(layers[0].y - 563.0f) < 0.01f);
  CHECK(std::abs(layers[0].width - 400.0f) < 0.01f);
  CHECK(layers[0].hasTwin);
  CHECK(std::abs(layers[0].twinX + 1.0f) < 0.01f);
  CHECK(std::abs(layers[0].twinY - 563.0f) < 0.01f);

  CHECK_EQ(layers[1].variable, std::string("BottomRight/BottomRight_XPos"));
  CHECK(std::abs(layers[1].x - 503.0f) < 0.01f);
  CHECK(std::abs(layers[1].y - 497.0f) < 0.01f);
  CHECK(layers[1].hasTwin);
  CHECK(std::abs(layers[1].twinX - 401.0f) < 0.01f);
  CHECK(std::abs(layers[1].twinY - 563.0f) < 0.01f);
}

// The right region is driven not by one variable but by a machine of five
// `CullVariableActionNode` in the file itself. It is taken apart in
// (`docs/formats/hud-meme-graph.md`):
//
//   direction = 1  -> the target is `oldXPos`, and on arrival `setDirection` is
//                     cleared and the alpha goes to `newAlpha` = 1;
//   direction = 0  -> `setDirection` comes on, the alpha goes to `oldAlpha` = 0,
//                     and **only once faded** does the region travel to
//                     `newXPos`.
//
// The key to this is `ToggleData::value` (`MemeDll.dll`, 0x100032a6):
// "Data 1" is taken when the switch is **zero**.
static void testGraphRunsTheRightPanelMachine() {
  if (!std::filesystem::exists(menuArchive())) return;
  auto archive = ZipArchive::open(menuArchive());
  if (archive == nullptr) return;
  const auto data = archive->read(normalizeAssetPath("Ingame"));
  if (!data) return;

  meme::Graph graph;
  if (!graph.load(*data)) return;
  auto& v = graph.variables();

  // The start comes from the file: hidden 503, shown 201, alpha 0.
  CHECK(std::abs(v.get("BottomRight/BottomRight_XPos") - 503.0f) < 0.01f);
  CHECK(std::abs(v.get("BottomRight/BottomRight_oldXPos") - 201.0f) < 0.01f);
  CHECK(std::abs(v.get("BottomRight/BottomRight_newXPos") - 503.0f) < 0.01f);
  CHECK(std::abs(v.get("BottomRight/Alpha/BottomRight_alpha")) < 0.01f);

  // Show: the region travels to `oldXPos` and fades in there.
  v.set("BottomRight/BottomRight_direction", 1.0f);
  for (int i = 0; i < 120; ++i) graph.update(1.0f / 30.0f);
  CHECK(std::abs(v.get("BottomRight/BottomRight_XPos") - 201.0f) < 0.01f);
  CHECK(std::abs(v.get("BottomRight/Alpha/BottomRight_alpha") - 1.0f) < 0.01f);

  // Hide: it fades first, and until it has faded it does not move.
  //
  // The one-tick delay here is real rather than an oversight of ours: the actions
  // go from the outermost node to the innermost, while `setDirection` is set by the
  // **innermost**. So on the first tick the alpha still travels towards `newAlpha`,
  // and only from the second towards `oldAlpha`.
  v.set("BottomRight/BottomRight_direction", 0.0f);
  graph.update(1.0f / 30.0f);
  graph.update(1.0f / 30.0f);
  CHECK(v.get("BottomRight/Alpha/BottomRight_alpha") < 1.0f);
  CHECK(std::abs(v.get("BottomRight/BottomRight_XPos") - 201.0f) < 0.01f);

  for (int i = 0; i < 240; ++i) graph.update(1.0f / 30.0f);
  CHECK(std::abs(v.get("BottomRight/Alpha/BottomRight_alpha")) < 0.01f);
  CHECK(std::abs(v.get("BottomRight/BottomRight_XPos") - 503.0f) < 0.01f);
}

TEST_MAIN({
  testIngameParsesWhole();
  testGraphRunsTheRightPanelMachine();
  testLayersComeFromTheFile();
  testGraphMovesTheCornerPanel();
  testGraphMovesTheAlpha();
  testLayerPositionComesFromTheFile();
  testEveryHudMemeParses();
})
