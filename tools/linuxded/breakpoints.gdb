# Точки зупину для стенда під gdb. Кожна лише друкує рядок і йде далі,
# щоб сервер працював як завжди.
set pagination off
set confirm off

break dice::hfe::GameServer::handleClientInfo
commands
  silent
  printf ">> handleClientInfo\n"
  continue
end

break dice::hfe::GameServer::clientLoadComplete
commands
  silent
  printf ">> clientLoadComplete\n"
  continue
end

break dice::hfe::GameServer::isClientReady
commands
  silent
  printf ">> isClientReady\n"
  continue
end

run
