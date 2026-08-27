set pagination off
set confirm off
break *0x46246b
commands
  silent
  printf ">>номер виклику %d\n", (int)$eax
  continue
end
break *0x462b1f
commands
  silent
  printf ">>ПОЗНАЧКА-ВСТАНОВЛЕНА\n"
  continue
end
run
