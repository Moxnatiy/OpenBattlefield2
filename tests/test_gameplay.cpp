#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"

using namespace obf2;

namespace {

// Дві точки: одна вже наша, друга нейтральна.
level::GameplayObjects makeGameplay() {
  level::GameplayObjects gameplay;

  level::ControlPoint ours;
  ours.id = 401;
  ours.nameKey = "CPNAME_base";
  ours.position = Vec3f{100.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  gameplay.controlPoints.push_back(ours);

  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.nameKey = "CPNAME_middle";
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.team = 0;
  gameplay.controlPoints.push_back(neutral);

  return gameplay;
}

void pump(server::GameServer& gameServer, server::GameClient& client, int steps = 8) {
  for (int i = 0; i < steps; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
}

}  // namespace

static void testSpawnUsesOwnedControlPoint() {
  // Гравець має з'явитися на точці своєї команди, а не в точці за
  // замовчуванням — так само, як в оригіналі.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  // Точка команди 1 стоїть на x = 100.
  CHECK(std::abs(gameServer.objects().front().position.x - 100.0f) < 1.0f);
}

static void testControlPointsAreLoaded() {
  server::GameServer gameServer(server::ServerSettings{});
  gameServer.setGameplay(makeGameplay());

  CHECK_EQ(gameServer.controlPoints().size(), std::size_t(2));
  if (gameServer.controlPoints().size() < 2) return;
  CHECK_EQ(gameServer.controlPoints()[0].id, 401);
  CHECK_EQ(gameServer.controlPoints()[0].team, 1);
  CHECK_EQ(gameServer.controlPoints()[1].team, 0);
  CHECK_EQ(gameServer.controlPoints()[1].nameKey, std::string("CPNAME_middle"));
}

static void testStandingOnNeutralPointCapturesIt() {
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};  // одразу на нейтральній точці

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.team = 0;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.controlPoints().front().team, 0);

  // Стоїмо на точці півтори секунди.
  for (int i = 0; i < 45; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testPointIsNotCapturedFromAfar() {
  server::ServerSettings settings;
  // Гравець з'являється далеко від точки.
  settings.spawnPosition = Vec3f{500.0f, 0.0f, 500.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 90; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 0);
}

static void testHeightDoesNotBlockCapture() {
  // Радіус захоплення горизонтальний: з даху будинку точка теж береться.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 30.0f, 0.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 60; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testEnemyFlagIsNeutralizedBeforeCapture() {
  // Чужу точку не можна забрати одразу: спершу прапор іде вниз (точка стає
  // нічия), і лише потім піднімається наш.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint enemy;
  enemy.id = 402;
  enemy.position = Vec3f{0.0f, 0.0f, 0.0f};
  enemy.radius = 10.0f;
  enemy.team = 2;  // точка противника
  enemy.timeToGetControl = 1.0f;
  enemy.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(enemy);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  // Через ~1.2 с прапор має впасти: точка нічия, але ще не наша.
  for (int i = 0; i < 36; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 0);

  // Ще секунда — і вона наша.
  for (int i = 0; i < 45; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testTicketsStartFromDefaults() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 250;
  settings.defaultTickets[2] = 250;
  settings.ticketRatio = 50.0f;  // sv.ticketRatio у відсотках

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  CHECK_EQ(gameServer.tickets(1), 125);
  CHECK_EQ(gameServer.tickets(2), 125);
  CHECK(gameServer.status() == server::GameStatus::Playing);
}

static void testTicketsBleedForTeamWithoutArea() {
  // Одна точка вагою 140 у команди 1: противник тече, ми — ні.
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 100;
  settings.ticketLossPerMin[2] = 60.0f;  // рівно 1 квиток за секунду за повної переваги

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint ours;
  ours.id = 401;
  ours.position = Vec3f{0.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  ours.areaValueTeam1 = 140.0f;
  ours.unableToChangeTeam = true;  // щоб ніхто її не перебрав під час тесту
  gameplay.controlPoints.push_back(ours);

  // Противник має лишатися з базою: команда взагалі без точок стікає
  // зовсім іншим, «кінцевим» темпом, і це перевіряє інший тест.
  level::ControlPoint theirs;
  theirs.id = 402;
  theirs.position = Vec3f{500.0f, 0.0f, 0.0f};
  theirs.radius = 10.0f;
  theirs.team = 2;
  theirs.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(theirs);
  gameServer.setGameplay(std::move(gameplay));

  // Десять секунд без жодного гравця.
  for (int i = 0; i < 300; ++i) gameServer.tick(1.0f / 30.0f);

  CHECK_EQ(gameServer.tickets(1), 100);
  // (60/60) * (140/100) = 1.4 квитка за секунду -> близько 14 за десять.
  const int lost = 100 - gameServer.tickets(2);
  CHECK(lost >= 13 && lost <= 15);
}

static void testRoundEndsWhenTicketsRunOut() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 2;
  settings.ticketLossPerMin[2] = 600.0f;

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint ours;
  ours.id = 401;
  ours.position = Vec3f{0.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  ours.areaValueTeam1 = 140.0f;
  ours.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(ours);
  level::ControlPoint theirs;
  theirs.id = 402;
  theirs.position = Vec3f{500.0f, 0.0f, 0.0f};
  theirs.radius = 10.0f;
  theirs.team = 2;
  theirs.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(theirs);
  gameServer.setGameplay(std::move(gameplay));

  for (int i = 0; i < 300; ++i) gameServer.tick(1.0f / 30.0f);

  CHECK_EQ(gameServer.tickets(2), 0);
  CHECK_EQ(gameServer.winner(), 1);
  CHECK(gameServer.status() == server::GameStatus::EndGame);
}

TEST_MAIN({
  testEnemyFlagIsNeutralizedBeforeCapture();
  testTicketsStartFromDefaults();
  testTicketsBleedForTeamWithoutArea();
  testRoundEndsWhenTicketsRunOut();
  testSpawnUsesOwnedControlPoint();
  testControlPointsAreLoaded();
  testStandingOnNeutralPointCapturesIt();
  testPointIsNotCapturedFromAfar();
  testHeightDoesNotBlockCapture();
})
