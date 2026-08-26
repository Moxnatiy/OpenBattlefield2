# Межі секцій у вихідному пакеті: три потоки пишуть один за одним, і
# позиція запису до/після кожного каже, скільки бітів зайняв кожен.
set pagination off
set confirm off

break dice::hfe::PlayerActionManager::transmit
commands
  silent
  printf ">>ДІЇ поля %d %d %d\n", *(int*)($rsi+0x10), *(int*)($rsi+0x14), *(int*)($rsi+0x18)
  continue
end

break dice::hfe::GameEventManager::transmit
commands
  silent
  printf ">>ПОДІЇ поля %d %d %d\n", *(int*)($rsi+0x10), *(int*)($rsi+0x14), *(int*)($rsi+0x18)
  continue
end

break dice::hfe::GhostManager::transmit
commands
  silent
  printf ">>ПРИВИДИ поля %d %d %d\n", *(int*)($rsi+0x10), *(int*)($rsi+0x14), *(int*)($rsi+0x18)
  continue
end

run
