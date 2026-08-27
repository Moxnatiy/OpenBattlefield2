set pagination off
set confirm off
break *0x460bb7
commands
  silent
  printf ">>getClient(%d) -> %p\n", *(int*)($rsp+0x174), (void*)$rax
  continue
end
break dice::hfe::GameServer::closeClientConnection
commands
  silent
  printf ">>closeClient причина %d\n", (int)$edx
  continue
end
run
