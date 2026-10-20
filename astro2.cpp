//// =====================================================================================
//   A S T R O N A U T I C A   3 D   —   "Subnautica, только в космосе"
// =====================================================================================
//   Полноценная 3D-версия. Один файл, только raylib, никаких внешних ассетов:
//   меши астероидов, существ и капсулы генерируются процедурно прямо в коде,
//   звуки синтезируются в рантайме.
//
//   ПОЧЕМУ ТАК СДЕЛАНО (важные решения, если будешь править):
//
//   * Освещение запечено в цвета вершин. Собственного шейдера нет вообще —
// 
// 
// 
// 
// 
// 
// 
// 
// 
//     стандартный шейдер raylib умножает colDiffuse на цвет вершины, поэтому
//     диффуз считается один раз при генерации меша. Никакого GLSL, никаких
//     проблем с версиями драйверов, и при этом объёмная картинка.
//   * Мир — толстый диск, а не шар. В сфере радиусом 7 км игрок теряется
//     мгновенно; диск высотой +-1100 оставляет полноценные три измерения,
//     но навигация остаётся человеческой.
//   * Туман сделан затемнением тинта к цвету фона по расстоянию. Объекты
//     растворяются до того, как упрутся в дистанцию отрисовки, так что
//     "выскакивания" геометрии не видно.
//   * Частицы, блики и маркеры рисуются в 2D поверх сцены: 3D-позиция
//     проецируется через GetWorldToScreen. Дёшево и без сортировки прозрачности.
//   * ВЕСЬ ВВОД ОБРАБАТЫВАЕТСЯ В ФАЗЕ ОБНОВЛЕНИЯ. Ни одна функция Draw* не
//     трогает клавиатуру. В 2D-версии именно из-за этого экран капсулы
//     открывался и закрывался за один кадр. Кнопки мыши дополнительно
//     защищены счётчиком g_stateAge.
//   * Три вида оружия (WPN_LASER/WPN_BLASTER/WPN_TORPEDO, клавиши 1/2/3).
//     Лазер — инструмент добычи: рубит руду, по живому бьёт вполсилы
//     (StatLaserCreatureDps). Бластер и торпеда рубят руду плохо или вообще
//     никак — это специализированное оружие против существ. Урон стекается
//     в три общие функции — DamageAsteroid/DamageCreature/ExplodeTorpedo —
//     чтобы дроп ресурсов и гибель существа не дублировались между лучевым
//     попаданием лазера и коллизией снаряда в UpdateProjectiles.
//   * Меши получают лёгкий запечённый specular (Блинн-Фонг с фиксированным
//     "взглядом" — честный view-dependent блик запечь нельзя, камера свободная)
//     поверх диффуза и контрового ободка — за счёт этого шары и капсула не
//     выглядят плоским пластиком, хотя шейдера всё ещё нет вообще.
//   * Swarmer — единственное существо с настоящим флокингом (разделение +
//     выравнивание + сплочение считаются в UpdateCreatures только между
//     сородичами-Swarmer'ами в радиусе видимости, дёшево). У Sталкера в
//     режиме блуждания корпус почти прозрачен (маскировка), глаза специально
//     оставлены на полной яркости — единственная подсказка, что он рядом.
//     У Левиафана перед укусом полсекунды разгорается пасть/глаза (bitePrep) —
//     читаемый замах вместо мгновенного "укуса из ниоткуда".
//
//   СБОРКА:
//     Linux:  g++ astronautica3d.cpp -o astronautica3d -std=c++17 -O2
//                 -lraylib -lm -lpthread -ldl -lrt -lX11
//     macOS:  clang++ astronautica3d.cpp -o astronautica3d -std=c++17 -O2
//                 -lraylib -framework Cocoa -framework IOKit -framework OpenGL
//     MinGW:  g++ astronautica3d.cpp -o astronautica3d.exe -std=c++17 -O2
//                 -lraylib -lopengl32 -lgdi32 -lwinmm
//     MSVC:   добавь /utf-8 (в комментариях кириллица) и /std:c++17
//
//   Рассчитано на raylib 4.5 / 5.x.
//
//   ЯЗЫК ИНТЕРФЕЙСА: встроенный шрифт raylib не содержит кириллицы, поэтому
//   текст в игре латиницей. Весь вывод идёт через одну функцию Txt() —
//   подмени в ней DrawText на DrawTextEx со своим TTF, и интерфейс станет русским.
// =====================================================================================

#include "raylib.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// =====================================================================================
//  РАЗДЕЛ 1. КОНСТАНТЫ
// =====================================================================================

#define SCREEN_W            1280
#define SCREEN_H            720

#define WORLD_RADIUS        7000.0f     // радиус диска
#define DISK_HALF           1100.0f     // половина толщины диска
#define DRAW_DIST           2800.0f     // дальше этого не рисуем
#define FADE_START          1100.0f     // отсюда начинается растворение в фон

#define MAX_ASTEROIDS       3000
#define MAX_PICKUPS         600
#define MAX_CREATURES       300
#define MAX_PARTICLES       2400
#define MAX_PROJECTILES     200
#define MAX_SITES           30
#define MAX_BUOYS           8
#define MAX_STARS           760
#define MAX_NEBULA          80
#define MAX_TOASTS          6
#define ROCK_MESHES         7

#define POD_RADIUS          110.0f
#define DOCK_RADIUS         320.0f
#define PICKUP_MAGNET       300.0f
#define PLAYER_RADIUS       20.0f

// =====================================================================================
//  РАЗДЕЛ 2. МАТЕМАТИКА
//  raymath намеренно не подключаем: всё своё, без риска конфликта имён.
// =====================================================================================

static inline Vector2 V2(float x, float y) { Vector2 v; v.x = x; v.y = y; return v; }
static inline Vector3 V3(float x, float y, float z) { Vector3 v; v.x = x; v.y = y; v.z = z; return v; }

static inline Vector3 VAdd(Vector3 a, Vector3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vector3 VSub(Vector3 a, Vector3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vector3 VMul(Vector3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
static inline float   VDot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float   VLenSq(Vector3 a) { return VDot(a, a); }
static inline float   VLen(Vector3 a) { return sqrtf(VDot(a, a)); }
static inline float   VDistSq(Vector3 a, Vector3 b) { return VLenSq(VSub(a, b)); }
static inline float   VDist(Vector3 a, Vector3 b) { return VLen(VSub(a, b)); }

static inline Vector3 VCross(Vector3 a, Vector3 b)
{
    return V3(a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

static inline Vector3 VNorm(Vector3 a)
{
    float l = VLen(a);
    if (l < 0.000001f) return V3(0.0f, 0.0f, 0.0f);
    return V3(a.x / l, a.y / l, a.z / l);
}

static inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }
static inline float MinF(float a, float b) { return a < b ? a : b; }
static inline float MaxF(float a, float b) { return a > b ? a : b; }

static inline float Approach(float cur, float target, float rate, float dt)
{
    if (cur < target) return MinF(target, cur + rate * dt);
    return MaxF(target, cur - rate * dt);
}

// Ортонормированный базис по направлению вперёд. Мировой "верх" — +Y,
// кроме вырожденного случая, когда смотрим почти вертикально.
static void BasisFromForward(Vector3 fwd, Vector3* right, Vector3* up)
{
    Vector3 wu = V3(0.0f, 1.0f, 0.0f);
    if (fabsf(VDot(fwd, wu)) > 0.985f) wu = V3(1.0f, 0.0f, 0.0f);
    *right = VNorm(VCross(wu, fwd));
    *up = VNorm(VCross(fwd, *right));
}

// Матрица модель->мир из базиса. raylib хранит Matrix по столбцам,
// перенос лежит в m12..m14 — стандартная раскладка OpenGL.
static Matrix MatBasis(Vector3 right, Vector3 up, Vector3 fwd, Vector3 pos, Vector3 scale)
{
    Matrix m;
    memset(&m, 0, sizeof(m));   // memset, а не { 0 }: поля Matrix/Mesh меняются между версиями raylib
    m.m0 = right.x * scale.x; m.m1 = right.y * scale.x; m.m2 = right.z * scale.x; m.m3 = 0.0f;
    m.m4 = up.x * scale.y; m.m5 = up.y * scale.y; m.m6 = up.z * scale.y; m.m7 = 0.0f;
    m.m8 = fwd.x * scale.z; m.m9 = fwd.y * scale.z; m.m10 = fwd.z * scale.z; m.m11 = 0.0f;
    m.m12 = pos.x;             m.m13 = pos.y;             m.m14 = pos.z;             m.m15 = 1.0f;
    return m;
}

static Matrix MatPosScale(Vector3 pos, float s)
{
    return MatBasis(V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1), pos, V3(s, s, s));
}

// Пересечение луча (o, нормированное d) со сферой. t — расстояние до входа.
static bool RaySphere(Vector3 o, Vector3 d, Vector3 c, float r, float* t)
{
    Vector3 m = VSub(o, c);
    float b = VDot(m, d);
    float cc = VDot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return false;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float tt = -b - sqrtf(disc);
    if (tt < 0.0f) tt = 0.0f;
    *t = tt;
    return true;
}

static Color RGBA(int r, int g, int b, int a)
{
    Color c;
    c.r = (unsigned char)Clampi(r, 0, 255);
    c.g = (unsigned char)Clampi(g, 0, 255);
    c.b = (unsigned char)Clampi(b, 0, 255);
    c.a = (unsigned char)Clampi(a, 0, 255);
    return c;
}

static Color Alpha(Color c, float a) { return RGBA(c.r, c.g, c.b, (int)(c.a * Clampf(a, 0.0f, 1.0f))); }

static Color ColLerp(Color a, Color b, float t)
{
    t = Clampf(t, 0.0f, 1.0f);
    return RGBA((int)LerpF(a.r, b.r, t), (int)LerpF(a.g, b.g, t),
        (int)LerpF(a.b, b.b, t), (int)LerpF(a.a, b.a, t));
}

// Единственная точка вывода текста — здесь менять шрифт на кириллический.
static void Txt(const char* text, int x, int y, int size, Color col) { DrawText(text, x, y, size, col); }
static int  TxtW(const char* text, int size) { return MeasureText(text, size); }

// --- Детерминированный ГПСЧ ---
static unsigned int g_rngState = 20260920u;

static unsigned int RndU()
{
    g_rngState ^= g_rngState << 13;
    g_rngState ^= g_rngState >> 17;
    g_rngState ^= g_rngState << 5;
    return g_rngState;
}
static float Rnd01() { return (float)(RndU() % 1000000u) / 1000000.0f; }
static float RndRange(float a, float b) { return a + (b - a) * Rnd01(); }
static int   RndInt(int a, int b) { if (b <= a) return a; return a + (int)(RndU() % (unsigned)(b - a + 1)); }
static bool  RndChance(float p) { return Rnd01() < p; }

static Vector3 RndDir()
{
    // равномерное направление на сфере
    float z = RndRange(-1.0f, 1.0f);
    float th = RndRange(0.0f, 2.0f * PI);
    float r = sqrtf(MaxF(0.0f, 1.0f - z * z));
    return V3(r * cosf(th), z, r * sinf(th));
}

// =====================================================================================
//  РАЗДЕЛ 3. ТИПЫ
// =====================================================================================

enum ResType {
    RES_TITANIUM = 0, RES_ICE, RES_SILICON, RES_CRYSTAL,
    RES_ORGANIC, RES_ALLOY, RES_PLASMA, RES_COUNT
};

static const char* g_resName[RES_COUNT] = { "Titanium", "Ice", "Silicon", "Crystal", "Organic", "Alloy", "Plasma" };
static const char* g_resShort[RES_COUNT] = { "TIT", "ICE", "SIL", "CRY", "ORG", "ALY", "PLZ" };
static Color g_resColor[RES_COUNT];

enum SectorId { SEC_BELT = 0, SEC_REEF, SEC_NEBULA, SEC_TRENCH, SEC_CORE, SEC_COUNT };

static const char* g_secName[SEC_COUNT] = {
    "DEBRIS BELT", "CRYSTAL REEF", "NEBULA SHROUD", "VOID TRENCH", "DERELICT CORE"
};
static const float g_secOuter[SEC_COUNT] = { 1400.0f, 2800.0f, 4200.0f, 5700.0f, WORLD_RADIUS };
static Color g_secTint[SEC_COUNT];

enum CreatureKind { CR_DRIFTER = 0, CR_SNAPPER, CR_STALKER, CR_DRONE, CR_LEVIATHAN, CR_SWARMER, CR_KIND_COUNT };
enum UpgId { UPG_TANK = 0, UPG_THRUST, UPG_HULL, UPG_LASER, UPG_POWER, UPG_COUNT };
enum RecipeKind { RK_UPGRADE = 0, RK_UNLOCK, RK_CONSUMABLE, RK_WARP };
enum UnlockId { UNL_SCANNER = 0, UNL_RADSUIT, UNL_BLASTER, UNL_TORPEDO };
enum ConsumableId { CON_REPAIR = 0, CON_O2CAP, CON_BUOY, CON_TORPEDO };
enum SiteKind { SITE_BEACON = 0, SITE_CACHE, SITE_FRAGMENT };

enum GState { ST_MENU = 0, ST_PLAY, ST_BASE, ST_DATABANK, ST_PAUSE, ST_DEAD, ST_WIN, ST_HELP };

// Три вида вооружения: лазер добывает руду и слабо ранит живое, бластер и
// торпеда — наоборот, специализированы против существ и руду не берут.
enum WeaponId { WPN_LASER = 0, WPN_BLASTER, WPN_TORPEDO, WPN_COUNT };
// Чей это снаряд (кому наносит урон) и какого вида (как рисуется/ведёт себя).
enum ProjOwner { OWNER_DRONE = 0, OWNER_PLAYER };
enum ProjKind { PK_DRONE_SHOT = 0, PK_BLASTER, PK_TORPEDO };

struct Particle {
    Vector3 pos, vel;
    float   life, maxLife, size, drag;
    Color   col;
    bool    alive, glow;
};

struct Asteroid {
    Vector3 pos, vel;
    Vector3 axisR, axisU, axisF;    // фиксированная ориентация (свет запечён, вращать нельзя)
    float   radius;
    float   hp, maxHp;
    float   hitFlash;
    int     mesh;
    ResType res;
    int     amount;
    int     sector;
    float   respawn;
    bool    alive;
    Color   col;
};

struct Pickup {
    Vector3 pos, vel;
    ResType res;
    int     amount;
    float   life, spin;
    bool    alive;
};

struct Creature {
    CreatureKind kind;
    Vector3 pos, vel, fwd;
    float   hp, maxHp;
    float   radius, speed, damage;
    int     state;                  // 0 блуждание, 1 преследование, 2 отступление
    float   stateT;
    Vector3 wander;
    float   attackCd, scanProg, phase, respawn, hitFlash;
    int     homeSector;
    bool    alive;
    float   bitePrep;   // 0..1 — только Левиафан: замах перед укусом, виден как разгорающаяся пасть
};

struct Projectile {
    Vector3 pos, vel;
    float   life, dmg, radius;
    Color   col;
    bool    alive;
    int     owner;      // ProjOwner — кому наносит урон при попадании
    int     kind;       // ProjKind — как рисуется и ведёт себя
    float   splash;     // >0 у торпеды: радиус взрыва при попадании/истечении жизни
    int     homeTarget;  // индекс в g_cre для самонаведения торпеды, -1 если цели нет
};

struct Site {
    Vector3  pos;
    SiteKind kind;
    int      payload;
    float    radius, scanProg, spin;
    bool     used;
    int      sector;
};

struct Buoy { Vector3 pos; float charge; bool alive; };
struct Nebula { Vector3 pos; float radius; Color col; };
struct Star { Vector3 dir; float size, bright; };
struct Toast { char text[100]; float life; Color col; };

struct Recipe {
    const char* name;
    const char* desc;
    int         cost[RES_COUNT];
    RecipeKind  kind;
    int         param;
};

struct LogEntry { const char* title; const char* body; };

struct Player {
    Vector3 pos, vel;
    float   yaw, pitch;
    Vector3 fwd, right, up;

    float   o2, o2Max;
    float   energy, energyMax;
    float   hull, hullMax;

    int     inv[RES_COUNT];
    int     invCap;
    int     upg[UPG_COUNT];
    bool    hasScanner, hasRadSuit;
    int     repairKits, o2Caps, buoys;
    bool    warpPart[3];
    bool    warpBuilt;

    float   invulT, hitFlash, boostT;
    bool    thrusting;

    int     weapon;                    // WeaponId — выбранное сейчас оружие
    bool    hasBlaster, hasTorpedo;
    int     torpedoAmmo;
    float   blasterCd, torpedoCd;      // собственные кулдауны, отдельные от attackCd существ
};

// =====================================================================================
//  РАЗДЕЛ 4. ГЛОБАЛЬНОЕ СОСТОЯНИЕ
// =====================================================================================

static Player     g_player;
static Asteroid   g_ast[MAX_ASTEROIDS];
static Pickup     g_pick[MAX_PICKUPS];
static Creature   g_cre[MAX_CREATURES];
static Particle   g_part[MAX_PARTICLES];
static Projectile g_proj[MAX_PROJECTILES];
static Site       g_site[MAX_SITES];
static Buoy       g_buoy[MAX_BUOYS];
static Nebula     g_neb[MAX_NEBULA];
static Star       g_star[MAX_STARS];
static Toast      g_toast[MAX_TOASTS];

static int g_nAst = 0, g_nCre = 0, g_nSite = 0, g_nNeb = 0;
static int g_partHead = 0;

static int  g_store[RES_COUNT];
static bool g_logUnlocked[24];

static GState g_state = ST_MENU;
static GState g_prevSt = ST_PLAY;
static GState g_dbReturn = ST_PLAY;
static int    g_stateAge = 0;       // кадров с момента смены состояния
static bool   g_quit = false;

static Camera3D g_cam;
static Vector3  g_camFwd;
static float    g_projScale = 1.0f; // пикселей на единицу мира на расстоянии 1
static bool     g_thirdPerson = false;

static Color g_bg = { 5, 7, 14, 255 };
static Color g_bgTop = { 10, 14, 27, 255 };   // верх экрана чуть светлее — глубина неба, не плоская заливка

static float g_shake = 0.0f, g_redFlash = 0.0f, g_warpT = 0.0f;
static float g_timeAlive = 0.0f, g_globalTime = 0.0f;
static int   g_deaths = 0, g_mined = 0, g_killed = 0;
static float g_deepest = 0.0f;
static int   g_baseTab = 0, g_logSel = 0;
static float g_fabScroll = 0.0f;   // прокрутка списка рецептов на вкладке FABRICATOR
static char  g_deathCause[64] = "UNKNOWN";

static Vector3 g_beamEnd;
static bool    g_beamOn = false, g_beamHit = false;
static Vector3 g_scanTarget;
static bool    g_scanActive = false;

// Ресурсы GPU
static Mesh     g_meshRock[ROCK_MESHES];
static Mesh     g_meshBall;         // гладкий шар — существа, капсула
static Mesh     g_meshGem;          // гранёный кристалл — ресурсы, обломки
static Material g_mat;
static bool     g_meshesReady = false;

// Аудио
static bool  g_audioOk = false;
static Sound g_sMine, g_sBreak, g_sPick, g_sHurt, g_sCraft, g_sScan, g_sDock, g_sRoar, g_sShoot, g_sDeath;

// =====================================================================================
//  РАЗДЕЛ 5. ТАБЛИЦЫ: РЕЦЕПТЫ И БОРТЖУРНАЛ
// =====================================================================================

#define RECIPE_COUNT 14
#define LOG_COUNT    15

static Recipe   g_recipe[RECIPE_COUNT];
static LogEntry g_log[LOG_COUNT];

static void InitTables()
{
    g_resColor[RES_TITANIUM] = RGBA(190, 196, 205, 255);
    g_resColor[RES_ICE] = RGBA(140, 220, 255, 255);
    g_resColor[RES_SILICON] = RGBA(215, 175, 120, 255);
    g_resColor[RES_CRYSTAL] = RGBA(170, 130, 255, 255);
    g_resColor[RES_ORGANIC] = RGBA(120, 235, 160, 255);
    g_resColor[RES_ALLOY] = RGBA(255, 190, 90, 255);
    g_resColor[RES_PLASMA] = RGBA(255, 105, 150, 255);

    g_secTint[SEC_BELT] = RGBA(48, 78, 105, 255);
    g_secTint[SEC_REEF] = RGBA(78, 55, 120, 255);
    g_secTint[SEC_NEBULA] = RGBA(105, 50, 88, 255);
    g_secTint[SEC_TRENCH] = RGBA(26, 40, 62, 255);
    g_secTint[SEC_CORE] = RGBA(105, 46, 34, 255);

    Recipe r;

    memset(&r, 0, sizeof(r));
    r.name = "O2 TANK";
    r.desc = "Bigger lungs. +40 seconds of oxygen per level.";
    r.cost[RES_ICE] = 6; r.cost[RES_TITANIUM] = 4;
    r.kind = RK_UPGRADE; r.param = UPG_TANK;
    g_recipe[0] = r;

    memset(&r, 0, sizeof(r));
    r.name = "THRUSTERS";
    r.desc = "More acceleration and a higher top speed in every axis.";
    r.cost[RES_TITANIUM] = 6; r.cost[RES_SILICON] = 4;
    r.kind = RK_UPGRADE; r.param = UPG_THRUST;
    g_recipe[1] = r;

    memset(&r, 0, sizeof(r));
    r.name = "HULL PLATING";
    r.desc = "+50 hull integrity and a bigger cargo hold per level.";
    r.cost[RES_TITANIUM] = 8; r.cost[RES_ALLOY] = 2;
    r.kind = RK_UPGRADE; r.param = UPG_HULL;
    g_recipe[2] = r;

    memset(&r, 0, sizeof(r));
    r.name = "MINING LASER";
    r.desc = "Cuts harder rock, reaches further. Deep sectors need Mk3 or better.";
    r.cost[RES_CRYSTAL] = 4; r.cost[RES_SILICON] = 5;
    r.kind = RK_UPGRADE; r.param = UPG_LASER;
    g_recipe[3] = r;

    memset(&r, 0, sizeof(r));
    r.name = "POWER CELL";
    r.desc = "+40 energy and faster recharge per level.";
    r.cost[RES_CRYSTAL] = 5; r.cost[RES_PLASMA] = 2;
    r.kind = RK_UPGRADE; r.param = UPG_POWER;
    g_recipe[4] = r;

    memset(&r, 0, sizeof(r));
    r.name = "SCANNER MODULE";
    r.desc = "Hold RIGHT MOUSE on lifeforms and beacons to fill the databank.";
    r.cost[RES_SILICON] = 6; r.cost[RES_CRYSTAL] = 3;
    r.kind = RK_UNLOCK; r.param = UNL_SCANNER;
    g_recipe[5] = r;

    memset(&r, 0, sizeof(r));
    r.name = "RAD SUIT";
    r.desc = "Survive the radiation of the DERELICT CORE sector.";
    r.cost[RES_ALLOY] = 4; r.cost[RES_ORGANIC] = 8;
    r.kind = RK_UNLOCK; r.param = UNL_RADSUIT;
    g_recipe[6] = r;

    memset(&r, 0, sizeof(r));
    r.name = "REPAIR KIT";
    r.desc = "Consumable. Press [F] in flight to restore 45 hull.";
    r.cost[RES_TITANIUM] = 3; r.cost[RES_ORGANIC] = 2;
    r.kind = RK_CONSUMABLE; r.param = CON_REPAIR;
    g_recipe[7] = r;

    memset(&r, 0, sizeof(r));
    r.name = "O2 CAPSULE";
    r.desc = "Consumable. Press [Q] to refill 55 seconds of oxygen.";
    r.cost[RES_ICE] = 4;
    r.kind = RK_CONSUMABLE; r.param = CON_O2CAP;
    g_recipe[8] = r;

    memset(&r, 0, sizeof(r));
    r.name = "OXYGEN BUOY";
    r.desc = "Deploy with [B]. Refills oxygen inside its halo. Your forward base.";
    r.cost[RES_ICE] = 6; r.cost[RES_TITANIUM] = 5; r.cost[RES_SILICON] = 2;
    r.kind = RK_CONSUMABLE; r.param = CON_BUOY;
    g_recipe[9] = r;

    memset(&r, 0, sizeof(r));
    r.name = "WARP DRIVE";
    r.desc = "Requires all 3 drive fragments from the deep. Your ticket home.";
    r.cost[RES_ALLOY] = 10; r.cost[RES_PLASMA] = 8; r.cost[RES_CRYSTAL] = 10;
    r.kind = RK_WARP; r.param = 0;
    g_recipe[10] = r;

    memset(&r, 0, sizeof(r));
    r.name = "PULSE CANNON";
    r.desc = "Rapid-fire energy bolts built for flesh, not rock. Switch to it with [2].";
    r.cost[RES_SILICON] = 8; r.cost[RES_ALLOY] = 4;
    r.kind = RK_UNLOCK; r.param = UNL_BLASTER;
    g_recipe[11] = r;

    memset(&r, 0, sizeof(r));
    r.name = "TORPEDO LAUNCHER";
    r.desc = "Slow, dumb, and devastating. Locks onto whatever is in front of you. Switch to it with [3].";
    r.cost[RES_ALLOY] = 8; r.cost[RES_PLASMA] = 5;
    r.kind = RK_UNLOCK; r.param = UNL_TORPEDO;
    g_recipe[12] = r;

    memset(&r, 0, sizeof(r));
    r.name = "TORPEDO";
    r.desc = "Consumable ammo for the launcher. One shot, one splash of damage.";
    r.cost[RES_ALLOY] = 3; r.cost[RES_ORGANIC] = 3; r.cost[RES_PLASMA] = 1;
    r.kind = RK_CONSUMABLE; r.param = CON_TORPEDO;
    g_recipe[13] = r;

    // ---- Записи о существах (открываются сканером) ----
    g_log[0].title = "LIFEFORM: DRIFTER";
    g_log[0].body = "Gas-bladder grazer. Rides the magnetic seams of the belt, filtering ice dust "
        "through a ring of cilia. No teeth, no aggression, no survival instinct worth "
        "the name. The ship's biologist called them the only honest thing out here.";

    g_log[1].title = "LIFEFORM: SNAPPER";
    g_log[1].body = "Territorial. Mistakes thruster plumes for rival display flares and charges. "
        "Weak against plating, but it rarely stays alone for long. Kill the first one "
        "and the rest usually reconsider.";

    g_log[2].title = "LIFEFORM: VOID STALKER";
    g_log[2].body = "Ambush predator of the trench. Runs cold until it has a heading, then burns "
        "everything it has in one straight line. If you see it slow down, it has "
        "already decided where you will be.";

    g_log[3].title = "MACHINE: SENTRY DRONE";
    g_log[3].body = "Odyssey perimeter drone, still running a security routine for a ship that no "
        "longer exists. Friend-or-foe table corrupted. It is not hunting you. It simply "
        "cannot confirm that you are crew.";

    g_log[4].title = "LIFEFORM: VOID MAW";
    g_log[4].body = "Apex. Forty metres of cartilage and appetite. It does not chase what it cannot "
        "hear, and it hears thrusters. Cut power, drift, let it pass. Scanning this was "
        "the bravest thing you have done today.";

    // ---- Сюжетные записи с маяков ----
    g_log[5].title = "LOG 01 - IMPACT";
    g_log[5].body = "Colony ship ODYSSEY, day 2,114. We came out of warp inside something that was "
        "not on any chart. Debris, dense, moving. The hull opened like a can. I got to "
        "a pod. I do not know who else did.";

    g_log[6].title = "LOG 02 - HEADCOUNT";
    g_log[6].body = "Eleven pod beacons on the grid this morning. Six by evening. They are not going "
        "dark from damage. They are going dark one at a time, from the outside in. "
        "Something is working its way toward the centre.";

    g_log[7].title = "LOG 03 - THE REEF";
    g_log[7].body = "The crystal formations grow. I marked one, came back nine days later, and it "
        "had doubled. They feed on the wreck field. We did not crash into an ecosystem. "
        "We crashed into a mouth, and we are the meal it is still chewing.";

    g_log[8].title = "LOG 04 - ENGINEERING";
    g_log[8].body = "Good news: a warp drive is three parts. Coil, containment cell, ignition core. "
        "Bad news: they blew clear of the ship on different vectors, so they are out in "
        "the deep. I am an engineer, not a hero. Someone else go.";

    g_log[9].title = "LOG 05 - OXYGEN";
    g_log[9].body = "Rule I wish someone had told me on day one: your tank is not a resource, it is "
        "a leash. Measure every trip by the return leg. The belt is forgiving. The "
        "trench is not, and it does not warn you twice.";

    g_log[10].title = "LOG 06 - THE NEBULA";
    g_log[10].body = "Visibility inside the shroud is under two hundred metres. My scanner picks up "
        "movement at the edge constantly and finds nothing when I turn. I have started "
        "flying with the lamp off. It is worse. I keep doing it.";

    g_log[11].title = "LOG 07 - CAPTAIN";
    g_log[11].body = "Found the bridge section. Captain still strapped in, hand on the manual cutoff. "
        "She pulled it. That is why the reactor did not take the whole field with it, "
        "and why the core sector is merely hot instead of gone. She bought us this.";

    g_log[12].title = "LOG 08 - RADIATION";
    g_log[12].body = "The core reads lethal without shielding. Alloy plate and organic lining hold "
        "long enough. Not comfortably. Long enough. Everything worth having is inside "
        "the hot zone, which is exactly how this place is built.";

    g_log[13].title = "LOG 09 - LAST POD";
    g_log[13].body = "Whoever reads this: I am out of ice and out of ideas. The drive parts are real, "
        "I have seen two of them on the grid myself. Build the thing. Do not come "
        "looking for me. Come looking for the third fragment.";

    g_log[14].title = "LOG 10 - THE WAY OUT";
    g_log[14].body = "A warp drive assembled at a pod charts its own jump. One shot, no correction "
        "burn, no coming back for anything you forgot. Load what matters, sit down, "
        "and pull the lever. Nothing here is worth a second trip.";
}

// =====================================================================================
//  РАЗДЕЛ 6. СООБЩЕНИЯ, ЧАСТИЦЫ, СМЕНА СОСТОЯНИЯ
// =====================================================================================

static int SectorAt(float dist)
{
    for (int i = 0; i < SEC_COUNT; i++) if (dist < g_secOuter[i]) return i;
    return SEC_CORE;
}
static int PlayerSector() { return SectorAt(VLen(g_player.pos)); }

static void Msg(Color col, const char* fmt, ...)
{
    for (int i = MAX_TOASTS - 1; i > 0; i--) g_toast[i] = g_toast[i - 1];

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_toast[0].text, sizeof(g_toast[0].text), fmt, args);
    va_end(args);

    g_toast[0].life = 4.5f;
    g_toast[0].col = col;
}

static void Emit(Vector3 pos, Vector3 vel, Color col, float life, float size, int count,
    float spread, bool glow)
{
    for (int i = 0; i < count; i++) {
        Particle* p = &g_part[g_partHead];
        g_partHead = (g_partHead + 1) % MAX_PARTICLES;

        p->pos = pos;
        p->vel = VAdd(vel, VMul(RndDir(), RndRange(0.0f, spread)));
        p->col = col;
        p->maxLife = life * RndRange(0.7f, 1.3f);
        p->life = p->maxLife;
        p->size = size * RndRange(0.7f, 1.4f);
        p->drag = RndRange(0.6f, 1.6f);
        p->glow = glow;
        p->alive = true;
    }
}

// Все переходы состояний идут только отсюда. Здесь же курсор и сброс возраста
// состояния — именно g_stateAge не даёт кнопке сработать в кадре открытия экрана.
static void SetState(GState s)
{
    if (s == g_state) return;
    g_state = s;
    g_stateAge = 0;

    bool wantCursor = (s != ST_PLAY);
    if (wantCursor) EnableCursor();
    else            DisableCursor();
}

// =====================================================================================
//  РАЗДЕЛ 7. ЗВУК (синтез на лету, файлы не нужны)
// =====================================================================================

static Sound MakeSound(float f0, float f1, float dur, float noiseAmt, float vol, float envPow)
{
    const unsigned int rate = 22050;
    unsigned int frames = (unsigned int)(rate * dur);
    if (frames < 32) frames = 32;

    short* data = (short*)malloc(sizeof(short) * frames);
    float phase = 0.0f;

    for (unsigned int i = 0; i < frames; i++) {
        float t = (float)i / (float)frames;
        float f = LerpF(f0, f1, t);
        phase += 2.0f * PI * f / (float)rate;
        float env = powf(1.0f - t, envPow);
        float tone = sinf(phase);
        float noise = Rnd01() * 2.0f - 1.0f;
        float s = tone * (1.0f - noiseAmt) + noise * noiseAmt;
        data[i] = (short)(Clampf(s * env * vol, -1.0f, 1.0f) * 31000.0f);
    }

    Wave w;
    w.frameCount = frames;
    w.sampleRate = rate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;

    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

static void InitAudio()
{
    InitAudioDevice();
    g_audioOk = IsAudioDeviceReady();
    if (!g_audioOk) return;

    g_sMine = MakeSound(220.0f, 180.0f, 0.10f, 0.55f, 0.35f, 1.2f);
    g_sBreak = MakeSound(160.0f, 40.0f, 0.45f, 0.85f, 0.70f, 1.6f);
    g_sPick = MakeSound(880.0f, 1500.0f, 0.10f, 0.05f, 0.30f, 2.0f);
    g_sHurt = MakeSound(300.0f, 90.0f, 0.30f, 0.60f, 0.75f, 1.4f);
    g_sCraft = MakeSound(440.0f, 990.0f, 0.35f, 0.02f, 0.45f, 1.0f);
    g_sScan = MakeSound(1200.0f, 700.0f, 0.18f, 0.02f, 0.30f, 1.5f);
    g_sDock = MakeSound(160.0f, 420.0f, 0.40f, 0.05f, 0.45f, 1.0f);
    g_sRoar = MakeSound(90.0f, 32.0f, 1.10f, 0.45f, 0.90f, 0.8f);
    g_sShoot = MakeSound(700.0f, 260.0f, 0.14f, 0.30f, 0.30f, 1.8f);
    g_sDeath = MakeSound(240.0f, 30.0f, 1.40f, 0.35f, 0.85f, 0.9f);
}

static void Play(Sound s, float pitch)
{
    if (!g_audioOk) return;
    SetSoundPitch(s, pitch);
    PlaySound(s);
}

// Звук с громкостью по расстоянию — для рёва левиафана и выстрелов дронов.
static void PlayAt(Sound s, float pitch, Vector3 pos, float range)
{
    if (!g_audioOk) return;
    float d = VDist(pos, g_player.pos);
    float v = Clampf(1.0f - d / range, 0.0f, 1.0f);
    if (v <= 0.02f) return;
    SetSoundVolume(s, v);
    SetSoundPitch(s, pitch);
    PlaySound(s);
}

// =====================================================================================
//  РАЗДЕЛ 8. ПРОЦЕДУРНЫЕ МЕШИ С ЗАПЕЧЁННЫМ ОСВЕЩЕНИЕМ
//
//  Ключевая идея всей 3D-части: шейдера нет. Стандартный шейдер raylib считает
//  finalColor = texel * colDiffuse * vertexColor, поэтому диффузное освещение
//  вычисляется ОДИН РАЗ при генерации меша и кладётся в цвета вершин. Дальше
//  объект тонируется через материал и рисуется одним вызовом DrawMesh.
//
//  Плата за это: меш нельзя вращать, иначе свет поедет вместе с ним. Поэтому у
//  астероидов ориентация постоянная, а разнообразие даётся семью разными
//  формами и неравномерным масштабом.
// =====================================================================================

static Mesh GenLitMesh(int rings, int slices, unsigned int seed, float lumpy,
    Color base, float ambient, float rim, float spec)
{
    Mesh mesh;
    memset(&mesh, 0, sizeof(mesh));

    int vcount = (rings + 1) * (slices + 1);
    int tcount = rings * slices * 2;

    mesh.vertexCount = vcount;
    mesh.triangleCount = tcount;
    mesh.vertices = (float*)MemAlloc((unsigned int)(vcount * 3 * sizeof(float)));
    mesh.normals = (float*)MemAlloc((unsigned int)(vcount * 3 * sizeof(float)));
    mesh.texcoords = (float*)MemAlloc((unsigned int)(vcount * 2 * sizeof(float)));
    mesh.colors = (unsigned char*)MemAlloc((unsigned int)(vcount * 4));
    mesh.indices = (unsigned short*)MemAlloc((unsigned int)(tcount * 3 * sizeof(unsigned short)));

    // --- Оси шума для неровностей (детерминированно по seed) ---
    unsigned int saved = g_rngState;
    g_rngState = seed | 1u;

    Vector3 axis[4];
    float   freq[4], amp[4], phase[4];
    for (int k = 0; k < 4; k++) {
        axis[k] = RndDir();
        freq[k] = RndRange(1.3f, 4.4f);
        amp[k] = RndRange(0.05f, 0.21f) * lumpy;
        phase[k] = RndRange(0.0f, 2.0f * PI);
    }

    // --- Вершины ---
    for (int i = 0; i <= rings; i++) {
        float phi = -PI * 0.5f + PI * (float)i / (float)rings;
        for (int j = 0; j <= slices; j++) {
            float th = 2.0f * PI * (float)j / (float)slices;
            Vector3 n = V3(cosf(phi) * cosf(th), sinf(phi), cosf(phi) * sinf(th));

            float r = 1.0f;
            for (int k = 0; k < 4; k++) r += amp[k] * sinf(VDot(n, axis[k]) * freq[k] + phase[k]);

            int v = i * (slices + 1) + j;
            mesh.vertices[v * 3 + 0] = n.x * r;
            mesh.vertices[v * 3 + 1] = n.y * r;
            mesh.vertices[v * 3 + 2] = n.z * r;
            mesh.texcoords[v * 2 + 0] = (float)j / (float)slices;
            mesh.texcoords[v * 2 + 1] = (float)i / (float)rings;
        }
    }

    // --- Индексы (обход даёт нормаль наружу, проверка ниже подстрахует) ---
    int t = 0;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < slices; j++) {
            unsigned short a = (unsigned short)(i * (slices + 1) + j);
            unsigned short b = (unsigned short)(a + 1);
            unsigned short d = (unsigned short)((i + 1) * (slices + 1) + j);
            unsigned short c = (unsigned short)(d + 1);

            mesh.indices[t++] = a; mesh.indices[t++] = c; mesh.indices[t++] = b;
            mesh.indices[t++] = a; mesh.indices[t++] = d; mesh.indices[t++] = c;
        }
    }

    // --- Сглаженные нормали через накопление нормалей граней ---
    memset(mesh.normals, 0, (size_t)vcount * 3 * sizeof(float));
    for (int k = 0; k < tcount; k++) {
        int i0 = mesh.indices[k * 3 + 0];
        int i1 = mesh.indices[k * 3 + 1];
        int i2 = mesh.indices[k * 3 + 2];

        Vector3 p0 = V3(mesh.vertices[i0 * 3], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]);
        Vector3 p1 = V3(mesh.vertices[i1 * 3], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]);
        Vector3 p2 = V3(mesh.vertices[i2 * 3], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]);

        Vector3 fn = VCross(VSub(p1, p0), VSub(p2, p0));
        int idx[3] = { i0, i1, i2 };
        for (int q = 0; q < 3; q++) {
            mesh.normals[idx[q] * 3 + 0] += fn.x;
            mesh.normals[idx[q] * 3 + 1] += fn.y;
            mesh.normals[idx[q] * 3 + 2] += fn.z;
        }
    }

    for (int v = 0; v < vcount; v++) {
        Vector3 n = VNorm(V3(mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]));
        mesh.normals[v * 3 + 0] = n.x;
        mesh.normals[v * 3 + 1] = n.y;
        mesh.normals[v * 3 + 2] = n.z;
    }

    // --- Страховка от вывернутого меша ---
    // Если нормали в среднем смотрят внутрь, разворачиваем обход и знак нормалей.
    float orient = 0.0f;
    for (int v = 0; v < vcount; v++) {
        Vector3 p = VNorm(V3(mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]));
        Vector3 n = V3(mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]);
        orient += VDot(p, n);
    }
    if (orient < 0.0f) {
        for (int k = 0; k < tcount; k++) {
            unsigned short tmp = mesh.indices[k * 3 + 1];
            mesh.indices[k * 3 + 1] = mesh.indices[k * 3 + 2];
            mesh.indices[k * 3 + 2] = tmp;
        }
        for (int v = 0; v < vcount * 3; v++) mesh.normals[v] = -mesh.normals[v];
    }

    // --- Запекание освещения в цвета вершин ---
    Vector3 lightDir = VNorm(V3(-0.42f, -0.76f, -0.50f));   // направление ЛУЧЕЙ
    Vector3 fillDir = VNorm(V3(0.55f, 0.30f, 0.70f));      // слабая заполняющая
    // Блик считается по фиксированному условному "взгляду" — камера свободная,
    // поэтому честный view-dependent specular запечь нельзя; этот вектор просто
    // выбран так, чтобы блик ложился правдоподобно на большинстве ракурсов.
    Vector3 fixedView = VNorm(V3(0.35f, 0.25f, 0.90f));
    Vector3 halfVec = VNorm(VSub(fixedView, lightDir));

    for (int v = 0; v < vcount; v++) {
        Vector3 n = V3(mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]);

        float key = MaxF(0.0f, VDot(n, VMul(lightDir, -1.0f)));
        float fill = MaxF(0.0f, VDot(n, fillDir)) * 0.28f;
        float f = ambient + (1.0f - ambient) * key + fill;

        // лёгкий контровой ободок — силуэт читается даже на тёмном фоне
        float edge = powf(1.0f - MaxF(0.0f, VDot(n, VMul(lightDir, -1.0f))), 3.0f) * rim;
        // глянцевый блик (Блинн-Фонг с фиксированным "взглядом") — убирает
        // плоский "пластиковый" вид плоского диффуза, особенно на гладких мешах
        float specTerm = powf(MaxF(0.0f, VDot(n, halfVec)), 28.0f) * spec;

        f = Clampf(f, 0.0f, 1.55f);

        mesh.colors[v * 4 + 0] = (unsigned char)Clampi((int)(base.r * f + 255.0f * edge * 0.35f + 255.0f * specTerm), 0, 255);
        mesh.colors[v * 4 + 1] = (unsigned char)Clampi((int)(base.g * f + 255.0f * edge * 0.42f + 255.0f * specTerm), 0, 255);
        mesh.colors[v * 4 + 2] = (unsigned char)Clampi((int)(base.b * f + 255.0f * edge * 0.55f + 255.0f * specTerm), 0, 255);
        mesh.colors[v * 4 + 3] = 255;
    }

    g_rngState = saved;

    UploadMesh(&mesh, false);
    return mesh;
}

static void InitMeshes()
{
    // Разрешение сфер поднято с 12x16 до 18x24: заметно меньше огранки на
    // существах и капсуле, ближе к гладкому телу, а не многограннику.
    // У камней оставлена лёгкая "рубленость" — это часть их характера,
    // но тоже слегка приподнята с 9x12 до 11x15 для менее блочного силуэта.
    for (int i = 0; i < ROCK_MESHES; i++)
        g_meshRock[i] = GenLitMesh(11, 15, 7717u + i * 5231u, 1.0f + i * 0.14f,
            RGBA(170, 175, 185, 255), 0.20f, 0.55f, 0.12f);

    g_meshBall = GenLitMesh(18, 24, 991u, 0.0f, RGBA(200, 205, 215, 255), 0.26f, 0.70f, 0.50f);
    g_meshGem = GenLitMesh(4, 8, 4243u, 0.55f, RGBA(220, 225, 235, 255), 0.40f, 0.90f, 0.80f);

    g_mat = LoadMaterialDefault();
    g_meshesReady = true;
}

static void UnloadMeshes()
{
    if (!g_meshesReady) return;
    for (int i = 0; i < ROCK_MESHES; i++) UnloadMesh(g_meshRock[i]);
    UnloadMesh(g_meshBall);
    UnloadMesh(g_meshGem);
    g_meshesReady = false;
}

// Рисование освещённого меша с тинтом. Материал один на всех, меняем только цвет.
static void DrawLit(Mesh m, Matrix transform, Color tint)
{
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    DrawMesh(m, g_mat, transform);
}

// "Туман": растворяем цвет в фон по расстоянию, чтобы дальняя граница отрисовки
// не выглядела как обрыв мира.
static Color Fog(Color c, float dist)
{
    float f = Clampf((dist - FADE_START) / (DRAW_DIST - FADE_START), 0.0f, 1.0f);
    return ColLerp(c, g_bg, f * f * 0.95f);
}

// =====================================================================================
//  РАЗДЕЛ 9. ХАРАКТЕРИСТИКИ ИГРОКА
// =====================================================================================

static float StatO2Max() { return 75.0f + 40.0f * g_player.upg[UPG_TANK]; }
static float StatEnergyMax() { return 100.0f + 40.0f * g_player.upg[UPG_POWER]; }
static float StatHullMax() { return 100.0f + 50.0f * g_player.upg[UPG_HULL]; }
static float StatAccel() { return 620.0f + 190.0f * g_player.upg[UPG_THRUST]; }
static float StatMaxSpeed() { return 330.0f + 95.0f * g_player.upg[UPG_THRUST]; }
static float StatEnergyRegen() { return 7.0f + 3.5f * g_player.upg[UPG_POWER]; }
static int   StatInvCap() { return 24 + 8 * g_player.upg[UPG_HULL]; }

static float StatLaserDps()
{
    const float v[4] = { 24.0f, 44.0f, 72.0f, 108.0f };
    return v[Clampi(g_player.upg[UPG_LASER], 0, 3)];
}
static float StatLaserRange()
{
    const float v[4] = { 320.0f, 390.0f, 460.0f, 540.0f };
    return v[Clampi(g_player.upg[UPG_LASER], 0, 3)];
}
// Лазер остаётся в первую очередь инструментом добычи: по живому он бьёт
// заметно слабее полного значения StatLaserDps(), чтобы бластер и торпеда
// были не просто опциональными, а реально нужными против существ.
static float StatLaserCreatureDps() { return StatLaserDps() * 0.55f; }

// --- Пульсовая пушка: скорострельно, дёшево по патронам (жрёт энергию), слабее за выстрел ---
static float StatBlasterDamage() { return 30.0f + 6.0f * g_player.upg[UPG_LASER]; } // тот же лазерный тех-ствол
static float StatBlasterFireDelay() { return 0.16f; }
static float StatBlasterEnergyCost() { return 8.0f; }
static float StatBlasterSpeed() { return 900.0f; }

// --- Торпедный аппарат: медленно, по одному выстрелу, ограниченный боезапас, урон в области ---
static float StatTorpedoDamage() { return 240.0f; }
static float StatTorpedoSplash() { return 130.0f; }
static float StatTorpedoSplashRadius() { return 150.0f; }
static float StatTorpedoSpeed() { return 260.0f; }
static float StatTorpedoCooldown() { return 1.1f; }
static float StatTorpedoTurnRate() { return 2.2f; } // рад/сек — предел доворота при самонаведении

static int InvTotal()
{
    int t = 0;
    for (int i = 0; i < RES_COUNT; i++) t += g_player.inv[i];
    return t;
}

static void RecalcStats()
{
    g_player.o2Max = StatO2Max();
    g_player.energyMax = StatEnergyMax();
    g_player.hullMax = StatHullMax();
    g_player.invCap = StatInvCap();
    g_player.o2 = MinF(g_player.o2, g_player.o2Max);
    g_player.energy = MinF(g_player.energy, g_player.energyMax);
    g_player.hull = MinF(g_player.hull, g_player.hullMax);
}

static void UpdatePlayerBasis()
{
    Player* p = &g_player;
    p->fwd = V3(cosf(p->pitch) * cosf(p->yaw), sinf(p->pitch), cosf(p->pitch) * sinf(p->yaw));
    p->right = VNorm(VCross(V3(0.0f, 1.0f, 0.0f), p->fwd));
    p->up = VNorm(VCross(p->fwd, p->right));
}

// =====================================================================================
//  РАЗДЕЛ 10. ГЕНЕРАЦИЯ МИРА
// =====================================================================================

static ResType RollResource(int sector)
{
    float r = Rnd01();
    switch (sector) {
    case SEC_BELT:
        if (r < 0.52f) return RES_TITANIUM;
        if (r < 0.88f) return RES_ICE;
        return RES_SILICON;
    case SEC_REEF:
        if (r < 0.32f) return RES_CRYSTAL;
        if (r < 0.62f) return RES_SILICON;
        if (r < 0.84f) return RES_ICE;
        return RES_TITANIUM;
    case SEC_NEBULA:
        if (r < 0.34f) return RES_ORGANIC;
        if (r < 0.58f) return RES_ICE;
        if (r < 0.80f) return RES_CRYSTAL;
        return RES_SILICON;
    case SEC_TRENCH:
        if (r < 0.30f) return RES_ALLOY;
        if (r < 0.54f) return RES_CRYSTAL;
        if (r < 0.80f) return RES_TITANIUM;
        return RES_PLASMA;
    default:
        if (r < 0.40f) return RES_PLASMA;
        if (r < 0.74f) return RES_ALLOY;
        return RES_CRYSTAL;
    }
}

static float SectorRockHp(int sector)
{
    const float v[SEC_COUNT] = { 48.0f, 105.0f, 190.0f, 320.0f, 480.0f };
    return v[Clampi(sector, 0, SEC_COUNT - 1)];
}

// Точка внутри кольцевого сегмента диска
static Vector3 RandomPointInSector(int sector)
{
    float inner = (sector == 0) ? 420.0f : g_secOuter[sector - 1];
    float outer = g_secOuter[sector];

    float a = RndRange(0.0f, 2.0f * PI);
    float d = sqrtf(RndRange(inner * inner, outer * outer));

    // к краю диск становится тоньше — так он читается как настоящее кольцо обломков
    float halfH = DISK_HALF * (1.0f - 0.35f * (d / WORLD_RADIUS));
    float y = RndRange(-1.0f, 1.0f);
    y = y * fabsf(y) * halfH;    // сгущение к плоскости диска

    return V3(cosf(a) * d, y, sinf(a) * d);
}

static void SpawnAsteroid(Asteroid* a, int sector)
{
    a->pos = RandomPointInSector(sector);
    a->vel = VMul(RndDir(), RndRange(1.0f, 14.0f));
    a->radius = RndRange(26.0f, 95.0f) * (1.0f + 0.12f * sector);
    a->mesh = RndInt(0, ROCK_MESHES - 1);
    a->sector = sector;
    a->res = RollResource(sector);
    a->amount = RndInt(1, 3) + (a->radius > 65.0f ? 2 : 0);
    a->maxHp = SectorRockHp(sector) * (a->radius / 60.0f);
    a->hp = a->maxHp;
    a->respawn = 0.0f;
    a->hitFlash = 0.0f;
    a->alive = true;

    // Ориентация постоянная и одинаковая для всех: свет запечён в меш,
    // вращать его нельзя. Разнообразие даёт неравномерный масштаб.
    a->axisR = V3(RndRange(0.75f, 1.25f), 0.0f, 0.0f);
    a->axisU = V3(0.0f, RndRange(0.70f, 1.15f), 0.0f);
    a->axisF = V3(0.0f, 0.0f, RndRange(0.75f, 1.25f));

    Color base = g_secTint[sector];
    Color rc = g_resColor[a->res];
    a->col = RGBA((base.r + rc.r) / 2 + 20, (base.g + rc.g) / 2 + 20, (base.b + rc.b) / 2 + 24, 255);
}

static void SetupCreature(Creature* c, CreatureKind k, int sector)
{
    c->kind = k;
    c->pos = RandomPointInSector(sector);
    c->vel = V3(0, 0, 0);
    c->fwd = RndDir();
    c->state = 0;
    c->stateT = RndRange(0.0f, 3.0f);
    c->wander = c->pos;
    c->attackCd = 0.0f;
    c->scanProg = 0.0f;
    c->phase = RndRange(0.0f, 2.0f * PI);
    c->respawn = 0.0f;
    c->homeSector = sector;
    c->hitFlash = 0.0f;
    c->bitePrep = 0.0f;
    c->alive = true;

    switch (k) {
    case CR_DRIFTER:   c->maxHp = 28.0f;   c->radius = 26.0f;  c->speed = 42.0f;  c->damage = 0.0f;  break;
    case CR_SNAPPER:   c->maxHp = 70.0f;   c->radius = 24.0f;  c->speed = 185.0f; c->damage = 9.0f;  break;
    case CR_STALKER:   c->maxHp = 135.0f;  c->radius = 34.0f;  c->speed = 285.0f; c->damage = 17.0f; break;
    case CR_DRONE:     c->maxHp = 90.0f;   c->radius = 22.0f;  c->speed = 170.0f; c->damage = 0.0f;  break;
    case CR_LEVIATHAN: c->maxHp = 1500.0f; c->radius = 165.0f; c->speed = 215.0f; c->damage = 48.0f; break;
        // Мелкий стайный хищник: слаб поодиночке, но охотится косяками —
        // см. флокинг в UpdateCreatures.
    case CR_SWARMER:   c->maxHp = 22.0f;   c->radius = 15.0f;  c->speed = 260.0f; c->damage = 6.0f;  break;
    default: break;
    }
    c->hp = c->maxHp;
}

static void GenerateWorld()
{
    g_rngState = 20260920u;
    g_nAst = 0; g_nCre = 0; g_nSite = 0; g_nNeb = 0;

    // --- Астероиды ---
    const int perSector[SEC_COUNT] = { 330, 480, 620, 780, 700 };
    for (int s = 0; s < SEC_COUNT; s++) {
        for (int i = 0; i < perSector[s] && g_nAst < MAX_ASTEROIDS; i++) {
            SpawnAsteroid(&g_ast[g_nAst], s);
            if (VLen(g_ast[g_nAst].pos) < 500.0f) continue;   // не засоряем площадку у капсулы
            g_nAst++;
        }
    }

    // --- Существа ---
    struct { int sector; CreatureKind kind; int count; } plan[] = {
        { SEC_BELT,   CR_DRIFTER,    20 },
        { SEC_REEF,   CR_DRIFTER,    16 },
        { SEC_REEF,   CR_SNAPPER,    14 },
        { SEC_REEF,   CR_SWARMER,    24 },
        { SEC_NEBULA, CR_SNAPPER,    24 },
        { SEC_NEBULA, CR_DRONE,      12 },
        { SEC_NEBULA, CR_DRIFTER,     8 },
        { SEC_NEBULA, CR_SWARMER,    30 },
        { SEC_TRENCH, CR_STALKER,    22 },
        { SEC_TRENCH, CR_DRONE,      14 },
        { SEC_TRENCH, CR_LEVIATHAN,   2 },
        { SEC_TRENCH, CR_SWARMER,    20 },
        { SEC_CORE,   CR_STALKER,    18 },
        { SEC_CORE,   CR_DRONE,      16 },
        { SEC_CORE,   CR_LEVIATHAN,   2 },
    };
    int planCount = (int)(sizeof(plan) / sizeof(plan[0]));
    for (int i = 0; i < planCount; i++)
        for (int j = 0; j < plan[i].count && g_nCre < MAX_CREATURES; j++)
            SetupCreature(&g_cre[g_nCre++], plan[i].kind, plan[i].sector);

    // --- Маяки с записями 5..14 ---
    int beaconSectors[10] = { SEC_BELT, SEC_BELT, SEC_REEF, SEC_REEF, SEC_NEBULA,
                              SEC_NEBULA, SEC_TRENCH, SEC_TRENCH, SEC_CORE, SEC_CORE };
    for (int i = 0; i < 10 && g_nSite < MAX_SITES; i++) {
        Site* s = &g_site[g_nSite++];
        s->kind = SITE_BEACON;   s->sector = beaconSectors[i];
        s->pos = RandomPointInSector(s->sector);
        s->payload = 5 + i;      s->radius = 46.0f;
        s->scanProg = 0.0f;      s->spin = RndRange(0.0f, 6.28f);
        s->used = false;
    }

    // --- Три обломка варп-двигателя ---
    int fragSectors[3] = { SEC_NEBULA, SEC_TRENCH, SEC_CORE };
    for (int i = 0; i < 3 && g_nSite < MAX_SITES; i++) {
        Site* s = &g_site[g_nSite++];
        s->kind = SITE_FRAGMENT;  s->sector = fragSectors[i];
        s->pos = RandomPointInSector(s->sector);
        s->payload = i;           s->radius = 62.0f;
        s->scanProg = 0.0f;       s->spin = RndRange(0.0f, 6.28f);
        s->used = false;
    }

    // --- Контейнеры ---
    for (int i = 0; i < 14 && g_nSite < MAX_SITES; i++) {
        Site* s = &g_site[g_nSite++];
        s->kind = SITE_CACHE;     s->sector = RndInt(0, SEC_COUNT - 1);
        s->pos = RandomPointInSector(s->sector);
        s->payload = 0;           s->radius = 34.0f;
        s->scanProg = 0.0f;       s->spin = RndRange(0.0f, 6.28f);
        s->used = false;
    }

    // --- Туманности (дальний фон) ---
    for (int i = 0; i < MAX_NEBULA; i++) {
        int sec = RndChance(0.45f) ? SEC_NEBULA : RndInt(0, SEC_COUNT - 1);
        g_neb[g_nNeb].pos = RandomPointInSector(sec);
        g_neb[g_nNeb].radius = RndRange(700.0f, 2600.0f);
        Color t = g_secTint[sec];
        g_neb[g_nNeb].col = RGBA(t.r + RndInt(0, 50), t.g + RndInt(0, 34), t.b + RndInt(0, 60), 255);
        g_nNeb++;
    }

    // --- Звёзды: бесконечно далёкие направления ---
    for (int i = 0; i < MAX_STARS; i++) {
        g_star[i].dir = RndDir();
        g_star[i].size = RndRange(0.7f, 2.1f);
        g_star[i].bright = RndRange(0.25f, 1.0f);
    }
}

static void ResetRun()
{
    memset(&g_player, 0, sizeof(g_player));
    g_player.pos = V3(0.0f, 0.0f, 300.0f);
    g_player.yaw = -PI * 0.5f;
    g_player.pitch = 0.0f;
    UpdatePlayerBasis();
    RecalcStats();
    g_player.o2 = g_player.o2Max;
    g_player.energy = g_player.energyMax;
    g_player.hull = g_player.hullMax;

    memset(g_store, 0, sizeof(g_store));
    memset(g_logUnlocked, 0, sizeof(g_logUnlocked));
    memset(g_toast, 0, sizeof(g_toast));
    memset(g_part, 0, sizeof(g_part));
    memset(g_pick, 0, sizeof(g_pick));
    memset(g_proj, 0, sizeof(g_proj));
    memset(g_buoy, 0, sizeof(g_buoy));

    g_store[RES_TITANIUM] = 4;
    g_store[RES_ICE] = 4;

    g_timeAlive = 0.0f;
    g_deaths = 0; g_mined = 0; g_killed = 0;
    g_deepest = 0.0f;
    g_shake = 0.0f; g_redFlash = 0.0f; g_warpT = 0.0f;
    g_baseTab = 0; g_logSel = 0; g_fabScroll = 0.0f;
    g_thirdPerson = false;

    GenerateWorld();

    g_cam.position = g_player.pos;
    g_cam.target = VAdd(g_player.pos, g_player.fwd);
    g_cam.up = V3(0.0f, 1.0f, 0.0f);
    g_cam.fovy = 70.0f;
    g_cam.projection = CAMERA_PERSPECTIVE;

    Msg(RGBA(150, 220, 255, 255), "POD ONLINE. MINE WITH LEFT MOUSE. WATCH YOUR OXYGEN.");
}

// =====================================================================================
//  РАЗДЕЛ 11. ИНВЕНТАРЬ, КРАФТ, УРОН
// =====================================================================================

static bool AddToInv(ResType r, int amount)
{
    int space = g_player.invCap - InvTotal();
    if (space <= 0) return false;
    int add = (amount < space) ? amount : space;
    g_player.inv[r] += add;
    return add > 0;
}

static void DepositAll()
{
    int total = 0;
    for (int i = 0; i < RES_COUNT; i++) {
        total += g_player.inv[i];
        g_store[i] += g_player.inv[i];
        g_player.inv[i] = 0;
    }
    if (total > 0) Msg(RGBA(150, 255, 190, 255), "CARGO TRANSFERRED: %d UNITS", total);
}

static void GetRecipeCost(int idx, int out[RES_COUNT])
{
    const Recipe* r = &g_recipe[idx];
    int mult = (r->kind == RK_UPGRADE) ? (g_player.upg[r->param] + 1) : 1;
    for (int i = 0; i < RES_COUNT; i++) out[i] = r->cost[i] * mult;
}

static bool RecipeMaxed(int idx)
{
    const Recipe* r = &g_recipe[idx];
    if (r->kind == RK_UPGRADE) return g_player.upg[r->param] >= 3;
    if (r->kind == RK_UNLOCK) {
        if (r->param == UNL_SCANNER) return g_player.hasScanner;
        if (r->param == UNL_RADSUIT) return g_player.hasRadSuit;
        if (r->param == UNL_BLASTER) return g_player.hasBlaster;
        if (r->param == UNL_TORPEDO) return g_player.hasTorpedo;
    }
    if (r->kind == RK_WARP) return g_player.warpBuilt;
    return false;
}

static bool RecipeLocked(int idx)
{
    if (g_recipe[idx].kind == RK_WARP)
        return !(g_player.warpPart[0] && g_player.warpPart[1] && g_player.warpPart[2]);
    return false;
}

static bool CanAfford(int idx)
{
    int cost[RES_COUNT];
    GetRecipeCost(idx, cost);
    for (int i = 0; i < RES_COUNT; i++) if (g_store[i] < cost[i]) return false;
    return true;
}

static void DoCraft(int idx)
{
    if (RecipeMaxed(idx) || RecipeLocked(idx) || !CanAfford(idx)) { Play(g_sHurt, 1.6f); return; }

    int cost[RES_COUNT];
    GetRecipeCost(idx, cost);
    for (int i = 0; i < RES_COUNT; i++) g_store[i] -= cost[i];

    Recipe* r = &g_recipe[idx];
    switch (r->kind) {
    case RK_UPGRADE:
        g_player.upg[r->param]++;
        RecalcStats();
        Msg(RGBA(255, 220, 120, 255), "%s UPGRADED TO MK%d", r->name, g_player.upg[r->param] + 1);
        break;
    case RK_UNLOCK:
        switch (r->param) {
        case UNL_SCANNER:
            g_player.hasScanner = true;
            Msg(RGBA(150, 220, 255, 255), "SCANNER ONLINE. HOLD RIGHT MOUSE ON TARGETS.");
            break;
        case UNL_RADSUIT:
            g_player.hasRadSuit = true;
            Msg(RGBA(150, 220, 255, 255), "RAD SUIT SEALED. THE CORE IS SURVIVABLE NOW.");
            break;
        case UNL_BLASTER:
            g_player.hasBlaster = true;
            Msg(RGBA(150, 220, 255, 255), "PULSE CANNON ONLINE. SWITCH WITH [2].");
            break;
        case UNL_TORPEDO:
            g_player.hasTorpedo = true;
            Msg(RGBA(255, 150, 255, 255), "TORPEDO LAUNCHER MOUNTED. SWITCH WITH [3].");
            break;
        }
        break;
    case RK_CONSUMABLE:
        if (r->param == CON_REPAIR) { g_player.repairKits++;  Msg(RGBA(200, 255, 200, 255), "REPAIR KIT x%d", g_player.repairKits); }
        if (r->param == CON_O2CAP) { g_player.o2Caps++;      Msg(RGBA(200, 255, 200, 255), "O2 CAPSULE x%d", g_player.o2Caps); }
        if (r->param == CON_BUOY) { g_player.buoys++;       Msg(RGBA(200, 255, 200, 255), "OXYGEN BUOY x%d", g_player.buoys); }
        if (r->param == CON_TORPEDO) { g_player.torpedoAmmo++; Msg(RGBA(255, 200, 255, 255), "TORPEDO x%d", g_player.torpedoAmmo); }
        break;
    case RK_WARP:
        g_player.warpBuilt = true;
        g_logUnlocked[14] = true;
        Msg(RGBA(255, 160, 255, 255), "WARP DRIVE ASSEMBLED. ENGAGE FROM THE POD MENU.");
        break;
    }
    Play(g_sCraft, 1.0f);
}

static void UnlockLog(int id, bool announce)
{
    if (id < 0 || id >= LOG_COUNT || g_logUnlocked[id]) return;
    g_logUnlocked[id] = true;
    if (announce) Msg(RGBA(190, 190, 255, 255), "DATABANK: %s  [TAB]", g_log[id].title);
}

static void HurtPlayer(float dmg, const char* cause)
{
    if (g_player.invulT > 0.0f) return;

    g_player.hull -= dmg;
    g_player.hitFlash = 1.0f;
    g_player.invulT = 0.35f;
    g_redFlash = MinF(1.0f, g_redFlash + dmg / 55.0f);
    g_shake = MinF(22.0f, g_shake + dmg * 0.32f);

    Play(g_sHurt, RndRange(0.85f, 1.15f));
    Emit(g_player.pos, VMul(g_player.vel, 0.3f), RGBA(255, 120, 90, 255), 0.5f, 3.0f, 14, 140.0f, true);

    if (g_player.hull <= 0.0f) {
        g_player.hull = 0.0f;
        snprintf(g_deathCause, sizeof(g_deathCause), "%s", cause);
        g_deaths++;
        Play(g_sDeath, 1.0f);
        Emit(g_player.pos, V3(0, 0, 0), RGBA(255, 180, 120, 255), 1.4f, 6.0f, 80, 320.0f, true);
        SetState(ST_DEAD);
    }
}

static void RespawnAtPod()
{
    g_player.pos = V3(0.0f, 0.0f, 300.0f);
    g_player.vel = V3(0, 0, 0);
    g_player.hull = g_player.hullMax;
    g_player.o2 = g_player.o2Max;
    g_player.energy = g_player.energyMax;
    g_player.invulT = 2.0f;
    memset(g_player.inv, 0, sizeof(g_player.inv));
    g_redFlash = 0.0f;
    SetState(ST_PLAY);
    Msg(RGBA(255, 160, 160, 255), "SUIT REBOOTED AT POD. CARGO LOST.");
}

// =====================================================================================
//  РАЗДЕЛ 12. ОБНОВЛЕНИЕ: ИГРОК И КАМЕРА
// =====================================================================================

static void UpdatePlayer(float dt)
{
    Player* p = &g_player;

    // --- Обзор мышью. Крена нет: ориентация задаётся только рысканьем и тангажом,
    //     как в Subnautica. Это убирает укачивание и вырождение базиса. ---
    // Первые два кадра после захвата курсора GetMouseDelta выдаёт мусорный скачок,
    // поэтому ввод мыши игнорируется, пока состояние не "устоится".
    if (g_stateAge > 2) {
        Vector2 md = GetMouseDelta();
        const float sens = 0.0026f;
        p->yaw += md.x * sens;
        p->pitch -= md.y * sens;
        p->pitch = Clampf(p->pitch, -1.45f, 1.45f);
    }
    UpdatePlayerBasis();

    // --- Тяга во всех шести направлениях ---
    Vector3 dir = V3(0, 0, 0);
    if (IsKeyDown(KEY_W))            dir = VAdd(dir, p->fwd);
    if (IsKeyDown(KEY_S))            dir = VSub(dir, p->fwd);
    if (IsKeyDown(KEY_D))            dir = VAdd(dir, p->right);
    if (IsKeyDown(KEY_A))            dir = VSub(dir, p->right);
    if (IsKeyDown(KEY_SPACE))        dir = VAdd(dir, V3(0.0f, 1.0f, 0.0f));
    if (IsKeyDown(KEY_C) || IsKeyDown(KEY_LEFT_CONTROL)) dir = VSub(dir, V3(0.0f, 1.0f, 0.0f));

    bool wantBoost = IsKeyDown(KEY_LEFT_SHIFT) && p->energy > 1.0f;
    p->thrusting = (VLenSq(dir) > 0.01f);

    float accel = StatAccel();
    float maxSp = StatMaxSpeed();
    if (p->thrusting && wantBoost) {
        accel *= 2.0f;
        maxSp *= 1.85f;
        p->energy -= 22.0f * dt;
        p->boostT = 1.0f;
    }

    if (p->thrusting) {
        dir = VNorm(dir);
        p->vel = VAdd(p->vel, VMul(dir, accel * dt));

        Vector3 back = VAdd(p->pos, VMul(dir, -26.0f));
        Color flame = wantBoost ? RGBA(180, 220, 255, 255) : RGBA(255, 175, 90, 255);
        Emit(back, VAdd(VMul(p->vel, 0.25f), VMul(dir, -110.0f)), flame,
            0.35f, wantBoost ? 4.0f : 2.6f, wantBoost ? 3 : 2, 45.0f, true);
    }

    // Стабилизатор
    if (IsKeyDown(KEY_X)) {
        p->vel = VMul(p->vel, 1.0f / (1.0f + 4.5f * dt));
        p->energy -= 6.0f * dt;
    }

    // Лёгкое сопротивление — управление ближе к плаванию, чем к чистой Ньютоновой механике
    float drag = 0.75f;
    if (PlayerSector() == SEC_NEBULA) drag = 1.25f;
    p->vel = VMul(p->vel, 1.0f / (1.0f + drag * dt));

    float sp = VLen(p->vel);
    if (sp > maxSp) p->vel = VMul(p->vel, maxSp / sp);

    p->pos = VAdd(p->pos, VMul(p->vel, dt));

    // --- Границы мира ---
    float dist = VLen(p->pos);
    if (dist > WORLD_RADIUS) {
        p->vel = VAdd(p->vel, VMul(VNorm(p->pos), -1100.0f * dt));
        if (dist > WORLD_RADIUS + 400.0f) HurtPlayer(26.0f * dt, "LOST OUTSIDE THE FIELD");
    }
    float ceiling = DISK_HALF + 900.0f;
    if (fabsf(p->pos.y) > ceiling) {
        p->vel.y -= (p->pos.y > 0.0f ? 1.0f : -1.0f) * 900.0f * dt;
        if (fabsf(p->pos.y) > ceiling + 500.0f) HurtPlayer(20.0f * dt, "DRIFTED OUT OF THE DEBRIS PLANE");
    }

    if (dist > g_deepest) g_deepest = dist;

    // --- Жизнеобеспечение ---
    float o2Rate = 1.0f;
    if (p->thrusting) o2Rate += 0.4f;
    if (wantBoost)    o2Rate += 0.5f;

    bool nearPod = (dist < DOCK_RADIUS);
    bool nearBuoy = false;
    for (int i = 0; i < MAX_BUOYS; i++) {
        if (!g_buoy[i].alive) continue;
        if (VDist(p->pos, g_buoy[i].pos) < 420.0f && g_buoy[i].charge > 0.0f) {
            nearBuoy = true;
            g_buoy[i].charge -= 18.0f * dt;
            if (g_buoy[i].charge <= 0.0f) {
                g_buoy[i].alive = false;
                Msg(RGBA(255, 190, 120, 255), "A BUOY RAN DRY.");
            }
            break;
        }
    }

    if (nearPod || nearBuoy) p->o2 = Approach(p->o2, p->o2Max, 42.0f, dt);
    else                     p->o2 -= o2Rate * dt;

    if (nearPod) {
        p->energy = Approach(p->energy, p->energyMax, 30.0f, dt);
        p->hull = Approach(p->hull, p->hullMax, 6.0f, dt);
    }
    else {
        p->energy = Approach(p->energy, p->energyMax,
            StatEnergyRegen() * (p->thrusting ? 0.55f : 1.0f), dt);
    }
    p->energy = Clampf(p->energy, 0.0f, p->energyMax);

    if (p->o2 <= 0.0f) {
        p->o2 = 0.0f;
        HurtPlayer(13.0f * dt, "ASPHYXIATION");
        g_redFlash = MaxF(g_redFlash, 0.30f);
    }

    if (PlayerSector() == SEC_CORE && !p->hasRadSuit) HurtPlayer(11.0f * dt, "RADIATION POISONING");

    if (p->invulT > 0.0f) p->invulT -= dt;
    if (p->hitFlash > 0.0f) p->hitFlash -= dt * 3.0f;
    if (p->boostT > 0.0f) p->boostT -= dt * 2.0f;

    // --- Расходники (ввод только здесь, в фазе обновления) ---
    if (IsKeyPressed(KEY_F) && p->repairKits > 0 && p->hull < p->hullMax) {
        p->repairKits--;
        p->hull = MinF(p->hullMax, p->hull + 45.0f);
        Play(g_sCraft, 1.3f);
        Emit(p->pos, V3(0, 0, 0), RGBA(140, 255, 170, 255), 0.7f, 3.0f, 24, 120.0f, true);
        Msg(RGBA(150, 255, 170, 255), "HULL PATCHED. KITS LEFT: %d", p->repairKits);
    }
    if (IsKeyPressed(KEY_Q) && p->o2Caps > 0) {
        p->o2Caps--;
        p->o2 = MinF(p->o2Max, p->o2 + 55.0f);
        Play(g_sPick, 0.8f);
        Emit(p->pos, V3(0, 0, 0), RGBA(140, 220, 255, 255), 0.7f, 3.0f, 24, 120.0f, true);
        Msg(RGBA(150, 220, 255, 255), "O2 TOPPED UP. CAPSULES LEFT: %d", p->o2Caps);
    }
    if (IsKeyPressed(KEY_B) && p->buoys > 0) {
        for (int i = 0; i < MAX_BUOYS; i++) {
            if (g_buoy[i].alive) continue;
            g_buoy[i].alive = true;
            g_buoy[i].pos = p->pos;
            g_buoy[i].charge = 100.0f;
            p->buoys--;
            Play(g_sDock, 1.4f);
            Msg(RGBA(150, 220, 255, 255), "BUOY DEPLOYED. BUOYS LEFT: %d", p->buoys);
            break;
        }
    }
    if (IsKeyPressed(KEY_V)) g_thirdPerson = !g_thirdPerson;

    // --- Стыковка ---
    if (nearPod && IsKeyPressed(KEY_E)) {
        DepositAll();
        Play(g_sDock, 1.0f);
        SetState(ST_BASE);
    }
}

static void UpdateCamera3D(float dt)
{
    Player* p = &g_player;

    Vector3 wantPos, wantTarget;
    if (g_thirdPerson) {
        wantPos = VAdd(VAdd(p->pos, VMul(p->fwd, -105.0f)), VMul(p->up, 32.0f));
        wantTarget = VAdd(p->pos, VMul(p->fwd, 60.0f));
    }
    else {
        wantPos = VAdd(p->pos, VMul(p->up, 4.0f));
        wantTarget = VAdd(p->pos, VMul(p->fwd, 100.0f));
    }

    float k = MinF(1.0f, dt * (g_thirdPerson ? 9.0f : 26.0f));
    g_cam.position = VAdd(g_cam.position, VMul(VSub(wantPos, g_cam.position), k));
    g_cam.target = VAdd(g_cam.target, VMul(VSub(wantTarget, g_cam.target), k));
    g_cam.up = p->up;

    // Тряска: смещаем и позицию, и цель, иначе камера "косит"
    if (g_shake > 0.1f) {
        Vector3 j = VMul(RndDir(), g_shake * 0.55f);
        g_cam.position = VAdd(g_cam.position, j);
        g_cam.target = VAdd(g_cam.target, j);
        g_shake *= powf(0.05f, dt);
    }

    g_camFwd = VNorm(VSub(g_cam.target, g_cam.position));
    g_projScale = (SCREEN_H * 0.5f) / tanf(g_cam.fovy * 0.5f * DEG2RAD);

    if (g_redFlash > 0.0f) g_redFlash -= dt * 1.4f;
}

// =====================================================================================
//  РАЗДЕЛ 13. ОБНОВЛЕНИЕ: ЛАЗЕР И СКАНЕР (лучевая трассировка)
// =====================================================================================

static Vector3 BeamOrigin()
{
    // В виде от первого лица луч стартует чуть ниже и правее, чтобы его было видно.
    Player* p = &g_player;
    if (g_thirdPerson) return VAdd(p->pos, VMul(p->fwd, 26.0f));
    return VAdd(VAdd(p->pos, VMul(p->fwd, 16.0f)), VAdd(VMul(p->right, 11.0f), VMul(p->up, -9.0f)));
}

// Разбивает астероид, если урон свалил его в ноль; общая точка для лазера и
// для взрыва торпеды, чтобы дроп ресурсов и эффекты не дублировались в двух местах.
static void DamageAsteroid(int idx, float dmg, Vector3 pushDir)
{
    Asteroid* a = &g_ast[idx];
    if (!a->alive) return;

    a->hp -= dmg;
    a->hitFlash = 1.0f;
    a->vel = VAdd(a->vel, VMul(pushDir, 6.0f));

    if (a->hp > 0.0f) return;

    a->alive = false;
    a->respawn = RndRange(35.0f, 80.0f);
    g_mined++;
    Play(g_sBreak, RndRange(0.8f, 1.1f));
    Emit(a->pos, V3(0, 0, 0), a->col, 0.9f, 4.0f, 28, 200.0f, false);
    Emit(a->pos, V3(0, 0, 0), g_resColor[a->res], 0.8f, 3.0f, 16, 160.0f, true);
    g_shake = MinF(12.0f, g_shake + 3.0f);

    for (int k = 0; k < a->amount; k++) {
        for (int s = 0; s < MAX_PICKUPS; s++) {
            if (g_pick[s].alive) continue;
            g_pick[s].alive = true;
            g_pick[s].pos = VAdd(a->pos, VMul(RndDir(), a->radius * 0.5f));
            g_pick[s].vel = VMul(RndDir(), RndRange(30.0f, 100.0f));
            g_pick[s].res = a->res;
            g_pick[s].amount = 1;
            g_pick[s].life = 120.0f;
            g_pick[s].spin = RndRange(0.0f, 6.28f);
            break;
        }
    }
}

// Общая точка нанесения урона существу для всех трёх видов оружия (лазер,
// бластер, торпеда). kick — сила отталкивания одним вызовом: маленькая для
// непрерывного лазера (масштабируется на dt в месте вызова), заметно больше
// для разового попадания снаряда.
static void DamageCreature(int idx, float dmg, float kick, Vector3 pushDir)
{
    Creature* c = &g_cre[idx];
    if (!c->alive) return;

    c->hp -= dmg;
    c->hitFlash = 1.0f;
    if (c->state == 0 && c->kind != CR_DRIFTER) { c->state = 1; c->stateT = 6.0f; }
    if (kick > 0.0f) c->vel = VAdd(c->vel, VMul(pushDir, kick));

    if (c->hp > 0.0f) return;

    c->alive = false;
    c->respawn = RndRange(45.0f, 95.0f);
    g_killed++;
    Play(g_sBreak, 0.7f);
    Emit(c->pos, V3(0, 0, 0), RGBA(160, 255, 190, 255), 1.0f, 4.0f, 32, 190.0f, true);

    int drops = 1;
    ResType dropRes = RES_ORGANIC;
    if (c->kind == CR_DRONE) { drops = 2;  dropRes = RES_SILICON; }
    if (c->kind == CR_STALKER) { drops = 3;  dropRes = RES_ORGANIC; }
    if (c->kind == CR_SWARMER) { drops = 1;  dropRes = RES_ORGANIC; }
    if (c->kind == CR_LEVIATHAN) { drops = 10; dropRes = RES_PLASMA; g_shake = 26.0f; }

    for (int k = 0; k < drops; k++) {
        for (int s = 0; s < MAX_PICKUPS; s++) {
            if (g_pick[s].alive) continue;
            g_pick[s].alive = true;
            g_pick[s].pos = c->pos;
            g_pick[s].vel = VMul(RndDir(), RndRange(50.0f, 150.0f));
            g_pick[s].res = (c->kind == CR_LEVIATHAN && (k % 3 == 0)) ? RES_ALLOY : dropRes;
            g_pick[s].amount = 1;
            g_pick[s].life = 120.0f;
            g_pick[s].spin = RndRange(0.0f, 6.28f);
            break;
        }
    }
    if (c->kind == CR_LEVIATHAN)
        Msg(RGBA(255, 180, 120, 255), "THE VOID MAW IS DEAD. THE TRENCH IS QUIETER.");
}

// Взрыв торпеды: урон по площади существам И астероидам (хорошо чистит завалы),
// плюс общий визуальный/звуковой отклик. Вызывается и при прямом попадании, и
// когда торпеда просто долетела до конца дальности, не встретив цель.
static void ExplodeTorpedo(Vector3 pos)
{
    float dmg = StatTorpedoSplash();
    float rad = StatTorpedoSplashRadius();

    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];
        if (!c->alive) continue;
        float d = VDist(c->pos, pos);
        if (d > rad) continue;
        DamageCreature(i, dmg * (1.0f - d / rad), 70.0f, VNorm(VSub(c->pos, pos)));
    }
    for (int i = 0; i < g_nAst; i++) {
        Asteroid* a = &g_ast[i];
        if (!a->alive) continue;
        float d = VDist(a->pos, pos);
        if (d > rad) continue;
        DamageAsteroid(i, dmg * (1.0f - d / rad) * 0.8f, VNorm(VSub(a->pos, pos)));
    }

    Emit(pos, V3(0, 0, 0), RGBA(255, 170, 120, 255), 1.0f, 6.0f, 46, 260.0f, true);
    Emit(pos, V3(0, 0, 0), RGBA(255, 255, 255, 255), 0.4f, 4.0f, 20, 200.0f, true);
    g_shake = MinF(24.0f, g_shake + 14.0f);
    Play(g_sBreak, 0.55f);
}

// Единая функция ввода/логики для всех видов вооружения. Переключение оружия
// читает клавиатуру только здесь — как и всё остальное управление в игре.
static void UpdateWeapons(float dt)
{
    Player* p = &g_player;

    if (IsKeyPressed(KEY_ONE)) {
        p->weapon = WPN_LASER;
        Msg(RGBA(255, 200, 140, 255), "MINING LASER EQUIPPED");
    }
    if (IsKeyPressed(KEY_TWO)) {
        if (p->hasBlaster) { p->weapon = WPN_BLASTER; Msg(RGBA(150, 220, 255, 255), "PULSE CANNON EQUIPPED"); }
        else Msg(RGBA(255, 150, 150, 255), "PULSE CANNON NOT BUILT - FABRICATE IT AT THE POD");
    }
    if (IsKeyPressed(KEY_THREE)) {
        if (p->hasTorpedo) { p->weapon = WPN_TORPEDO; Msg(RGBA(255, 150, 255, 255), "TORPEDO LAUNCHER EQUIPPED"); }
        else Msg(RGBA(255, 150, 150, 255), "TORPEDO LAUNCHER NOT BUILT - FABRICATE IT AT THE POD");
    }

    if (p->blasterCd > 0.0f) p->blasterCd -= dt;
    if (p->torpedoCd > 0.0f) p->torpedoCd -= dt;

    g_beamOn = false;
    g_beamHit = false;

    Vector3 start = BeamOrigin();
    Vector3 dir = p->fwd;

    // --- [1] Лазер: непрерывный луч, добывает руду, слабо ранит живое ---
    if (p->weapon == WPN_LASER) {
        float range = StatLaserRange();
        g_beamEnd = VAdd(start, VMul(dir, range));

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || p->energy <= 0.5f) return;

        g_beamOn = true;
        p->energy -= 15.0f * dt;

        float bestT = range;
        int   bestAst = -1, bestCre = -1;

        for (int i = 0; i < g_nAst; i++) {
            Asteroid* a = &g_ast[i];
            if (!a->alive) continue;
            if (VDistSq(a->pos, p->pos) > (range + a->radius) * (range + a->radius)) continue;
            float t;
            if (RaySphere(start, dir, a->pos, a->radius, &t) && t < bestT) { bestT = t; bestAst = i; bestCre = -1; }
        }
        for (int i = 0; i < g_nCre; i++) {
            Creature* c = &g_cre[i];
            if (!c->alive) continue;
            if (VDistSq(c->pos, p->pos) > (range + c->radius) * (range + c->radius)) continue;
            float t;
            if (RaySphere(start, dir, c->pos, c->radius, &t) && t < bestT) { bestT = t; bestCre = i; bestAst = -1; }
        }

        if (bestAst < 0 && bestCre < 0) return;

        Vector3 hit = VAdd(start, VMul(dir, bestT));
        g_beamEnd = hit;
        g_beamHit = true;

        if (bestAst >= 0) {
            Emit(hit, VMul(dir, -40.0f), g_resColor[g_ast[bestAst].res], 0.35f, 2.2f, 2, 110.0f, true);
            if (RndChance(0.35f)) Play(g_sMine, RndRange(0.9f, 1.25f));
            DamageAsteroid(bestAst, StatLaserDps() * dt, dir);
        }
        else {
            Emit(hit, VMul(dir, -30.0f), RGBA(255, 130, 160, 255), 0.35f, 2.2f, 2, 110.0f, true);
            if (RndChance(0.3f)) Play(g_sMine, RndRange(1.2f, 1.5f));
            DamageCreature(bestCre, StatLaserCreatureDps() * dt, 18.0f * dt, dir);
        }
        return;
    }

    // --- [2] Пульсовая пушка: полуавтомат по кулдауну, тратит энергию за выстрел ---
    if (p->weapon == WPN_BLASTER) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
        if (p->blasterCd > 0.0f || p->energy < StatBlasterEnergyCost()) return;

        p->blasterCd = StatBlasterFireDelay();
        p->energy -= StatBlasterEnergyCost();

        for (int s = 0; s < MAX_PROJECTILES; s++) {
            if (g_proj[s].alive) continue;
            g_proj[s].alive = true;
            g_proj[s].pos = start;
            g_proj[s].vel = VMul(dir, StatBlasterSpeed());
            g_proj[s].life = 1.6f;
            g_proj[s].dmg = StatBlasterDamage();
            g_proj[s].radius = 5.0f;
            g_proj[s].col = RGBA(150, 225, 255, 255);
            g_proj[s].owner = OWNER_PLAYER;
            g_proj[s].kind = PK_BLASTER;
            g_proj[s].splash = 0.0f;
            g_proj[s].homeTarget = -1;
            break;
        }
        Emit(start, VMul(dir, -60.0f), RGBA(170, 225, 255, 255), 0.2f, 2.4f, 4, 40.0f, true);
        Play(g_sShoot, RndRange(1.1f, 1.3f));
        return;
    }

    // --- [3] Торпеда: одиночный клик, ограниченный боезапас, самонаведение по конусу впереди ---
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (p->torpedoCd > 0.0f) return;
    if (p->torpedoAmmo <= 0) { Msg(RGBA(255, 160, 140, 255), "OUT OF TORPEDOES - CRAFT MORE AT THE POD"); return; }

    p->torpedoCd = StatTorpedoCooldown();
    p->torpedoAmmo--;

    int   bestIdx = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];
        if (!c->alive) continue;
        Vector3 toC = VSub(c->pos, p->pos);
        float   d = VLen(toC);
        if (d < 1.0f || d > 1500.0f) continue;
        float align = VDot(VNorm(toC), dir);
        if (align < 0.6f) continue;   // примерно 53° конус захвата
        float score = d - align * 400.0f;
        if (score < bestScore) { bestScore = score; bestIdx = i; }
    }

    for (int s = 0; s < MAX_PROJECTILES; s++) {
        if (g_proj[s].alive) continue;
        g_proj[s].alive = true;
        g_proj[s].pos = start;
        g_proj[s].vel = VMul(dir, StatTorpedoSpeed());
        g_proj[s].life = 5.0f;
        g_proj[s].dmg = StatTorpedoDamage();
        g_proj[s].radius = 9.0f;
        g_proj[s].col = RGBA(255, 150, 255, 255);
        g_proj[s].owner = OWNER_PLAYER;
        g_proj[s].kind = PK_TORPEDO;
        g_proj[s].splash = StatTorpedoSplashRadius();
        g_proj[s].homeTarget = bestIdx;
        break;
    }
    Emit(start, VMul(dir, -80.0f), RGBA(255, 190, 150, 255), 0.4f, 4.0f, 10, 70.0f, true);
    Play(g_sShoot, 0.6f);
    Msg(RGBA(255, 180, 255, 255), bestIdx >= 0 ? "TORPEDO LOCKED ON" : "TORPEDO AWAY - NO LOCK");
}

static void UpdateScanner(float dt)
{
    g_scanActive = false;
    if (!g_player.hasScanner) return;
    if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || g_player.energy < 0.5f) return;

    float   range = StatLaserRange() * 1.6f;
    Vector3 start = g_player.pos;
    Vector3 dir = g_player.fwd;

    int   bestCre = -1, bestSite = -1;
    float bestT = range;

    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];
        if (!c->alive || g_logUnlocked[(int)c->kind]) continue;
        float t;
        // радиус захвата щедрее физического — целиться в рыбу в 3D и так непросто
        if (RaySphere(start, dir, c->pos, c->radius * 2.0f, &t) && t < bestT) {
            bestT = t; bestCre = i; bestSite = -1;
        }
    }
    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        if (s->used || s->kind != SITE_BEACON) continue;
        float t;
        if (RaySphere(start, dir, s->pos, s->radius * 2.2f, &t) && t < bestT) {
            bestT = t; bestSite = i; bestCre = -1;
        }
    }

    if (bestCre < 0 && bestSite < 0) return;

    g_player.energy -= 6.0f * dt;
    g_scanActive = true;

    if (bestCre >= 0) {
        Creature* c = &g_cre[bestCre];
        g_scanTarget = c->pos;
        c->scanProg += dt * 0.45f;
        if (c->scanProg >= 1.0f) {
            UnlockLog((int)c->kind, true);
            Play(g_sScan, 1.0f);
            c->scanProg = 0.0f;
        }
    }
    else {
        Site* s = &g_site[bestSite];
        g_scanTarget = s->pos;
        s->scanProg += dt * 0.5f;
        if (s->scanProg >= 1.0f) {
            s->used = true;
            UnlockLog(s->payload, true);
            Play(g_sScan, 1.0f);
        }
    }
}

// =====================================================================================
//  РАЗДЕЛ 14. ОБНОВЛЕНИЕ: МИР
// =====================================================================================

static void UpdateAsteroids(float dt)
{
    for (int i = 0; i < g_nAst; i++) {
        Asteroid* a = &g_ast[i];

        if (!a->alive) {
            a->respawn -= dt;
            if (a->respawn <= 0.0f) {
                int sec = a->sector;
                SpawnAsteroid(a, sec);
                if (VDist(a->pos, g_player.pos) < 800.0f) { a->alive = false; a->respawn = 6.0f; }
            }
            continue;
        }

        float dsq = VDistSq(a->pos, g_player.pos);
        if (dsq > DRAW_DIST * DRAW_DIST) continue;   // дальние камни спят

        a->pos = VAdd(a->pos, VMul(a->vel, dt));
        if (a->hitFlash > 0.0f) a->hitFlash -= dt * 4.0f;

        float rr = a->radius + PLAYER_RADIUS;
        if (dsq < rr * rr) {
            Vector3 n = VNorm(VSub(g_player.pos, a->pos));
            g_player.pos = VAdd(a->pos, VMul(n, rr));

            float impact = VLen(VSub(g_player.vel, a->vel));
            float along = VDot(g_player.vel, n);
            if (along < 0.0f) g_player.vel = VSub(g_player.vel, VMul(n, along * 1.6f));

            if (impact > 220.0f) {
                HurtPlayer(impact * 0.030f, "HULL BREACH ON IMPACT");
                Emit(g_player.pos, V3(0, 0, 0), RGBA(255, 200, 120, 255), 0.5f, 3.0f, 12, 130.0f, true);
            }
        }
    }
}

static void UpdateCreatures(float dt)
{
    Player* p = &g_player;

    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];

        if (!c->alive) {
            c->respawn -= dt;
            if (c->respawn <= 0.0f) {
                SetupCreature(c, c->kind, c->homeSector);
                if (VDist(c->pos, p->pos) < 1200.0f) { c->alive = false; c->respawn = 10.0f; }
            }
            continue;
        }

        float distToPlayer = VDist(c->pos, p->pos);
        if (distToPlayer > 3000.0f) continue;

        c->phase += dt * 2.0f;
        if (c->hitFlash > 0.0f) c->hitFlash -= dt * 4.0f;
        if (c->attackCd > 0.0f) c->attackCd -= dt;

        // Хищники охотятся на звук: без тяги радиус обнаружения падает вдвое
        float noise = p->thrusting ? 1.0f : 0.45f;
        float aggro = 0.0f;
        switch (c->kind) {
        case CR_SNAPPER:   aggro = 640.0f * noise; break;
        case CR_STALKER:   aggro = 1050.0f * noise; break;
        case CR_DRONE:     aggro = 820.0f;          break;
        case CR_LEVIATHAN: aggro = 1600.0f * noise; break;
        case CR_SWARMER:   aggro = 560.0f * noise; break;
        default:           aggro = 0.0f;            break;
        }

        c->stateT -= dt;
        if (c->kind != CR_DRIFTER) {
            if (distToPlayer < aggro) {
                if (c->state == 0) {
                    c->state = 1;
                    c->stateT = 8.0f;
                    if (c->kind == CR_LEVIATHAN) {
                        PlayAt(g_sRoar, 1.0f, c->pos, 2400.0f);
                        g_shake = MaxF(g_shake, 10.0f * Clampf(1.0f - distToPlayer / 1600.0f, 0.0f, 1.0f));
                    }
                }
            }
            else if (c->state == 1 && c->stateT <= 0.0f) {
                c->state = 0; c->stateT = RndRange(2.0f, 5.0f);
            }
            if ((c->kind == CR_SNAPPER || c->kind == CR_DRONE || c->kind == CR_SWARMER) && c->hp < c->maxHp * 0.3f) {
                c->state = 2; c->stateT = 4.0f;
            }
            if (c->state == 2 && c->stateT <= 0.0f) c->state = 0;
        }

        Vector3 desired = V3(0, 0, 0);

        if (c->kind == CR_SWARMER) {
            // Стайное поведение: разделение (не толкаться), выравнивание (лететь
            // в одну сторону с соседями) и сплочение (тянуться к центру стаи),
            // плюс общая цель поверх этого — жертва в погоне или блуждание.
            // Дёшево: внутренний цикл считается только для сородичей-Swarmer'ов
            // и только пока сам Swarmer уже прошёл фильтр "рядом с игроком" выше.
            Vector3 sep = V3(0, 0, 0), align = V3(0, 0, 0), coh = V3(0, 0, 0);
            int neighbors = 0;
            for (int j = 0; j < g_nCre; j++) {
                if (j == i) continue;
                Creature* o = &g_cre[j];
                if (!o->alive || o->kind != CR_SWARMER) continue;
                float d = VDist(o->pos, c->pos);
                if (d > 260.0f || d < 0.01f) continue;
                neighbors++;
                if (d < 70.0f) sep = VAdd(sep, VMul(VSub(c->pos, o->pos), 1.0f / d));
                align = VAdd(align, o->fwd);
                coh = VAdd(coh, o->pos);
            }

            Vector3 flock = V3(0, 0, 0);
            if (neighbors > 0) {
                align = VNorm(align);
                coh = VNorm(VSub(VMul(coh, 1.0f / (float)neighbors), c->pos));
                sep = VNorm(sep);
                flock = VNorm(VAdd(VAdd(VMul(sep, 1.6f), VMul(align, 0.8f)), VMul(coh, 0.6f)));
            }

            Vector3 goal;
            if (c->state == 1)      goal = VNorm(VSub(p->pos, c->pos));
            else if (c->state == 2) goal = VNorm(VSub(c->pos, p->pos));
            else {
                if (VDist(c->pos, c->wander) < 200.0f || c->stateT <= 0.0f) {
                    c->wander = RandomPointInSector(c->homeSector);
                    c->stateT = RndRange(5.0f, 10.0f);
                }
                goal = VNorm(VSub(c->wander, c->pos));
            }

            desired = VNorm(VAdd(VMul(flock, neighbors > 0 ? 0.65f : 0.0f), VMul(goal, 0.6f)));
        }
        else if (c->state == 1) {
            if (c->kind == CR_DRONE) {
                float want = 460.0f;
                Vector3 toP = VNorm(VSub(p->pos, c->pos));
                if (distToPlayer > want + 60.0f)      desired = toP;
                else if (distToPlayer < want - 60.0f) desired = VMul(toP, -1.0f);
                else {
                    Vector3 r, u;
                    BasisFromForward(toP, &r, &u);
                    desired = r;
                }

                if (c->attackCd <= 0.0f && distToPlayer < 780.0f) {
                    c->attackCd = 1.7f;
                    for (int s = 0; s < MAX_PROJECTILES; s++) {
                        if (g_proj[s].alive) continue;
                        g_proj[s].alive = true;
                        g_proj[s].pos = c->pos;
                        g_proj[s].vel = VMul(toP, 620.0f);
                        g_proj[s].life = 2.4f;
                        g_proj[s].dmg = 12.0f;
                        g_proj[s].radius = 6.0f;
                        g_proj[s].col = RGBA(255, 120, 90, 255);
                        g_proj[s].owner = OWNER_DRONE;
                        g_proj[s].kind = PK_DRONE_SHOT;
                        g_proj[s].splash = 0.0f;
                        g_proj[s].homeTarget = -1;
                        PlayAt(g_sShoot, RndRange(0.9f, 1.2f), c->pos, 1400.0f);
                        break;
                    }
                }
            }
            else {
                desired = VNorm(VSub(p->pos, c->pos));
            }
        }
        else if (c->state == 2) {
            desired = VNorm(VSub(c->pos, p->pos));
        }
        else {
            if (VDist(c->pos, c->wander) < 140.0f || c->stateT <= 0.0f) {
                c->wander = RandomPointInSector(c->homeSector);
                c->stateT = RndRange(4.0f, 9.0f);
            }
            desired = VNorm(VSub(c->wander, c->pos));
            if (c->kind == CR_DRIFTER)
                desired = VAdd(desired, VMul(V3(sinf(c->phase * 0.4f), cosf(c->phase * 0.31f),
                    sinf(c->phase * 0.23f)), 0.35f));
        }

        // Swarmer всегда в движении, даже в "школьном" режиме — рыбам не идёт стоять на месте.
        float idleMul = (c->kind == CR_SWARMER) ? 0.75f : 0.45f;
        float sp = c->speed * ((c->state == 1) ? 1.0f : idleMul);
        c->vel = VAdd(c->vel, VMul(desired, sp * 3.0f * dt));
        c->vel = VMul(c->vel, 1.0f / (1.0f + 1.8f * dt));

        float cs = VLen(c->vel);
        if (cs > sp) c->vel = VMul(c->vel, sp / cs);

        c->pos = VAdd(c->pos, VMul(c->vel, dt));
        if (cs > 4.0f) c->fwd = VNorm(VAdd(c->fwd, VMul(VSub(VNorm(c->vel), c->fwd), MinF(1.0f, dt * 5.0f))));

        if (c->kind == CR_LEVIATHAN && RndChance(0.5f))
            Emit(VAdd(c->pos, VMul(c->fwd, -c->radius * 0.9f)), V3(0, 0, 0),
                RGBA(120, 60, 110, 180), 1.2f, 6.0f, 1, 30.0f, true);
        if (c->kind == CR_STALKER && c->state == 1 && RndChance(0.4f))
            Emit(c->pos, V3(0, 0, 0), RGBA(200, 80, 120, 160), 0.5f, 3.0f, 1, 22.0f, true);

        if (c->damage > 0.0f && c->attackCd <= 0.0f) {
            float touch = c->radius + PLAYER_RADIUS + (c->kind == CR_LEVIATHAN ? 55.0f : 0.0f);
            bool  inRange = (distToPlayer < touch);

            if (c->kind == CR_LEVIATHAN) {
                // Заметный замах перед укусом (~0.5с): подсветка пасти нарастает,
                // давая шанс уйти с линии атаки до того, как урон реально применится.
                if (inRange) c->bitePrep = MinF(1.0f, c->bitePrep + dt / 0.50f);
                else         c->bitePrep = MaxF(0.0f, c->bitePrep - dt * 2.2f);

                if (c->bitePrep >= 1.0f) {
                    HurtPlayer(c->damage, "EATEN BY THE VOID MAW");
                    c->attackCd = 2.2f;
                    c->bitePrep = 0.0f;
                    Vector3 push = VNorm(VSub(p->pos, c->pos));
                    p->vel = VAdd(p->vel, VMul(push, 760.0f));
                }
            }
            else if (inRange) {
                HurtPlayer(c->damage,
                    (c->kind == CR_STALKER) ? "TORN APART BY A STALKER" :
                    (c->kind == CR_SWARMER) ? "SWARMED AND BITTEN APART" :
                    "BITTEN TO DEATH");
                c->attackCd = 1.1f;
                Vector3 push = VNorm(VSub(p->pos, c->pos));
                p->vel = VAdd(p->vel, VMul(push, 320.0f));
            }
        }
        else if (c->kind == CR_LEVIATHAN && c->bitePrep > 0.0f) {
            // Кулдаун после укуса — сбрасываем замах, чтобы он не "телепортировался"
            // в 100% на следующем же кадре, едва игрок опять окажется в радиусе.
            c->bitePrep = MaxF(0.0f, c->bitePrep - dt * 2.2f);
        }
    }
}

static void UpdateProjectiles(float dt)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* pr = &g_proj[i];
        if (!pr->alive) continue;

        // Самонаведение торпеды: доворачиваем вектор скорости к цели с ограниченной
        // скоростью поворота, не меняя саму скорость полёта.
        if (pr->kind == PK_TORPEDO && pr->homeTarget >= 0) {
            Creature* tgt = &g_cre[pr->homeTarget];
            if (!tgt->alive) {
                pr->homeTarget = -1;
            }
            else {
                Vector3 want = VNorm(VSub(tgt->pos, pr->pos));
                Vector3 cur = VNorm(pr->vel);
                float   speed = VLen(pr->vel);
                Vector3 next = VNorm(VAdd(cur, VMul(VSub(want, cur), MinF(1.0f, StatTorpedoTurnRate() * dt))));
                pr->vel = VMul(next, speed);
            }
        }

        pr->pos = VAdd(pr->pos, VMul(pr->vel, dt));
        pr->life -= dt;

        Color trailCol = (pr->kind == PK_TORPEDO) ? RGBA(255, 190, 150, 255) :
            (pr->kind == PK_BLASTER) ? RGBA(170, 225, 255, 255) : pr->col;
        Emit(pr->pos, V3(0, 0, 0), trailCol, 0.22f, 1.6f, 1, 6.0f, true);

        if (pr->life <= 0.0f) {
            pr->alive = false;
            if (pr->kind == PK_TORPEDO) ExplodeTorpedo(pr->pos);   // самоподрыв на излёте
            continue;
        }

        if (pr->owner == OWNER_DRONE) {
            if (VDist(pr->pos, g_player.pos) < pr->radius + PLAYER_RADIUS) {
                pr->alive = false;
                HurtPlayer(pr->dmg, "SHOT DOWN BY A SENTRY DRONE");
            }
            continue;
        }

        // OWNER_PLAYER: бьёт по существам; торпеда вдобавок подрывается и на астероидах.
        bool hitSomething = false;
        for (int ci = 0; ci < g_nCre && !hitSomething; ci++) {
            Creature* cr = &g_cre[ci];
            if (!cr->alive) continue;
            if (VDist(pr->pos, cr->pos) > pr->radius + cr->radius) continue;

            pr->alive = false;
            if (pr->kind == PK_TORPEDO) {
                DamageCreature(ci, pr->dmg, 90.0f, VNorm(pr->vel));
                ExplodeTorpedo(pr->pos);
            }
            else {
                DamageCreature(ci, pr->dmg, 40.0f, VNorm(pr->vel));
                Emit(pr->pos, V3(0, 0, 0), RGBA(255, 130, 160, 255), 0.3f, 2.0f, 8, 60.0f, true);
            }
            hitSomething = true;
        }
        if (hitSomething) continue;

        for (int ai = 0; ai < g_nAst && pr->alive; ai++) {
            Asteroid* ast = &g_ast[ai];
            if (!ast->alive) continue;
            if (VDist(pr->pos, ast->pos) > pr->radius + ast->radius) continue;

            pr->alive = false;
            if (pr->kind == PK_TORPEDO) ExplodeTorpedo(pr->pos);
            else Emit(pr->pos, V3(0, 0, 0), RGBA(200, 210, 225, 255), 0.3f, 2.0f, 6, 50.0f, true);
        }
    }
}

static void UpdatePickups(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup* k = &g_pick[i];
        if (!k->alive) continue;

        k->life -= dt;
        k->spin += dt * 2.0f;
        if (k->life <= 0.0f) { k->alive = false; continue; }

        float d = VDist(k->pos, g_player.pos);
        if (d < PICKUP_MAGNET) {
            Vector3 pull = VNorm(VSub(g_player.pos, k->pos));
            k->vel = VAdd(k->vel, VMul(pull, 760.0f * dt * (1.0f - d / PICKUP_MAGNET + 0.3f)));
        }

        k->vel = VMul(k->vel, 1.0f / (1.0f + 1.2f * dt));
        k->pos = VAdd(k->pos, VMul(k->vel, dt));

        if (d < 30.0f) {
            if (AddToInv(k->res, k->amount)) {
                k->alive = false;
                Play(g_sPick, RndRange(1.0f, 1.35f));
                Emit(k->pos, V3(0, 0, 0), g_resColor[k->res], 0.35f, 2.0f, 6, 70.0f, true);
            }
            else {
                k->vel = VMul(VNorm(VSub(k->pos, g_player.pos)), 180.0f);
                if (RndChance(0.02f)) Msg(RGBA(255, 180, 120, 255), "CARGO HOLD FULL. RETURN TO THE POD.");
            }
        }
    }
}

static void UpdateSites(float dt)
{
    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        s->spin += dt * 0.7f;
        if (s->used && s->kind != SITE_BEACON) continue;

        float d = VDist(s->pos, g_player.pos);
        if (d > s->radius + 46.0f) continue;

        if (s->kind == SITE_CACHE && !s->used) {
            s->used = true;
            int n = RndInt(3, 6);
            for (int k = 0; k < n; k++) {
                for (int q = 0; q < MAX_PICKUPS; q++) {
                    if (g_pick[q].alive) continue;
                    g_pick[q].alive = true;
                    g_pick[q].pos = s->pos;
                    g_pick[q].vel = VMul(RndDir(), RndRange(50.0f, 160.0f));
                    g_pick[q].res = RollResource(s->sector);
                    g_pick[q].amount = 1;
                    g_pick[q].life = 120.0f;
                    g_pick[q].spin = RndRange(0.0f, 6.28f);
                    break;
                }
            }
            Play(g_sPick, 0.7f);
            Emit(s->pos, V3(0, 0, 0), RGBA(255, 220, 140, 255), 0.8f, 3.0f, 22, 160.0f, true);
            Msg(RGBA(255, 220, 140, 255), "SUPPLY CACHE CRACKED OPEN.");
        }
        else if (s->kind == SITE_FRAGMENT && !s->used) {
            s->used = true;
            g_player.warpPart[s->payload] = true;
            const char* names[3] = { "WARP COIL", "CONTAINMENT CELL", "IGNITION CORE" };
            Play(g_sCraft, 0.8f);
            Emit(s->pos, V3(0, 0, 0), RGBA(255, 150, 255, 255), 1.2f, 5.0f, 45, 230.0f, true);
            int have = (g_player.warpPart[0] ? 1 : 0) + (g_player.warpPart[1] ? 1 : 0) + (g_player.warpPart[2] ? 1 : 0);
            Msg(RGBA(255, 150, 255, 255), "%s RECOVERED  (%d/3)", names[s->payload], have);
            if (have == 3) Msg(RGBA(255, 150, 255, 255), "ALL FRAGMENTS FOUND. BUILD THE DRIVE AT THE POD.");
        }
        else if (s->kind == SITE_BEACON && !s->used && !g_player.hasScanner) {
            if (d < s->radius) {
                s->used = true;
                UnlockLog(s->payload, true);
                Play(g_sScan, 0.9f);
            }
        }
    }
}

static void UpdateParticles(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g_part[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0.0f) { p->alive = false; continue; }
        p->pos = VAdd(p->pos, VMul(p->vel, dt));
        p->vel = VMul(p->vel, 1.0f / (1.0f + p->drag * dt));
    }
}

static void UpdateToasts(float dt)
{
    for (int i = 0; i < MAX_TOASTS; i++)
        if (g_toast[i].life > 0.0f) g_toast[i].life -= dt;
}

// =====================================================================================
//  РАЗДЕЛ 15. ПРОЕКЦИЯ И ОТСЕЧЕНИЕ
// =====================================================================================

// Проекция мировой точки на экран. false — точка за камерой (GetWorldToScreen
// в этом случае возвращает мусор, поэтому проверка обязательна).
static bool Project(Vector3 world, Vector2* out, float* distOut)
{
    Vector3 d = VSub(world, g_cam.position);
    float   dist = VLen(d);
    if (distOut) *distOut = dist;
    if (dist < 0.01f) return false;
    if (VDot(d, g_camFwd) <= 1.0f) return false;
    *out = GetWorldToScreen(world, g_cam);
    return true;
}

// Грубое отсечение по конусу обзора с запасом на радиус объекта
static bool Visible(Vector3 pos, float radius, float maxDist)
{
    Vector3 d = VSub(pos, g_cam.position);
    float   dist = VLen(d);
    if (dist > maxDist + radius) return false;
    if (dist < radius * 1.5f) return true;
    float dp = VDot(d, g_camFwd) / dist;
    return dp > (0.25f - radius / MaxF(dist, 1.0f));
}

// Радиус объекта в пикселях
static float ScreenRadius(float worldRadius, float dist)
{
    if (dist < 1.0f) dist = 1.0f;
    return worldRadius * g_projScale / dist;
}

// =====================================================================================
//  РАЗДЕЛ 16. ОТРИСОВКА 3D
// =====================================================================================

static void DrawAsteroids3D()
{
    for (int i = 0; i < g_nAst; i++) {
        Asteroid* a = &g_ast[i];
        if (!a->alive) continue;
        if (!Visible(a->pos, a->radius, DRAW_DIST)) continue;

        float dist = VDist(a->pos, g_cam.position);

        Color col = a->col;
        if (a->hitFlash > 0.0f) col = ColLerp(col, RGBA(255, 245, 220, 255), a->hitFlash * 0.75f);
        col = Fog(col, dist);

        Matrix m = MatBasis(VMul(a->axisR, a->radius), VMul(a->axisU, a->radius),
            VMul(a->axisF, a->radius), a->pos, V3(1.0f, 1.0f, 1.0f));
        DrawLit(g_meshRock[a->mesh], m, col);
    }
}

static void DrawCreature3D(Creature* c, float dist)
{
    Vector3 r, u;
    BasisFromForward(c->fwd, &r, &u);

    Color body, glow;
    switch (c->kind) {
    case CR_DRIFTER:   body = RGBA(110, 210, 190, 255); glow = RGBA(90, 255, 220, 255); break;
    case CR_SNAPPER:   body = RGBA(215, 120, 110, 255); glow = RGBA(255, 130, 90, 255); break;
    case CR_STALKER:   body = RGBA(185, 70, 120, 255);  glow = RGBA(255, 60, 120, 255); break;
    case CR_DRONE:     body = RGBA(150, 160, 180, 255); glow = RGBA(255, 120, 60, 255); break;
    case CR_SWARMER:   body = RGBA(230, 70, 90, 255);   glow = RGBA(255, 130, 130, 255); break;
    default:           body = RGBA(105, 60, 120, 255);  glow = RGBA(190, 80, 220, 255); break;
    }
    if (c->hitFlash > 0.0f) body = ColLerp(body, RGBA(255, 255, 255, 255), c->hitFlash * 0.8f);
    Color fogged = Fog(body, dist);

    float pulse = 0.5f + 0.5f * sinf(c->phase);

    if (c->kind == CR_DRIFTER) {
        DrawLit(g_meshBall, MatBasis(r, u, c->fwd, c->pos,
            V3(c->radius, c->radius * (1.05f + pulse * 0.12f), c->radius)), fogged);
        // щупальца
        for (int i = 0; i < 7; i++) {
            float a = c->phase * 0.4f + i * (2.0f * PI / 7.0f);
            Vector3 o = VAdd(VMul(r, cosf(a)), VMul(u, sinf(a)));
            Vector3 p0 = VAdd(c->pos, VMul(o, c->radius * 0.85f));
            Vector3 p1 = VAdd(VAdd(c->pos, VMul(o, c->radius * 1.5f)),
                VMul(c->fwd, -c->radius * (1.4f + 0.3f * sinf(c->phase + i))));
            DrawLine3D(p0, p1, Fog(Alpha(glow, 0.5f), dist));
        }
    }
    else if (c->kind == CR_DRONE) {
        DrawLit(g_meshBall, MatBasis(r, u, c->fwd, c->pos,
            V3(c->radius, c->radius * 0.8f, c->radius * 1.1f)), fogged);
        DrawCircle3D(c->pos, c->radius * 1.5f, r, 90.0f + sinf(c->phase) * 12.0f, Fog(glow, dist));
        DrawCircle3D(c->pos, c->radius * 1.35f, u, c->phase * 24.0f, Fog(Alpha(glow, 0.6f), dist));
        Vector3 eye = VAdd(c->pos, VMul(c->fwd, c->radius * 0.9f));
        DrawLit(g_meshBall, MatPosScale(eye, 4.0f + pulse * 1.6f), glow);
    }
    else if (c->kind == CR_LEVIATHAN) {
        // Сегментированное тело: 8 эллипсоидов с синусоидальным изгибом
        for (int i = 7; i >= 0; i--) {
            float t = (float)i / 7.0f;
            Vector3 seg = VAdd(c->pos, VMul(c->fwd, -t * c->radius * 2.4f));
            seg = VAdd(seg, VMul(r, sinf(c->phase - t * 2.6f) * c->radius * 0.42f));
            seg = VAdd(seg, VMul(u, cosf(c->phase * 0.7f - t * 1.8f) * c->radius * 0.16f));

            float rr = c->radius * (1.0f - t * 0.72f);
            DrawLit(g_meshBall, MatBasis(r, u, c->fwd, seg,
                V3(rr, rr * 0.82f, rr * 1.15f)), Fog(ColLerp(body, g_bg, t * 0.25f), dist));
        }
        // Челюсти: замах перед укусом (bitePrep) приоткрывает их сильнее обычного —
        // видно ЗАРАНЕЕ, что атака вот-вот случится.
        float open = LerpF((c->state == 1) ? 0.50f : 0.17f, 0.85f, c->bitePrep);
        Vector3 jaw = VAdd(c->pos, VMul(c->fwd, c->radius * 0.85f));
        for (int s = -1; s <= 1; s += 2) {
            Vector3 hinge = VAdd(jaw, VMul(u, c->radius * open * s));
            Vector3 tip = VAdd(VAdd(jaw, VMul(c->fwd, c->radius * 0.95f)),
                VMul(u, c->radius * open * 0.35f * s));
            DrawCylinderEx(hinge, tip, c->radius * 0.30f, 1.5f, 6, Fog(RGBA(60, 30, 55, 255), dist));
        }
        // Заряд в глотке — маленький яркий шар, разгорающийся вместе с замахом.
        if (c->bitePrep > 0.02f)
            DrawLit(g_meshBall, MatPosScale(jaw, c->radius * 0.30f * c->bitePrep),
                RGBA(255, 255, 255, (int)(220 * c->bitePrep)));
        // Глаза: белеют по мере роста замаха — тот же читаемый сигнал издалека.
        Color eyeGlow = ColLerp(glow, RGBA(255, 255, 255, 255), c->bitePrep);
        for (int s = -1; s <= 1; s += 2) {
            Vector3 eye = VAdd(VAdd(c->pos, VMul(c->fwd, c->radius * 0.55f)),
                VMul(r, c->radius * 0.48f * s));
            DrawLit(g_meshBall, MatPosScale(eye, c->radius * (0.10f + 0.03f * c->bitePrep)), eyeGlow);
        }
    }
    else if (c->kind == CR_SWARMER) {
        // Маленький хищник-косяк: гранёное ядро (тот же меш, что у кристаллов) плюс
        // расходящиеся шипы — силуэт морского ежа, легко отличим на фоне остальных.
        float pulse2 = 0.5f + 0.5f * sinf(c->phase * 3.0f);
        Color glowS = (c->state == 1) ? RGBA(255, 90, 110, 255) : RGBA(255, 150, 120, 255);

        DrawLit(g_meshGem, MatBasis(r, u, c->fwd, c->pos,
            V3(c->radius, c->radius, c->radius * 1.3f)), fogged);

        for (int s = 0; s < 5; s++) {
            float a = c->phase * 0.6f + s * (2.0f * PI / 5.0f);
            Vector3 spikeDir = VAdd(VMul(r, cosf(a)), VMul(u, sinf(a)));
            Vector3 tip = VAdd(c->pos, VAdd(VMul(spikeDir, c->radius * 1.1f), VMul(c->fwd, -c->radius * 0.3f)));
            DrawCylinderEx(c->pos, tip, c->radius * 0.16f, 0.5f, 4, Fog(Alpha(glowS, 0.85f), dist));
        }

        Vector3 eyeS = VAdd(c->pos, VMul(c->fwd, c->radius * 0.7f));
        DrawLit(g_meshBall, MatPosScale(eyeS, c->radius * (0.22f + pulse2 * 0.05f)), glowS);
    }
    else {
        // Snapper / Stalker — вытянутый эллипсоид с плавниками.
        // У Stalker в режиме блуждания включена маскировка: корпус почти
        // прозрачен, пока он не переходит в погоню — читается как затаившийся
        // хищник, а не как баг с прозрачностью.
        float len = (c->kind == CR_STALKER) ? 2.3f : 1.8f;
        float cloak = (c->kind == CR_STALKER && c->state == 0) ? 0.30f : 1.0f;

        DrawLit(g_meshBall, MatBasis(r, u, c->fwd, c->pos,
            V3(c->radius * 0.72f, c->radius * 0.62f, c->radius * len)), Alpha(fogged, cloak));

        float wag = sinf(c->phase * 2.2f) * c->radius * 0.6f;
        Vector3 tail = VAdd(c->pos, VMul(c->fwd, -c->radius * len));
        Vector3 fin = VAdd(VAdd(tail, VMul(c->fwd, -c->radius * 0.7f)), VMul(r, wag));
        DrawCylinderEx(tail, fin, c->radius * 0.35f, 2.0f, 5, Fog(Alpha(body, 0.85f * cloak), dist));

        // спинной плавник
        Vector3 dorsal0 = VAdd(c->pos, VMul(u, c->radius * 0.5f));
        Vector3 dorsal1 = VAdd(VAdd(c->pos, VMul(u, c->radius * 1.25f)), VMul(c->fwd, -c->radius * 0.5f));
        DrawCylinderEx(dorsal0, dorsal1, c->radius * 0.22f, 1.5f, 4, Fog(Alpha(body, 0.8f * cloak), dist));

        // Глаза НЕ затемняются маскировкой — это намеренный читаемый "tell",
        // по которому внимательный игрок может высмотреть затаившегося Sталкера.
        for (int s = -1; s <= 1; s += 2) {
            Vector3 eye = VAdd(VAdd(c->pos, VMul(c->fwd, c->radius * len * 0.62f)),
                VMul(r, c->radius * 0.34f * s));
            DrawLit(g_meshBall, MatPosScale(eye, c->radius * 0.14f), glow);
        }
    }
}

static void DrawCreatures3D()
{
    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];
        if (!c->alive) continue;
        if (!Visible(c->pos, c->radius * 2.5f, DRAW_DIST)) continue;
        DrawCreature3D(c, VDist(c->pos, g_cam.position));
    }
}

static void DrawPod3D()
{
    if (!Visible(V3(0, 0, 0), POD_RADIUS * 2.0f, WORLD_RADIUS)) return;

    float dist = VLen(g_cam.position);
    float t = g_globalTime;

    if (dist < DRAW_DIST * 1.6f) {
        DrawLit(g_meshBall, MatPosScale(V3(0, 0, 0), POD_RADIUS), Fog(RGBA(70, 84, 102, 255), dist));
        DrawLit(g_meshBall, MatPosScale(V3(0, 0, 0), POD_RADIUS * 0.55f), RGBA(120, 210, 255, 255));

        // Опоры
        for (int i = 0; i < 4; i++) {
            float a = i * (PI * 0.5f);
            Vector3 o = V3(cosf(a), 0.0f, sinf(a));
            DrawCylinderEx(VMul(o, POD_RADIUS * 0.85f), VMul(o, POD_RADIUS * 1.55f),
                9.0f, 4.0f, 6, Fog(RGBA(150, 168, 190, 255), dist));
        }
        // Мачта
        DrawCylinderEx(V3(0, POD_RADIUS * 0.8f, 0), V3(0, POD_RADIUS * 1.9f, 0),
            7.0f, 2.0f, 6, Fog(RGBA(150, 168, 190, 255), dist));
    }

    // Вращающиеся кольца — видны издалека и работают как ориентир
    DrawCircle3D(V3(0, 0, 0), POD_RADIUS * 1.5f, V3(1, 0, 0), 90.0f, RGBA(150, 210, 255, 190));
    DrawCircle3D(V3(0, 0, 0), POD_RADIUS * 1.7f, V3(0, 0, 1), 90.0f + sinf(t * 0.6f) * 18.0f,
        RGBA(120, 190, 240, 150));
    DrawCircle3D(V3(0, 0, 0), POD_RADIUS * 1.35f, V3(0, 1, 0), t * 30.0f, RGBA(150, 230, 255, 130));

    // Зона стыковки
    // Три взаимно перпендикулярные окружности: XZ (плоскость диска), XY и YZ.
    DrawCircle3D(V3(0, 0, 0), DOCK_RADIUS, V3(1, 0, 0), 90.0f, RGBA(110, 200, 255, 70));
    DrawCircle3D(V3(0, 0, 0), DOCK_RADIUS, V3(1, 0, 0), 0.0f, RGBA(110, 200, 255, 45));
    DrawCircle3D(V3(0, 0, 0), DOCK_RADIUS, V3(0, 1, 0), 90.0f, RGBA(110, 200, 255, 45));
}

static void DrawSites3D()
{
    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        if (s->used && s->kind != SITE_BEACON) continue;
        if (!Visible(s->pos, s->radius * 2.0f, DRAW_DIST)) continue;

        float dist = VDist(s->pos, g_cam.position);

        if (s->kind == SITE_BEACON) {
            Color c = s->used ? RGBA(80, 100, 120, 255) : RGBA(120, 220, 255, 255);
            DrawLit(g_meshGem, MatPosScale(s->pos, s->radius * 0.55f), Fog(c, dist));
            DrawCylinderEx(VAdd(s->pos, V3(0, -s->radius, 0)), VAdd(s->pos, V3(0, s->radius * 1.8f, 0)),
                4.0f, 1.5f, 5, Fog(Alpha(c, 0.8f), dist));
            DrawCircle3D(s->pos, s->radius * 1.6f, V3(1, 0, 0), 90.0f, Alpha(c, 0.55f));
        }
        else if (s->kind == SITE_CACHE) {
            Color c = RGBA(255, 210, 120, 255);
            DrawLit(g_meshGem, MatPosScale(s->pos, s->radius * 0.8f), Fog(RGBA(120, 100, 70, 255), dist));
            DrawCircle3D(s->pos, s->radius * 1.4f, V3(0, 1, 0), s->spin * 30.0f, Alpha(c, 0.6f));
        }
        else {
            Color c = RGBA(255, 130, 255, 255);
            DrawLit(g_meshGem, MatPosScale(s->pos, s->radius * 0.7f), Fog(c, dist));
            DrawCircle3D(s->pos, s->radius * 1.7f, V3(0, 1, 0), s->spin * 46.0f, Alpha(c, 0.8f));
            DrawCircle3D(s->pos, s->radius * 1.5f, V3(1, 0, 0), 90.0f + s->spin * 30.0f, Alpha(c, 0.6f));
            DrawCircle3D(s->pos, s->radius * 1.9f, V3(0, 0, 1), 90.0f - s->spin * 22.0f, Alpha(c, 0.45f));
        }
    }
}

static void DrawBuoys3D()
{
    for (int i = 0; i < MAX_BUOYS; i++) {
        if (!g_buoy[i].alive) continue;
        if (!Visible(g_buoy[i].pos, 420.0f, DRAW_DIST * 1.4f)) continue;

        Vector3 p = g_buoy[i].pos;
        float frac = g_buoy[i].charge / 100.0f;
        float dist = VDist(p, g_cam.position);

        DrawLit(g_meshBall, MatPosScale(p, 14.0f), Fog(RGBA(80, 120, 160, 255), dist));
        DrawLit(g_meshBall, MatPosScale(p, 7.0f), RGBA(150, 230, 255, 255));

        Color halo = RGBA(120, 220, 255, (int)(75 * frac));
        DrawCircle3D(p, 420.0f, V3(1, 0, 0), 90.0f, halo);
        DrawCircle3D(p, 420.0f, V3(1, 0, 0), 0.0f, Alpha(halo, 0.6f));
        DrawCircle3D(p, 420.0f, V3(0, 1, 0), 90.0f, Alpha(halo, 0.6f));
    }
}

static void DrawPickups3D()
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup* k = &g_pick[i];
        if (!k->alive) continue;
        if (!Visible(k->pos, 20.0f, DRAW_DIST * 0.7f)) continue;

        float dist = VDist(k->pos, g_cam.position);
        float fade = (k->life < 8.0f) ? (0.4f + 0.6f * fabsf(sinf(k->life * 6.0f))) : 1.0f;

        Vector3 r, u, f;
        f = VNorm(V3(cosf(k->spin), 0.35f, sinf(k->spin)));
        BasisFromForward(f, &r, &u);

        DrawLit(g_meshGem, MatBasis(r, u, f, k->pos, V3(8.0f, 8.0f, 11.0f)),
            Fog(Alpha(g_resColor[k->res], fade), dist));
    }
}

static void DrawProjectiles3D()
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile* p = &g_proj[i];
        if (!p->alive) continue;
        if (!Visible(p->pos, 24.0f, DRAW_DIST)) continue;

        Vector3 dir = VNorm(p->vel);

        if (p->kind == PK_TORPEDO) {
            // Корпус + маленький хвостовой огонёк — читается как медленная тяжёлая ракета.
            Vector3 tail = VSub(p->pos, VMul(dir, 22.0f));
            DrawCylinderEx(tail, p->pos, 5.0f, 2.0f, 8, RGBA(230, 230, 240, 255));
            DrawLit(g_meshBall, MatPosScale(p->pos, 6.0f), RGBA(255, 150, 255, 255));
            DrawLit(g_meshBall, MatPosScale(VSub(tail, VMul(dir, 10.0f)), 4.0f), RGBA(255, 190, 120, 255));
        }
        else if (p->kind == PK_BLASTER) {
            // Короткий яркий штрих вместо шарика — читается как быстрый энергоболт.
            Vector3 tail = VSub(p->pos, VMul(dir, 14.0f));
            DrawCylinderEx(tail, p->pos, 2.6f, 0.6f, 6, RGBA(140, 230, 255, 255));
            DrawLit(g_meshBall, MatPosScale(p->pos, 3.0f), RGBA(210, 245, 255, 255));
        }
        else {
            DrawLit(g_meshBall, MatPosScale(p->pos, p->radius), RGBA(255, 240, 200, 255));
        }
    }
}

static void DrawShip3D()
{
    if (!g_thirdPerson) return;

    Player* p = &g_player;
    Color hull = RGBA(225, 232, 240, 255);
    if (p->hitFlash > 0.0f) hull = ColLerp(hull, RGBA(255, 80, 60, 255), p->hitFlash);
    if (p->invulT > 0.0f && fmodf(g_globalTime * 14.0f, 1.0f) < 0.5f) hull = RGBA(120, 130, 150, 255);

    Vector3 nose = VAdd(p->pos, VMul(p->fwd, 24.0f));
    Vector3 tail = VAdd(p->pos, VMul(p->fwd, -18.0f));

    DrawLit(g_meshBall, MatBasis(p->right, p->up, p->fwd, p->pos, V3(9.0f, 7.0f, 20.0f)), hull);
    DrawCylinderEx(tail, nose, 3.0f, 1.0f, 6, RGBA(160, 175, 195, 255));

    // Крылья
    for (int s = -1; s <= 1; s += 2) {
        Vector3 wingTip = VAdd(VAdd(p->pos, VMul(p->right, 22.0f * s)), VMul(p->fwd, -8.0f));
        DrawCylinderEx(VAdd(p->pos, VMul(p->right, 5.0f * s)), wingTip, 4.0f, 1.5f, 5,
            RGBA(150, 165, 185, 255));
        // Двигатель
        Vector3 eng = VAdd(VAdd(p->pos, VMul(p->right, 12.0f * s)), VMul(p->fwd, -16.0f));
        DrawLit(g_meshBall, MatPosScale(eng, 4.0f), RGBA(70, 80, 100, 255));
        if (p->thrusting) {
            Color fl = (p->boostT > 0.1f) ? RGBA(170, 215, 255, 255) : RGBA(255, 170, 80, 255);
            DrawCylinderEx(eng, VAdd(eng, VMul(p->fwd, -(12.0f + 10.0f * p->boostT))), 3.5f, 0.5f, 5, fl);
        }
    }

    // Кабина
    DrawLit(g_meshBall, MatPosScale(VAdd(VAdd(p->pos, VMul(p->fwd, 4.0f)), VMul(p->up, 4.0f)), 5.0f),
        RGBA(90, 190, 255, 255));
}

static void DrawBeam3D()
{
    if (!g_beamOn) return;
    Vector3 start = BeamOrigin();
    float flick = 0.75f + 0.25f * sinf(g_globalTime * 45.0f);
    DrawCylinderEx(start, g_beamEnd, 1.6f, 0.7f, 6, Alpha(RGBA(255, 190, 120, 255), flick));
    DrawLine3D(start, g_beamEnd, RGBA(255, 255, 240, 255));
}

static void DrawSectorRings3D()
{
    // Границы секторов на плоскости диска — главный навигационный ориентир
    // ВНИМАНИЕ: базовая окружность DrawCircle3D лежит в плоскости XY. Чтобы положить её
    // в плоскость диска (XZ), нужен поворот на 90 градусов вокруг оси X.
    for (int i = 0; i < SEC_COUNT; i++) {
        Color c = g_secTint[i];
        Color ring = RGBA(c.r + 55, c.g + 55, c.b + 65, 55);
        DrawCircle3D(V3(0, 0, 0), g_secOuter[i], V3(1, 0, 0), 90.0f, ring);
        DrawCircle3D(V3(0, DISK_HALF * 0.55f, 0), g_secOuter[i], V3(1, 0, 0), 90.0f, Alpha(ring, 0.45f));
        DrawCircle3D(V3(0, -DISK_HALF * 0.55f, 0), g_secOuter[i], V3(1, 0, 0), 90.0f, Alpha(ring, 0.45f));
    }
    DrawCircle3D(V3(0, 0, 0), WORLD_RADIUS, V3(1, 0, 0), 90.0f, RGBA(255, 95, 70, 70));
}

static void DrawScene3D()
{
    BeginMode3D(g_cam);
    DrawSectorRings3D();
    DrawPod3D();
    DrawBuoys3D();
    DrawSites3D();
    DrawAsteroids3D();
    DrawPickups3D();
    DrawCreatures3D();
    DrawProjectiles3D();
    DrawShip3D();
    DrawBeam3D();
    EndMode3D();
}

// =====================================================================================
//  РАЗДЕЛ 17. ОТРИСОВКА 2D: ФОН, БЛИКИ, ЧАСТИЦЫ
// =====================================================================================

static void DrawStarfield()
{
    // Звёзды — бесконечно далёкие направления, спроецированные из позиции камеры
    for (int i = 0; i < MAX_STARS; i++) {
        Vector3 wp = VAdd(g_cam.position, VMul(g_star[i].dir, 200000.0f));
        Vector2 sp;
        if (!Project(wp, &sp, NULL)) continue;
        if (sp.x < -10 || sp.x > SCREEN_W + 10 || sp.y < -10 || sp.y > SCREEN_H + 10) continue;

        float tw = 0.75f + 0.25f * sinf(g_globalTime * 1.6f + i * 0.7f);
        int v = (int)(g_star[i].bright * 215.0f * tw);
        DrawCircleV(sp, g_star[i].size, RGBA(v, v, (int)(v * 1.08f), 255));
    }
}

static void DrawNebulaBackdrop()
{
    // Только дальние облака и только как фон: так они не перекрывают геометрию
    for (int i = 0; i < g_nNeb; i++) {
        float dist;
        Vector2 sp;
        if (!Project(g_neb[i].pos, &sp, &dist)) continue;
        if (dist < DRAW_DIST * 0.8f) continue;

        float rad = ScreenRadius(g_neb[i].radius, dist);
        if (rad < 12.0f || rad > 2200.0f) continue;
        if (sp.x < -rad || sp.x > SCREEN_W + rad || sp.y < -rad || sp.y > SCREEN_H + rad) continue;

        float a = Clampf(1.0f - dist / (WORLD_RADIUS * 1.4f), 0.05f, 1.0f) * 0.30f;
        DrawCircleGradient((int)sp.x, (int)sp.y, rad, Alpha(g_neb[i].col, a), Alpha(g_neb[i].col, 0.0f));
    }
}

static void DrawParticles2D()
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g_part[i];
        if (!p->alive) continue;

        float dist;
        Vector2 sp;
        if (!Project(p->pos, &sp, &dist)) continue;
        if (dist > DRAW_DIST) continue;
        if (sp.x < -40 || sp.x > SCREEN_W + 40 || sp.y < -40 || sp.y > SCREEN_H + 40) continue;

        float t = p->life / p->maxLife;
        float rr = ScreenRadius(p->size * t, dist);
        if (rr < 0.4f) continue;
        if (rr > 60.0f) rr = 60.0f;

        if (p->glow)
            DrawCircleGradient((int)sp.x, (int)sp.y, rr * 4.0f, Alpha(p->col, 0.30f * t), Alpha(p->col, 0.0f));
        DrawCircleV(sp, rr, Alpha(p->col, t));
    }
}

// Блики поверх сцены: ресурсы, маяки, огни капсулы, точка попадания лазера
static void DrawGlows2D()
{
    float dist;
    Vector2 sp;

    // Капсула
    if (Project(V3(0, 0, 0), &sp, &dist)) {
        float rr = MaxF(14.0f, ScreenRadius(POD_RADIUS * 0.8f, dist));
        DrawCircleGradient((int)sp.x, (int)sp.y, rr * 2.6f, RGBA(110, 200, 255, 70), RGBA(110, 200, 255, 0));
        for (int i = 0; i < 6; i++) {
            float a = g_globalTime * 0.8f + i * (2.0f * PI / 6.0f);
            Vector3 lp = V3(cosf(a) * POD_RADIUS * 0.9f, sinf(a * 1.3f) * POD_RADIUS * 0.4f,
                sinf(a) * POD_RADIUS * 0.9f);
            Vector2 lsp;
            float ld;
            if (!Project(lp, &lsp, &ld)) continue;
            float blink = 0.5f + 0.5f * sinf(g_globalTime * 4.0f + i);
            DrawCircleV(lsp, MaxF(1.5f, ScreenRadius(5.0f, ld)),
                RGBA(255, (int)(140 + 100 * blink), 90, 230));
        }
    }

    // Ресурсы
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (!g_pick[i].alive) continue;
        if (!Project(g_pick[i].pos, &sp, &dist)) continue;
        if (dist > DRAW_DIST * 0.7f) continue;
        float rr = MaxF(3.0f, ScreenRadius(26.0f, dist));
        DrawCircleGradient((int)sp.x, (int)sp.y, rr, Alpha(g_resColor[g_pick[i].res], 0.55f),
            Alpha(g_resColor[g_pick[i].res], 0.0f));
    }

    // Точки интереса
    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        if (s->used) continue;
        if (!Project(s->pos, &sp, &dist)) continue;
        if (dist > DRAW_DIST * 1.5f) continue;

        Color c = (s->kind == SITE_FRAGMENT) ? RGBA(255, 130, 255, 255) :
            (s->kind == SITE_BEACON) ? RGBA(120, 220, 255, 255) : RGBA(255, 210, 120, 255);
        float rr = MaxF(8.0f, ScreenRadius(s->radius * 2.2f, dist));
        DrawCircleGradient((int)sp.x, (int)sp.y, rr, Alpha(c, 0.45f), Alpha(c, 0.0f));
    }

    // Снаряды
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!g_proj[i].alive) continue;
        if (!Project(g_proj[i].pos, &sp, &dist)) continue;
        float rr = MaxF(4.0f, ScreenRadius(26.0f, dist));
        DrawCircleGradient((int)sp.x, (int)sp.y, rr, Alpha(g_proj[i].col, 0.7f), Alpha(g_proj[i].col, 0.0f));
    }

    // Попадание лазера
    if (g_beamOn && g_beamHit && Project(g_beamEnd, &sp, &dist)) {
        float flick = 0.7f + 0.3f * sinf(g_globalTime * 40.0f);
        float rr = MaxF(6.0f, ScreenRadius(22.0f, dist)) * flick;
        DrawCircleGradient((int)sp.x, (int)sp.y, rr * 2.5f, RGBA(255, 210, 140, 200), RGBA(255, 160, 60, 0));
        DrawCircleV(sp, rr * 0.5f, RGBA(255, 250, 230, 230));
    }

    // Кольцо сканирования
    if (g_scanActive && Project(g_scanTarget, &sp, &dist)) {
        float rr = MaxF(18.0f, ScreenRadius(70.0f, dist));
        DrawCircleLines((int)sp.x, (int)sp.y, rr + 6.0f * sinf(g_globalTime * 7.0f), RGBA(120, 220, 255, 220));
        DrawCircleLines((int)sp.x, (int)sp.y, rr * 0.6f, RGBA(120, 220, 255, 140));
    }

    // Прогресс сканирования существ
    for (int i = 0; i < g_nCre; i++) {
        Creature* c = &g_cre[i];
        if (!c->alive || c->scanProg <= 0.0f || c->scanProg >= 1.0f) continue;
        if (!Project(c->pos, &sp, &dist)) continue;
        float rr = MaxF(20.0f, ScreenRadius(c->radius * 1.6f, dist));
        DrawRing(sp, rr, rr + 5.0f, -90.0f, -90.0f + 360.0f * c->scanProg, 48, RGBA(120, 220, 255, 230));
    }
}

// =====================================================================================
//  РАЗДЕЛ 18. HUD
// =====================================================================================

static void DrawBar(float x, float y, float w, float h, float frac, Color fill,
    const char* label, const char* value)
{
    Rectangle bg = { x, y, w, h };
    DrawRectangleRec(bg, RGBA(12, 16, 24, 205));
    DrawRectangleRec(Rectangle{ x, y, w * Clampf(frac, 0.0f, 1.0f), h }, fill);
    DrawRectangleLinesEx(bg, 1.5f, RGBA(fill.r, fill.g, fill.b, 150));
    Txt(label, (int)(x + 8), (int)(y + h * 0.5f - 5), 11, RGBA(255, 255, 255, 220));
    if (value) Txt(value, (int)(x + w - 8 - TxtW(value, 11)), (int)(y + h * 0.5f - 5), 11,
        RGBA(255, 255, 255, 200));
}

// Карта сверху вниз: мир — диск, поэтому проекция на XZ честно отражает обстановку
static void DrawMinimap()
{
    float R = 100.0f;
    Vector2 c = V2(SCREEN_W - R - 26.0f, R + 26.0f);
    float scale = R / 2200.0f;

    DrawCircleV(c, R, RGBA(8, 12, 20, 205));
    DrawCircleLines((int)c.x, (int)c.y, R, RGBA(90, 150, 200, 160));
    DrawCircleLines((int)c.x, (int)c.y, R * 0.5f, RGBA(90, 150, 200, 55));

    // Направление взгляда
    float yaw = g_player.yaw;
    DrawLineEx(c, V2(c.x + cosf(yaw) * R * 0.9f, c.y + sinf(yaw) * R * 0.9f), 1.5f, RGBA(120, 200, 255, 90));

    for (int i = 0; i < g_nAst; i++) {
        if (!g_ast[i].alive) continue;
        Vector3 d = VSub(g_ast[i].pos, g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        if (m.x * m.x + m.y * m.y > R * R) continue;
        DrawCircleV(V2(c.x + m.x, c.y + m.y), 1.3f, Alpha(g_resColor[g_ast[i].res], 0.7f));
    }
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (!g_pick[i].alive) continue;
        Vector3 d = VSub(g_pick[i].pos, g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        if (m.x * m.x + m.y * m.y > R * R) continue;
        DrawCircleV(V2(c.x + m.x, c.y + m.y), 2.0f, RGBA(255, 255, 255, 220));
    }
    for (int i = 0; i < g_nCre; i++) {
        Creature* cr = &g_cre[i];
        if (!cr->alive) continue;
        Vector3 d = VSub(cr->pos, g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        if (m.x * m.x + m.y * m.y > R * R) continue;
        Color col = (cr->kind == CR_DRIFTER) ? RGBA(110, 230, 190, 220) :
            (cr->kind == CR_LEVIATHAN) ? RGBA(210, 90, 240, 255) : RGBA(255, 100, 90, 230);
        DrawCircleV(V2(c.x + m.x, c.y + m.y), (cr->kind == CR_LEVIATHAN) ? 5.0f : 2.6f, col);
    }
    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        if (s->used) continue;
        Vector3 d = VSub(s->pos, g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        if (m.x * m.x + m.y * m.y > R * R) continue;
        Color col = (s->kind == SITE_FRAGMENT) ? RGBA(255, 130, 255, 255) :
            (s->kind == SITE_BEACON) ? RGBA(120, 220, 255, 255) : RGBA(255, 210, 120, 255);
        DrawPoly(V2(c.x + m.x, c.y + m.y), 4, 4.0f, g_globalTime * 40.0f, col);
    }
    for (int i = 0; i < MAX_BUOYS; i++) {
        if (!g_buoy[i].alive) continue;
        Vector3 d = VSub(g_buoy[i].pos, g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        if (m.x * m.x + m.y * m.y > R * R) continue;
        DrawCircleV(V2(c.x + m.x, c.y + m.y), 3.0f, RGBA(140, 230, 255, 255));
    }

    // Капсула — всегда на карте, прижатая к краю если далеко
    {
        Vector3 d = VSub(V3(0, 0, 0), g_player.pos);
        Vector2 m = V2(d.x * scale, d.z * scale);
        float len = sqrtf(m.x * m.x + m.y * m.y);
        bool clamped = (len > R - 6.0f);
        if (clamped && len > 0.01f) m = V2(m.x / len * (R - 6.0f), m.y / len * (R - 6.0f));
        Vector2 pp = V2(c.x + m.x, c.y + m.y);
        DrawPoly(pp, 3, 7.0f, g_globalTime * 30.0f, RGBA(120, 255, 180, 255));
        if (clamped) DrawCircleLines((int)pp.x, (int)pp.y, 9.0f, RGBA(120, 255, 180, 140));
    }

    DrawPoly(c, 3, 5.0f, yaw * RAD2DEG, RGBA(255, 255, 255, 255));

    const char* lbl = "TOP-DOWN 2.2km";
    Txt(lbl, (int)(c.x - TxtW(lbl, 10) * 0.5f), (int)(c.y + R + 6), 10, RGBA(150, 180, 210, 180));

    // Вертикальная шкала: где игрок по высоте внутри диска
    float bx = c.x + R + 14.0f, by = c.y - R, bh = R * 2.0f;
    DrawRectangleRec(Rectangle{ bx, by, 10.0f, bh }, RGBA(10, 14, 22, 200));
    DrawRectangleLinesEx(Rectangle{ bx, by, 10.0f, bh }, 1.0f, RGBA(90, 150, 200, 120));
    DrawLineEx(V2(bx, by + bh * 0.5f), V2(bx + 10.0f, by + bh * 0.5f), 1.0f, RGBA(90, 150, 200, 110));
    float yf = Clampf(g_player.pos.y / (DISK_HALF + 900.0f), -1.0f, 1.0f);
    float my = by + bh * 0.5f - yf * bh * 0.5f;
    DrawRectangleRec(Rectangle{ bx - 2.0f, my - 2.0f, 14.0f, 4.0f }, RGBA(255, 255, 255, 235));
    Txt("ALT", (int)(bx - 6), (int)(by + bh + 6), 10, RGBA(150, 180, 210, 180));
}

// Маркер цели на краю экрана, если она за спиной или вне кадра
static void DrawEdgeMarker(Vector3 world, Color col, const char* label)
{
    Vector2 center = V2(SCREEN_W * 0.5f, SCREEN_H * 0.5f);
    Vector2 sp;
    float dist;

    bool inFront = Project(world, &sp, &dist);
    bool onScreen = inFront && sp.x > 40 && sp.x < SCREEN_W - 40 && sp.y > 40 && sp.y < SCREEN_H - 40;

    if (onScreen) {
        DrawCircleLines((int)sp.x, (int)sp.y, 13.0f, Alpha(col, 0.75f));
        DrawCircleLines((int)sp.x, (int)sp.y, 3.0f, col);
        if (label) Txt(label, (int)(sp.x - TxtW(label, 11) * 0.5f), (int)(sp.y + 19), 11, Alpha(col, 0.85f));
        return;
    }

    // Направление в экранных координатах через базис камеры
    Vector3 d = VSub(world, g_cam.position);
    Vector3 camRight = VNorm(VCross(g_cam.up, g_camFwd));
    Vector3 camUp = VNorm(VCross(g_camFwd, camRight));

    float sx = VDot(d, camRight);
    float sy = VDot(d, camUp);
    float sz = VDot(d, g_camFwd);
    if (sz < 0.0f) { sx = -sx; sy = -sy; }   // за спиной — отражаем

    Vector2 dir = V2(sx, -sy);
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) dir = V2(0.0f, -1.0f);
    else              dir = V2(dir.x / len, dir.y / len);

    float rad = 250.0f;
    Vector2 p = V2(center.x + dir.x * rad, center.y + dir.y * rad);

    float ang = atan2f(dir.y, dir.x) * RAD2DEG;
    DrawPoly(p, 3, 11.0f, ang, Alpha(col, 0.55f));
    DrawPolyLinesEx(p, 3, 11.0f, ang, 1.6f, col);
    if (label) Txt(label, (int)(p.x - TxtW(label, 11) * 0.5f), (int)(p.y + 16), 11, Alpha(col, 0.85f));
}

static void DrawCrosshair()
{
    Vector2 m = V2(SCREEN_W * 0.5f, SCREEN_H * 0.5f);
    // Базовый цвет прицела зависит от выбранного оружия — сразу видно, что
    // взведено, даже не глядя на подпись в HUD; попадание лазера перекрывает
    // это ярко-оранжевым.
    Color baseCol = (g_player.weapon == WPN_BLASTER) ? RGBA(150, 225, 255, 190) :
        (g_player.weapon == WPN_TORPEDO) ? RGBA(255, 170, 255, 190) :
        RGBA(190, 220, 245, 170);
    Color cc = g_beamHit ? RGBA(255, 210, 140, 230) : baseCol;

    DrawLineEx(V2(m.x - 14, m.y), V2(m.x - 5, m.y), 1.6f, cc);
    DrawLineEx(V2(m.x + 5, m.y), V2(m.x + 14, m.y), 1.6f, cc);
    DrawLineEx(V2(m.x, m.y - 14), V2(m.x, m.y - 5), 1.6f, cc);
    DrawLineEx(V2(m.x, m.y + 5), V2(m.x, m.y + 14), 1.6f, cc);
    DrawCircleLines((int)m.x, (int)m.y, 2.0f, cc);

    // Дуга дальности лазера
    if (g_beamOn) DrawRing(m, 22.0f, 24.0f, -90.0f, 270.0f, 32, RGBA(255, 180, 110, 90));
}

static void DrawHud()
{
    Player* p = &g_player;
    int sec = PlayerSector();

    float x = 22.0f, y = 22.0f, w = 250.0f, h = 20.0f;

    float o2f = p->o2 / p->o2Max;
    Color o2c = (o2f < 0.25f) ? ColLerp(RGBA(255, 60, 60, 255), RGBA(90, 200, 255, 255),
        0.5f + 0.5f * sinf(g_globalTime * 9.0f))
        : RGBA(90, 200, 255, 255);
    DrawBar(x, y, w, h, o2f, o2c, "OXYGEN", TextFormat("%ds", (int)p->o2));
    DrawBar(x, y + 26.0f, w, h, p->hull / p->hullMax,
        (p->hull / p->hullMax < 0.3f) ? RGBA(255, 70, 70, 255) : RGBA(120, 230, 150, 255),
        "HULL", TextFormat("%d/%d", (int)p->hull, (int)p->hullMax));
    DrawBar(x, y + 52.0f, w, h, p->energy / p->energyMax, RGBA(255, 200, 90, 255),
        "ENERGY", TextFormat("%d", (int)p->energy));

    Color st = g_secTint[sec];
    Txt(g_secName[sec], (int)x, (int)(y + 82.0f), 18, RGBA(st.r + 130, st.g + 130, st.b + 140, 235));
    Txt(TextFormat("RANGE %.2f km   ALT %+d m", VLen(p->pos) / 1000.0f, (int)p->pos.y),
        (int)x, (int)(y + 104.0f), 12, RGBA(150, 175, 200, 200));
    Txt(TextFormat("SPEED %d m/s", (int)VLen(p->vel)), (int)x, (int)(y + 120.0f), 12,
        RGBA(150, 175, 200, 200));

    // --- Инвентарь ---
    float iy = SCREEN_H - 118.0f;
    int total = InvTotal();
    Rectangle invBg = { 18.0f, iy - 26.0f, 300.0f, 120.0f };
    DrawRectangleRounded(invBg, 0.12f, 8, RGBA(10, 14, 22, 185));
    DrawRectangleLinesEx(invBg, 1.5f, RGBA(70, 100, 130, 130));

    Color capCol = (total >= p->invCap) ? RGBA(255, 110, 90, 255) : RGBA(170, 200, 225, 230);
    Txt(TextFormat("CARGO  %d / %d", total, p->invCap), 30, (int)(iy - 18.0f), 13, capCol);

    for (int i = 0; i < RES_COUNT; i++) {
        int col = i % 4, row = i / 4;
        float cx = 30.0f + col * 72.0f;
        float cy = iy + 6.0f + row * 36.0f;
        Color rc = g_resColor[i];
        bool has = p->inv[i] > 0;

        DrawPoly(V2(cx + 9.0f, cy + 10.0f), 4, 8.0f, 45.0f, has ? rc : Alpha(rc, 0.25f));
        Txt(g_resShort[i], (int)(cx + 22), (int)(cy + 1), 11,
            Alpha(RGBA(200, 215, 235, 255), has ? 1.0f : 0.35f));
        Txt(TextFormat("%d", p->inv[i]), (int)(cx + 22), (int)(cy + 13), 13,
            has ? RGBA(255, 255, 255, 255) : RGBA(120, 135, 155, 160));
    }

    // --- Расходники ---
    float ry = SCREEN_H - 156.0f;

    // Текущее оружие — собрано вручную через snprintf, а не вложенным
    // TextFormat: у TextFormat общий кольцевой буфер на несколько вызовов,
    // и вложенный вызов внутри другого — источник трудноуловимых багов.
    {
        char wline[64];
        const char* wpnName = (p->weapon == WPN_LASER) ? "MINING LASER" :
            (p->weapon == WPN_BLASTER) ? "PULSE CANNON" : "TORPEDO LAUNCHER";
        if (p->weapon == WPN_TORPEDO)
            snprintf(wline, sizeof(wline), "[%d] %s  AMMO x%d", p->weapon + 1, wpnName, p->torpedoAmmo);
        else
            snprintf(wline, sizeof(wline), "[%d] %s", p->weapon + 1, wpnName);
        Txt(wline, SCREEN_W - 200, (int)(ry - 24.0f), 13, RGBA(255, 220, 150, 235));
    }

    Txt(TextFormat("[F] REPAIR x%d", p->repairKits), SCREEN_W - 200, (int)ry, 13,
        p->repairKits > 0 ? RGBA(150, 255, 180, 230) : RGBA(90, 110, 125, 160));
    Txt(TextFormat("[Q] O2 CAP x%d", p->o2Caps), SCREEN_W - 200, (int)(ry + 20), 13,
        p->o2Caps > 0 ? RGBA(150, 220, 255, 230) : RGBA(90, 110, 125, 160));
    Txt(TextFormat("[B] BUOY   x%d", p->buoys), SCREEN_W - 200, (int)(ry + 40), 13,
        p->buoys > 0 ? RGBA(150, 220, 255, 230) : RGBA(90, 110, 125, 160));

    int fragHave = (p->warpPart[0] ? 1 : 0) + (p->warpPart[1] ? 1 : 0) + (p->warpPart[2] ? 1 : 0);
    Txt(TextFormat("DRIVE FRAGMENTS %d/3", fragHave), SCREEN_W - 200, (int)(ry + 64), 13,
        fragHave == 3 ? RGBA(255, 150, 255, 255) : RGBA(160, 130, 180, 200));
    Txt(g_thirdPerson ? "[V] CHASE CAM" : "[V] COCKPIT", SCREEN_W - 200, (int)(ry + 88), 12,
        RGBA(110, 135, 160, 190));

    // --- Маркеры целей ---
    if (VLen(p->pos) > DOCK_RADIUS)
        DrawEdgeMarker(V3(0, 0, 0), RGBA(120, 255, 180, 255),
            TextFormat("POD %.1fkm", VLen(p->pos) / 1000.0f));

    for (int i = 0; i < g_nSite; i++) {
        Site* s = &g_site[i];
        if (s->used || s->kind != SITE_FRAGMENT) continue;
        float d = VDist(s->pos, p->pos);
        if (d > 2400.0f) continue;
        DrawEdgeMarker(s->pos, RGBA(255, 130, 255, 255), TextFormat("FRAGMENT %.0fm", d));
    }
    for (int i = 0; i < MAX_BUOYS; i++) {
        if (!g_buoy[i].alive) continue;
        float d = VDist(g_buoy[i].pos, p->pos);
        if (d < 420.0f || d > 3000.0f) continue;
        DrawEdgeMarker(g_buoy[i].pos, RGBA(140, 230, 255, 255), TextFormat("BUOY %.0fm", d));
    }

    // --- Подсказка стыковки ---
    if (VLen(p->pos) < DOCK_RADIUS) {
        const char* t = "[E]  DOCK WITH POD";
        float pulse = 0.6f + 0.4f * sinf(g_globalTime * 4.0f);
        Txt(t, (int)(SCREEN_W * 0.5f - TxtW(t, 22) * 0.5f), SCREEN_H - 214, 22,
            RGBA(150, 255, 200, (int)(255 * pulse)));
    }

    // --- Предупреждения ---
    float wy = 140.0f;
    if (p->o2 < 25.0f) {
        const char* t = "OXYGEN CRITICAL";
        if (fmodf(g_globalTime * 3.0f, 1.0f) < 0.6f)
            Txt(t, (int)(SCREEN_W * 0.5f - TxtW(t, 30) * 0.5f), (int)wy, 30, RGBA(120, 210, 255, 255));
        wy += 38.0f;
    }
    if (p->hull < p->hullMax * 0.3f) {
        const char* t = "HULL FAILING";
        if (fmodf(g_globalTime * 3.0f, 1.0f) < 0.6f)
            Txt(t, (int)(SCREEN_W * 0.5f - TxtW(t, 30) * 0.5f), (int)wy, 30, RGBA(255, 70, 70, 255));
        wy += 38.0f;
    }
    if (sec == SEC_CORE && !p->hasRadSuit) {
        const char* t = "RADIATION - NO SHIELDING";
        if (fmodf(g_globalTime * 4.0f, 1.0f) < 0.6f)
            Txt(t, (int)(SCREEN_W * 0.5f - TxtW(t, 26) * 0.5f), (int)wy, 26, RGBA(255, 200, 60, 255));
        wy += 34.0f;
    }
    if (VLen(p->pos) > WORLD_RADIUS || fabsf(p->pos.y) > DISK_HALF + 900.0f) {
        const char* t = "TURN BACK - LEAVING THE FIELD";
        Txt(t, (int)(SCREEN_W * 0.5f - TxtW(t, 26) * 0.5f), (int)wy, 26, RGBA(255, 120, 90, 255));
    }

    for (int i = 0; i < MAX_TOASTS; i++) {
        if (g_toast[i].life <= 0.0f) continue;
        float a = Clampf(g_toast[i].life / 1.2f, 0.0f, 1.0f);
        Txt(g_toast[i].text, 24, (int)(SCREEN_H - 190.0f - i * 19.0f), 13, Alpha(g_toast[i].col, a));
    }

    Txt("[TAB] DATABANK   [ESC] PAUSE", SCREEN_W - 230, SCREEN_H - 26, 12, RGBA(110, 135, 160, 170));

    DrawMinimap();
    DrawCrosshair();
}

static void DrawScreenEffects()
{
    Player* p = &g_player;
    int sec = PlayerSector();

    DrawCircleGradient(SCREEN_W / 2, SCREEN_H / 2, SCREEN_W * 0.78f, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 165));

    if (sec == SEC_NEBULA) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(120, 55, 100, 40));
        DrawCircleGradient(SCREEN_W / 2, SCREEN_H / 2, 520.0f, RGBA(120, 55, 100, 0), RGBA(120, 55, 100, 72));
    }
    if (sec == SEC_CORE) {
        float f = p->hasRadSuit ? 0.35f : 0.8f;
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 130, 50, (int)(26 * f)));
    }

    float o2f = p->o2 / p->o2Max;
    if (o2f < 0.3f) {
        float pulse = (0.5f + 0.5f * sinf(g_globalTime * 6.0f)) * (1.0f - o2f / 0.3f);
        DrawCircleGradient(SCREEN_W / 2, SCREEN_H / 2, SCREEN_W * 0.7f,
            RGBA(0, 0, 0, 0), RGBA(20, 60, 120, (int)(150 * pulse)));
    }

    if (g_redFlash > 0.0f)
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(255, 40, 40, (int)(90 * Clampf(g_redFlash, 0.0f, 1.0f))));

    // Рама кокпита — только от первого лица
    if (!g_thirdPerson) {
        Color frame = RGBA(20, 30, 44, 150);
        DrawRectangle(0, 0, SCREEN_W, 14, frame);
        DrawRectangle(0, SCREEN_H - 14, SCREEN_W, 14, frame);
        DrawRectangle(0, 0, 14, SCREEN_H, frame);
        DrawRectangle(SCREEN_W - 14, 0, 14, SCREEN_H, frame);
        DrawCircleGradient(SCREEN_W / 2, SCREEN_H + 210, 620.0f, RGBA(90, 150, 210, 28), RGBA(90, 150, 210, 0));
    }
}

static void DrawWorldFrame()
{
    ClearBackground(g_bg);
    // Вертикальный градиент вместо плоской заливки — небо не выглядит
    // пустой чёрной стеной, даже когда звёзды и туманности ещё не легли.
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, g_bgTop, g_bg);
    DrawNebulaBackdrop();
    DrawStarfield();
    DrawScene3D();
    DrawParticles2D();
    DrawGlows2D();
    DrawScreenEffects();
    DrawHud();
}

// =====================================================================================
//  РАЗДЕЛ 19. ЭЛЕМЕНТЫ ИНТЕРФЕЙСА
// =====================================================================================

// g_stateAge защищает от срабатывания кнопки в том же кадре, в котором открылся экран.
static bool UiButton(Rectangle r, const char* label, bool enabled, Color accent)
{
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    bool click = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (g_stateAge > 0);

    Color bg = enabled ? (hover ? Alpha(accent, 0.30f) : RGBA(24, 30, 42, 220)) : RGBA(18, 20, 26, 190);
    DrawRectangleRounded(r, 0.2f, 6, bg);
    DrawRectangleLinesEx(r, 1.6f, enabled ? Alpha(accent, hover ? 1.0f : 0.6f) : RGBA(60, 65, 78, 200));

    Color tc = enabled ? RGBA(235, 242, 250, 255) : RGBA(105, 115, 130, 200);
    Txt(label, (int)(r.x + r.width * 0.5f - TxtW(label, 14) * 0.5f), (int)(r.y + r.height * 0.5f - 7), 14, tc);
    return click;
}

static void DrawPanel(Rectangle r, const char* title, Color accent)
{
    DrawRectangleRounded(r, 0.03f, 8, RGBA(8, 11, 18, 238));
    DrawRectangleLinesEx(r, 2.0f, Alpha(accent, 0.55f));
    if (title) {
        DrawRectangleRec(Rectangle{ r.x, r.y, r.width, 34.0f }, Alpha(accent, 0.18f));
        Txt(title, (int)(r.x + 14), (int)(r.y + 10), 16, RGBA(235, 242, 250, 255));
    }
}

// Перенос по словам. Склейка вручную, без snprintf, чтобы компилятор не ругался
// на возможное усечение и границы проверялись явно.
static int DrawWrapped(const char* text, int x, int y, int maxW, int fontSize, int gap, Color col)
{
    const int LINE_MAX = 512;
    const int WORD_MAX = 128;

    char word[WORD_MAX];
    char line[LINE_MAX];
    char test[LINE_MAX];

    int lineLen = 0;
    line[0] = '\0';
    int cy = y, i = 0;

    while (true) {
        int wl = 0;
        while (text[i] != '\0' && text[i] != ' ' && text[i] != '\n' && wl < WORD_MAX - 1)
            word[wl++] = text[i++];
        word[wl] = '\0';

        bool newline = (text[i] == '\n');
        bool end = (text[i] == '\0');
        if (text[i] != '\0') i++;

        int tl = 0;
        for (int k = 0; k < lineLen && tl < LINE_MAX - 1; k++) test[tl++] = line[k];
        if (lineLen > 0 && tl < LINE_MAX - 1) test[tl++] = ' ';
        for (int k = 0; k < wl && tl < LINE_MAX - 1; k++) test[tl++] = word[k];
        test[tl] = '\0';

        if (TxtW(test, fontSize) > maxW && lineLen > 0) {
            Txt(line, x, cy, fontSize, col);
            cy += fontSize + gap;
            lineLen = 0;
            for (int k = 0; k < wl && lineLen < LINE_MAX - 1; k++) line[lineLen++] = word[k];
            line[lineLen] = '\0';
        }
        else {
            lineLen = 0;
            for (int k = 0; k < tl && lineLen < LINE_MAX - 1; k++) line[lineLen++] = test[k];
            line[lineLen] = '\0';
        }

        if (newline) {
            Txt(line, x, cy, fontSize, col);
            cy += fontSize + gap;
            lineLen = 0; line[0] = '\0';
        }
        if (end) break;
    }
    if (lineLen > 0) { Txt(line, x, cy, fontSize, col); cy += fontSize + gap; }
    return cy;
}

// =====================================================================================
//  РАЗДЕЛ 20. ЭКРАН КАПСУЛЫ
// =====================================================================================

static void DrawBaseScreen()
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 8, 14, 195));
    Rectangle panel = { 90.0f, 50.0f, SCREEN_W - 180.0f, SCREEN_H - 100.0f };
    DrawPanel(panel, "ESCAPE POD - FABRICATOR", RGBA(90, 190, 255, 255));

    const char* tabs[3] = { "FABRICATOR", "STORAGE", "STATUS" };
    for (int i = 0; i < 3; i++) {
        Rectangle tb = { panel.x + 14.0f + i * 132.0f, panel.y + 44.0f, 124.0f, 28.0f };
        bool active = (g_baseTab == i);
        if (UiButton(tb, tabs[i], true, active ? RGBA(120, 255, 200, 255) : RGBA(90, 140, 190, 255)))
            g_baseTab = i;
        if (active)
            DrawRectangleRec(Rectangle{ tb.x, tb.y + tb.height + 2.0f, tb.width, 2.0f },
                RGBA(120, 255, 200, 255));
    }

    float contentY = panel.y + 86.0f;

    if (g_baseTab == 0) {
        float rowH = 44.0f;
        float listW = panel.width * 0.62f;

        // 14 рецептов больше не влезают одним экраном (влезало 11) — список
        // теперь скроллится колесом мыши и обрезается по видимой области.
        float listVisibleH = panel.height - 146.0f;
        float listTotalH = RECIPE_COUNT * rowH;
        float maxScroll = MaxF(0.0f, listTotalH - listVisibleH);
        g_fabScroll = Clampf(g_fabScroll, 0.0f, maxScroll);

        Rectangle listClip = { panel.x + 14.0f, contentY, listW, listVisibleH };
        if (maxScroll > 0.0f && CheckCollisionPointRec(GetMousePosition(), listClip))
            g_fabScroll = Clampf(g_fabScroll - GetMouseWheelMove() * 40.0f, 0.0f, maxScroll);

        BeginScissorMode((int)listClip.x, (int)listClip.y, (int)listClip.width, (int)listClip.height);
        for (int i = 0; i < RECIPE_COUNT; i++) {
            Recipe* rc = &g_recipe[i];
            float rowTop = contentY + i * rowH - g_fabScroll;
            if (rowTop + rowH < contentY || rowTop > contentY + listVisibleH) continue;   // за пределами видимой полосы
            Rectangle row = { panel.x + 14.0f, rowTop, listW, rowH - 6.0f };

            bool maxed = RecipeMaxed(i);
            bool locked = RecipeLocked(i);
            bool can = !maxed && !locked && CanAfford(i);

            DrawRectangleRounded(row, 0.15f, 6, maxed ? RGBA(16, 30, 24, 200) : RGBA(14, 18, 26, 200));
            DrawRectangleLinesEx(row, 1.2f, can ? RGBA(120, 255, 200, 140) : RGBA(60, 70, 85, 160));

            char title[96];
            if (rc->kind == RK_UPGRADE) snprintf(title, sizeof(title), "%s  MK%d", rc->name, g_player.upg[rc->param] + 1);
            else                        snprintf(title, sizeof(title), "%s", rc->name);

            Color tcol = maxed ? RGBA(120, 255, 180, 255)
                : (can ? RGBA(240, 246, 252, 255) : RGBA(140, 150, 165, 255));
            Txt(title, (int)(row.x + 12), (int)(row.y + 7), 15, tcol);

            int cost[RES_COUNT];
            GetRecipeCost(i, cost);
            float cx = row.x + 12.0f;
            if (maxed) {
                Txt("MAX LEVEL", (int)cx, (int)(row.y + 24), 11, RGBA(120, 255, 180, 200));
            }
            else if (locked) {
                Txt("LOCKED - FIND ALL 3 DRIVE FRAGMENTS", (int)cx, (int)(row.y + 24), 11,
                    RGBA(255, 150, 255, 200));
            }
            else {
                for (int rr = 0; rr < RES_COUNT; rr++) {
                    if (cost[rr] <= 0) continue;
                    bool ok = g_store[rr] >= cost[rr];
                    const char* s = TextFormat("%s %d/%d", g_resShort[rr], g_store[rr], cost[rr]);
                    Txt(s, (int)cx, (int)(row.y + 24), 11,
                        ok ? Alpha(g_resColor[rr], 0.95f) : RGBA(190, 90, 80, 230));
                    cx += TxtW(s, 11) + 14.0f;
                }
            }

            Rectangle btn = { row.x + row.width - 96.0f, row.y + 8.0f, 86.0f, row.height - 16.0f };
            const char* bl = maxed ? "DONE" : (rc->kind == RK_WARP ? "ASSEMBLE" : "BUILD");
            if (UiButton(btn, bl, can, rc->kind == RK_WARP ? RGBA(255, 130, 255, 255) : RGBA(120, 255, 200, 255)))
                DoCraft(i);
        }
        EndScissorMode();

        if (maxScroll > 0.0f) {
            // Тонкая полоса прокрутки справа от списка — единственная подсказка,
            // что рецептов больше, чем видно на экране.
            float barX = listClip.x + listClip.width - 6.0f;
            float thumbH = MaxF(24.0f, listVisibleH * (listVisibleH / listTotalH));
            float thumbY = contentY + (listVisibleH - thumbH) * (g_fabScroll / maxScroll);
            DrawRectangleRec(Rectangle{ barX, contentY, 4.0f, listVisibleH }, RGBA(40, 50, 65, 180));
            DrawRectangleRec(Rectangle{ barX, thumbY, 4.0f, thumbH }, RGBA(120, 255, 200, 200));
        }

        Rectangle info = { panel.x + panel.width * 0.66f, contentY, panel.width * 0.32f, panel.height - 150.0f };
        DrawRectangleRounded(info, 0.06f, 6, RGBA(12, 16, 24, 200));
        DrawRectangleLinesEx(info, 1.2f, RGBA(70, 110, 150, 150));

        Vector2 m = GetMousePosition();
        int hovered = -1;
        for (int i = 0; i < RECIPE_COUNT; i++) {
            float rowTop = contentY + i * rowH - g_fabScroll;
            if (rowTop + rowH < contentY || rowTop > contentY + listVisibleH) continue;
            Rectangle row = { panel.x + 14.0f, rowTop, listW, rowH - 6.0f };
            if (CheckCollisionPointRec(m, row)) hovered = i;
        }
        if (hovered >= 0) {
            Txt(g_recipe[hovered].name, (int)(info.x + 12), (int)(info.y + 12), 16, RGBA(150, 230, 255, 255));
            DrawWrapped(g_recipe[hovered].desc, (int)(info.x + 12), (int)(info.y + 38),
                (int)info.width - 24, 13, 5, RGBA(190, 205, 225, 235));
        }
        else {
            Txt("HOVER A RECIPE", (int)(info.x + 12), (int)(info.y + 12), 14, RGBA(110, 130, 155, 200));
            DrawWrapped("Ore is refined automatically when you dock. Upgrades are permanent. "
                "Deeper sectors need a stronger laser before the rock will even chip.",
                (int)(info.x + 12), (int)(info.y + 36), (int)info.width - 24, 12, 5,
                RGBA(140, 160, 185, 210));
        }

        if (g_player.warpBuilt) {
            Rectangle wb = { panel.x + panel.width * 0.66f, panel.y + panel.height - 128.0f,
                             panel.width * 0.32f, 44.0f };
            if (UiButton(wb, "ENGAGE WARP - LEAVE", true, RGBA(255, 130, 255, 255))) {
                g_warpT = 0.0f;
                Play(g_sCraft, 0.6f);
                SetState(ST_WIN);
            }
        }
    }
    else if (g_baseTab == 1) {
        Txt("POD STORAGE", (int)(panel.x + 16), (int)contentY, 16, RGBA(150, 230, 255, 255));
        for (int i = 0; i < RES_COUNT; i++) {
            float ry = contentY + 34.0f + i * 40.0f;
            DrawPoly(V2(panel.x + 34.0f, ry + 10.0f), 4, 11.0f, 45.0f, g_resColor[i]);
            Txt(g_resName[i], (int)(panel.x + 58), (int)ry, 16, RGBA(225, 235, 245, 255));
            const char* amt = TextFormat("%d", g_store[i]);
            Txt(amt, (int)(panel.x + 300 - TxtW(amt, 18)), (int)ry, 18, g_resColor[i]);

            float frac = Clampf((float)g_store[i] / 60.0f, 0.0f, 1.0f);
            DrawRectangleRec(Rectangle{ panel.x + 330.0f, ry + 6.0f, 260.0f, 10.0f }, RGBA(20, 25, 35, 200));
            DrawRectangleRec(Rectangle{ panel.x + 330.0f, ry + 6.0f, 260.0f * frac, 10.0f },
                Alpha(g_resColor[i], 0.85f));
        }

        Txt("WHERE TO FIND IT", (int)(panel.x + panel.width * 0.62f), (int)contentY, 16, RGBA(150, 230, 255, 255));
        const char* hints[RES_COUNT] = {
            "Titanium - grey rock, everywhere, densest in the belt.",
            "Ice - pale blue rock. Your oxygen comes from here.",
            "Silicon - warm sandy rock, common around the reef.",
            "Crystal - violet veins, crystal reef and deeper.",
            "Organic - nebula rock and anything with teeth.",
            "Alloy - gold veins, void trench and the core only.",
            "Plasma - pink veins, deepest sectors. Leviathans carry it."
        };
        for (int i = 0; i < RES_COUNT; i++)
            DrawWrapped(hints[i], (int)(panel.x + panel.width * 0.62f), (int)(contentY + 30.0f + i * 40.0f),
                (int)(panel.width * 0.34f), 12, 4, Alpha(g_resColor[i], 0.85f));
    }
    else {
        const char* upgNames[UPG_COUNT] = { "O2 TANK", "THRUSTERS", "HULL PLATING", "MINING LASER", "POWER CELL" };
        Txt("SUIT STATUS", (int)(panel.x + 16), (int)contentY, 16, RGBA(150, 230, 255, 255));

        for (int i = 0; i < UPG_COUNT; i++) {
            float ry = contentY + 34.0f + i * 40.0f;
            Txt(upgNames[i], (int)(panel.x + 22), (int)ry, 15, RGBA(225, 235, 245, 255));
            for (int l = 0; l < 4; l++)
                DrawRectangleRec(Rectangle{ panel.x + 220.0f + l * 34.0f, ry + 2.0f, 28.0f, 14.0f },
                    (l <= g_player.upg[i]) ? RGBA(120, 255, 200, 255) : RGBA(50, 60, 75, 255));
            Txt(TextFormat("MK%d", g_player.upg[i] + 1), (int)(panel.x + 372), (int)ry, 14,
                RGBA(170, 200, 225, 230));
        }

        float sy = contentY + 34.0f + UPG_COUNT * 40.0f + 14.0f;
        Txt(TextFormat("SCANNER   : %s", g_player.hasScanner ? "INSTALLED" : "MISSING"),
            (int)(panel.x + 22), (int)sy, 14,
            g_player.hasScanner ? RGBA(120, 255, 200, 255) : RGBA(150, 160, 175, 220));
        Txt(TextFormat("RAD SUIT  : %s", g_player.hasRadSuit ? "SEALED" : "MISSING"),
            (int)(panel.x + 22), (int)(sy + 22), 14,
            g_player.hasRadSuit ? RGBA(120, 255, 200, 255) : RGBA(150, 160, 175, 220));
        Txt(TextFormat("PULSE GUN : %s", g_player.hasBlaster ? "INSTALLED" : "MISSING"),
            (int)(panel.x + 22), (int)(sy + 44), 14,
            g_player.hasBlaster ? RGBA(120, 255, 200, 255) : RGBA(150, 160, 175, 220));
        {
            // Снова вручную через snprintf — то же самое правило, что и в HUD:
            // TextFormat нельзя надёжно вкладывать в TextFormat, у него общий
            // кольцевой буфer на несколько последних вызовов.
            char torpLine[48];
            if (g_player.hasTorpedo) snprintf(torpLine, sizeof(torpLine), "TORPEDOES : x%d STOCKED", g_player.torpedoAmmo);
            else                     snprintf(torpLine, sizeof(torpLine), "TORPEDOES : MISSING");
            Txt(torpLine, (int)(panel.x + 22), (int)(sy + 66), 14,
                g_player.hasTorpedo ? RGBA(120, 255, 200, 255) : RGBA(150, 160, 175, 220));
        }

        const char* frag[3] = { "WARP COIL", "CONTAINMENT CELL", "IGNITION CORE" };
        Txt("DRIVE FRAGMENTS", (int)(panel.x + panel.width * 0.55f), (int)contentY, 16, RGBA(255, 150, 255, 255));
        for (int i = 0; i < 3; i++)
            Txt(TextFormat("%s  %s", g_player.warpPart[i] ? "[X]" : "[ ]", frag[i]),
                (int)(panel.x + panel.width * 0.55f), (int)(contentY + 32.0f + i * 26.0f), 15,
                g_player.warpPart[i] ? RGBA(255, 180, 255, 255) : RGBA(130, 115, 145, 220));

        Txt("RUN STATS", (int)(panel.x + panel.width * 0.55f), (int)(contentY + 130.0f), 16,
            RGBA(150, 230, 255, 255));
        Txt(TextFormat("TIME SURVIVED : %d:%02d", (int)(g_timeAlive / 60.0f), (int)fmodf(g_timeAlive, 60.0f)),
            (int)(panel.x + panel.width * 0.55f), (int)(contentY + 158.0f), 13, RGBA(190, 205, 225, 230));
        Txt(TextFormat("ROCKS MINED   : %d", g_mined),
            (int)(panel.x + panel.width * 0.55f), (int)(contentY + 178.0f), 13, RGBA(190, 205, 225, 230));
        Txt(TextFormat("KILLS         : %d", g_killed),
            (int)(panel.x + panel.width * 0.55f), (int)(contentY + 198.0f), 13, RGBA(190, 205, 225, 230));
        Txt(TextFormat("DEATHS        : %d", g_deaths),
            (int)(panel.x + panel.width * 0.55f), (int)(contentY + 218.0f), 13, RGBA(190, 205, 225, 230));
        Txt(TextFormat("DEEPEST       : %.2f km", g_deepest / 1000.0f),
            (int)(panel.x + panel.width * 0.55f), (int)(contentY + 238.0f), 13, RGBA(190, 205, 225, 230));
    }

    Rectangle undock = { panel.x + panel.width - 180.0f, panel.y + panel.height - 46.0f, 166.0f, 34.0f };
    // Клавиша [E] здесь НЕ проверяется — весь ввод живёт в главном цикле.
    if (UiButton(undock, "UNDOCK  [E]", true, RGBA(120, 200, 255, 255))) {
        Play(g_sDock, 1.3f);
        SetState(ST_PLAY);
    }
    Txt("Docking refills oxygen, energy and slowly repairs the hull.",
        (int)(panel.x + 16), (int)(panel.y + panel.height - 36.0f), 12, RGBA(120, 145, 170, 200));
}

// =====================================================================================
//  РАЗДЕЛ 21. БОРТЖУРНАЛ
// =====================================================================================

static void DrawDatabankScreen()
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 8, 14, 210));
    Rectangle panel = { 110.0f, 60.0f, SCREEN_W - 220.0f, SCREEN_H - 120.0f };
    DrawPanel(panel, "DATABANK", RGBA(150, 160, 255, 255));

    float listX = panel.x + 14.0f;
    float listY = panel.y + 48.0f;
    float listW = 300.0f;

    int unlockedCount = 0;
    for (int i = 0; i < LOG_COUNT; i++) if (g_logUnlocked[i]) unlockedCount++;
    Txt(TextFormat("ENTRIES %d / %d", unlockedCount, LOG_COUNT), (int)listX, (int)(listY - 20.0f), 12,
        RGBA(150, 160, 200, 220));

    for (int i = 0; i < LOG_COUNT; i++) {
        Rectangle row = { listX, listY + i * 30.0f, listW, 26.0f };
        bool unlocked = g_logUnlocked[i];
        bool sel = (g_logSel == i);

        if (unlocked && CheckCollisionPointRec(GetMousePosition(), row) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_stateAge > 0) {
            g_logSel = i;
            Play(g_sScan, 1.4f);
        }

        if (sel) DrawRectangleRounded(row, 0.25f, 4, RGBA(60, 60, 120, 190));
        Color tc = unlocked ? (sel ? RGBA(230, 230, 255, 255) : RGBA(175, 180, 215, 240))
            : RGBA(78, 82, 100, 200);
        Txt(unlocked ? g_log[i].title : "- ENCRYPTED -", (int)(row.x + 10), (int)(row.y + 6), 13, tc);
    }

    Rectangle body = { listX + listW + 20.0f, listY, panel.width - listW - 48.0f, panel.height - 110.0f };
    DrawRectangleRounded(body, 0.04f, 6, RGBA(10, 12, 22, 210));
    DrawRectangleLinesEx(body, 1.2f, RGBA(80, 85, 140, 150));

    if (g_logUnlocked[g_logSel]) {
        Txt(g_log[g_logSel].title, (int)(body.x + 18), (int)(body.y + 16), 20, RGBA(200, 205, 255, 255));
        DrawLineEx(V2(body.x + 18, body.y + 44), V2(body.x + body.width - 18, body.y + 44), 1.5f,
            RGBA(90, 95, 160, 180));
        DrawWrapped(g_log[g_logSel].body, (int)(body.x + 18), (int)(body.y + 58),
            (int)body.width - 36, 15, 7, RGBA(200, 210, 235, 235));
    }
    else {
        Txt("ENTRY NOT RECOVERED", (int)(body.x + 18), (int)(body.y + 16), 18, RGBA(110, 115, 145, 220));
        DrawWrapped("Aim at lifeforms and derelict beacons and hold RIGHT MOUSE to decrypt this entry. "
            "Without a scanner module you must fly straight into a beacon to pull its log.",
            (int)(body.x + 18), (int)(body.y + 48), (int)body.width - 36, 14, 6,
            RGBA(120, 128, 155, 220));
    }

    Txt("[TAB] or [ESC] to close", (int)(panel.x + 16), (int)(panel.y + panel.height - 30.0f), 13,
        RGBA(120, 128, 165, 210));
}

// =====================================================================================
//  РАЗДЕЛ 22. ЭКРАНЫ: МЕНЮ, СПРАВКА, ПАУЗА, СМЕРТЬ, ПОБЕДА
// =====================================================================================

static void DrawMenuBackdrop()
{
    // Медленный облёт вокруг капсулы вместо статичной картинки
    float t = g_globalTime * 0.12f;
    g_cam.position = V3(cosf(t) * 620.0f, 180.0f + sinf(t * 0.7f) * 90.0f, sinf(t) * 620.0f);
    g_cam.target = V3(0, 0, 0);
    g_cam.up = V3(0, 1, 0);
    g_cam.fovy = 60.0f;
    g_cam.projection = CAMERA_PERSPECTIVE;
    g_camFwd = VNorm(VSub(g_cam.target, g_cam.position));
    g_projScale = (SCREEN_H * 0.5f) / tanf(g_cam.fovy * 0.5f * DEG2RAD);

    ClearBackground(g_bg);
    // Вертикальный градиент вместо плоской заливки — небо не выглядит
    // пустой чёрной стеной, даже когда звёзды и туманности ещё не легли.
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, g_bgTop, g_bg);
    DrawNebulaBackdrop();
    DrawStarfield();

    BeginMode3D(g_cam);
    DrawSectorRings3D();
    DrawPod3D();
    DrawAsteroids3D();
    EndMode3D();

    DrawGlows2D();
    DrawCircleGradient(SCREEN_W / 2, SCREEN_H / 2, SCREEN_W * 0.78f, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 150));
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 8, 16, 80));
}

static void DrawMenuScreen()
{
    DrawMenuBackdrop();

    float cy = SCREEN_H * 0.16f;
    const char* title = "A S T R O N A U T I C A";
    int w = TxtW(title, 52);
    float glow = 0.6f + 0.4f * sinf(g_globalTime * 1.6f);
    DrawCircleGradient(SCREEN_W / 2, (int)cy + 26, 340.0f,
        RGBA(70, 150, 230, (int)(45 * glow)), RGBA(70, 150, 230, 0));
    Txt(title, SCREEN_W / 2 - w / 2 + 2, (int)cy + 2, 52, RGBA(20, 60, 110, 220));
    Txt(title, SCREEN_W / 2 - w / 2, (int)cy, 52, RGBA(215, 238, 255, 255));

    const char* sub = "3D - survive the wreck field, build the drive, leave";
    Txt(sub, SCREEN_W / 2 - TxtW(sub, 16) / 2, (int)cy + 64, 16, RGBA(130, 175, 215, 220));

    Rectangle b1 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.50f, 260.0f, 46.0f };
    Rectangle b2 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.50f + 58.0f, 260.0f, 46.0f };
    Rectangle b3 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.50f + 116.0f, 260.0f, 46.0f };

    if (UiButton(b1, "LAUNCH", true, RGBA(120, 255, 200, 255))) {
        ResetRun();
        Play(g_sDock, 0.8f);
        SetState(ST_PLAY);
    }
    if (UiButton(b2, "HOW TO PLAY", true, RGBA(120, 200, 255, 255))) { g_prevSt = ST_MENU; SetState(ST_HELP); }
    if (UiButton(b3, "QUIT", true, RGBA(255, 120, 120, 255))) g_quit = true;

    const char* foot = "C++ / raylib - single file, no assets, no shaders";
    Txt(foot, SCREEN_W / 2 - TxtW(foot, 12) / 2, SCREEN_H - 38, 12, RGBA(90, 115, 145, 200));
}

static void DrawHelpScreen()
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 8, 14, 218));
    Rectangle panel = { 130.0f, 46.0f, SCREEN_W - 260.0f, SCREEN_H - 92.0f };
    DrawPanel(panel, "HOW TO PLAY", RGBA(120, 200, 255, 255));

    float x = panel.x + 24.0f;
    float y = panel.y + 48.0f;

    Txt("CONTROLS", (int)x, (int)y, 18, RGBA(150, 230, 255, 255)); y += 26.0f;
    const char* ctrl[] = {
        "MOUSE         look - yaw and pitch, no roll",
        "W / S         thrust forward / reverse",
        "A / D         strafe left / right",
        "SPACE / C     climb / dive",
        "SHIFT         boost (burns energy and oxygen)",
        "X             stabilise - kill your drift",
        "1 / 2 / 3     mining laser / pulse cannon / torpedo launcher",
        "LEFT MOUSE    fire the equipped weapon",
        "RIGHT MOUSE   scanner (must be built first)",
        "E             dock with the pod inside the blue sphere",
        "F / Q / B     repair kit / oxygen capsule / deploy buoy",
        "V             cockpit or chase camera",
        "TAB databank      ESC pause"
    };
    for (int i = 0; i < 13; i++) { Txt(ctrl[i], (int)x, (int)y, 14, RGBA(200, 214, 232, 235)); y += 19.0f; }

    y += 12.0f;
    Txt("SURVIVAL", (int)x, (int)y, 18, RGBA(150, 230, 255, 255)); y += 26.0f;
    DrawWrapped(
        "The field is a thick disc, not an endless void: everything sits within about a "
        "kilometre of the plane, so the top-down minimap plus the altitude strip beside it tell "
        "you everything you need. Five sectors ring the pod, and the further out you go, the "
        "harder the rock, the richer the ore and the worse the things living in it.\n"
        "Oxygen is the clock. It refills only at the pod or inside the halo of a deployed buoy, "
        "and every trip must be measured by the return leg. When the tank empties the hull starts "
        "taking damage; when the hull is gone you wake up at the pod without your cargo.\n"
        "Three warp drive fragments lie in the nebula, the trench and the core. Collect all three, "
        "assemble the drive at the pod, and engage it to end the run.\n"
        "The mining laser is a mining tool first: it barely scratches living things. Fabricate the "
        "Pulse Cannon for fast, cheap damage against lifeforms, and the Torpedo Launcher for a slow, "
        "ammo-limited shot that locks onto whatever is in front of you and explodes on impact.\n"
        "Predators hunt by sound. Cutting your thrusters roughly halves the range at which they "
        "notice you, which is the only reason anyone survives the trench. Swarmers move in packs "
        "and rarely attack alone; the Void Stalker is nearly invisible until it commits to a charge; "
        "and the Void Maw telegraphs its bite with a glowing mouth half a second before it lands.",
        (int)x, (int)y, (int)panel.width - 48, 15, 6, RGBA(190, 205, 225, 235));

    Rectangle back = { panel.x + panel.width - 160.0f, panel.y + panel.height - 50.0f, 146.0f, 36.0f };
    if (UiButton(back, "BACK  [ESC]", true, RGBA(120, 200, 255, 255))) SetState(g_prevSt);
}

static void DrawPauseScreen()
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 8, 14, 175));
    const char* t = "PAUSED";
    Txt(t, SCREEN_W / 2 - TxtW(t, 46) / 2, (int)(SCREEN_H * 0.26f), 46, RGBA(225, 238, 250, 255));

    Rectangle b1 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.42f, 260.0f, 44.0f };
    Rectangle b2 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.42f + 54.0f, 260.0f, 44.0f };
    Rectangle b3 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.42f + 108.0f, 260.0f, 44.0f };

    if (UiButton(b1, "RESUME", true, RGBA(120, 255, 200, 255))) SetState(ST_PLAY);
    if (UiButton(b2, "HOW TO PLAY", true, RGBA(120, 200, 255, 255))) { g_prevSt = ST_PAUSE; SetState(ST_HELP); }
    if (UiButton(b3, "ABANDON RUN", true, RGBA(255, 120, 120, 255))) SetState(ST_MENU);

    Txt(TextFormat("TIME %d:%02d   MINED %d   KILLS %d   DEEPEST %.2f km",
        (int)(g_timeAlive / 60.0f), (int)fmodf(g_timeAlive, 60.0f), g_mined, g_killed,
        g_deepest / 1000.0f),
        SCREEN_W / 2 - 220, (int)(SCREEN_H * 0.72f), 14, RGBA(140, 165, 195, 220));
}

static void DrawDeathScreen()
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(40, 4, 8, 190));

    const char* t = "SIGNAL LOST";
    Txt(t, SCREEN_W / 2 - TxtW(t, 52) / 2, (int)(SCREEN_H * 0.26f), 52, RGBA(255, 120, 110, 255));
    Txt(g_deathCause, SCREEN_W / 2 - TxtW(g_deathCause, 20) / 2, (int)(SCREEN_H * 0.26f) + 62, 20,
        RGBA(255, 180, 170, 220));

    Txt(TextFormat("SURVIVED %d:%02d    DEEPEST %.2f km    DEATH #%d",
        (int)(g_timeAlive / 60.0f), (int)fmodf(g_timeAlive, 60.0f), g_deepest / 1000.0f, g_deaths),
        SCREEN_W / 2 - 200, (int)(SCREEN_H * 0.44f), 15, RGBA(220, 190, 190, 220));

    Rectangle b1 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.56f, 260.0f, 46.0f };
    Rectangle b2 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.56f + 58.0f, 260.0f, 46.0f };

    if (UiButton(b1, "REBOOT AT POD  [R]", true, RGBA(255, 160, 140, 255))) RespawnAtPod();
    if (UiButton(b2, "MAIN MENU", true, RGBA(160, 160, 190, 255)))          SetState(ST_MENU);

    const char* note = "Your cargo hold is lost, but the pod keeps everything you already stored.";
    Txt(note, SCREEN_W / 2 - TxtW(note, 13) / 2, (int)(SCREEN_H * 0.56f + 126.0f), 13, RGBA(190, 160, 160, 200));
}

static void DrawWinScreen(float dt)
{
    g_warpT += dt;

    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 4, 16, 205));

    float t = Clampf(g_warpT / 3.0f, 0.0f, 1.0f);
    for (int i = 0; i < 90; i++) {
        float a = (float)i / 90.0f * 2.0f * PI + g_warpT * 0.7f;
        float len = 120.0f + 900.0f * t * (0.4f + 0.6f * fmodf((float)i * 0.37f, 1.0f));
        Vector2 c = V2(SCREEN_W * 0.5f, SCREEN_H * 0.5f);
        Vector2 p0 = V2(c.x + cosf(a) * 70.0f, c.y + sinf(a) * 70.0f);
        Vector2 p1 = V2(c.x + cosf(a) * (70.0f + len), c.y + sinf(a) * (70.0f + len));
        DrawLineEx(p0, p1, 2.0f, RGBA(180, 120, 255, (int)(140 * t)));
    }
    DrawCircleGradient(SCREEN_W / 2, SCREEN_H / 2, 200.0f + 120.0f * t,
        RGBA(255, 220, 255, (int)(200 * t)), RGBA(180, 120, 255, 0));

    const char* t1 = "WARP ENGAGED";
    Txt(t1, SCREEN_W / 2 - TxtW(t1, 54) / 2, (int)(SCREEN_H * 0.20f), 54, RGBA(255, 210, 255, 255));

    const char* t2 = "The wreck field closes behind you. Nobody else answers the channel.";
    Txt(t2, SCREEN_W / 2 - TxtW(t2, 16) / 2, (int)(SCREEN_H * 0.20f) + 70, 16, RGBA(220, 190, 240, 230));

    int logs = 0;
    for (int i = 0; i < LOG_COUNT; i++) if (g_logUnlocked[i]) logs++;

    float sy = SCREEN_H * 0.46f;
    Txt(TextFormat("TIME SURVIVED   %d:%02d", (int)(g_timeAlive / 60.0f), (int)fmodf(g_timeAlive, 60.0f)),
        SCREEN_W / 2 - 150, (int)sy, 17, RGBA(230, 220, 245, 235));
    Txt(TextFormat("ROCKS MINED     %d", g_mined), SCREEN_W / 2 - 150, (int)(sy + 26), 17, RGBA(230, 220, 245, 235));
    Txt(TextFormat("KILLS           %d", g_killed), SCREEN_W / 2 - 150, (int)(sy + 52), 17, RGBA(230, 220, 245, 235));
    Txt(TextFormat("DEATHS          %d", g_deaths), SCREEN_W / 2 - 150, (int)(sy + 78), 17, RGBA(230, 220, 245, 235));
    Txt(TextFormat("DATABANK        %d / %d", logs, LOG_COUNT),
        SCREEN_W / 2 - 150, (int)(sy + 104), 17, RGBA(230, 220, 245, 235));
    Txt(TextFormat("DEEPEST         %.2f km", g_deepest / 1000.0f),
        SCREEN_W / 2 - 150, (int)(sy + 130), 17, RGBA(230, 220, 245, 235));

    Rectangle b1 = { SCREEN_W * 0.5f - 130.0f, SCREEN_H * 0.82f, 260.0f, 44.0f };
    if (UiButton(b1, "MAIN MENU", true, RGBA(255, 180, 255, 255))) SetState(ST_MENU);
}

// =====================================================================================
//  РАЗДЕЛ 23. ГЛАВНЫЙ ЦИКЛ
//
//  Строгое правило: КЛАВИАТУРА ЧИТАЕТСЯ ТОЛЬКО ЗДЕСЬ. switch выполняет ровно одну
//  ветку за кадр, поэтому нажатие, открывшее экран, не может в том же кадре его
//  закрыть. Кнопки мыши в интерфейсе дополнительно ждут g_stateAge > 0.
// =====================================================================================

static void UpdatePlaying(float dt)
{
    g_timeAlive += dt;

    UpdatePlayer(dt);
    if (g_state != ST_PLAY) return;   // игрок умер или пристыковался

    UpdateWeapons(dt);
    UpdateScanner(dt);
    UpdateAsteroids(dt);
    UpdateCreatures(dt);
    UpdateProjectiles(dt);
    UpdatePickups(dt);
    UpdateSites(dt);
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "ASTRONAUTICA 3D - a Subnautica-like, in space");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);           // ESC отдан под паузу

    InitTables();
    InitAudio();
    InitMeshes();
    ResetRun();

    g_state = ST_MENU;
    g_stateAge = 0;
    EnableCursor();

    while (!WindowShouldClose() && !g_quit)
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        g_globalTime += dt;
        g_stateAge++;

        // --------------------------- ОБНОВЛЕНИЕ ---------------------------
        switch (g_state)
        {
        case ST_PLAY:
            if (IsKeyPressed(KEY_ESCAPE)) { SetState(ST_PAUSE); break; }
            if (IsKeyPressed(KEY_TAB)) { g_dbReturn = ST_PLAY; SetState(ST_DATABANK); break; }
            UpdatePlaying(dt);
            UpdateParticles(dt);
            if (g_state == ST_PLAY) UpdateCamera3D(dt);
            UpdateToasts(dt);
            break;

        case ST_BASE:
            if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE)) {
                Play(g_sDock, 1.3f);
                SetState(ST_PLAY);
                break;
            }
            if (IsKeyPressed(KEY_TAB)) { g_dbReturn = ST_BASE; SetState(ST_DATABANK); break; }

            g_player.o2 = Approach(g_player.o2, g_player.o2Max, 55.0f, dt);
            g_player.energy = Approach(g_player.energy, g_player.energyMax, 40.0f, dt);
            g_player.hull = Approach(g_player.hull, g_player.hullMax, 10.0f, dt);
            g_player.vel = VMul(g_player.vel, 1.0f / (1.0f + 6.0f * dt));
            UpdateParticles(dt);
            UpdateCamera3D(dt);
            UpdateToasts(dt);
            break;

        case ST_DATABANK:
            if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) { SetState(g_dbReturn); break; }
            if (IsKeyPressed(KEY_DOWN))
                for (int i = g_logSel + 1; i < LOG_COUNT; i++) if (g_logUnlocked[i]) { g_logSel = i; break; }
            if (IsKeyPressed(KEY_UP))
                for (int i = g_logSel - 1; i >= 0; i--) if (g_logUnlocked[i]) { g_logSel = i; break; }
            UpdateParticles(dt);
            break;

        case ST_PAUSE:
            if (IsKeyPressed(KEY_ESCAPE)) SetState(ST_PLAY);
            break;

        case ST_HELP:
            if (IsKeyPressed(KEY_ESCAPE)) SetState(g_prevSt);
            break;

        case ST_DEAD:
            if (IsKeyPressed(KEY_R)) { RespawnAtPod(); break; }
            UpdateParticles(dt);
            break;

        case ST_WIN:
        case ST_MENU:
            UpdateParticles(dt);
            break;
        }

        // --------------------------- ОТРИСОВКА ---------------------------
        BeginDrawing();

        switch (g_state)
        {
        case ST_MENU:
            DrawMenuScreen();
            break;

        case ST_HELP:
            if (g_prevSt == ST_MENU) DrawMenuBackdrop();
            else                     DrawWorldFrame();
            DrawHelpScreen();
            break;

        case ST_PLAY:
            DrawWorldFrame();
            break;

        case ST_BASE:
            DrawWorldFrame();
            DrawBaseScreen();
            break;

        case ST_DATABANK:
            DrawWorldFrame();
            DrawDatabankScreen();
            break;

        case ST_PAUSE:
            DrawWorldFrame();
            DrawPauseScreen();
            break;

        case ST_DEAD:
            DrawWorldFrame();
            DrawDeathScreen();
            break;

        case ST_WIN:
            ClearBackground(g_bg);
            // Вертикальный градиент вместо плоской заливки — небо не выглядит
            // пустой чёрной стеной, даже когда звёзды и туманности ещё не легли.
            DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, g_bgTop, g_bg);
            DrawStarfield();
            DrawWinScreen(dt);
            break;
        }

        EndDrawing();
    }

    UnloadMeshes();

    if (g_audioOk) {
        UnloadSound(g_sMine);  UnloadSound(g_sBreak); UnloadSound(g_sPick);
        UnloadSound(g_sHurt);  UnloadSound(g_sCraft); UnloadSound(g_sScan);
        UnloadSound(g_sDock);  UnloadSound(g_sRoar);  UnloadSound(g_sShoot);
        UnloadSound(g_sDeath);
        CloseAudioDevice();
    }

    CloseWindow();
    return 0;
}