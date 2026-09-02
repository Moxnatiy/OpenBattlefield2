#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"
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

// Стіна поперек шляху: два трикутники в площині X = wallX.
mesh::CollisionLayer makeWall(float wallX) {
  mesh::CollisionLayer layer;
  layer.type = mesh::ColType::Soldier;
  layer.vertices = {
      mesh::Vec3{wallX, -5.0f, -10.0f}, mesh::Vec3{wallX, 10.0f, -10.0f},
      mesh::Vec3{wallX, 10.0f, 10.0f},  mesh::Vec3{wallX, -5.0f, 10.0f},
  };
  layer.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  layer.bounds.min = mesh::Vec3{wallX - 0.1f, -5.0f, -10.0f};
  layer.bounds.max = mesh::Vec3{wallX + 0.1f, 10.0f, 10.0f};
  return layer;
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

static void testSpawnUsesRealSpawnPoint() {
  // Є справжня точка появи, прив'язана до нашої контрольної, — солдат має
  // стати саме на неї, а не в центр прапора.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay = makeGameplay();
  level::SpawnPoint spawn;
  spawn.templateName = "base_1";
  spawn.position = Vec3f{123.0f, 10.0f, 45.0f};
  spawn.offset = Vec3f{0.0f, 1.25f, 0.0f};
  spawn.controlPointId = 401;  // точка команди 1
  gameplay.spawnPoints.push_back(spawn);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  const Vec3f position = gameServer.objects().front().position;
  CHECK(std::abs(position.x - 123.0f) < 0.01f);
  CHECK(std::abs(position.z - 45.0f) < 0.01f);
  // Зсув появи (setSpawnPositionOffset) додається до позиції точки: без
  // нього солдат стартував би з 10.0 і за ці такти вже впав би нижче.
  CHECK(position.y > 10.5f);
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

static void testDeathCostsTicketAndRespawns() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 100;
  settings.respawnDelay = 0.5f;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.tickets(1), 100);
  CHECK_EQ(gameServer.players().size(), std::size_t(1));
  if (gameServer.players().empty()) return;
  CHECK(gameServer.players().front().alive);

  gameServer.killPlayer(gameServer.players().front().id, "тест");
  CHECK_EQ(gameServer.tickets(1), 99);
  CHECK(!gameServer.players().front().alive);

  // Через півсекунди гравець має з'явитися знову з повним здоров'ям.
  for (int i = 0; i < 30; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK(gameServer.players().front().alive);
  CHECK(gameServer.players().front().health > 99.0f);
  CHECK_EQ(gameServer.tickets(1), 99);
}

static void testCollisionStillWorksAfterRespawn() {
  // Повідомлена помилка: після появи гравець проходив крізь об'єкти.
  server::ServerSettings settings;
  settings.respawnDelay = 0.2f;
  settings.spawnPosition = Vec3f{-3.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(makeWall(0.0f), Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  const auto pushForward = [&]() {
    net::PlayerInput input;
    // Праворуч від нульового кута — це +X, тобто рівно у стіну.
    input.moveRight = 1.0f;
    for (int i = 0; i < 120; ++i) {
      client.setInput(input);
      gameServer.tick(1.0f / 30.0f);
      client.tick(1.0f / 30.0f);
    }
  };

  pushForward();
  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  // До появи стіна тримає.
  CHECK(gameServer.objects().front().position.x < 0.2f);

  // Тепер смерть, поява — і знову у стіну.
  gameServer.killPlayer(gameServer.players().front().id, "тест");
  for (int i = 0; i < 30; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK(gameServer.players().front().alive);

  pushForward();
  CHECK(gameServer.objects().front().position.x < 0.2f);
}

static void testSoldierStandsOnObject() {
  // Була помилка: зайшовши на об'єкт, гравець лишався на висоті терену.
  // Земля має рахуватися й по геометрії зіткнень.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 5.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  // Пласка «підлога» на висоті 2 метри під точкою появи.
  mesh::CollisionLayer floorLayer;
  floorLayer.type = mesh::ColType::Soldier;
  floorLayer.vertices = {
      mesh::Vec3{-5.0f, 2.0f, -5.0f}, mesh::Vec3{5.0f, 2.0f, -5.0f},
      mesh::Vec3{5.0f, 2.0f, 5.0f},   mesh::Vec3{-5.0f, 2.0f, 5.0f},
  };
  // Обхід такий, щоб нормаль дивилася вгору за домовленістю рушія.
  floorLayer.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  floorLayer.bounds.min = mesh::Vec3{-5.0f, 1.9f, -5.0f};
  floorLayer.bounds.max = mesh::Vec3{5.0f, 2.1f, 5.0f};

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(floorLayer, Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 90; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }

  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  // Має стояти на об'єкті (y = 2), а не провалитися до нуля.
  CHECK(std::abs(gameServer.objects().front().position.y - 2.0f) < 0.2f);
}

static void testSoldierWalksUpStep() {
  // Сходинка нижча за нижню сферу солдата має прохо­дитися, а не спиняти.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-2.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  // Верхня площина сходинки заввишки 0.3 м, починається на x = 0.
  mesh::CollisionLayer step;
  step.type = mesh::ColType::Soldier;
  step.vertices = {
      mesh::Vec3{0.0f, 0.3f, -5.0f}, mesh::Vec3{5.0f, 0.3f, -5.0f},
      mesh::Vec3{5.0f, 0.3f, 5.0f},  mesh::Vec3{0.0f, 0.3f, 5.0f},
  };
  step.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  step.bounds.min = mesh::Vec3{0.0f, 0.2f, -5.0f};
  step.bounds.max = mesh::Vec3{5.0f, 0.4f, 5.0f};

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(step, Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  net::PlayerInput input;
  input.moveRight = 1.0f;  // на схід, у сходинку
  // Один такт бігу це 3.9 м/с, тож за секунду солдат саме зійде на
  // майданчик і лишиться на ньому, а не пробіжить його наскрізь.
  for (int i = 0; i < 30; ++i) {
    client.setInput(input);
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }

  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  const Vec3f position = gameServer.objects().front().position;
  CHECK(position.x > 0.5f);                     // зійшов, а не вперся
  CHECK(std::abs(position.y - 0.3f) < 0.15f);   // і стоїть на сходинці
}

// Обране місце появи звужує вибір до однієї групи. Група в рушії — це
// набір точок одного прапора (`SpawnGroup::getControlPointId`), а точка
// всередині групи береться випадково (docs/functions/spawn.md).
static void testSpawnGroupNarrowsTheChoice() {
  server::ServerSettings settings;
  settings.spawnOnJoin = false;  // перевіряємо саме шлях через DONE

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;

  // Два прапори однієї команди, у кожного своя точка появи.
  for (int i = 0; i < 2; ++i) {
    level::ControlPoint point;
    point.id = 401 + i;
    point.nameKey = "CPNAME_" + std::to_string(i);
    point.position = Vec3f{static_cast<float>(i) * 500.0f, 0.0f, 0.0f};
    point.team = 1;
    gameplay.controlPoints.push_back(point);

    level::SpawnPoint spawn;
    spawn.templateName = "sp_" + std::to_string(i);
    spawn.position = Vec3f{static_cast<float>(i) * 500.0f, 10.0f, 7.0f};
    spawn.controlPointId = 401 + i;
    gameplay.spawnPoints.push_back(spawn);
  }
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  CHECK_EQ(gameServer.objects().size(), std::size_t(0));
  if (gameServer.players().empty()) return;

  // Просимо другий прапор — і маємо стати саме на його точку.
  CHECK(gameServer.requestSpawn(gameServer.players().front().id, 1, 0, 402));
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  CHECK(std::abs(gameServer.objects().front().position.x - 500.0f) < 0.01f);
}

// Прапор, якого немає, не має лишити гравця без появи: беремо будь-яку
// свою точку.
static void testUnknownSpawnGroupFallsBack() {
  server::ServerSettings settings;
  settings.spawnOnJoin = false;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay = makeGameplay();
  level::SpawnPoint spawn;
  spawn.templateName = "base_1";
  spawn.position = Vec3f{123.0f, 10.0f, 45.0f};
  spawn.controlPointId = 401;
  gameplay.spawnPoints.push_back(spawn);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  if (gameServer.players().empty()) return;

  CHECK(gameServer.requestSpawn(gameServer.players().front().id, 1, 0, 999));
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  CHECK(std::abs(gameServer.objects().front().position.x - 123.0f) < 0.01f);
}

TEST_MAIN({
  testSpawnGroupNarrowsTheChoice();
  testUnknownSpawnGroupFallsBack();
  testSoldierStandsOnObject();
  testSoldierWalksUpStep();
  testCollisionStillWorksAfterRespawn();
  testDeathCostsTicketAndRespawns();
  testEnemyFlagIsNeutralizedBeforeCapture();
  testTicketsStartFromDefaults();
  testTicketsBleedForTeamWithoutArea();
  testRoundEndsWhenTicketsRunOut();
  testSpawnUsesOwnedControlPoint();
  testSpawnUsesRealSpawnPoint();
  testControlPointsAreLoaded();
  testStandingOnNeutralPointCapturesIt();
  testPointIsNotCapturedFromAfar();
  testHeightDoesNotBlockCapture();
})
