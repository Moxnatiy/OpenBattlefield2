set pagination off
set confirm off
break *0x7d453f
commands
  silent
  printf ">>фільтр=%d\n", (int)$eax
  continue
end
run
