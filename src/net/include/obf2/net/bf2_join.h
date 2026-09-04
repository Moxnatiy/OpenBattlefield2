#pragma once
// Порядок, у якому клієнт доводить розмову з оригінальним сервером BF2
// до появи гравця.
//
// Це чиста логіка без сокета: вона лише каже, **що** слати наступним і
// **коли** настав час. Пакети складає і відправляє той, хто нею
// користується. Так її можна перевірити тестом, не піднімаючи сервера, —
// а перевіряти є що, бо кожне правило тут здобуте вимірюванням на
// живому сервері й коштувало нам не одного хибного висновку.
//
// Правила, і звідки вони взялися:
//
//   * **пауза між кроками.** Мережеві події виконуються наступним тактом
//     сервера, тож два кроки в одному пакеті застають старий стан. Три
//     секунди — з досліду; менше не перевіряли;
//   * **`NELoadComplete` — після справжнього завантаження.** Подія саме
//     це й означає. Поки ми вантажили рівень (близько одинадцяти секунд)
//     і мовчали, сервер устигав нас відключити;
//   * **вибір чекає на гравця.** Команду, набір і місце появи шле не
//     послідовність сама, а екран появи — після DONE. У безголовому
//     запуску вибір задають наперед, і тоді `Ready` проходиться відразу;
//   * **порядок вибору саме такий.** `NESelectTeam`, `NESelectKit`,
//     `NESelectSpawnGroup` — перевірено на оригінальному сервері, після
//     них викликається `ServerGameLogic::spawnPlayer`.
//
// Докладніше — docs/functions/network-events.md.
#include <chrono>
#include <optional>

namespace obf2::net::bf2 {

// Що робимо на цьому кроці.
enum class JoinStep {
  Level,       // сказати «рівень завантажено»
  Content,     // перевірка вмісту
  Database,    // сказати «база гравців отримана»
  Simulation,  // NEStartSimulation — «почати відлік»
  Ready,       // екран появи: чекаємо, поки гравець натисне DONE
  Team,        // NESelectTeam
  Kit,         // NESelectKit
  Group,       // NESelectSpawnGroup
  Done,        // більше нічого не шлемо
};

const char* joinStepName(JoinStep step);

// Що обрав гравець на екрані появи.
struct JoinChoice {
  int team = 1;
  int kit = 0;
  // Номер групи появи **з переліку сервера** (`CreateSpawnGroupEvent`),
  // а не номер контрольної точки з даних рівня. Нуль означає «не
  // обрано»: сервер спавнить лише тих, у кого `getSpawnGroup() > 0`.
  int group = 0;
};

class JoinSequence {
 public:
  using Clock = std::chrono::steady_clock;

  // Паузи взяті зі знятого трафіку оригіналу, а не з обережності.
  //
  // Перевірка вмісту йде **в кінці завантаження**, разом із
  // `NELoadComplete` — одним пакетом, без паузи. Так воно й має бути за
  // змістом: сервер питає, чи збігається вміст, саме тоді, коли клієнт
  // щойно його прочитав. Ми ж тримали тут три секунди «про запас», і на
  // швидкому натисканні DONE перевірка виходила **після** появи —
  // послідовність, неможлива в оригіналі.
  static constexpr std::chrono::milliseconds kContentDelay{0};

  // `NEDatabaseComplete` — через 1.1 с після перевірки. Це виміряне
  // число з дампу, а не округлення.
  static constexpr std::chrono::milliseconds kStepDelay{1100};

  // А от вибір гравця йде **без паузи**: у дампі три події появи
  // розділяють ті самі частки секунди, що й натискання, а сервер
  // відповідає `NEPlayerSpawned` за 100 мс. Пауза тут була нашою
  // вигадкою і давала майже десять секунд між DONE і появою.
  static constexpr std::chrono::milliseconds kChoiceDelay{0};

  // Сервер сказав, який рівень він грає.
  void setLevelReady() { levelReady_ = true; }
  // Ми справді завантажили рівень і можемо відповідати на пінги.
  void setClientLoaded() { clientLoaded_ = true; }

  // Досліди: пропустити перевірку вмісту або повідомлення про базу.
  void setSkipContent(bool skip) { skipContent_ = skip; }
  void setSkipDatabase(bool skip) { skipDatabase_ = skip; }
  // `NEStartSimulation` — «почати відлік». Чи потрібна вона для появи,
  // ми ще з'ясовуємо, тож крок вимикається прапорцем.
  void setSkipSimulation(bool skip) { skipSimulation_ = skip; }

  // Гравець натиснув DONE. Можна кликати ще до рукостискання — тоді
  // послідовність не спиниться на `Ready`.
  void ask(const JoinChoice& choice) {
    choice_ = choice;
    asked_ = true;
  }

  bool asked() const { return asked_; }
  const JoinChoice& choice() const { return choice_; }
  JoinStep step() const { return step_; }
  bool done() const { return step_ == JoinStep::Done; }

  // Чи настав час для наступного кроку. nullopt — ще чекаємо: не минула
  // пауза, не завантажилися, або стоїмо на `Ready` без вибору.
  //
  // Кроки, які нічого не шлють (пропущена перевірка, `Ready` з уже
  // зробленим вибором), проходяться тут-таки, тож повернутий крок завжди
  // той, який справді треба відіслати.
  std::optional<JoinStep> next(Clock::time_point now);

  // Крок відіслано: наступний піде не раніше, ніж через паузу.
  void commit(Clock::time_point now);

 private:
  JoinStep step_ = JoinStep::Level;
  // Окремий прапорець, а не порівняння часу з нулем: нульова мітка — це
  // теж дійсний момент, і в тесті вона трапляється відразу.
  bool started_ = false;
  Clock::time_point last_{};
  bool levelReady_ = false;
  bool clientLoaded_ = false;
  bool skipContent_ = false;
  bool skipDatabase_ = false;
  bool skipSimulation_ = true;
  bool asked_ = false;
  JoinChoice choice_;
};

}  // namespace obf2::net::bf2
