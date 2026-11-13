// WEB_SOCKET_VARS.h - WebSocket variable declarations
// This file should be included only once in the main compilation unit

#include <WebSocketsServer.h>

#ifndef WEBSOCKETS_SERVER_CLIENT_MAX
#define WEBSOCKETS_SERVER_CLIENT_MAX 5
#endif

int socet_flash = 0;
int pass = 0;
int userIndex = -1;
int modul = 0;

// Клиент подтвердил ключ устройства кадром auth. До этого ему доступна только
// телеметрия; команды, меняющие состояние, отклоняются.
bool wsAuthorized[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};

WebSocketsServer webSocket = WebSocketsServer(81);
