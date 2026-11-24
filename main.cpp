#include "raylib.h"
#include "raylib.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

#if !defined(PLATFORM_WEB)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

using json = nlohmann::json;

enum GameScreen {
    SCREEN_MENU = 0,
    SCREEN_PLAYING = 1
};

enum MatchResult {
    MATCH_ACTIVE = 0,
    MATCH_VICTORY = 1,
    MATCH_DEFEAT = 2
};

struct UnitTemplate {
    std::string id;
    std::string name;
    int cost;
    float hp;
    float damage;
    float speed;
    float range;
    float attackCooldown;
    float scale;
    int spriteRow;
    Color tint;
    Color enemyTint;
    std::string allyTrigger;
    std::string enemyTrigger;
};

struct Unit {
    std::string templateId;
    std::string name;
    std::string ownerName;
    bool isPlayer;
    float x, y;
    float hp, maxHp;
    float damage;
    float speed;
    float range;
    float attackCooldown;
    float attackTimer;
    float scale;
    int spriteRow;
    Color tint;

    int animFrame = 0;
    float animTimer = 0.0f;
    bool isAttacking = false;
};

struct FloatingAlert {
    std::string text;
    Color color;
    float timer;
};

const int SCREEN_W = 1100;
const int SCREEN_H = 600;
const float GROUND_Y = 470.0f;
const std::string CONFIG_PATH = "mods/config.json";

GameScreen currentScreen = SCREEN_MENU;
MatchResult matchResult = MATCH_ACTIVE;

std::vector<UnitTemplate> unitCatalog;
std::vector<Unit> units;
std::vector<FloatingAlert> alerts;

// Textures
Texture2D unitSheet;
Texture2D statueTex;
Texture2D bgTex;

const int SHEET_COLS = 4;
const int SHEET_ROWS = 3;
int FRAME_W = 64;
int FRAME_H = 64;

// Match state
float playerGold = 500.0f;
float playerStatueHp = 1000.0f, playerStatueMaxHp = 1000.0f;
float enemyStatueHp = 1000.0f, enemyStatueMaxHp = 1000.0f;
float enemySpawnTimer = 0.0f;
bool rageActive = false;
float rageTimer = 0.0f;

// Customizable Win Ratio State (e.g., Win 2/10)
int targetWins = 2;
int matchPool = 10;
int totalWins = 0;
int totalMatches = 0;

// Main Menu & Live Stream Connector State
char usernameInput[64] = "";
int usernameLen = 0;
bool inputBoxActive = false;
bool streamConnected = false;
std::string connectedUser = "";
std::string menuStatusMsg = "Enter streamer @username and press CONNECT";

bool liveMenuOpen = true;
int udpSock = -1;
int liveEventsReceived = 0;

#if defined(PLATFORM_WEB)
EM_JS(void, js_open_keyboard, (const char* initialVal), {
    var inp = document.getElementById("mobile_username_input");
    if (!inp) {
        inp = document.createElement("input");
        inp.id = "mobile_username_input";
        inp.type = "text";
        inp.autocapitalize = "off";
        inp.autocomplete = "off";
        inp.autocorrect = "off";
        inp.spellcheck = false;
        inp.style.position = "fixed";
        inp.style.left = "50%";
        inp.style.top = "10px";
        inp.style.transform = "translateX(-50%)";
        inp.style.width = "260px";
        inp.style.height = "34px";
        inp.style.fontSize = "16px";
        inp.style.zIndex = "9999";
        inp.style.background = "#1e2330";
        inp.style.color = "#ffffff";
        inp.style.border = "2px solid gold";
        inp.style.borderRadius = "6px";
        inp.style.padding = "4px 8px";
        document.body.appendChild(inp);

        inp.addEventListener("input", function() {
            var val = inp.value.slice(0, 40);
            if (inp.value !== val) inp.value = val;
            Module.ccall("web_set_username", null, ["string"], [val]);
        });

        inp.addEventListener("keydown", function(e) {
            if (e.key === "Enter") {
                inp.blur();
                Module.ccall("web_close_keyboard", null, [], []);
            }
        });
    }
    inp.value = UTF8ToString(initialVal || "");
    inp.style.display = "block";
    setTimeout(function() { inp.focus(); }, 20);
});

EM_JS(void, js_close_keyboard, (), {
    var inp = document.getElementById("mobile_username_input");
    if (inp) {
        inp.blur();
        inp.style.display = "none";
    }
});

EM_JS(void, js_init_pointer_tracking, (), {
    if (Module["_sw_pointer_bound"]) return;
    Module["_sw_pointer_bound"] = true;
    Module["_sw_ptr_x"] = 0.0;
    Module["_sw_ptr_y"] = 0.0;
    Module["_sw_ptr_down"] = false;
    Module["_sw_ptr_pressed"] = false;

    function updatePos(clientX, clientY) {
        var canvas = document.getElementById("canvas") || document.querySelector("canvas");
        if (!canvas) return;
        var rect = canvas.getBoundingClientRect();
        if (rect.width <= 0 || rect.height <= 0) return;
        var gameW = Module["_sw_gameW"] || 1100;
        var gameH = Module["_sw_gameH"] || 600;
        var lx = ((clientX - rect.left) / rect.width) * gameW;
        var ly = ((clientY - rect.top) / rect.height) * gameH;
        Module["_sw_ptr_x"] = lx;
        Module["_sw_ptr_y"] = ly;
    }

    window.addEventListener("pointermove", function(e) {
        updatePos(e.clientX, e.clientY);
    }, { passive: true });

    window.addEventListener("pointerdown", function(e) {
        updatePos(e.clientX, e.clientY);
        if (!Module["_sw_ptr_down"]) {
            Module["_sw_ptr_pressed"] = true;
        }
        Module["_sw_ptr_down"] = true;
    }, { passive: true });

    window.addEventListener("pointerup", function(e) {
        updatePos(e.clientX, e.clientY);
        Module["_sw_ptr_down"] = false;
    }, { passive: true });

    window.addEventListener("pointercancel", function() {
        Module["_sw_ptr_down"] = false;
    }, { passive: true });
});

EM_JS(float, js_get_pointer_x, (), {
    return Module["_sw_ptr_x"] || 0.0;
});

EM_JS(float, js_get_pointer_y, (), {
    return Module["_sw_ptr_y"] || 0.0;
});

EM_JS(int, js_consume_pointer_pressed, (), {
    var p = Module["_sw_ptr_pressed"] ? 1 : 0;
    Module["_sw_ptr_pressed"] = false;
    return p;
});

EM_JS(void, js_toggle_fullscreen, (), {
    var canvas = document.getElementById("canvas") || document.querySelector("canvas");
    var target = (canvas && canvas.parentElement) ? canvas.parentElement : canvas;
    if (!target) return;

    var fsEl = document.fullscreenElement || document.webkitFullscreenElement;
    if (!fsEl) {
        var req = target.requestFullscreen || target.webkitRequestFullscreen || target.mozRequestFullScreen || target.msRequestFullscreen;
        if (req) {
            try {
                var p = req.call(target);
                if (p && p.catch) p.catch(function() {});
            } catch (e) {}
        }
    } else {
        var exit = document.exitFullscreen || document.webkitExitFullscreen || document.mozCancelFullScreen || document.msExitFullscreen;
        if (exit) {
            try {
                var p2 = exit.call(document);
                if (p2 && p2.catch) p2.catch(function() {});
            } catch (e) {}
        }
    }
});

EM_JS(void, js_fit_canvas_to_viewport, (int gameW, int gameH), {
    Module["_sw_gameW"] = gameW;
    Module["_sw_gameH"] = gameH;

    var canvas = document.getElementById("canvas") || document.querySelector("canvas");
    if (!canvas) return;

    var vw = window.innerWidth || document.documentElement.clientWidth || 1100;
    var vh = window.innerHeight || document.documentElement.clientHeight || 600;
    if (window.visualViewport) {
        vw = window.visualViewport.width || vw;
        vh = window.visualViewport.height || vh;
    }

    var scale = Math.min(vw / gameW, vh / gameH);
    var dispW = Math.max(1, Math.floor(gameW * scale));
    var dispH = Math.max(1, Math.floor(gameH * scale));
    var offX = Math.floor((vw - dispW) * 0.5);
    var offY = Math.floor((vh - dispH) * 0.5);

    canvas.style.position = "fixed";
    canvas.style.left = offX + "px";
    canvas.style.top = offY + "px";
    canvas.style.width = dispW + "px";
    canvas.style.height = dispH + "px";
    canvas.style.maxWidth = "none";
    canvas.style.maxHeight = "none";
    canvas.style.margin = "0";
    canvas.style.display = "block";
    canvas.style.touchAction = "none";
    document.documentElement.style.overflow = "hidden";
    document.body.style.overflow = "hidden";
    document.body.style.margin = "0";
    document.body.style.background = "#0d1017";
});

extern "C" {
EMSCRIPTEN_KEEPALIVE
void web_set_username(const char* str) {
    if (!str) return;
    size_t len = std::min(strlen(str), sizeof(usernameInput) - 1);
    memcpy(usernameInput, str, len);
    usernameInput[len] = '\0';
    usernameLen = (int)len;
}

EMSCRIPTEN_KEEPALIVE
void web_close_keyboard() {
    inputBoxActive = false;
}
}
#endif

void SetInputBoxActive(bool active) {
    if (inputBoxActive == active) return;
    inputBoxActive = active;
#if defined(PLATFORM_WEB)
    if (active) {
        js_open_keyboard(usernameInput);
    } else {
        js_close_keyboard();
    }
#endif
}

void ToggleFullscreenMode() {
#if defined(PLATFORM_WEB)
    js_toggle_fullscreen();
    js_fit_canvas_to_viewport(SCREEN_W, SCREEN_H);
#else
    ToggleFullscreen();
#endif
}

Vector2 GetLogicalMousePosition() {
#if defined(PLATFORM_WEB)
    return Vector2{ js_get_pointer_x(), js_get_pointer_y() };
#else
    return GetMousePosition();
#endif
}

bool WasLogicalClickPressed() {
#if defined(PLATFORM_WEB)
    bool jsPressed = (js_consume_pointer_pressed() != 0);
    bool rlPressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    return jsPressed || rlPressed;
#else
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
#endif
}

void SaveConfig() {
    json cfg;
    cfg["targetWins"] = targetWins;
    cfg["matchPool"] = matchPool;
    cfg["totalWins"] = totalWins;
    cfg["totalMatches"] = totalMatches;
    cfg["lastUsername"] = std::string(usernameInput);

    std::ofstream out(CONFIG_PATH);
    if (out.is_open()) {
        out << cfg.dump(2) << std::endl;
    }
}

void LoadConfig() {
    std::ifstream in(CONFIG_PATH);
    if (!in.is_open()) {
        SaveConfig();
        return;
    }
    try {
        json cfg = json::parse(in);
        targetWins = std::max(1, cfg.value("targetWins", 2));
        matchPool = std::max(targetWins, cfg.value("matchPool", 10));
        totalWins = std::max(0, cfg.value("totalWins", 0));
        totalMatches = std::max(0, cfg.value("totalMatches", 0));

        std::string savedUser = cfg.value("lastUsername", "");
        if (!savedUser.empty()) {
            size_t len = std::min(savedUser.size(), sizeof(usernameInput) - 1);
            memcpy(usernameInput, savedUser.c_str(), len);
            usernameInput[len] = '\0';
            usernameLen = (int)len;
        }
    } catch (...) {}
}

Color ParseColor(const json& arr, Color fallback) {
    if (arr.is_array() && arr.size() >= 4) {
        return Color{
            (unsigned char)arr[0].get<int>(),
            (unsigned char)arr[1].get<int>(),
            (unsigned char)arr[2].get<int>(),
            (unsigned char)arr[3].get<int>()
        };
    }
    return fallback;
}

void LoadUnitCatalog(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    json data = json::parse(f);
    unitCatalog.clear();
    for (const auto& u : data["units"]) {
        UnitTemplate t;
        t.id = u.value("id", "unit");
        t.name = u.value("name", "Unit");
        t.cost = u.value("cost", 100);
        t.hp = u.value("hp", 100.0f);
        t.damage = u.value("damage", 15.0f);
        t.speed = u.value("speed", 80.0f);
        t.range = u.value("range", 40.0f);
        t.attackCooldown = u.value("attackCooldown", 1.0f);
        t.scale = u.value("scale", 1.0f);
        t.spriteRow = u.value("spriteRow", 0);
        t.tint = ParseColor(u["tint"], BLUE);
        t.enemyTint = ParseColor(u["enemyTint"], RED);
        t.allyTrigger = u.value("allyTrigger", "Rose");
        t.enemyTrigger = u.value("enemyTrigger", "GG");
        unitCatalog.push_back(t);
    }
}

void EnsureStarterAssets() {
    if (!FileExists("assets/units.png")) {
        Image img = GenImageColor(64 * SHEET_COLS, 64 * SHEET_ROWS, BLANK);
        for (int row = 0; row < SHEET_ROWS; row++) {
            for (int col = 0; col < SHEET_COLS; col++) {
                int ox = col * 64;
                int oy = row * 64;
                int legOffset = (col % 2 == 0) ? -4 : 4;
                int weaponSwing = (col == 2 || col == 3) ? 10 : 0;

                ImageDrawCircle(&img, ox + 32, oy + 16, 7, WHITE);
                ImageDrawRectangle(&img, ox + 29, oy + 23, 6, 18, WHITE);
                ImageDrawLine(&img, ox + 31, oy + 41, ox + 26 + legOffset, oy + 58, WHITE);
                ImageDrawLine(&img, ox + 33, oy + 41, ox + 38 - legOffset, oy + 58, WHITE);
                if (row == 0) {
                    ImageDrawLine(&img, ox + 32, oy + 27, ox + 45, oy + 30 - weaponSwing, WHITE);
                    ImageDrawRectangle(&img, ox + 45, oy + 16 - weaponSwing, 4, 18, LIGHTGRAY);
                } else if (row == 1) {
                    ImageDrawCircleLines(&img, ox + 42, oy + 30, 10, WHITE);
                    ImageDrawLine(&img, ox + 35, oy + 30, ox + 52 + weaponSwing / 2, oy + 30, YELLOW);
                } else {
                    ImageDrawRectangle(&img, ox + 24, oy + 20, 16, 22, WHITE);
                    ImageDrawRectangle(&img, ox + 42, oy + 14 + weaponSwing, 8, 26, LIGHTGRAY);
                }
            }
        }
        ExportImage(img, "assets/units.png");
        UnloadImage(img);
    }
    unitSheet = LoadTexture("assets/units.png");
    FRAME_W = unitSheet.width / SHEET_COLS;
    FRAME_H = unitSheet.height / SHEET_ROWS;

    if (!FileExists("assets/statue.png")) {
        Image sImg = GenImageColor(64, 140, BLANK);
        ImageDrawRectangle(&sImg, 6, 112, 52, 28, GRAY);
        ImageDrawRectangle(&sImg, 12, 98, 40, 14, LIGHTGRAY);
        ImageDrawRectangle(&sImg, 20, 38, 24, 60, RAYWHITE);
        ImageDrawCircle(&sImg, 32, 26, 11, RAYWHITE);
        ImageDrawRectangle(&sImg, 44, 22, 5, 55, GOLD);
        ImageDrawRectangle(&sImg, 38, 34, 17, 5, GOLD);
        ExportImage(sImg, "assets/statue.png");
        UnloadImage(sImg);
    }
    statueTex = LoadTexture("assets/statue.png");

    if (!FileExists("assets/bg.png")) {
        Image bImg = GenImageGradientLinear(SCREEN_W, SCREEN_H, 0, Color{24, 32, 48, 255}, Color{52, 68, 92, 255});
        ImageDrawCircle(&bImg, 260, 480, 190, Color{36, 48, 56, 255});
        ImageDrawCircle(&bImg, 780, 490, 220, Color{32, 43, 50, 255});
        ImageDrawRectangle(&bImg, 0, (int)GROUND_Y, SCREEN_W, SCREEN_H - (int)GROUND_Y, Color{56, 74, 48, 255});
        ImageDrawRectangle(&bImg, 0, (int)GROUND_Y, SCREEN_W, 6, Color{84, 115, 68, 255});
        ExportImage(bImg, "assets/bg.png");
        UnloadImage(bImg);
    }
    bgTex = LoadTexture("assets/bg.png");
}

void ResetMatch() {
    units.clear();
    alerts.clear();
    matchResult = MATCH_ACTIVE;
    playerGold = 500.0f;
    playerStatueHp = playerStatueMaxHp;
    enemyStatueHp = enemyStatueMaxHp;
    enemySpawnTimer = 0.0f;
    rageActive = false;
    rageTimer = 0.0f;
}

void PushAlert(const std::string& msg, Color col) {
    alerts.push_back({msg, col, 3.5f});
    if (alerts.size() > 5) alerts.erase(alerts.begin());
}

void SpawnUnit(const std::string& id, bool isPlayer, const std::string& ownerName = "") {
    if (matchResult != MATCH_ACTIVE) return;
    for (const auto& t : unitCatalog) {
        if (t.id == id) {
            Unit u;
            u.templateId = t.id;
            u.name = t.name;
            u.ownerName = ownerName;
            u.isPlayer = isPlayer;
            u.x = isPlayer ? 130.0f : (SCREEN_W - 130.0f);
            u.y = GROUND_Y;
            u.hp = t.hp;
            u.maxHp = t.hp;
            u.damage = t.damage;
            u.speed = t.speed;
            u.range = t.range;
            u.attackCooldown = t.attackCooldown;
            u.attackTimer = 0.0f;
            u.scale = t.scale;
            u.spriteRow = std::clamp(t.spriteRow, 0, SHEET_ROWS - 1);
            u.tint = isPlayer ? t.tint : t.enemyTint;
            units.push_back(u);

            if (!ownerName.empty()) {
                PushAlert(ownerName + " spawned " + (isPlayer ? "ALLY " : "ENEMY ") + t.name + "!",
                          isPlayer ? SKYBLUE : ORANGE);
            }
            return;
        }
    }
}

void InitLiveSocket(int port) {
#if !defined(PLATFORM_WEB)
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock >= 0) {
        int flags = fcntl(udpSock, F_GETFL, 0);
        fcntl(udpSock, F_SETFL, flags | O_NONBLOCK);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (bind(udpSock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(udpSock);
            udpSock = -1;
        }
    }
#else
    (void)port;
#endif
}

void SendBridgeCommand(const std::string& action, const std::string& username) {
#if !defined(PLATFORM_WEB)
    int txSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (txSock < 0) return;

    sockaddr_in bridgeAddr{};
    bridgeAddr.sin_family = AF_INET;
    bridgeAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bridgeAddr.sin_port = htons(9091);

    json cmd = {
        {"action", action},
        {"username", username}
    };
    std::string payload = cmd.dump();
    sendto(txSock, payload.c_str(), payload.size(), 0, (sockaddr*)&bridgeAddr, sizeof(bridgeAddr));
    close(txSock);
#else
    (void)action;
    (void)username;
#endif
}

void PollLiveEvents() {
#if !defined(PLATFORM_WEB)
    if (udpSock < 0) return;
    char buf[1024];
    while (true) {
        ssize_t n = recvfrom(udpSock, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
        if (n <= 0) break;
        buf[n] = '\0';
        try {
            json msg = json::parse(buf);
            std::string type = msg.value("type", "spawn");
            if (type == "status") {
                streamConnected = msg.value("connected", false);
                menuStatusMsg = msg.value("message", "Bridge updated");
                continue;
            }
            if (currentScreen == SCREEN_PLAYING && matchResult == MATCH_ACTIVE) {
                std::string team = msg.value("team", "enemy");
                std::string unitId = msg.value("unit", "swordwrath");
                std::string user = msg.value("user", "Viewer");
                bool isPlayer = (team == "ally" || team == "player");
                SpawnUnit(unitId, isPlayer, user);
                liveEventsReceived++;
            }
        } catch (...) {}
    }
#endif
}

void DrawFullscreenButton() {
    Rectangle fsBtn = { SCREEN_W - 72.0f, SCREEN_H - 42.0f, 56.0f, 30.0f };
    Vector2 mouse = GetLogicalMousePosition();
    bool clicked = WasLogicalClickPressed();

    DrawRectangleRec(fsBtn, Fade(BLACK, 0.75f));
    DrawRectangleLinesEx(fsBtn, 2, GOLD);
    DrawText("FS", (int)fsBtn.x + 17, (int)fsBtn.y + 7, 16, GOLD);

    if (clicked && CheckCollisionPointRec(mouse, fsBtn)) {
        ToggleFullscreenMode();
    }
}

void UpdateDrawMenu() {
    Vector2 mouse = GetLogicalMousePosition();
    bool clicked = WasLogicalClickPressed();

    float centerX = SCREEN_W * 0.5f;
    Rectangle inputBox = { centerX - 210.0f, 215.0f, 355.0f, 44.0f };
    Rectangle delBtn   = { centerX + 153.0f, 215.0f, 57.0f,  44.0f };

    Rectangle winMinusBtn  = { centerX - 195.0f, 282.0f, 36.0f, 36.0f };
    Rectangle winPlusBtn   = { centerX - 95.0f,  282.0f, 36.0f, 36.0f };
    Rectangle poolMinusBtn = { centerX + 45.0f,  282.0f, 36.0f, 36.0f };
    Rectangle poolPlusBtn  = { centerX + 145.0f, 282.0f, 36.0f, 36.0f };
    Rectangle resetRecBtn  = { centerX + 115.0f, 326.0f, 95.0f, 26.0f };

    Rectangle connectPlayBtn = { centerX - 210.0f, 362.0f, 200.0f, 48.0f };
    Rectangle quickPlayBtn   = { centerX + 10.0f,  362.0f, 200.0f, 48.0f };

    if (clicked) {
        if (CheckCollisionPointRec(mouse, delBtn)) {
            if (usernameLen > 0) {
                usernameLen--;
                usernameInput[usernameLen] = '\0';
            }
            SetInputBoxActive(true);
        } else if (CheckCollisionPointRec(mouse, inputBox)) {
            SetInputBoxActive(true);
        } else {
            SetInputBoxActive(false);
        }

        if (CheckCollisionPointRec(mouse, winMinusBtn) && targetWins > 1) {
            targetWins--;
            SaveConfig();
        }
        if (CheckCollisionPointRec(mouse, winPlusBtn)) {
            targetWins++;
            if (matchPool < targetWins) matchPool = targetWins;
            SaveConfig();
        }
        if (CheckCollisionPointRec(mouse, poolMinusBtn) && matchPool > 1) {
            matchPool--;
            if (targetWins > matchPool) targetWins = matchPool;
            SaveConfig();
        }
        if (CheckCollisionPointRec(mouse, poolPlusBtn)) {
            matchPool++;
            SaveConfig();
        }
        if (CheckCollisionPointRec(mouse, resetRecBtn)) {
            totalWins = 0;
            totalMatches = 0;
            SaveConfig();
        }
    }

    if (inputBoxActive) {
        int key = GetCharPressed();
        while (key > 0) {
            if (key == 8 || key == 127) {
                if (usernameLen > 0) {
                    usernameLen--;
                    usernameInput[usernameLen] = '\0';
                }
            } else if ((key >= 33) && (key <= 126) && (usernameLen < 40)) {
                usernameInput[usernameLen] = (char)key;
                usernameInput[usernameLen + 1] = '\0';
                usernameLen++;
            }
            key = GetCharPressed();
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && usernameLen > 0) {
            usernameLen--;
            usernameInput[usernameLen] = '\0';
        }
    }

    bool triggerConnect = (clicked && CheckCollisionPointRec(mouse, connectPlayBtn)) || IsKeyPressed(KEY_ENTER);
    if (triggerConnect) {
        SetInputBoxActive(false);
        std::string cleanUser = usernameInput;
        if (!cleanUser.empty() && cleanUser[0] == '@') cleanUser.erase(0, 1);

        SaveConfig();
        if (cleanUser.empty()) {
            menuStatusMsg = "Please type a streamer username first!";
        } else {
            connectedUser = cleanUser;
            streamConnected = true;
            menuStatusMsg = "Connecting @" + connectedUser + " via bridge...";
            SendBridgeCommand("connect", connectedUser);
            ResetMatch();
            currentScreen = SCREEN_PLAYING;
        }
    }

    if (clicked && CheckCollisionPointRec(mouse, quickPlayBtn)) {
        SetInputBoxActive(false);
        SaveConfig();
        connectedUser = "Offline / Sim";
        streamConnected = false;
        ResetMatch();
        currentScreen = SCREEN_PLAYING;
    }

    BeginDrawing();
    DrawTexturePro(bgTex, {0, 0, (float)bgTex.width, (float)bgTex.height},
                   {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0.0f, Color{150, 150, 170, 255});
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f));

    const char* title = "STICK WAR: LIVE INTERACTIVE";
    int titleW = MeasureText(title, 42);
    DrawText(title, (SCREEN_W - titleW) / 2, 70, 42, GOLD);

    const char* subtitle = "Viewers control the battlefield via Live Chat & Gifts";
    int subW = MeasureText(subtitle, 19);
    DrawText(subtitle, (SCREEN_W - subW) / 2, 120, 19, LIGHTGRAY);

    DrawRectangleRounded({ centerX - 245.0f, 155.0f, 490.0f, 290.0f }, 0.08f, 8, Fade(BLACK, 0.82f));
    DrawRectangleRoundedLinesEx({ centerX - 245.0f, 155.0f, 490.0f, 290.0f }, 0.08f, 8, 2, PURPLE);

    DrawText("STREAMER USERNAME (@handle):", (int)(centerX - 210), 185, 17, SKYBLUE);

    DrawRectangleRec(inputBox, Color{30, 35, 48, 255});
    DrawRectangleLinesEx(inputBox, 2, inputBoxActive ? GOLD : GRAY);
    if (usernameLen == 0) {
        DrawText("e.g. aiman_live", (int)inputBox.x + 14, (int)inputBox.y + 11, 20, DARKGRAY);
    } else {
        DrawText(usernameInput, (int)inputBox.x + 14, (int)inputBox.y + 11, 20, WHITE);
    }
    DrawRectangleRec(delBtn, MAROON);
    DrawRectangleLinesEx(delBtn, 2, LIGHTGRAY);
    DrawText("DEL", (int)delBtn.x + 12, (int)delBtn.y + 13, 16, WHITE);
    if (inputBoxActive && ((int)(GetTime() * 2) % 2 == 0)) {
        int textW = MeasureText(usernameInput, 20);
        DrawRectangle((int)inputBox.x + 16 + textW, (int)inputBox.y + 10, 2, 24, GOLD);
    }

    DrawText(TextFormat("CUSTOM WIN RATIO: Win %d/%d", targetWins, matchPool),
             (int)(centerX - 210), 263, 16, GOLD);

    DrawRectangleRec(winMinusBtn, DARKGRAY);
    DrawText("-", (int)winMinusBtn.x + 13, (int)winMinusBtn.y + 8, 20, WHITE);
    DrawText(TextFormat("W:%d", targetWins), (int)(centerX - 150), 290, 18, SKYBLUE);
    DrawRectangleRec(winPlusBtn, DARKGRAY);
    DrawText("+", (int)winPlusBtn.x + 12, (int)winPlusBtn.y + 8, 20, WHITE);

    DrawRectangleRec(poolMinusBtn, DARKGRAY);
    DrawText("-", (int)poolMinusBtn.x + 13, (int)poolMinusBtn.y + 8, 20, WHITE);
    DrawText(TextFormat("M:%d", matchPool), (int)(centerX + 90), 290, 18, ORANGE);
    DrawRectangleRec(poolPlusBtn, DARKGRAY);
    DrawText("+", (int)poolPlusBtn.x + 12, (int)poolPlusBtn.y + 8, 20, WHITE);

    DrawText(TextFormat("Record: %d Wins / %d Matches", totalWins, totalMatches),
             (int)(centerX - 210), 331, 15, LIGHTGRAY);
    DrawRectangleRec(resetRecBtn, MAROON);
    DrawText("Reset", (int)resetRecBtn.x + 24, (int)resetRecBtn.y + 6, 14, WHITE);

    DrawRectangleRec(connectPlayBtn, Color{110, 40, 175, 255});
    DrawRectangleLinesEx(connectPlayBtn, 2, WHITE);
    DrawText("CONNECT & PLAY", (int)connectPlayBtn.x + 18, (int)connectPlayBtn.y + 15, 18, WHITE);

    DrawRectangleRec(quickPlayBtn, Color{40, 95, 140, 255});
    DrawRectangleLinesEx(quickPlayBtn, 2, SKYBLUE);
    DrawText("PLAY / SIM MODE", (int)quickPlayBtn.x + 22, (int)quickPlayBtn.y + 15, 18, WHITE);

    int statusW = MeasureText(menuStatusMsg.c_str(), 15);
    DrawText(menuStatusMsg.c_str(), (SCREEN_W - statusW) / 2, 420, 15, GOLD);

    DrawText("Custom Art: Edit assets/units.png, assets/statue.png & assets/bg.png | Config: mods/config.json",
             20, SCREEN_H - 28, 14, LIGHTGRAY);

    DrawFullscreenButton();
    EndDrawing();
}

void UpdateDrawGame() {
    float dt = GetFrameTime();

    if (matchResult == MATCH_ACTIVE) {
        playerGold += 18.0f * dt;
        if (rageActive) {
            rageTimer -= dt;
            if (rageTimer <= 0.0f) rageActive = false;
        }

        enemySpawnTimer += dt;
        if (enemySpawnTimer >= 6.5f) {
            enemySpawnTimer = 0.0f;
            SpawnUnit("swordwrath", false);
        }

        for (auto& u : units) {
            float dir = u.isPlayer ? 1.0f : -1.0f;
            float speedMult = (u.isPlayer && rageActive) ? 1.5f : 1.0f;
            float dmgMult = (u.isPlayer && rageActive) ? 1.6f : 1.0f;

            float targetX = u.isPlayer ? (SCREEN_W - 95.0f) : 95.0f;
            int targetIdx = -1;
            float bestDist = std::abs(targetX - u.x);

            for (int j = 0; j < (int)units.size(); j++) {
                if (units[j].isPlayer != u.isPlayer && units[j].hp > 0) {
                    float d = std::abs(units[j].x - u.x);
                    if (d < bestDist) {
                        bestDist = d;
                        targetIdx = j;
                    }
                }
            }

            u.attackTimer -= dt;
            if (bestDist <= u.range) {
                u.isAttacking = true;
                if (u.attackTimer <= 0.0f) {
                    u.attackTimer = u.attackCooldown;
                    if (targetIdx >= 0) {
                        units[targetIdx].hp -= u.damage * dmgMult;
                    } else if (u.isPlayer) {
                        enemyStatueHp = std::max(0.0f, enemyStatueHp - u.damage * dmgMult);
                    } else {
                        playerStatueHp = std::max(0.0f, playerStatueHp - u.damage * dmgMult);
                    }
                }
            } else {
                u.isAttacking = false;
                u.x += dir * u.speed * speedMult * dt;
            }

            u.animTimer += dt;
            if (u.animTimer >= 0.12f) {
                u.animTimer = 0.0f;
                if (u.isAttacking) {
                    u.animFrame = (u.animFrame == 2) ? 3 : 2;
                } else {
                    u.animFrame = (u.animFrame == 0) ? 1 : 0;
                }
            }
        }

        units.erase(std::remove_if(units.begin(), units.end(),
            [](const Unit& u) { return u.hp <= 0.0f; }), units.end());

        if (enemyStatueHp <= 0.0f) {
            matchResult = MATCH_VICTORY;
            totalWins++;
            totalMatches++;
            SaveConfig();
        } else if (playerStatueHp <= 0.0f) {
            matchResult = MATCH_DEFEAT;
            totalMatches++;
            SaveConfig();
        }
    }

    for (int i = (int)alerts.size() - 1; i >= 0; i--) {
        alerts[i].timer -= dt;
        if (alerts[i].timer <= 0.0f) alerts.erase(alerts.begin() + i);
    }

    BeginDrawing();
    DrawTexturePro(bgTex, {0, 0, (float)bgTex.width, (float)bgTex.height},
                   {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0.0f, WHITE);

    float stW = 64.0f, stH = 140.0f;
    DrawTexturePro(statueTex, {0, 0, (float)statueTex.width, (float)statueTex.height},
                   {42.0f, GROUND_Y - stH, stW, stH}, {0, 0}, 0.0f, SKYBLUE);
    DrawText(TextFormat("ALLY %d", (int)playerStatueHp), 36, (int)(GROUND_Y - stH - 24), 16, SKYBLUE);

    DrawTexturePro(statueTex, {0, 0, -(float)statueTex.width, (float)statueTex.height},
                   {SCREEN_W - 42.0f - stW, GROUND_Y - stH, stW, stH}, {0, 0}, 0.0f, RED);
    DrawText(TextFormat("ENEMY %d", (int)enemyStatueHp), SCREEN_W - 115, (int)(GROUND_Y - stH - 24), 16, RED);

    for (const auto& u : units) {
        float drawW = FRAME_W * u.scale;
        float drawH = FRAME_H * u.scale;

        Rectangle srcRec = {
            (float)(u.animFrame * FRAME_W),
            (float)(u.spriteRow * FRAME_H),
            u.isPlayer ? (float)FRAME_W : -(float)FRAME_W,
            (float)FRAME_H
        };
        Rectangle destRec = { u.x - drawW * 0.5f, u.y - drawH, drawW, drawH };
        DrawTexturePro(unitSheet, srcRec, destRec, {0, 0}, 0.0f, u.tint);

        float hpPct = std::clamp(u.hp / u.maxHp, 0.0f, 1.0f);
        DrawRectangle((int)(u.x - 20), (int)(u.y - drawH - 10), 40, 5, DARKGRAY);
        DrawRectangle((int)(u.x - 20), (int)(u.y - drawH - 10), (int)(40 * hpPct), 5, u.isPlayer ? GREEN : RED);

        if (!u.ownerName.empty()) {
            int tw = MeasureText(u.ownerName.c_str(), 14);
            DrawRectangle((int)(u.x - tw / 2 - 4), (int)(u.y - drawH - 30), tw + 8, 18, Fade(BLACK, 0.7f));
            DrawText(u.ownerName.c_str(), (int)(u.x - tw / 2), (int)(u.y - drawH - 28), 14, GOLD);
        }
    }

    DrawText(TextFormat("GOLD: %d", (int)playerGold), 20, 16, 22, GOLD);
    DrawText(TextFormat("WIN RATIO: %d/%d (Rec: %d/%d)", totalWins, matchPool, totalWins, totalMatches),
             165, 19, 18, SKYBLUE);

    Vector2 mouse = GetLogicalMousePosition();
    bool clicked = WasLogicalClickPressed();

    for (int i = 0; i < (int)unitCatalog.size(); i++) {
        Rectangle btn = { 20.0f + i * 165.0f, 52.0f, 155.0f, 42.0f };
        bool canAfford = (matchResult == MATCH_ACTIVE) && (playerGold >= unitCatalog[i].cost);
        DrawRectangleRec(btn, canAfford ? DARKBLUE : DARKGRAY);
        DrawRectangleLinesEx(btn, 2, SKYBLUE);
        DrawText(TextFormat("%s (%d)", unitCatalog[i].name.c_str(), unitCatalog[i].cost),
                 (int)btn.x + 8, (int)btn.y + 13, 15, WHITE);

        if (clicked && canAfford && CheckCollisionPointRec(mouse, btn)) {
            playerGold -= unitCatalog[i].cost;
            SpawnUnit(unitCatalog[i].id, true);
        }
    }

    Rectangle rageBtn = { 525.0f, 52.0f, 110.0f, 42.0f };
    DrawRectangleRec(rageBtn, rageActive ? ORANGE : MAROON);
    DrawRectangleLinesEx(rageBtn, 2, GOLD);
    DrawText(rageActive ? "RAGE ON!" : "RAGE", (int)rageBtn.x + 18, (int)rageBtn.y + 13, 16, WHITE);
    if (clicked && matchResult == MATCH_ACTIVE && !rageActive && CheckCollisionPointRec(mouse, rageBtn)) {
        rageActive = true;
        rageTimer = 6.0f;
    }

    Rectangle menuBtn = { SCREEN_W - 285.0f, 15.0f, 100.0f, 36.0f };
    DrawRectangleRec(menuBtn, Fade(BLACK, 0.75f));
    DrawRectangleLinesEx(menuBtn, 2, LIGHTGRAY);
    DrawText("< MENU", (int)menuBtn.x + 16, (int)menuBtn.y + 10, 16, WHITE);
    if (clicked && CheckCollisionPointRec(mouse, menuBtn)) {
        SendBridgeCommand("disconnect", connectedUser);
        currentScreen = SCREEN_MENU;
    }

    for (int i = 0; i < (int)alerts.size(); i++) {
        DrawText(alerts[i].text.c_str(), 20, 110 + i * 22, 18, alerts[i].color);
    }

    Rectangle toggleBtn = { SCREEN_W - 175.0f, 15.0f, 160.0f, 36.0f };
    DrawRectangleRec(toggleBtn, Color{120, 40, 180, 240});
    DrawRectangleLinesEx(toggleBtn, 2, WHITE);
    DrawText(liveMenuOpen ? "LIVE MENU [-]" : "LIVE MENU [+]", (int)toggleBtn.x + 18, (int)toggleBtn.y + 10, 16, WHITE);
    if (clicked && CheckCollisionPointRec(mouse, toggleBtn)) {
        liveMenuOpen = !liveMenuOpen;
    }

    if (liveMenuOpen) {
        Rectangle panel = { SCREEN_W - 310.0f, 58.0f, 295.0f, 305.0f };
        DrawRectangleRec(panel, Fade(BLACK, 0.85f));
        DrawRectangleLinesEx(panel, 2, PURPLE);

        DrawText(TextFormat("Channel: @%s", connectedUser.c_str()),
                 (int)panel.x + 12, (int)panel.y + 10, 14, SKYBLUE);
        DrawText(TextFormat("Live Spawns: %d", liveEventsReceived),
                 (int)panel.x + 12, (int)panel.y + 28, 14, LIGHTGRAY);

        for (int i = 0; i < (int)unitCatalog.size(); i++) {
            float rowY = panel.y + 58.0f + i * 76.0f;
            DrawText(TextFormat("%s (Ally:%s | Enemy:%s)",
                     unitCatalog[i].name.c_str(),
                     unitCatalog[i].allyTrigger.c_str(),
                     unitCatalog[i].enemyTrigger.c_str()),
                     (int)panel.x + 12, (int)rowY, 13, GOLD);

            Rectangle allyBtn = { panel.x + 12.0f, rowY + 20.0f, 130.0f, 34.0f };
            DrawRectangleRec(allyBtn, Color{30, 110, 180, 255});
            DrawText("+ ALLY", (int)allyBtn.x + 35, (int)allyBtn.y + 9, 15, WHITE);
            if (clicked && CheckCollisionPointRec(mouse, allyBtn)) {
                SpawnUnit(unitCatalog[i].id, true, "Viewer_Ally");
            }

            Rectangle enemyBtn = { panel.x + 152.0f, rowY + 20.0f, 130.0f, 34.0f };
            DrawRectangleRec(enemyBtn, Color{185, 45, 45, 255});
            DrawText("+ ENEMY", (int)enemyBtn.x + 28, (int)enemyBtn.y + 9, 15, WHITE);
            if (clicked && CheckCollisionPointRec(mouse, enemyBtn)) {
                SpawnUnit(unitCatalog[i].id, false, "Viewer_Enemy");
            }
        }
    }

    if (matchResult != MATCH_ACTIVE) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.65f));
        float boxW = 440.0f, boxH = 220.0f;
        float bx = (SCREEN_W - boxW) * 0.5f;
        float by = (SCREEN_H - boxH) * 0.5f;

        DrawRectangleRounded({bx, by, boxW, boxH}, 0.08f, 8, Color{25, 30, 42, 250});
        DrawRectangleRoundedLinesEx({bx, by, boxW, boxH}, 0.08f, 8, 3,
                                      matchResult == MATCH_VICTORY ? GOLD : RED);

        const char* banner = (matchResult == MATCH_VICTORY) ? "VICTORY!" : "DEFEAT!";
        Color bannerCol = (matchResult == MATCH_VICTORY) ? GOLD : RED;
        int bw = MeasureText(banner, 36);
        DrawText(banner, (int)(bx + (boxW - bw) * 0.5f), (int)(by + 24), 36, bannerCol);

        bool metTarget = (totalWins >= targetWins);
        DrawText(TextFormat("Win Ratio: %d/%d  |  Target Goal: Win %d/%d",
                 totalWins, totalMatches, targetWins, matchPool),
                 (int)(bx + 42), (int)(by + 78), 18, SKYBLUE);
        DrawText(metTarget ? "Target Win Quota Reached!" : "Keep battling toward your Win Ratio!",
                 (int)(bx + 72), (int)(by + 106), 16, metTarget ? GREEN : LIGHTGRAY);

        Rectangle nextBtn = { bx + 45.0f, by + 145.0f, 160.0f, 44.0f };
        Rectangle backBtn = { bx + 235.0f, by + 145.0f, 160.0f, 44.0f };

        DrawRectangleRec(nextBtn, Color{40, 130, 80, 255});
        DrawText("NEXT MATCH", (int)nextBtn.x + 25, (int)nextBtn.y + 13, 17, WHITE);
        if (clicked && CheckCollisionPointRec(mouse, nextBtn)) {
            ResetMatch();
        }

        DrawRectangleRec(backBtn, Color{90, 50, 140, 255});
        DrawText("MAIN MENU", (int)backBtn.x + 28, (int)backBtn.y + 13, 17, WHITE);
        if (clicked && CheckCollisionPointRec(mouse, backBtn)) {
            SendBridgeCommand("disconnect", connectedUser);
            currentScreen = SCREEN_MENU;
        }
    }

    DrawFullscreenButton();
    EndDrawing();
}

void UpdateDrawFrame() {
#if defined(PLATFORM_WEB)
    js_init_pointer_tracking();
    js_fit_canvas_to_viewport(SCREEN_W, SCREEN_H);
#endif
    PollLiveEvents();
    if (currentScreen == SCREEN_MENU) {
        UpdateDrawMenu();
    } else {
        UpdateDrawGame();
    }
}

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "StickWar Live Interactive");
    LoadConfig();
    LoadUnitCatalog("mods/units.json");
    EnsureStarterAssets();
    InitLiveSocket(9090);

#if defined(PLATFORM_WEB)
    js_init_pointer_tracking();
    js_fit_canvas_to_viewport(SCREEN_W, SCREEN_H);
    emscripten_set_main_loop(UpdateDrawFrame, 60, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
#endif

#if !defined(PLATFORM_WEB)
    if (udpSock >= 0) close(udpSock);
#endif
    UnloadTexture(unitSheet);
    UnloadTexture(statueTex);
    UnloadTexture(bgTex);
    CloseWindow();
    return 0;
}
