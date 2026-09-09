set pagination off
set confirm off
break *0x46246b
commands
  silent
  printf ">>challenge number %d\n", (int)$eax
  continue
end
break *0x462b1f
commands
  silent
  printf ">>FLAG-SET\n"
  continue
end
run
