set pagination off
set confirm off
break dice::hfe::GhostManager::transmit
commands
  silent
  printf ">>межа %d\n", *(int*)($rsi+0x18)
  continue
end
run
