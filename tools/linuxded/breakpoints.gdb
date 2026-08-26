set pagination off
set confirm off
break dice::hfe::GameServer::closeClientConnection
commands
  silent
  printf ">>відключення клієнта %d, причина %d\n", (int)$esi, (int)$edx
  continue
end
break *0x441755
commands
  silent
  printf ">>пачка поле5=%d\n", *(int*)($rsp+0x58)
  continue
end
run
