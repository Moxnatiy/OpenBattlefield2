#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/hud/hud.h"
#include "obf2/hud/animation.h"
#include "obf2/hud/render.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

hud::Builder build(const std::string& source) {
  MemoryFiles files;
  files.files["hud.con"] = source;
  hud::Builder builder;
  con::Interpreter interpreter(files, [&](const con::Command& c) { builder.feed(c); });
  interpreter.runFile("hud.con");
  builder.finish();
  return builder;
}

}  // namespace

static void testPictureNode() {
  // Форма взята з реального HudElementsActionIcons.con.
  const hud::Builder builder = build(
      "hudBuilder.createPictureNode\tIngameHud WarningIcon 701 292 32 32\n"
      "hudBuilder.setPictureNodeTexture\tIngame/GeneralIcons/icon.tga\n"
      "hudBuilder.setNodeShowVariable\tWarningIconShow\n"
      "hudBuilder.setNodeInTime\t0.2\n"
      "hudBuilder.setNodeOutTime\t0.4\n"
      "hudBuilder.addNodeAlphaShowEffect\n");

  CHECK_EQ(builder.nodes().size(), std::size_t(1));
  if (builder.nodes().empty()) return;

  const hud::Node& node = builder.nodes().front();
  CHECK(node.type == hud::NodeType::Picture);
  CHECK_EQ(node.group, std::string("IngameHud"));
  CHECK_EQ(node.name, std::string("WarningIcon"));
  CHECK_EQ(node.x, 701.0f);
  CHECK_EQ(node.width, 32.0f);
  CHECK_EQ(node.texture, std::string("Ingame/GeneralIcons/icon.tga"));
  CHECK_EQ(node.showVariable, std::string("WarningIconShow"));
  CHECK_EQ(node.showEffects.size(), std::size_t(1));
  CHECK_EQ(builder.unknownCommands(), 0LL);
}

static void testPropertiesGoToLastCreatedNode() {
  const hud::Builder builder = build(
      "hudBuilder.createPictureNode A First 0 0 10 10\n"
      "hudBuilder.setPictureNodeTexture first.tga\n"
      "hudBuilder.createPictureNode A Second 0 0 10 10\n"
      "hudBuilder.setPictureNodeTexture second.tga\n");

  CHECK_EQ(builder.nodes().size(), std::size_t(2));
  if (builder.nodes().size() < 2) return;
  CHECK_EQ(builder.nodes()[0].texture, std::string("first.tga"));
  CHECK_EQ(builder.nodes()[1].texture, std::string("second.tga"));
}

static void testSetActiveObjectSwitchesTarget() {
  // Без setActiveObject друга текстура осіла б на другому вузлі —
  // і частина інтерфейсу зібралася б неправильно.
  const hud::Builder builder = build(
      "hudBuilder.createPictureNode A First 0 0 10 10\n"
      "hudBuilder.createPictureNode A Second 0 0 10 10\n"
      "hudBuilder.setActiveObject First\n"
      "hudBuilder.setPictureNodeTexture back_to_first.tga\n");

  CHECK_EQ(builder.nodes().size(), std::size_t(2));
  if (builder.nodes().size() < 2) return;
  CHECK_EQ(builder.nodes()[0].texture, std::string("back_to_first.tga"));
  CHECK(builder.nodes()[1].texture.empty());
}

static void testButtonRunsConsoleCommand() {
  // Кнопки інтерфейсу керують грою консольними командами — так це
  // влаштовано в оригіналі.
  const hud::Builder builder = build(
      "hudBuilder.createButtonNode Menu Quit 10 20 100 30\n"
      "hudBuilder.setButtonNodeConCmd game.quit\n"
      "hudBuilder.setButtonNodeAltConCmd game.confirmQuit 1\n"
      "hudBuilder.setButtonNodeMouseArea 12 22 96 26\n");

  CHECK_EQ(builder.nodes().size(), std::size_t(1));
  if (builder.nodes().empty()) return;

  const hud::Node& node = builder.nodes().front();
  CHECK(node.type == hud::NodeType::Button);
  CHECK_EQ(node.command, std::string("game.quit"));
  CHECK_EQ(node.altCommand, std::string("game.confirmQuit 1"));
  CHECK(node.hasMouseArea);
  CHECK_EQ(node.mouseWidth, 96.0f);
}

static void testColorAcceptsBothRanges() {
  // У файлах гри колір трапляється і як 0..1, і як 0..255.
  const hud::Builder normalized = build(
      "hudBuilder.createTextNode A T 0 0 10 10\n"
      "hudBuilder.setNodeColor 1.0 0.5 0.25 1.0\n");
  const hud::Builder bytes = build(
      "hudBuilder.createTextNode A T 0 0 10 10\n"
      "hudBuilder.setNodeColor 255 128 64 255\n");

  CHECK(!normalized.nodes().empty() && !bytes.nodes().empty());
  if (normalized.nodes().empty() || bytes.nodes().empty()) return;

  CHECK(std::abs(normalized.nodes()[0].color.g - 0.5f) < 0.01f);
  CHECK(std::abs(bytes.nodes()[0].color.g - 0.5f) < 0.01f);
}

static void testGroupsAreSeparated() {
  const hud::Builder builder = build(
      "hudBuilder.createPictureNode IngameHud A 0 0 1 1\n"
      "hudBuilder.createPictureNode Scoreboard B 0 0 1 1\n"
      "hudBuilder.createPictureNode IngameHud C 0 0 1 1\n");

  CHECK_EQ(builder.group("IngameHud").size(), std::size_t(2));
  CHECK_EQ(builder.group("Scoreboard").size(), std::size_t(1));
  CHECK_EQ(builder.groups().size(), std::size_t(2));
}

static void testCommandsBeforeAnyNodeAreCounted() {
  const hud::Builder builder = build("hudBuilder.setPictureNodeTexture orphan.tga\n");
  CHECK_EQ(builder.nodes().size(), std::size_t(0));
  CHECK_EQ(builder.unknownCommands(), 1LL);
}

static void testNodeRectScalesFromReference() {
  // Вузли задані в базових 800x600; на 1600x1200 усе рівно вдвічі більше.
  const hud::Builder builder = build("hudBuilder.createPictureNode Menu A 40 30 200 15\n");
  const hud::Screen screen{1600, 1200};
  const hud::ScreenRect rect = hud::nodeRect(builder.nodes()[0], screen);
  CHECK(std::abs(rect.x - 80.0f) < 0.01f);
  CHECK(std::abs(rect.y - 60.0f) < 0.01f);
  CHECK(std::abs(rect.width - 400.0f) < 0.01f);
  CHECK(std::abs(rect.height - 30.0f) < 0.01f);
}

static void testChildCoordinatesAreRelativeToParent() {
  // Головне правило HUD, записане в Readme.txt самих розробників:
  // перший аргумент — батьківський вузол, а x/y відлічуються від його
  // лівого верхнього кута. Доти ми брали їх як екранні, і все, що
  // глибше за один рівень, розповзалося по екрану.
  const hud::Builder builder = build(
      "hudBuilder.createSplitNode Global Panel 300 200 100 100\n"
      "hudBuilder.createPictureNode Panel Icon 10 5 16 16\n"
      "hudBuilder.createSplitNode Panel Inner 40 20 50 50\n"
      "hudBuilder.createPictureNode Inner Deep 1 2 8 8\n");

  const hud::Node* icon = nullptr;
  const hud::Node* deep = nullptr;
  for (const hud::Node& node : builder.nodes()) {
    if (node.name == "Icon") icon = &node;
    if (node.name == "Deep") deep = &node;
  }
  CHECK(icon != nullptr);
  CHECK(deep != nullptr);
  if (icon != nullptr) {
    CHECK(std::abs(icon->absX - 310.0f) < 0.01f);
    CHECK(std::abs(icon->absY - 205.0f) < 0.01f);
    CHECK_EQ(icon->area, std::string("Global"));
  }
  // Три рівні: 300+40+1 і 200+20+2.
  if (deep != nullptr) {
    CHECK(std::abs(deep->absX - 341.0f) < 0.01f);
    CHECK(std::abs(deep->absY - 222.0f) < 0.01f);
    CHECK_EQ(deep->area, std::string("Global"));
  }
}

static void testButtonAtFindsButtonUnderCursor() {
  const hud::Builder builder = build(
      "hudBuilder.createPictureNode Menu Background 0 0 800 600\n"
      "hudBuilder.createButtonNode Menu Play 40 120 220 18\n"
      "hudBuilder.setButtonNodeConCmd openbf2.startLevel\n"
      "hudBuilder.createButtonNode Menu Quit 40 144 220 18\n"
      "hudBuilder.setButtonNodeConCmd openbf2.quit\n");
  const hud::Screen screen{800, 600};

  const hud::Node* play = hud::buttonAt(builder, "Menu", screen, 50.0f, 125.0f);
  CHECK(play != nullptr);
  if (play != nullptr) CHECK_EQ(play->command, std::string("openbf2.startLevel"));

  const hud::Node* quit = hud::buttonAt(builder, "Menu", screen, 50.0f, 150.0f);
  CHECK(quit != nullptr);
  if (quit != nullptr) CHECK_EQ(quit->command, std::string("openbf2.quit"));

  // Поза кнопками — нічого, хоча під курсором є картинка-тло.
  CHECK(hud::buttonAt(builder, "Menu", screen, 500.0f, 400.0f) == nullptr);
}

static void testBarNodeHasDirectionBeforeRect() {
  // У смуги перед прямокутником стоїть напрям росту: без цього зсуву
  // координати з'їжджають на одну позицію.
  const hud::Builder builder = build(
      "hudBuilder.createBarNode Map FriendlyCPs 2 643 180 108 31\n"
      "hudbuilder.setBarNodeTexture 1 Ingame/Minimap/flags_Captured_Left.tga\n"
      "hudBuilder.setBarNodeValueVariable FriendlyCPs\n");

  CHECK_EQ(builder.nodes().size(), std::size_t(1));
  if (builder.nodes().empty()) return;
  const hud::Node& node = builder.nodes()[0];
  CHECK_EQ(node.barDirection, 2);
  CHECK(std::abs(node.x - 643.0f) < 0.01f);
  CHECK(std::abs(node.y - 180.0f) < 0.01f);
  CHECK(std::abs(node.width - 108.0f) < 0.01f);
  CHECK(std::abs(node.height - 31.0f) < 0.01f);
  CHECK_EQ(node.valueVariable, std::string("FriendlyCPs"));
  CHECK_EQ(node.barTextureFull, std::string("Ingame/Minimap/flags_Captured_Left.tga"));
}

static void testBarIsClippedByValue() {
  const hud::Builder builder = build(
      "hudBuilder.createBarNode Map Bar 2 0 0 100 10\n"
      "hudbuilder.setBarNodeTexture 1 bar.tga\n"
      "hudBuilder.setBarNodeValueVariable Fill\n");
  const hud::Screen screen{800, 600};

  hud::Context context;
  context.variableValue = [](std::string_view) { return 0.25f; };
  auto pieces = hud::buildNode(builder.nodes()[0], font::Font{}, "atlas", screen, context);
  CHECK_EQ(pieces.size(), std::size_t(1));
  if (pieces.empty()) return;

  // Чверть значення — чверть ширини: у NDC це від -1 до -0.95 при 800px.
  const auto& vertices = pieces[0].geometry.vertices;
  CHECK_EQ(vertices.size(), std::size_t(4));
  if (vertices.size() < 4) return;
  CHECK(std::abs(vertices[1].position.x - (-0.9375f)) < 0.01f);
  // Обрізана й текстура, інакше картинка стиснулася б.
  CHECK(std::abs(vertices[1].uv[0] - 0.25f) < 0.01f);
}

// Поява й зникнення в часі. Кут -pi/2 з відстанню 376 має відсунути
// вузол **униз** — саме так у грі виїжджає панель голосування за карту.
static void testShowEffectsAnimate() {
  hud::Builder builder = build(
      "hudBuilder.createPictureNode Root Panel 332 377 188 18\n"
      "hudBuilder.setNodeShowVariable VoteMapSelected\n"
      "hudBuilder.setNodeInTime 0.4\n"
      "hudBuilder.setNodeOutTime 0.2\n"
      "hudBuilder.addNodeMoveShowEffect -1.57 376\n"
      "hudBuilder.addNodeAlphaShowEffect\n");
  CHECK_EQ(builder.nodes().size(), std::size_t(1));
  if (builder.nodes().empty()) return;
  const hud::Node& node = builder.nodes()[0];

  hud::Animator animator;
  // Перша поява теж іде переходом: щойно створений `CullNode` має хід
  // -4, і 0x10004a57 ставить його в 0, а не в кінець.
  animator.setVisible(node, true);
  animator.advance(0.2f);
  CHECK(std::abs(animator.state(node).progress - 0.5f) < 0.001f);
  CHECK(animator.animating());
  animator.advance(0.2f);
  CHECK(std::abs(animator.state(node).progress - 1.0f) < 0.001f);
  // Кадр, у якому вузол став на місце, ще треба перемалювати — а вже
  // наступний ні.
  CHECK(animator.animating());
  animator.advance(0.0f);
  CHECK(!animator.animating());

  // Ховаємо: за outTime = 0.2 половина шляху проходить за 0.1 с.
  animator.setVisible(node, false);
  animator.advance(0.1f);
  const hud::ShowState half = animator.state(node);
  CHECK(std::abs(half.progress - 0.5f) < 0.001f);
  CHECK(std::abs(half.alpha - 0.5f) < 0.001f);
  CHECK(std::abs(half.offsetX) < 0.5f);  // -1.57 це майже рівно -pi/2
  // dy = +sin(a) * довжина * (1 - хід) = sin(-pi/2) * 376 * 0.5 = -188.
  // Знак саме такий: `MoveEffect::picturePaint` (`MemeDll.dll`,
  // 0x10001b27) рахує зсув як (-cos a, +sin a). Раніше тут стояло +188,
  // бо ми брали знаки з міркування, а не з коду.
  CHECK(std::abs(half.offsetY + 188.0f) < 0.5f);
  CHECK(animator.animating());

  animator.advance(0.1f);
  CHECK(std::abs(animator.state(node).progress) < 0.001f);
  animator.advance(0.1f);
  CHECK(!animator.animating());
}

TEST_MAIN({
  testShowEffectsAnimate();
  testBarNodeHasDirectionBeforeRect();
  testBarIsClippedByValue();
  testNodeRectScalesFromReference();
  testChildCoordinatesAreRelativeToParent();
  testButtonAtFindsButtonUnderCursor();
  testPictureNode();
  testPropertiesGoToLastCreatedNode();
  testSetActiveObjectSwitchesTarget();
  testButtonRunsConsoleCommand();
  testColorAcceptsBothRanges();
  testGroupsAreSeparated();
  testCommandsBeforeAnyNodeAreCounted();
})
