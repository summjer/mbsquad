#pragma once

namespace sp {

class Server;
class LogParser;
class WsHandler;
class PointsService;

void registerEventRoutes(Server& server);
void initEventModule(LogParser* parser, WsHandler* ws, PointsService* pts);
void shutdownEventModule();

} // namespace sp
