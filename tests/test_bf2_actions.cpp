// Потік дій гравця — те, чим клієнт рухає свого солдата.
//
// Розкладка з `PlayerActionManager::processReceivedPacket` (0x44d670), а
// значення осей і кнопок — зі знятого трафіку оригінального клієнта:
// коли гравець біг уперед, третя ось стояла на 99, а маска кнопок
// дорівнювала 32, поки він тримав спринт.
#include "check.h"
#include "obf2/net/bf2_protocol.h"

using namespace obf2::net;
using namespace obf2::net::bf2;

namespace {

// Читач бітів у тому ж порядку, що й BitStream рушія.
struct Bits {
  const std::vector<std::byte>& data;
  std::size_t at = 0;

  std::uint32_t read(int count) {
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i) {
      const std::size_t byte = at >> 3;
      if (byte >= data.size()) return value;
      const int bit = (std::to_integer<int>(data[byte]) >> (at & 7)) & 1;
      value |= static_cast<std::uint32_t>(bit) << i;
      ++at;
    }
    return value;
  }
  // Число зі знаком: біт знака, далі значення.
  std::int32_t readSigned(int count) {
    const bool negative = read(1) == 1;
    const std::int32_t value = static_cast<std::int32_t>(read(count));
    return negative ? -value : value;
  }
};

}  // namespace

// Пакет має розбиратися рівно за тією розкладкою, що й у сервера.
static void testActionPacketLayout() {
  ExtendedHeader header;
  header.sequence = 5;
  header.ack = 7;
  header.ackBits = 0xFFFFFFFFu;

  PlayerAction action;
  action.axes[kAxisForward] = kAxisFull;
  action.axes[kAxisMouseX] = -13;
  action.axes[kAxisMouseY] = 40;
  action.buttons = kButtonSprint;

  const PlayerAction three[3] = {action, action, action};
  const auto packet = writePlayerActions(9, header, 172, three, 3, 200);

  Bits bits{packet};
  CHECK_EQ(bits.read(4), std::uint32_t(15));  // вид: дані
  CHECK_EQ(bits.read(8), std::uint32_t(9));   // номер з'єднання
  CHECK_EQ(bits.read(6), std::uint32_t(5));   // sequence
  CHECK_EQ(bits.read(6), std::uint32_t(7));   // ack
  CHECK_EQ(bits.read(32), 0xFFFFFFFFu);
  bits.read(16);                              // довжина корисної частини

  CHECK_EQ(bits.read(1), std::uint32_t(1));    // дії є
  CHECK_EQ(bits.read(4), std::uint32_t(3));    // три набори
  CHECK_EQ(bits.read(9), std::uint32_t(200));  // номер
  CHECK_EQ(bits.readSigned(31), 172);          // лічильник вводу

  for (int i = 0; i < 3; ++i) {
    CHECK_EQ(bits.readSigned(15), 0);
    CHECK_EQ(bits.readSigned(15), 0);
    CHECK_EQ(bits.readSigned(15), 0);
    CHECK_EQ(bits.readSigned(15), 99);   // вперед
    CHECK_EQ(bits.readSigned(15), -13);  // миша по горизонталі
    CHECK_EQ(bits.readSigned(15), 40);   // миша по вертикалі
    CHECK_EQ(bits.read(32), std::uint32_t(32));  // спринт
    CHECK_EQ(bits.read(9), std::uint32_t(0));
    CHECK_EQ(bits.read(1), std::uint32_t(1));    // прапорець
  }
  CHECK_EQ(bits.read(1), std::uint32_t(0));  // подій немає
}

// Стояти на місці — це всі осі в нулі й порожня маска. Такий пакет теж
// має бути правильним: оригінал шле його весь час, поки гравець не
// рухається.
static void testStandingStill() {
  ExtendedHeader header;
  const PlayerAction still;
  const auto packet = writePlayerActions(1, header, 0, &still, 1);

  Bits bits{packet};
  bits.read(4 + 8 + 6 + 6 + 32 + 16);
  CHECK_EQ(bits.read(1), std::uint32_t(1));
  CHECK_EQ(bits.read(4), std::uint32_t(1));
  bits.read(9);
  CHECK_EQ(bits.readSigned(31), 0);
  for (int i = 0; i < 6; ++i) CHECK_EQ(bits.readSigned(15), 0);
  CHECK_EQ(bits.read(32), std::uint32_t(0));
}

// Від'ємні осі: хід назад і рух миші вліво.
static void testNegativeAxes() {
  ExtendedHeader header;
  PlayerAction action;
  action.axes[kAxisForward] = -kAxisFull;
  action.axes[kAxisMouseX] = -127;
  const auto packet = writePlayerActions(1, header, 1, &action, 1);

  Bits bits{packet};
  bits.read(4 + 8 + 6 + 6 + 32 + 16);
  bits.read(1 + 4 + 9);
  bits.readSigned(31);
  bits.readSigned(15);
  bits.readSigned(15);
  bits.readSigned(15);
  CHECK_EQ(bits.readSigned(15), -99);
  CHECK_EQ(bits.readSigned(15), -127);
}

TEST_MAIN({
  testActionPacketLayout();
  testStandingStill();
  testNegativeAxes();
})
