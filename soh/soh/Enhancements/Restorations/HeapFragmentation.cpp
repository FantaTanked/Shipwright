#include <libultraship/bridge/consolevariablebridge.h>
#include <cstring>
#include <vector>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetAlternateHeaders.h"
#include "soh/resource/type/scenecommand/SetSpecialObjects.h"
#include "soh/ShipInit.hpp"
#include "HeapFragmentation.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
}

extern "C" PlayState* gPlayState;

#define CVAR_HEAP_FRAGMENTATION_NAME CVAR_ENHANCEMENT("HeapFragmentation")
#define HEAP_FRAGMENTATION_DEFAULT 0
#define CVAR_HEAP_FRAGMENTATION_VALUE CVarGetInteger(CVAR_HEAP_FRAGMENTATION_NAME, HEAP_FRAGMENTATION_DEFAULT)

#ifdef _WIN64
// Pin the shadow heap at a fixed VA (like the real heaps in soh/src/buffers/heaps.c) so a savestate can
// restore its internal pointers with no relocation. Third base, clear of the real heaps.
extern "C" void* VirtualAlloc(void* lpAddress, size_t dwSize, unsigned long flAllocationType, unsigned long flProtect);
extern "C" int VirtualFree(void* lpAddress, size_t dwSize, unsigned long dwFreeType);
#define HF_MEM_COMMIT 0x00001000ul
#define HF_MEM_RESERVE 0x00002000ul
#define HF_MEM_RELEASE 0x00008000ul
#define HF_PAGE_READWRITE 0x04ul
#define HF_SHADOW_HEAP_FIXED_BASE ((void*)(uintptr_t)0x0000202000000000ull)
#endif

Arena sHeapFragmentationSystemArena;
Arena sHeapFragmentationZeldaArena;
static TwoHeadArena sGameStateTHA;
static uintptr_t sAbsoluteSpacePtr;

// Shadow heap backing buffer: pinned at the fixed VA on _WIN64 (sShadowPinned), else the aligned allocator.
// sShadowHeapSize is the __osMallocInit'd span; savestated via Serialize/DeserializeShadow below.
static u8* sHeapFragmentationHeap = nullptr;
static bool sShadowPinned = false;
static uint32_t sShadowHeapSize = 0;

static std::unordered_map<uintptr_t, uintptr_t> sHeapFragmentationZeldaArenaMap;
static std::unordered_map<uint16_t, uintptr_t> sHeapFragmentationRegisteredOverlays;

// CONSTS

// Absolute-space reservation shared by absolute-alloc actors (NTSC 1.0). Actor alloc types use the
// canonical AllocType enum (ALLOCTYPE_NORMAL/ABSOLUTE/PERMANENT) from z64actor.h.
#define ACTOROVL_ABSOLUTE_SPACE_SIZE 0x24E0

// NTSC 1.0 gamestate arena size. The port reallocs 2x this for PC headroom; the shadow models the console's
// single allocation so the actor arena emerges at the console size -- the baseline the whole model rests on.
#define HF_GAMESTATE_ARENA_SIZE 0x1D4790

// NTSC 1.0 Address
#define _buffersSegmentEnd 0x801c6e60
#define _ovl_kaleido_scopeSegmentStart 0x808137c0
#define _ovl_kaleido_scopeSegmentEnd 0x808301c0
#define _ovl_player_actorSegmentStart 0x808301c0
#define _ovl_player_actorSegmentEnd 0x808567f0

#define DEFINE_ACTOR_INTERNAL(name, id, allocType) { id, allocType },
#define DEFINE_ACTOR(name, id, allocType) { id, allocType },
#define DEFINE_ACTOR_UNSET(_0)

static const std::unordered_map<int16_t, uint16_t> sHeapFragmentationActorTable = {
#include "tables/actor_table.h"
};

#undef DEFINE_ACTOR_INTERNAL
#undef DEFINE_ACTOR_UNSET
#undef DEFINE_ACTOR

// Using sizes from NTSC 1.0
static const std::unordered_map<int16_t, size_t> sHeapFragmentationActorOverlaySizes = {
    { ACTOR_PLAYER, 0x0 },
    { ACTOR_EN_TEST, 0x58B0 },
    { ACTOR_EN_GIRLA, 0x2920 },
    { ACTOR_EN_PART, 0x1610 },
    { ACTOR_EN_LIGHT, 0xDF0 },
    { ACTOR_EN_DOOR, 0xE30 },
    { ACTOR_EN_BOX, 0x1B40 },
    { ACTOR_BG_DY_YOSEIZO, 0x2D50 },
    { ACTOR_BG_HIDAN_FIREWALL, 0x760 },
    { ACTOR_EN_POH, 0x4190 },
    { ACTOR_EN_OKUTA, 0x25E0 },
    { ACTOR_BG_YDAN_SP, 0x1770 },
    { ACTOR_EN_BOM, 0xED0 },
    { ACTOR_EN_WALLMAS, 0x1A10 },
    { ACTOR_EN_DODONGO, 0x2DA0 },
    { ACTOR_EN_FIREFLY, 0x2170 },
    { ACTOR_EN_HORSE, 0xC220 },
    { ACTOR_EN_ITEM00, 0x0 },
    { ACTOR_EN_ARROW, 0x16F0 },
    { ACTOR_EN_ELF, 0x49C0 },
    { ACTOR_EN_NIW, 0x3330 },
    { ACTOR_EN_TITE, 0x2DA0 },
    { ACTOR_EN_REEBA, 0x1A70 },
    { ACTOR_EN_PEEHAT, 0x3700 },
    { ACTOR_EN_BUTTE, 0x15D0 },
    { ACTOR_EN_INSECT, 0x2520 },
    { ACTOR_EN_FISH, 0x2110 },
    { ACTOR_EN_HOLL, 0xFD0 },
    { ACTOR_EN_SCENE_CHANGE, 0x130 },
    { ACTOR_EN_ZF, 0x6B00 },
    { ACTOR_EN_HATA, 0x590 },
    { ACTOR_BOSS_DODONGO, 0x9AE0 },
    { ACTOR_BOSS_GOMA, 0x5F80 },
    { ACTOR_EN_ZL1, 0x3E00 },
    { ACTOR_EN_VIEWER, 0x2ED0 },
    { ACTOR_EN_GOMA, 0x2C90 },
    { ACTOR_BG_PUSHBOX, 0x300 },
    { ACTOR_EN_BUBBLE, 0x1420 },
    { ACTOR_DOOR_SHUTTER, 0x2280 },
    { ACTOR_EN_DODOJR, 0x1EA0 },
    { ACTOR_EN_BDFIRE, 0xB90 },
    { ACTOR_EN_BOOM, 0x8C0 },
    { ACTOR_EN_TORCH2, 0x27A0 },
    { ACTOR_EN_BILI, 0x22D0 },
    { ACTOR_EN_TP, 0x1E50 },
    { ACTOR_EN_ST, 0x2C70 },
    { ACTOR_EN_BW, 0x3360 },
    { ACTOR_EN_A_OBJ, 0x0 },
    { ACTOR_EN_EIYER, 0x1C60 },
    { ACTOR_EN_RIVER_SOUND, 0x990 },
    { ACTOR_EN_HORSE_NORMAL, 0x2620 },
    { ACTOR_EN_OSSAN, 0x65E0 },
    { ACTOR_BG_TREEMOUTH, 0x1650 },
    { ACTOR_BG_DODOAGO, 0xDB0 },
    { ACTOR_BG_HIDAN_DALM, 0x850 },
    { ACTOR_BG_HIDAN_HROCK, 0x830 },
    { ACTOR_EN_HORSE_GANON, 0xD80 },
    { ACTOR_BG_HIDAN_ROCK, 0x10F0 },
    { ACTOR_BG_HIDAN_RSEKIZOU, 0xBE0 },
    { ACTOR_BG_HIDAN_SEKIZOU, 0x1450 },
    { ACTOR_BG_HIDAN_SIMA, 0xF20 },
    { ACTOR_BG_HIDAN_SYOKU, 0x460 },
    { ACTOR_EN_XC, 0x6790 },
    { ACTOR_BG_HIDAN_CURTAIN, 0xAA0 },
    { ACTOR_BG_SPOT00_HANEBASI, 0x1110 },
    { ACTOR_EN_MB, 0x4140 },
    { ACTOR_EN_BOMBF, 0x1470 },
    { ACTOR_EN_ZL2, 0x4730 },
    { ACTOR_BG_HIDAN_FSLIFT, 0x4D0 },
    { ACTOR_EN_OE2, 0xE0 },
    { ACTOR_BG_YDAN_HASI, 0x7B0 },
    { ACTOR_BG_YDAN_MARUTA, 0x6E0 },
    { ACTOR_BOSS_GANONDROF, 0x4D70 },
    { ACTOR_EN_AM, 0x2400 },
    { ACTOR_EN_DEKUBABA, 0x3AA0 },
    { ACTOR_EN_M_FIRE1, 0x1A0 },
    { ACTOR_EN_M_THUNDER, 0x15F0 },
    { ACTOR_BG_DDAN_JD, 0x650 },
    { ACTOR_BG_BREAKWALL, 0xE70 },
    { ACTOR_EN_JJ, 0x15D0 },
    { ACTOR_EN_HORSE_ZELDA, 0xAF0 },
    { ACTOR_BG_DDAN_KD, 0x8F0 },
    { ACTOR_DOOR_WARP1, 0x42B0 },
    { ACTOR_OBJ_SYOKUDAI, 0xC40 },
    { ACTOR_ITEM_B_HEART, 0x3F0 },
    { ACTOR_EN_DEKUNUTS, 0x1800 },
    { ACTOR_BG_MENKURI_KAITEN, 0x190 },
    { ACTOR_BG_MENKURI_EYE, 0x4A0 },
    { ACTOR_EN_VALI, 0x26A0 },
    { ACTOR_BG_MIZU_MOVEBG, 0x11A0 },
    { ACTOR_BG_MIZU_WATER, 0xCD0 },
    { ACTOR_ARMS_HOOK, 0xD60 },
    { ACTOR_EN_FHG, 0x2930 },
    { ACTOR_BG_MORI_HINERI, 0xCD0 },
    { ACTOR_EN_BB, 0x3CD0 },
    { ACTOR_BG_TOKI_HIKARI, 0xDA0 },
    { ACTOR_EN_YUKABYUN, 0x610 },
    { ACTOR_BG_TOKI_SWD, 0x1650 },
    { ACTOR_EN_FHG_FIRE, 0x2620 },
    { ACTOR_BG_MJIN, 0x3E0 },
    { ACTOR_BG_HIDAN_KOUSI, 0x580 },
    { ACTOR_DOOR_TOKI, 0x160 },
    { ACTOR_BG_HIDAN_HAMSTEP, 0xE90 },
    { ACTOR_EN_BIRD, 0x4C0 },
    { ACTOR_EN_WOOD02, 0x11E0 },
    { ACTOR_EN_LIGHTBOX, 0x480 },
    { ACTOR_EN_PU_BOX, 0x340 },
    { ACTOR_EN_TRAP, 0x12A0 },
    { ACTOR_EN_AROW_TRAP, 0x150 },
    { ACTOR_EN_VASE, 0x100 },
    { ACTOR_EN_TA, 0x39C0 },
    { ACTOR_EN_TK, 0x1E30 },
    { ACTOR_BG_MORI_BIGST, 0x930 },
    { ACTOR_BG_MORI_ELEVATOR, 0xAF0 },
    { ACTOR_BG_MORI_KAITENKABE, 0x660 },
    { ACTOR_BG_MORI_RAKKATENJO, 0x970 },
    { ACTOR_EN_VM, 0x18B0 },
    { ACTOR_DEMO_EFFECT, 0x5B00 },
    { ACTOR_DEMO_KANKYO, 0x3D00 },
    { ACTOR_BG_HIDAN_FWBIG, 0xCE0 },
    { ACTOR_EN_FLOORMAS, 0x33E0 },
    { ACTOR_EN_HEISHI1, 0x1510 },
    { ACTOR_EN_RD, 0x28B0 },
    { ACTOR_EN_PO_SISTERS, 0x4CF0 },
    { ACTOR_BG_HEAVY_BLOCK, 0x18F0 },
    { ACTOR_BG_PO_EVENT, 0x1E40 },
    { ACTOR_OBJ_MURE, 0x1010 },
    { ACTOR_EN_SW, 0x37F0 },
    { ACTOR_BOSS_FD, 0x7330 },
    { ACTOR_OBJECT_KANKYO, 0x3220 },
    { ACTOR_EN_DU, 0x1AA0 },
    { ACTOR_EN_FD, 0x2CC0 },
    { ACTOR_EN_HORSE_LINK_CHILD, 0x1E00 },
    { ACTOR_DOOR_ANA, 0x670 },
    { ACTOR_BG_SPOT02_OBJECTS, 0x1350 },
    { ACTOR_BG_HAKA, 0x6C0 },
    { ACTOR_MAGIC_WIND, 0x1D00 },
    { ACTOR_MAGIC_FIRE, 0x22D0 },
    { ACTOR_EN_RU1, 0x76A0 },
    { ACTOR_BOSS_FD2, 0x3D30 },
    { ACTOR_EN_FD_FIRE, 0xD10 },
    { ACTOR_EN_DH, 0x1AD0 },
    { ACTOR_EN_DHA, 0xFD0 },
    { ACTOR_EN_RL, 0xEE0 },
    { ACTOR_EN_ENCOUNT1, 0xB60 },
    { ACTOR_DEMO_DU, 0x37E0 },
    { ACTOR_DEMO_IM, 0x3F70 },
    { ACTOR_DEMO_TRE_LGT, 0x710 },
    { ACTOR_EN_FW, 0x17B0 },
    { ACTOR_BG_VB_SIMA, 0x710 },
    { ACTOR_EN_VB_BALL, 0x11A0 },
    { ACTOR_BG_HAKA_MEGANE, 0x400 },
    { ACTOR_BG_HAKA_MEGANEBG, 0x6C0 },
    { ACTOR_BG_HAKA_SHIP, 0xA40 },
    { ACTOR_BG_HAKA_SGAMI, 0xC20 },
    { ACTOR_EN_HEISHI2, 0x2200 },
    { ACTOR_EN_ENCOUNT2, 0x1230 },
    { ACTOR_EN_FIRE_ROCK, 0x1110 },
    { ACTOR_EN_BROB, 0x10F0 },
    { ACTOR_MIR_RAY, 0x18C0 },
    { ACTOR_BG_SPOT09_OBJ, 0x510 },
    { ACTOR_BG_SPOT18_OBJ, 0x8D0 },
    { ACTOR_BOSS_VA, 0x171F0 },
    { ACTOR_BG_HAKA_TUBO, 0xA20 },
    { ACTOR_BG_HAKA_TRAP, 0x15D0 },
    { ACTOR_BG_HAKA_HUTA, 0xAA0 },
    { ACTOR_BG_HAKA_ZOU, 0x11F0 },
    { ACTOR_BG_SPOT17_FUNEN, 0x250 },
    { ACTOR_EN_SYATEKI_ITM, 0xDA0 },
    { ACTOR_EN_SYATEKI_MAN, 0xDC0 },
    { ACTOR_EN_TANA, 0x2A0 },
    { ACTOR_EN_NB, 0x45D0 },
    { ACTOR_BOSS_MO, 0x100B0 },
    { ACTOR_EN_SB, 0x1440 },
    { ACTOR_EN_BIGOKUTA, 0x2B10 },
    { ACTOR_EN_KAREBABA, 0x18F0 },
    { ACTOR_BG_BDAN_OBJECTS, 0x12D0 },
    { ACTOR_DEMO_SA, 0x2B20 },
    { ACTOR_DEMO_GO, 0xD60 },
    { ACTOR_EN_IN, 0x2D60 },
    { ACTOR_EN_TR, 0x1900 },
    { ACTOR_BG_SPOT16_BOMBSTONE, 0x1540 },
    { ACTOR_BG_HIDAN_KOWARERUKABE, 0xED0 },
    { ACTOR_BG_BOMBWALL, 0x8C0 },
    { ACTOR_BG_SPOT08_ICEBLOCK, 0x1040 },
    { ACTOR_EN_RU2, 0x2D80 },
    { ACTOR_OBJ_DEKUJR, 0x640 },
    { ACTOR_BG_MIZU_UZU, 0x1D0 },
    { ACTOR_BG_SPOT06_OBJECTS, 0x1410 },
    { ACTOR_BG_ICE_OBJECTS, 0xF40 },
    { ACTOR_BG_HAKA_WATER, 0x7E0 },
    { ACTOR_EN_MA2, 0x1060 },
    { ACTOR_EN_BOM_CHU, 0x16A0 },
    { ACTOR_EN_HORSE_GAME_CHECK, 0x10D0 },
    { ACTOR_BOSS_TW, 0x15B00 },
    { ACTOR_EN_RR, 0x2520 },
    { ACTOR_EN_BA, 0x1ED0 },
    { ACTOR_EN_BX, 0xAF0 },
    { ACTOR_EN_ANUBICE, 0x12B0 },
    { ACTOR_EN_ANUBICE_FIRE, 0xDC0 },
    { ACTOR_BG_MORI_HASHIGO, 0x8C0 },
    { ACTOR_BG_MORI_HASHIRA4, 0x590 },
    { ACTOR_BG_MORI_IDOMIZU, 0x640 },
    { ACTOR_BG_SPOT16_DOUGHNUT, 0x5B0 },
    { ACTOR_BG_BDAN_SWITCH, 0x1430 },
    { ACTOR_EN_MA1, 0x12E0 },
    { ACTOR_BOSS_GANON, 0x25DF0 },
    { ACTOR_BOSS_SST, 0xC560 },
    { ACTOR_EN_NY, 0x1930 },
    { ACTOR_EN_FR, 0x2A90 },
    { ACTOR_ITEM_SHIELD, 0xA10 },
    { ACTOR_BG_ICE_SHELTER, 0x1230 },
    { ACTOR_EN_ICE_HONO, 0x11F0 },
    { ACTOR_ITEM_OCARINA, 0x7D0 },
    { ACTOR_MAGIC_DARK, 0x1850 },
    { ACTOR_DEMO_6K, 0x2D10 },
    { ACTOR_EN_ANUBICE_TAG, 0x2D0 },
    { ACTOR_BG_HAKA_GATE, 0x1090 },
    { ACTOR_BG_SPOT15_SAKU, 0x340 },
    { ACTOR_BG_JYA_GOROIWA, 0x780 },
    { ACTOR_BG_JYA_ZURERUKABE, 0x6B0 },
    { ACTOR_BG_JYA_COBRA, 0x1D20 },
    { ACTOR_BG_JYA_KANAAMI, 0x3B0 },
    { ACTOR_FISHING, 0x1AAB0 },
    { ACTOR_OBJ_OSHIHIKI, 0x1AB0 },
    { ACTOR_BG_GATE_SHUTTER, 0x480 },
    { ACTOR_EFF_DUST, 0x13E0 },
    { ACTOR_BG_SPOT01_FUSYA, 0x2A0 },
    { ACTOR_BG_SPOT01_IDOHASHIRA, 0xC00 },
    { ACTOR_BG_SPOT01_IDOMIZU, 0x310 },
    { ACTOR_BG_PO_SYOKUDAI, 0x950 },
    { ACTOR_BG_GANON_OTYUKA, 0x2640 },
    { ACTOR_BG_SPOT15_RRBOX, 0xDE0 },
    { ACTOR_BG_UMAJUMP, 0x190 },
    { ACTOR_ARROW_FIRE, 0x1EC0 },
    { ACTOR_ARROW_ICE, 0x1EE0 },
    { ACTOR_ARROW_LIGHT, 0x1EF0 },
    { ACTOR_ITEM_ETCETERA, 0x8D0 },
    { ACTOR_OBJ_KIBAKO, 0xD00 },
    { ACTOR_OBJ_TSUBO, 0xFF0 },
    { ACTOR_EN_WONDER_ITEM, 0xD30 },
    { ACTOR_EN_IK, 0x4640 },
    { ACTOR_DEMO_IK, 0x1510 },
    { ACTOR_EN_SKJ, 0x3940 },
    { ACTOR_EN_SKJNEEDLE, 0x310 },
    { ACTOR_EN_G_SWITCH, 0x1830 },
    { ACTOR_DEMO_EXT, 0x940 },
    { ACTOR_DEMO_SHD, 0x2410 },
    { ACTOR_EN_DNS, 0x1390 },
    { ACTOR_ELF_MSG, 0x5F0 },
    { ACTOR_EN_HONOTRAP, 0x1550 },
    { ACTOR_EN_TUBO_TRAP, 0xCA0 },
    { ACTOR_OBJ_ICE_POLY, 0x9B0 },
    { ACTOR_BG_SPOT03_TAKI, 0x8F0 },
    { ACTOR_BG_SPOT07_TAKI, 0x5C0 },
    { ACTOR_EN_FZ, 0x2010 },
    { ACTOR_EN_PO_RELAY, 0x1710 },
    { ACTOR_BG_RELAY_OBJECTS, 0x7B0 },
    { ACTOR_EN_DIVING_GAME, 0x19B0 },
    { ACTOR_EN_KUSA, 0x14E0 },
    { ACTOR_OBJ_BEAN, 0x2790 },
    { ACTOR_OBJ_BOMBIWA, 0x570 },
    { ACTOR_OBJ_SWITCH, 0x1DC0 },
    { ACTOR_OBJ_ELEVATOR, 0x3C0 },
    { ACTOR_OBJ_LIFT, 0xA20 },
    { ACTOR_OBJ_HSBLOCK, 0x5D0 },
    { ACTOR_EN_OKARINA_TAG, 0x14E0 },
    { ACTOR_EN_YABUSAME_MARK, 0x6D0 },
    { ACTOR_EN_GOROIWA, 0x23C0 },
    { ACTOR_EN_EX_RUPPY, 0x10C0 },
    { ACTOR_EN_TORYO, 0xC90 },
    { ACTOR_EN_DAIKU, 0x1740 },
    { ACTOR_EN_NWC, 0xA40 },
    { ACTOR_EN_BLKOBJ, 0x560 },
    { ACTOR_ITEM_INBOX, 0x160 },
    { ACTOR_EN_GE1, 0x2030 },
    { ACTOR_OBJ_BLOCKSTOP, 0x1A0 },
    { ACTOR_EN_SDA, 0x1700 },
    { ACTOR_EN_CLEAR_TAG, 0xB5A0 },
    { ACTOR_EN_NIW_LADY, 0x18E0 },
    { ACTOR_EN_GM, 0xD30 },
    { ACTOR_EN_MS, 0x6F0 },
    { ACTOR_EN_HS, 0xBA0 },
    { ACTOR_BG_INGATE, 0x390 },
    { ACTOR_EN_KANBAN, 0x3150 },
    { ACTOR_EN_HEISHI3, 0x9D0 },
    { ACTOR_EN_SYATEKI_NIW, 0x2090 },
    { ACTOR_EN_ATTACK_NIW, 0x1260 },
    { ACTOR_BG_SPOT01_IDOSOKO, 0x210 },
    { ACTOR_EN_SA, 0x2270 },
    { ACTOR_EN_WONDER_TALK, 0x690 },
    { ACTOR_BG_GJYO_BRIDGE, 0x500 },
    { ACTOR_EN_DS, 0xC20 },
    { ACTOR_EN_MK, 0xE90 },
    { ACTOR_EN_BOM_BOWL_MAN, 0x1540 },
    { ACTOR_EN_BOM_BOWL_PIT, 0x970 },
    { ACTOR_EN_OWL, 0x3BA0 },
    { ACTOR_EN_ISHI, 0x9150 },
    { ACTOR_OBJ_HANA, 0x310 },
    { ACTOR_OBJ_LIGHTSWITCH, 0x1430 },
    { ACTOR_OBJ_MURE2, 0xA20 },
    { ACTOR_EN_GO, 0x4640 },
    { ACTOR_EN_FU, 0xD50 },
    { ACTOR_EN_CHANGER, 0x9E0 },
    { ACTOR_BG_JYA_MEGAMI, 0x11E0 },
    { ACTOR_BG_JYA_LIFT, 0x550 },
    { ACTOR_BG_JYA_BIGMIRROR, 0x840 },
    { ACTOR_BG_JYA_BOMBCHUIWA, 0xB30 },
    { ACTOR_BG_JYA_AMISHUTTER, 0x390 },
    { ACTOR_BG_JYA_BOMBIWA, 0x5C0 },
    { ACTOR_BG_SPOT18_BASKET, 0xFF0 },
    { ACTOR_EN_GANON_ORGAN, 0x7000 },
    { ACTOR_EN_SIOFUKI, 0xDB0 },
    { ACTOR_EN_STREAM, 0x590 },
    { ACTOR_EN_MM, 0x1620 },
    { ACTOR_EN_KO, 0x4140 },
    { ACTOR_EN_KZ, 0x1510 },
    { ACTOR_EN_WEATHER_TAG, 0xEF0 },
    { ACTOR_BG_SST_FLOOR, 0x560 },
    { ACTOR_EN_ANI, 0xD70 },
    { ACTOR_EN_EX_ITEM, 0x1170 },
    { ACTOR_BG_JYA_IRONOBJ, 0xDB0 },
    { ACTOR_EN_JS, 0x9D0 },
    { ACTOR_EN_JSJUTAN, 0x5920 },
    { ACTOR_EN_CS, 0x1230 },
    { ACTOR_EN_MD, 0x2670 },
    { ACTOR_EN_HY, 0x3940 },
    { ACTOR_EN_GANON_MANT, 0x4220 },
    { ACTOR_EN_OKARINA_EFFECT, 0x3B0 },
    { ACTOR_EN_MAG, 0x4F10 },
    { ACTOR_DOOR_GERUDO, 0x5F0 },
    { ACTOR_ELF_MSG2, 0x470 },
    { ACTOR_DEMO_GT, 0x5600 },
    { ACTOR_EN_PO_FIELD, 0x3A70 },
    { ACTOR_EFC_ERUPC, 0xAE0 },
    { ACTOR_BG_ZG, 0x470 },
    { ACTOR_EN_HEISHI4, 0xF00 },
    { ACTOR_EN_ZL3, 0x7E50 },
    { ACTOR_BOSS_GANON2, 0x12E10 },
    { ACTOR_EN_KAKASI, 0xD40 },
    { ACTOR_EN_TAKARA_MAN, 0x8C0 },
    { ACTOR_OBJ_MAKEOSHIHIKI, 0x490 },
    { ACTOR_OCEFF_SPOT, 0xF30 },
    { ACTOR_END_TITLE, 0x4130 },
    { ACTOR_EN_TORCH, 0xF0 },
    { ACTOR_DEMO_EC, 0x3860 },
    { ACTOR_SHOT_SUN, 0x6C0 },
    { ACTOR_EN_DY_EXTRA, 0x580 },
    { ACTOR_EN_WONDER_TALK2, 0x6A0 },
    { ACTOR_EN_GE2, 0x19A0 },
    { ACTOR_OBJ_ROOMTIMER, 0x250 },
    { ACTOR_EN_SSH, 0x25F0 },
    { ACTOR_EN_STH, 0x40B0 },
    { ACTOR_OCEFF_WIPE, 0xD50 },
    { ACTOR_OCEFF_STORM, 0x1BA0 },
    { ACTOR_EN_WEIYER, 0x1A00 },
    { ACTOR_BG_SPOT05_SOKO, 0x320 },
    { ACTOR_BG_JYA_1FLIFT, 0x690 },
    { ACTOR_BG_JYA_HAHENIRON, 0x7F0 },
    { ACTOR_BG_SPOT12_GATE, 0x410 },
    { ACTOR_BG_SPOT12_SAKU, 0x4C0 },
    { ACTOR_EN_HINTNUTS, 0x1A30 },
    { ACTOR_EN_NUTSBALL, 0x620 },
    { ACTOR_BG_SPOT00_BREAK, 0x1A0 },
    { ACTOR_EN_SHOPNUTS, 0xF10 },
    { ACTOR_EN_IT, 0x190 },
    { ACTOR_EN_GELDB, 0x53B0 },
    { ACTOR_OCEFF_WIPE2, 0x1770 },
    { ACTOR_OCEFF_WIPE3, 0x1750 },
    { ACTOR_EN_NIW_GIRL, 0xAD0 },
    { ACTOR_EN_DOG, 0x11B0 },
    { ACTOR_EN_SI, 0x500 },
    { ACTOR_BG_SPOT01_OBJECTS2, 0x4C0 },
    { ACTOR_OBJ_COMB, 0x860 },
    { ACTOR_BG_SPOT11_BAKUDANKABE, 0x640 },
    { ACTOR_OBJ_KIBAKO2, 0x6C0 },
    { ACTOR_EN_DNT_DEMO, 0xD20 },
    { ACTOR_EN_DNT_JIJI, 0x1510 },
    { ACTOR_EN_DNT_NOMAL, 0x2E00 },
    { ACTOR_EN_GUEST, 0x9A0 },
    { ACTOR_BG_BOM_GUARD, 0x220 },
    { ACTOR_EN_HS2, 0x5E0 },
    { ACTOR_DEMO_KEKKAI, 0x12E0 },
    { ACTOR_BG_SPOT08_BAKUDANKABE, 0x6A0 },
    { ACTOR_BG_SPOT17_BAKUDANKABE, 0x6E0 },
    { ACTOR_OBJ_MURE3, 0x7D0 },
    { ACTOR_EN_TG, 0x6D0 },
    { ACTOR_EN_MU, 0x920 },
    { ACTOR_EN_GO2, 0x6020 },
    { ACTOR_EN_WF, 0x4310 },
    { ACTOR_EN_SKB, 0x18F0 },
    { ACTOR_DEMO_GJ, 0x3CB0 },
    { ACTOR_DEMO_GEFF, 0x820 },
    { ACTOR_BG_GND_FIREMEIRO, 0x540 },
    { ACTOR_BG_GND_DARKMEIRO, 0x7C0 },
    { ACTOR_BG_GND_SOULMEIRO, 0x860 },
    { ACTOR_BG_GND_NISEKABE, 0x170 },
    { ACTOR_BG_GND_ICEBLOCK, 0x1100 },
    { ACTOR_EN_GB, 0x1730 },
    { ACTOR_EN_GS, 0x1EA0 },
    { ACTOR_BG_MIZU_BWALL, 0x14D0 },
    { ACTOR_BG_MIZU_SHUTTER, 0x800 },
    { ACTOR_EN_DAIKU_KAKARIKO, 0x13C0 },
    { ACTOR_BG_BOWL_WALL, 0x980 },
    { ACTOR_EN_WALL_TUBO, 0x4F0 },
    { ACTOR_EN_PO_DESERT, 0xDC0 },
    { ACTOR_EN_CROW, 0x16A0 },
    { ACTOR_DOOR_KILLER, 0x1570 },
    { ACTOR_BG_SPOT11_OASIS, 0x730 },
    { ACTOR_BG_SPOT18_FUTA, 0x1A0 },
    { ACTOR_BG_SPOT18_SHUTTER, 0x550 },
    { ACTOR_EN_MA3, 0xF70 },
    { ACTOR_EN_COW, 0x1460 },
    { ACTOR_BG_ICE_TURARA, 0x830 },
    { ACTOR_BG_ICE_SHUTTER, 0x470 },
    { ACTOR_EN_KAKASI2, 0x720 },
    { ACTOR_EN_KAKASI3, 0x10E0 },
    { ACTOR_OCEFF_WIPE4, 0xFE0 },
    { ACTOR_EN_EG, 0x1B0 },
    { ACTOR_BG_MENKURI_NISEKABE, 0x150 },
    { ACTOR_EN_ZO, 0x25B0 },
    { ACTOR_OBJ_MAKEKINSUTA, 0x150 },
    { ACTOR_EN_GE3, 0xB50 },
    { ACTOR_OBJ_TIMEBLOCK, 0xC40 },
    { ACTOR_OBJ_HAMISHI, 0x850 },
    { ACTOR_EN_ZL4, 0x4A30 },
    { ACTOR_EN_MM2, 0xDC0 },
    { ACTOR_BG_JYA_BLOCK, 0x270 },
    { ACTOR_OBJ_WARP2BLOCK, 0xB30 },
};

// Console instance sizes for the few actors whose SoH bloat (8-byte ptrs + FI epoch) changes which actor
// loses the saturation race. { id, {SoH, NTSC} }; ZAlloc remaps on an exact size match, so it self-disables.
static const std::unordered_map<int16_t, std::pair<size_t, size_t>> sHeapFragmentationActorInstanceSizes = {
    { ACTOR_OBJECT_KANKYO, { 0x17A8, 0x1650 } }, // SoH 0x17A8 (effects[64].epoch +0x100 + 64-bit ptrs) -> NTSC 0x1650
};

#define DEFINE_SCENE(file, _1, enum, _3, _4, _5) { enum, (_##file##SegmentRomEnd - _##file##SegmentRomStart) },

static const std::unordered_map<int16_t, size_t> sHeapFragmentationSceneFileSizes = {
#include "tables/scene_table.h"
};

#undef DEFINE_SCENE

// Gates the globally-fired hooks to normal gameplay; set in GameStateRealloc, cleared on OnPlayDestroy.
static bool sHeapFragmentationInPlay = false;

static int16_t sHfSpawningActorId = -1; // actorId currently being spawned (drives the instance remap + refusal log)
static bool sHfExpectInstanceAlloc = false; // the first Zelda-arena alloc after an overlay load is the instance
static bool sHfOverlayReserveFailed = false; // the current spawn's overlay couldn't be reserved (console aborts the spawn)

static void HeapFragmentation_GameStateRealloc(uintptr_t ptr, size_t size) {
    sHeapFragmentationInPlay = (gSaveContext.gameMode == GAMEMODE_NORMAL);

    u32 systemMaxFree;
    u32 systemFree;
    u32 systemAlloc;
    static void* gameArena = nullptr;

    if (gameArena != nullptr) {
        __osFree(&sHeapFragmentationSystemArena, gameArena);
    }
    THA_Dt(&sGameStateTHA);

    size = HF_GAMESTATE_ARENA_SIZE;
    ArenaImpl_GetSizes(&sHeapFragmentationSystemArena, &systemMaxFree, &systemFree, &systemAlloc);
    if (size > systemMaxFree - sizeof(GameAllocEntry)) {
        size = systemMaxFree - sizeof(GameAllocEntry);
    }

    gameArena = __osMalloc(&sHeapFragmentationSystemArena, size + sizeof(GameAllocEntry));
    THA_Ct(&sGameStateTHA, gameArena, size);
}

static void* HeapFragmentation_GameStateAlloc(size_t size) {
    void* ret;

    if (!sHeapFragmentationInPlay || gPlayState == nullptr) {
        return NULL;
    }

    if (size == THA_GetSize(&gPlayState->state.tha)) {
        return NULL;
    }

    if (THA_IsCrash(&sGameStateTHA)) {
        ret = NULL;
    } else if ((uintptr_t)THA_GetSize(&sGameStateTHA) < size) {
        ret = NULL;
    } else {
        ret = THA_AllocEndAlign16(&sGameStateTHA, size);
        if (THA_IsCrash(&sGameStateTHA)) {
            ret = NULL;
        }
    }

    return ret;
}

static void HeapFragmentation_ZInit() {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    // Reset per-scene state in ZInit (the carve path that always runs per scene). Leftover state -- esp. a
    // dangling sAbsoluteSpacePtr -- would otherwise make the arena emerge roomier on the next scene.
    sHeapFragmentationZeldaArenaMap.clear();
    sHeapFragmentationRegisteredOverlays.clear();
    sAbsoluteSpacePtr = (uintptr_t) nullptr;
    sHfExpectInstanceAlloc = false;
    sHfSpawningActorId = -1;

    size_t zAllocSize = THA_GetSize(&sGameStateTHA);
    uintptr_t zAlloc = (uintptr_t)HeapFragmentation_GameStateAlloc(zAllocSize);
    uintptr_t zAllocAligned = (zAlloc + 8) & ~0xF;
    __osMallocInit(&sHeapFragmentationZeldaArena, (void*)zAllocAligned, zAllocSize - (zAllocAligned - zAlloc));
}

static void HeapFragmentation_ZAlloc(uintptr_t ptr, size_t size) {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    // First Zelda alloc after an overlay load is the actor's instance: remap modeled actors to their console
    // size so the shadow fragments like console (the real instance is untouched). See the size table above.
    if (sHfExpectInstanceAlloc) {
        sHfExpectInstanceAlloc = false;
        auto it = sHeapFragmentationActorInstanceSizes.find(sHfSpawningActorId);
        if (it != sHeapFragmentationActorInstanceSizes.end() && size == it->second.first) {
            size = it->second.second;
        }
    }
    // Address-reuse guard: if the real arena handed back a ptr still mapped to a live shadow block (its ZFree
    // never fired), free the stale block first so it doesn't leak as permanent phantom occupancy.
    auto existing = sHeapFragmentationZeldaArenaMap.find(ptr);
    if (existing != sHeapFragmentationZeldaArenaMap.end() && existing->second != (uintptr_t) nullptr) {
        __osFree(&sHeapFragmentationZeldaArena, (void*)existing->second);
    }
    sHeapFragmentationZeldaArenaMap[ptr] = (uintptr_t)__osMalloc(&sHeapFragmentationZeldaArena, size);
}

static void HeapFragmentation_ZAllocR(uintptr_t ptr, size_t size) {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    auto existing = sHeapFragmentationZeldaArenaMap.find(ptr);
    if (existing != sHeapFragmentationZeldaArenaMap.end() && existing->second != (uintptr_t) nullptr) {
        __osFree(&sHeapFragmentationZeldaArena, (void*)existing->second);
    }
    sHeapFragmentationZeldaArenaMap[ptr] = (uintptr_t)__osMallocR(&sHeapFragmentationZeldaArena, size);
}

static void HeapFragmentation_ZFree(uintptr_t ptr) {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    auto it = sHeapFragmentationZeldaArenaMap.find(ptr);
    if (it == sHeapFragmentationZeldaArenaMap.end()) {
        return; // not tracked (e.g. an already-erased vetoed-spawn ptr) -- don't insert a phantom entry
    }
    uintptr_t shadowPtr = it->second;
    if (shadowPtr != (uintptr_t) nullptr) {
        __osFree(&sHeapFragmentationZeldaArena, (void*)shadowPtr);
    }
    sHeapFragmentationZeldaArenaMap.erase(it);
}

static void HeapFragmentation_ZCleanup() {
    __osMallocCleanup(&sHeapFragmentationZeldaArena);
    sHeapFragmentationZeldaArenaMap.clear();
    sHeapFragmentationRegisteredOverlays.clear();
}

static void HeapFragmentation_ActorOverlayLoad(int16_t actorId) {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    // Record the spawning actor (its id isn't readable at the VB_LOAD_ACTOR gate). The next Zelda alloc is its
    // instance, which lets ZAlloc apply the console-size remap.
    sHfSpawningActorId = actorId;
    sHfExpectInstanceAlloc = true;
    sHfOverlayReserveFailed = false;
    if (!sHeapFragmentationActorOverlaySizes.contains(actorId) || !sHeapFragmentationActorTable.contains(actorId)) {
        return;
    }

    // The map entry is the "overlay loaded" flag. Console loads an overlay once (freed when the last instance
    // despawns), so later instances must NOT re-reserve -- doing so over-saturated the arena.
    if (sHeapFragmentationRegisteredOverlays.contains(actorId)) {
        return;
    }

    size_t size = sHeapFragmentationActorOverlaySizes.at(actorId);
    uint16_t allocType = sHeapFragmentationActorTable.at(actorId);

    uintptr_t reserved;
    if (allocType == ALLOCTYPE_ABSOLUTE) {
        if (sAbsoluteSpacePtr == (uintptr_t) nullptr) {
            sAbsoluteSpacePtr = (uintptr_t)__osMallocR(&sHeapFragmentationZeldaArena, ACTOROVL_ABSOLUTE_SPACE_SIZE);
        }
        reserved = sAbsoluteSpacePtr;
    } else if (allocType == ALLOCTYPE_PERMANENT) {
        reserved = (uintptr_t)__osMallocR(&sHeapFragmentationZeldaArena, size);
    } else {
        reserved = (uintptr_t)__osMalloc(&sHeapFragmentationZeldaArena, size);
    }
    if (reserved != (uintptr_t) nullptr) {
        sHeapFragmentationRegisteredOverlays[actorId] = reserved;
    } else {
        // Overlay couldn't be reserved: console aborts the spawn here, so flag it for VB_LOAD_ACTOR to refuse
        // (the smaller instance might still fit). Erase rather than store NULL, so the next spawn retries.
        sHeapFragmentationRegisteredOverlays.erase(actorId);
        sHfOverlayReserveFailed = true;
    }
}

static void HeapFragmentation_ActorOverlayFree(int16_t actorId) {
    if (!sHeapFragmentationInPlay) {
        return;
    }
    if (!sHeapFragmentationRegisteredOverlays.contains(actorId) || !sHeapFragmentationActorTable.contains(actorId)) {
        return;
    }

    uint16_t allocType = sHeapFragmentationActorTable.at(actorId);

    if (allocType == ALLOCTYPE_PERMANENT) {
        // Permanent, do not de-allocate
    } else if (allocType == ALLOCTYPE_ABSOLUTE) {
        sHeapFragmentationRegisteredOverlays.erase(actorId);
    } else {
        uintptr_t ptr = sHeapFragmentationRegisteredOverlays.at(actorId);
        __osFree(&sHeapFragmentationZeldaArena, (void*)ptr);
        sHeapFragmentationRegisteredOverlays.erase(actorId);
    }
}

static void HeapFragmentation_CheckSceneCommands(SOH::Scene* scene) {
    for (int i = 0; i < scene->commands.size(); i++) {
        auto sceneCmd = scene->commands[i];

        if ((int)sceneCmd->cmdId == SCENE_CMD_ID_ALTERNATE_HEADER_LIST) {
            SOH::SetAlternateHeaders* cmdHeaders = (SOH::SetAlternateHeaders*)sceneCmd.get();
            if (gSaveContext.sceneSetupIndex != 0) {
                SOH::Scene* desiredHeader =
                    std::static_pointer_cast<SOH::Scene>(cmdHeaders->headers[gSaveContext.sceneSetupIndex - 1]).get();

                if (desiredHeader != nullptr) {
                    HeapFragmentation_CheckSceneCommands(desiredHeader);
                    break;
                }

                if (gSaveContext.sceneSetupIndex == 3) {
                    SOH::Scene* desiredHeader =
                        std::static_pointer_cast<SOH::Scene>(cmdHeaders->headers[gSaveContext.sceneSetupIndex - 2]).get();

                    if (desiredHeader != nullptr) {
                        HeapFragmentation_CheckSceneCommands(desiredHeader);
                        break;
                    }
                }
            }
        } else if ((int)sceneCmd->cmdId == SCENE_CMD_ID_SPECIAL_FILES) {
            SOH::SetSpecialObjects* specialCmd = (SOH::SetSpecialObjects*)sceneCmd.get();
            if (specialCmd->specialObjects.elfMessage == 1) {
                HeapFragmentation_GameStateAlloc(_elf_message_fieldSegmentRomEnd - _elf_message_fieldSegmentRomStart);
            } else if (specialCmd->specialObjects.elfMessage == 2) {
                HeapFragmentation_GameStateAlloc(_elf_message_ydanSegmentRomEnd - _elf_message_ydanSegmentRomStart);
            }
        }
    }
}

static void HeapFragmentation_OnSceneInit(int16_t sceneNum) {
    // Allocate extra memory to heap that port skips allocation for
    if (!sHeapFragmentationInPlay || gPlayState == nullptr) {
        return;
    }


    size_t largestSize = MAX(_ovl_kaleido_scopeSegmentEnd - _ovl_kaleido_scopeSegmentStart, _ovl_player_actorSegmentEnd - _ovl_player_actorSegmentStart);
    HeapFragmentation_GameStateAlloc(largestSize);

    if (sHeapFragmentationSceneFileSizes.contains(sceneNum)) {
        HeapFragmentation_GameStateAlloc(sHeapFragmentationSceneFileSizes.at(sceneNum));
    }

    RoomContext* roomCtx = &gPlayState->roomCtx;
    SOH::Scene* scene = (SOH::Scene*)roomCtx->roomToLoad;

    HeapFragmentation_CheckSceneCommands(scene);

    // parameter_static is the one interface buffer the port reserves 0 bytes for (its ROM symbols are stubbed)
    // while console DMAs ~0x1E390 -- re-create it so the actor arena emerges at the console size.
    HeapFragmentation_GameStateAlloc(0x1E390);
}

// === Savestate integration =====================================================================
// Serialize the shadow heap (pinned buffer + Arena structs + the two maps) into a SaveStateInfo blob; the
// fixed-address pinning keeps every pointer valid after restore. Returns bytes written.
extern "C" uint32_t HeapFragmentation_SerializeShadow(void* dst, uint32_t dstCap) {
    if (sHeapFragmentationHeap == nullptr || !__osMallocIsInitialized(&sHeapFragmentationSystemArena)) {
        return 0;
    }
    uint8_t* base = (uint8_t*)dst;
    uint8_t* p = base;
    uint8_t* end = base + dstCap;
    auto put = [&](const void* src, size_t n) {
        if (p + n <= end) {
            memcpy(p, src, n);
        }
        p += n;
    };

    put(sHeapFragmentationHeap, SYSTEM_HEAP_SIZE);
    put(&sHeapFragmentationSystemArena, sizeof(sHeapFragmentationSystemArena));
    put(&sHeapFragmentationZeldaArena, sizeof(sHeapFragmentationZeldaArena));
    put(&sGameStateTHA, sizeof(sGameStateTHA));
    put(&sAbsoluteSpacePtr, sizeof(sAbsoluteSpacePtr));
    uint8_t inPlay = sHeapFragmentationInPlay ? 1 : 0;
    put(&inPlay, sizeof(inPlay));

    uint32_t zCount = (uint32_t)sHeapFragmentationZeldaArenaMap.size();
    put(&zCount, sizeof(zCount));
    for (const auto& [k, v] : sHeapFragmentationZeldaArenaMap) {
        uint64_t kk = (uint64_t)k, vv = (uint64_t)v;
        put(&kk, sizeof(kk));
        put(&vv, sizeof(vv));
    }

    uint32_t oCount = (uint32_t)sHeapFragmentationRegisteredOverlays.size();
    put(&oCount, sizeof(oCount));
    for (const auto& [id, v] : sHeapFragmentationRegisteredOverlays) {
        uint16_t ii = id;
        uint64_t vv = (uint64_t)v;
        put(&ii, sizeof(ii));
        put(&vv, sizeof(vv));
    }

    uint32_t written = (uint32_t)(p - base);
    return (written <= dstCap) ? written : 0; // never persist a truncated snapshot
}

// Restore a SerializeShadow blob. crossRestart pointers are only valid if the buffer is pinned this session;
// otherwise (or size==0) reset to a clean, scene-rebuildable state.
extern "C" void HeapFragmentation_DeserializeShadow(const void* src, uint32_t size, uint8_t crossRestart) {
    if (sHeapFragmentationHeap == nullptr || !__osMallocIsInitialized(&sHeapFragmentationSystemArena)) {
        return; // feature inactive now -- nothing to restore into
    }

    if (size == 0 || (crossRestart && !sShadowPinned)) {
        // Can't (or needn't) restore: wipe the shadow so a stale snapshot can't desync the spawn gate.
        // The next scene load re-carves the Zelda arena from a clean system arena.
        __osMallocCleanup(&sHeapFragmentationSystemArena);
        __osMallocInit(&sHeapFragmentationSystemArena, (void*)sHeapFragmentationHeap, sShadowHeapSize);
        sHeapFragmentationZeldaArenaMap.clear();
        sHeapFragmentationRegisteredOverlays.clear();
        sAbsoluteSpacePtr = (uintptr_t) nullptr;
        sHeapFragmentationInPlay = false;
        return;
    }

    const uint8_t* p = (const uint8_t*)src;
    auto get = [&](void* d, size_t n) {
        memcpy(d, p, n);
        p += n;
    };

    get(sHeapFragmentationHeap, SYSTEM_HEAP_SIZE);
    get(&sHeapFragmentationSystemArena, sizeof(sHeapFragmentationSystemArena));
    get(&sHeapFragmentationZeldaArena, sizeof(sHeapFragmentationZeldaArena));
    get(&sGameStateTHA, sizeof(sGameStateTHA));
    get(&sAbsoluteSpacePtr, sizeof(sAbsoluteSpacePtr));
    uint8_t inPlay = 0;
    get(&inPlay, sizeof(inPlay));
    sHeapFragmentationInPlay = (inPlay != 0);

    sHeapFragmentationZeldaArenaMap.clear();
    sHeapFragmentationRegisteredOverlays.clear();

    uint32_t zCount = 0;
    get(&zCount, sizeof(zCount));
    for (uint32_t i = 0; i < zCount; i++) {
        uint64_t k = 0, v = 0;
        get(&k, sizeof(k));
        get(&v, sizeof(v));
        sHeapFragmentationZeldaArenaMap[(uintptr_t)k] = (uintptr_t)v;
    }

    uint32_t oCount = 0;
    get(&oCount, sizeof(oCount));
    for (uint32_t i = 0; i < oCount; i++) {
        uint16_t id = 0;
        uint64_t v = 0;
        get(&id, sizeof(id));
        get(&v, sizeof(v));
        sHeapFragmentationRegisteredOverlays[id] = (uintptr_t)v;
    }
}

void RegisterHeapFragmentation() {
    if (CVAR_HEAP_FRAGMENTATION_VALUE) {
        // Allocate + init the shadow system heap once. Guarding on the arena's init state (instead of
        // allocating unconditionally) avoids leaking a fresh buffer on every CVar toggle.
        if (!__osMallocIsInitialized(&sHeapFragmentationSystemArena)) {
            sShadowPinned = false;
#ifdef _WIN64
            sHeapFragmentationHeap = (u8*)VirtualAlloc(HF_SHADOW_HEAP_FIXED_BASE, SYSTEM_HEAP_SIZE,
                                                       HF_MEM_RESERVE | HF_MEM_COMMIT, HF_PAGE_READWRITE);
            if (sHeapFragmentationHeap != NULL && (void*)sHeapFragmentationHeap != HF_SHADOW_HEAP_FIXED_BASE) {
                VirtualFree(sHeapFragmentationHeap, 0, HF_MEM_RELEASE);
                sHeapFragmentationHeap = NULL;
            }
            sShadowPinned = (sHeapFragmentationHeap != NULL);
#endif
            if (sHeapFragmentationHeap == NULL) {
#ifdef _MSC_VER
                sHeapFragmentationHeap = (u8*)_aligned_malloc(SYSTEM_HEAP_SIZE, 0x10);
#elif defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)
                if (posix_memalign((void**)&sHeapFragmentationHeap, 0x10, SYSTEM_HEAP_SIZE) != 0)
                    sHeapFragmentationHeap = NULL;
#else
                sHeapFragmentationHeap = (u8*)memalign(0x10, SYSTEM_HEAP_SIZE);
#endif
            }
            assert(sHeapFragmentationHeap != NULL);
            uint32_t frameBufferStartAddress = 0x80400000 - (SCREEN_WIDTH * SCREEN_HEIGHT) * 4;
            sShadowHeapSize = frameBufferStartAddress - _buffersSegmentEnd;

            __osMallocInit(&sHeapFragmentationSystemArena, (void*)sHeapFragmentationHeap, sShadowHeapSize);
        }
    } else {
        if (__osMallocIsInitialized(&sHeapFragmentationSystemArena)) {
            __osMallocCleanup(&sHeapFragmentationSystemArena);
            sHeapFragmentationZeldaArenaMap.clear();
        }
        if (sHeapFragmentationHeap != NULL) {
#ifdef _WIN64
            if (sShadowPinned) {
                VirtualFree(sHeapFragmentationHeap, 0, HF_MEM_RELEASE);
                sHeapFragmentationHeap = NULL;
            }
#endif
            if (sHeapFragmentationHeap != NULL) {
#ifdef _MSC_VER
                _aligned_free(sHeapFragmentationHeap);
#else
                free(sHeapFragmentationHeap);
#endif
                sHeapFragmentationHeap = NULL;
            }
            sShadowPinned = false;
        }
    }

    // gSystemArena is deliberately NOT hooked: it's the hot boot/title allocator and hooking it broke
    // keyboard/menu input there. The model only needs the gamestate arena, Zelda arena, and actor overlays.
    COND_HOOK(OnGameStateRealloc, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_GameStateRealloc);

    COND_HOOK(OnGameStateAlloc, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_GameStateAlloc);

    COND_HOOK(OnZeldaArenaInit, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ZInit);

    COND_HOOK(OnZeldaArenaAlloc, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ZAlloc);

    COND_HOOK(OnZeldaArenaAllocR, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ZAllocR);

    COND_HOOK(OnZeldaArenaFree, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ZFree);

    COND_HOOK(OnZeldaArenaCleanup, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ZCleanup);

    COND_HOOK(OnActorOverlayLoad, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ActorOverlayLoad);

    COND_HOOK(OnActorOverlayFree, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_ActorOverlayFree);

    COND_HOOK(OnSceneInit, CVAR_HEAP_FRAGMENTATION_VALUE, HeapFragmentation_OnSceneInit);

    COND_HOOK(OnPlayDestroy, CVAR_HEAP_FRAGMENTATION_VALUE, []() {
        // Leaving the PlayState: stop the hooks from touching the shadow arena until the next scene loads.
        sHeapFragmentationInPlay = false;
        if (sAbsoluteSpacePtr != (uintptr_t) nullptr) {
            std::vector<int16_t> keysToDelete;
            for (auto& [id, ptr] : sHeapFragmentationRegisteredOverlays) {
                if (ptr == sAbsoluteSpacePtr) {
                    keysToDelete.emplace_back(id);
                }
            }
            for (auto& id : keysToDelete) {
                sHeapFragmentationRegisteredOverlays.erase(id);
            }
            __osFree(&sHeapFragmentationZeldaArena, (void*)sAbsoluteSpacePtr);
            sAbsoluteSpacePtr = (uintptr_t) nullptr;
        }
    });

    COND_VB_SHOULD(VB_LOAD_ACTOR, CVAR_HEAP_FRAGMENTATION_VALUE, {
        if (!sHeapFragmentationInPlay) {
            return;
        }
        Actor* actor = va_arg(args, Actor*);
        uintptr_t actorAddr = (uintptr_t)actor;
        auto instIt = sHeapFragmentationZeldaArenaMap.find(actorAddr);
        if (instIt == sHeapFragmentationZeldaArenaMap.end()) {
            // Error! This should always be true
            return;
        }

        uintptr_t shadowInstance = instIt->second;
        // Refuse if the instance OR the overlay couldn't be allocated: console needs both. Checking only the
        // instance wrongly allows it when a hole fits the instance but not the larger overlay.
        bool refuse = (shadowInstance == (uintptr_t) nullptr) || sHfOverlayReserveFailed;
        if (refuse) {
            *should = false;
            u32 mf = 0;
            u32 f = 0;
            u32 a = 0;
            ArenaImpl_GetSizes(&sHeapFragmentationZeldaArena, &mf, &f, &a);
            SPDLOG_INFO("[HFrefuse] actorId=0x{:X} overlayFail={} largestFree=0x{:X}", sHfSpawningActorId,
                        sHfOverlayReserveFailed, mf);
            // If the instance shadow-alloc succeeded but the overlay was the binding constraint, free the
            // instance so the refused actor occupies nothing in the shadow -- mirroring console.
            if (shadowInstance != (uintptr_t) nullptr) {
                __osFree(&sHeapFragmentationZeldaArena, (void*)shadowInstance);
            }
            // The vetoed actor never enters the world, so OnZeldaArenaFree never fires to clear this key.
            sHeapFragmentationZeldaArenaMap.erase(instIt);
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterHeapFragmentation, { CVAR_HEAP_FRAGMENTATION_NAME });
