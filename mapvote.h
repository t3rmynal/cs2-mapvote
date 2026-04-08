#pragma once

#include <string>
#include <vector>
#include <map>

// запись о карте из конфига
struct MapEntry
{
    std::string mapName;     // de_mirage
    std::string displayName; // Mirage
};

// forward declarations для menus.h
class IUtilsApi;
class IMenusApi;
class IPlayersApi;
class IVEngineServer;
class IGameEvent;

// api указатели, получаем в AllPluginsLoaded
extern IUtilsApi*     g_pUtils;
extern IMenusApi*     g_pMenus;
extern IPlayersApi*   g_pPlayers;
extern IVEngineServer* g_pEngineServer;

// состояние голосования
extern std::vector<MapEntry>       g_MapList;
extern std::map<std::string, int>  g_Votes;
extern std::map<int, std::string>  g_PlayerVoted;
extern bool g_VoteActive;
extern bool g_VoteStarted;
