set pagination off
set confirm off
break dice::hfe::GameServer::handleNetworkEvent
commands
  silent
  printf ">>мережева %d\n", (int)$rsi
  continue
end
break dice::hfe::ServerGameLogic::spawnPlayer
commands
  silent
  printf ">>spawnPlayer\n"
  continue
end
run
