set pagination off
set confirm off
break *0x7d4726
commands
  silent
  printf ">>тип=%d мережевий стан=%d\n", *(int*)($rsp+0x28), *(int*)($r12+0xb8)
  continue
end
run
