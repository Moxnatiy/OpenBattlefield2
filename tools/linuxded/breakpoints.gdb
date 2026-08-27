set pagination off
set confirm off
break *0x462b1f
commands
  silent
  printf ">>ПОЗНАЧКА-ВСТАНОВЛЕНА\n"
  continue
end
break *0x4631f1
commands
  silent
  printf ">>ВІДМОВА-27\n"
  continue
end
run
