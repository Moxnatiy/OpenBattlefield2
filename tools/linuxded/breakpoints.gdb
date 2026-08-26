set pagination off
set confirm off
break *0x463108
commands
  silent
  printf ">>СТАН-4 і StartSimulation\n"
  continue
end
break *0x4631f1
commands
  silent
  printf ">>ВІДМОВА-27\n"
  continue
end
break *0x462ac0
commands
  silent
  printf ">>вміст-збігся\n"
  continue
end
run
