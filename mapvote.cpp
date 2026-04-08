// cs2 map vote plugin
// by t3rmynal
// https://github.com/t3rmynal/cs2-mapvote

// порядок важен: SDK-типы до include/menus.h
#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <entity2/entitysystem.h>

// pisex utils api (IUtilsApi, IMenusApi, IPlayersApi, Menu struct)
#include "include/menus.h"

#include "mapvote.h"

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <ctime>

// глобали

IUtilsApi*     g_pUtils       = nullptr;
IMenusApi*     g_pMenus       = nullptr;
IPlayersApi*   g_pPlayers     = nullptr;
IVEngineServer* g_pEngineServer = nullptr;

std::vector<MapEntry>       g_MapList;
std::map<std::string, int>  g_Votes;
std::map<int, std::string>  g_PlayerVoted;
bool g_VoteActive  = false;
bool g_VoteStarted = false;

// классы

class MapVote final : public ISmmPlugin
{
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void AllPluginsLoaded() override;

    const char* GetAuthor()      override { return "t3rmynal"; }
    const char* GetName()        override { return "Map Vote"; }
    const char* GetDescription() override { return "Vote for next map at match end"; }
    const char* GetURL()         override { return "https://github.com/t3rmynal/cs2-mapvote"; }
    const char* GetLicense()     override { return "MIT"; }
    const char* GetVersion()     override { return "1.1.0"; }
    const char* GetDate()        override { return __DATE__; }
    const char* GetLogTag()      override { return "MAPVOTE"; }

private:
    void LoadConfig();

    void StartVote();
    void ShowVoteMenu(int iSlot);
    void EndVote();
    void ResetVote();

    std::string GetWinnerMap();
    std::string GetDisplayName(const std::string& mapName);
    int         GetTotalVotes();

    std::string m_sGamePath;
    int         m_nCurrentRound = 0;
};

MapVote g_Plugin;
PLUGIN_EXPOSE(MapVote, g_Plugin);

// Load / Unload

bool MapVote::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer,
                        IVEngineServer, INTERFACEVERSION_VENGINESERVER);

    m_sGamePath = ismm->GetGamePath();
    srand((unsigned)time(nullptr));

    LoadConfig();

    META_CONPRINTF("[MapVote] загружен, карт: %d\n", (int)g_MapList.size());
    return true;
}

bool MapVote::Unload(char* error, size_t maxlen)
{
    // снять все хуки нашего плагина
    if (g_pUtils)
        g_pUtils->ClearAllHooks(g_PLID);

    ResetVote();
    g_MapList.clear();
    g_Votes.clear();
    m_nCurrentRound = 0;

    return true;
}

// AllPluginsLoaded

void MapVote::AllPluginsLoaded()
{
    g_pUtils   = (IUtilsApi*)  g_SMAPI->MetaFactory(Utils_INTERFACE,    nullptr, nullptr);
    g_pMenus   = (IMenusApi*)  g_SMAPI->MetaFactory(Menus_INTERFACE,    nullptr, nullptr);
    g_pPlayers = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE,  nullptr, nullptr);

    if (!g_pUtils || !g_pMenus || !g_pPlayers)
    {
        META_CONPRINTF("[MapVote] ошибка: cs2-menus не загружен!\n");
        return;
    }

    // cs_win_panel_match — основной триггер, матч точно закончился
    g_pUtils->HookEvent(g_PLID, "cs_win_panel_match",
        [this](const char*, IGameEvent*, bool)
        {
            if (g_VoteStarted) return;
            g_VoteStarted = true;
            META_CONPRINTF("[MapVote] матч завершён, голосование через 3 сек\n");
            g_pUtils->CreateTimer(3.0f, [this]() -> float
            {
                StartVote();
                return -1.0f;
            });
        });

    // round_start — считаем раунды
    g_pUtils->HookEvent(g_PLID, "round_start",
        [this](const char*, IGameEvent*, bool)
        {
            m_nCurrentRound++;
        });

    // round_end — запасной триггер, если win_panel не пришёл
    g_pUtils->HookEvent(g_PLID, "round_end",
        [this](const char*, IGameEvent* pEvent, bool)
        {
            if (g_VoteStarted) return;

            // reason 16 = game_over в cs2
            int reason = pEvent ? pEvent->GetInt("reason") : 0;
            if (reason != 16) return;

            g_VoteStarted = true;
            META_CONPRINTF("[MapVote] game_over reason, голосование через 2 сек\n");
            g_pUtils->CreateTimer(2.0f, [this]() -> float
            {
                StartVote();
                return -1.0f;
            });
        });

    META_CONPRINTF("[MapVote] api получен, события зарегистрированы\n");
}

// конфиг

void MapVote::LoadConfig()
{
    std::string path = m_sGamePath + "/addons/mapvote/configs/maps.cfg";
    std::ifstream file(path);
    if (!file.is_open())
    {
        META_CONPRINTF("[MapVote] не найден конфиг: %s\n", path.c_str());
        // карты по умолчанию
        g_MapList = {{"de_mirage","Mirage"},{"de_inferno","Inferno"},{"de_dust2","Dust 2"}};
        for (const auto& e : g_MapList) g_Votes[e.mapName] = 0;
        return;
    }

    g_MapList.clear();
    g_Votes.clear();

    bool inBlock = false;
    std::string line;
    while (std::getline(file, line))
    {
        // убрать комментарий
        auto pos = line.find("//");
        if (pos != std::string::npos) line = line.substr(0, pos);

        // trim
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.erase(0, 1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();

        if (line == "{")  { inBlock = true;  continue; }
        if (line == "}")  { inBlock = false; continue; }
        if (!inBlock)     continue;

        // парсим "ключ" "значение"
        std::vector<std::string> tokens;
        size_t i = 0;
        while (i < line.size())
        {
            if (line[i] == '"')
            {
                size_t end = line.find('"', i + 1);
                if (end == std::string::npos) break;
                tokens.push_back(line.substr(i + 1, end - i - 1));
                i = end + 1;
            }
            else { i++; }
        }

        if (tokens.size() >= 2)
        {
            MapEntry e;
            e.mapName     = tokens[0];
            e.displayName = tokens[1];
            g_MapList.push_back(e);
            g_Votes[e.mapName] = 0;
        }
    }

    META_CONPRINTF("[MapVote] загружено %d карт из %s\n",
        (int)g_MapList.size(), path.c_str());
}

// голосование

void MapVote::StartVote()
{
    if (g_VoteActive) return;
    if (g_MapList.empty())
    {
        META_CONPRINTF("[MapVote] список карт пуст, голосование отменено\n");
        return;
    }

    g_VoteActive = true;
    META_CONPRINTF("[MapVote] голосование начато\n");

    g_pUtils->PrintToChatAll(" \x04[MapVote]\x01 Голосование за следующую карту! \x06%d секунд\x01.",
        30);

    // показываем меню каждому живому игроку
    for (int i = 0; i < 64; i++)
    {
        if (!g_pPlayers->IsConnected(i)) continue;
        if (g_pPlayers->IsFakeClient(i)) continue;
        ShowVoteMenu(i);
    }

    // таймер на 30 сек — конец голосования
    g_pUtils->CreateTimer(30.0f, [this]() -> float
    {
        if (g_VoteActive) EndVote();
        return -1.0f;
    });
}

void MapVote::ShowVoteMenu(int iSlot)
{
    Menu hMenu;
    g_pMenus->SetTitleMenu(hMenu, "Следующая карта");

    for (const auto& entry : g_MapList)
    {
        std::string label = entry.displayName
            + " [" + std::to_string(g_Votes.at(entry.mapName)) + "]";
        g_pMenus->AddItemMenu(hMenu, entry.mapName.c_str(), label.c_str());
    }

    // кнопка пропустить
    g_pMenus->AddItemMenu(hMenu, "skip", "Не участвовать");
    g_pMenus->SetExitMenu(hMenu, false);

    g_pMenus->SetCallback(hMenu,
        [this](const char* szBack, const char* szFront, int iItem, int iSlot)
        {
            if (!szBack) return;
            if (strcmp(szBack, "skip") == 0) return; // просто закрыл
            if (!g_VoteActive) return;

            std::string mapName = szBack;
            if (!g_Votes.count(mapName)) return; // неизвестная карта

            // снять старый голос
            if (g_PlayerVoted.count(iSlot))
            {
                const std::string& old = g_PlayerVoted[iSlot];
                if (g_Votes.count(old) && g_Votes[old] > 0)
                    g_Votes[old]--;
            }

            g_PlayerVoted[iSlot] = mapName;
            g_Votes[mapName]++;

            // ищем displayName для сообщения
            std::string disp = GetDisplayName(mapName);
            g_pUtils->PrintToChat(iSlot,
                " \x04[MapVote]\x01 Вы проголосовали за \x06%s\x01!", disp.c_str());

            META_CONPRINTF("[MapVote] слот %d → %s (итого %d)\n",
                iSlot, mapName.c_str(), g_Votes[mapName]);
        });

    g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

// конец голосования
void MapVote::EndVote()
{
    g_VoteActive = false;

    std::string winner    = GetWinnerMap();
    std::string winnerDisp = GetDisplayName(winner);
    int winVotes = g_Votes.count(winner) ? g_Votes[winner] : 0;
    int total    = GetTotalVotes();

    META_CONPRINTF("[MapVote] победитель: %s (%d/%d голосов)\n",
        winner.c_str(), winVotes, total);

    // показываем результат в чате и центре экрана
    g_pUtils->PrintToChatAll(
        " \x04[MapVote]\x01 Следующая карта: \x06%s\x01 (%d/%d голосов)",
        winnerDisp.c_str(), winVotes, total);

    g_pUtils->PrintToCenterAll(
        "Следующая карта: %s\n%d из %d голосов",
        winnerDisp.c_str(), winVotes, total);

    ResetVote();

    // через 3 сек меняем карту
    g_pUtils->CreateTimer(3.0f, [winner]() -> float
    {
        std::string cmd = "changelevel " + winner;
        g_pEngineServer->ServerCommand(cmd.c_str());
        return -1.0f;
    });
}

// helpers

std::string MapVote::GetWinnerMap()
{
    if (g_MapList.empty()) return "de_dust2";

    // если никто не голосовал - cлучайная
    if (GetTotalVotes() == 0)
        return g_MapList[rand() % g_MapList.size()].mapName;

    int maxVotes = 0;
    for (const auto& [map, votes] : g_Votes)
        if (votes > maxVotes) maxVotes = votes;

    // собираем лидеров (ничья -> рандом)
    std::vector<std::string> leaders;
    for (const auto& [map, votes] : g_Votes)
        if (votes == maxVotes) leaders.push_back(map);

    return leaders[rand() % leaders.size()];
}

std::string MapVote::GetDisplayName(const std::string& mapName)
{
    for (const auto& e : g_MapList)
        if (e.mapName == mapName) return e.displayName;
    return mapName;
}

int MapVote::GetTotalVotes()
{
    int total = 0;
    for (const auto& [map, votes] : g_Votes)
        total += votes;
    return total;
}

void MapVote::ResetVote()
{
    g_PlayerVoted.clear();
    g_VoteStarted = false;
    g_VoteActive  = false;
    for (auto& [map, votes] : g_Votes)
        votes = 0;
}
