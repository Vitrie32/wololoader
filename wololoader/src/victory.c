// Per-civilization victory sound.
// When a match ends and the local player has won, plays the local player's
// custom civ "victory_sound" (mp3/wav) instead of vanilla won1/won2 music.
// If the local player lost or has no victory_sound, keeps vanilla behaviour.
//
// Verified addresses (AoK HD.exe 5.8, see re/FINDINGS.md):
// - 0x6a2e00: AchievementsScreen::play_endgame_music (__thiscall(self), ret 0, void).
//   Prologue / stolen bytes (5 B): 55 8b ec 6a ff (push ebp; mov ebp,esp; push -1).
//   Called from 0x6a0581 inside AchievementsScreen::tick (0x6a0530, vtbl slot 17).
//   Fields on AchievementsScreen:
//     +0x728: SoundSystem* (from 0xada234)
//     +0x72c: SoundObject* (ref_ptr; released by 0x6a3020 and dtor 0x69f400)
//     +0x738: int played flag (0 -> 1)
// - 0xaf2c58: global Game*; 0x540c90: Game::get_local_player() (__thiscall, ret 0).
//   Prologue: 8b 81 c8 00 00 00 8b 88 74 01.
//   Player+0x35 (BYTE): 1 = won (verified at 0x6a2ec1: cmpb $1, 0x35(%eax)).
//   Player+0x1ae4: Civilization*, Civilization+8: int civ_id (verified at 0x6c5a99..0x6c5aa9).
// - 0xada234: global SoundSystem*.
// - 0x46ba10: SoundSystem::play_sound (__thiscall(ss, &out_handle, req_24B, chan, flag), ret 0x24).
//   Prologue: 55 8b ec 6a ff. Destroys req.filename on return (0x46bb7a).
//   SoundRequest (24 B): +0x00=0, +0x04=storage_id (0 = raw path per 0x828670),
//   +0x08..+0x13=std::string(path), +0x14=0. chan=0 is Music volume (0x46ab60).
// - 0x46b010: SoundObject::play (__thiscall(obj), ret 0). Prologue: 56 57 8b f9.
// - 0x433210: std::string::assign (__thiscall(str, const char *s), ret 4). Prologue: 55 8b ec 53 57.
// - 0xad0380: shared static empty-string descriptor.
#include "wololoader.h"
#include <string.h>

#define ADDR_PLAY_ENDGAME_MUSIC 0x6a2e00
#define ADDR_GAME_PTR           0xaf2c58
#define ADDR_GET_LOCAL_PLAYER   0x540c90
#define ADDR_SOUND_SYSTEM       0xada234
#define ADDR_PLAY_SOUND         0x46ba10
#define ADDR_START_SOUND        0x46b010
#define ADDR_STRING_ASSIGN      0x433210
#define ADDR_EMPTY_STR          0xad0380

typedef void (__thiscall *PlayEndgameMusic_t)(void *self);
typedef void *(__thiscall *GetLocalPlayer_t)(void *game);
typedef void (__thiscall *StartSound_t)(void *sound_obj);
typedef void *(__thiscall *StrAssign_t)(void *str, const char *s);

static PlayEndgameMusic_t o_play_endgame_music;

static int play_victory_file(void *self, void *ss, const char *path) {
    void *out_handle = NULL;
    DWORD str_buf[3] = { ADDR_EMPTY_STR, 0, 0 };
    ((StrAssign_t)ADDR_STRING_ASSIGN)(str_buf, path);

    // Build 24-byte SoundRequest (storage=0 for raw path) and call 0x46ba10(..., chan=0, flag=0).
    // 0x46ba10 is __thiscall with ret 0x24 and destroys SoundRequest.filename before returning.
    __asm__ __volatile__(
        "pushl $0\n\t"
        "pushl $0\n\t"
        "subl $24, %%esp\n\t"
        "movl %%esp, %%edx\n\t"
        "movl $0, 0(%%edx)\n\t"
        "movl $0, 4(%%edx)\n\t"
        "movl 0(%0), %%eax\n\t"
        "movl %%eax, 8(%%edx)\n\t"
        "movl 4(%0), %%eax\n\t"
        "movl %%eax, 12(%%edx)\n\t"
        "movl 8(%0), %%eax\n\t"
        "movl %%eax, 16(%%edx)\n\t"
        "movl $0, 20(%%edx)\n\t"
        "pushl %1\n\t"
        "movl %2, %%ecx\n\t"
        "movl $0x46ba10, %%eax\n\t"
        "call *%%eax\n\t"
        :
        : "r" (str_buf), "r" (&out_handle), "r" (ss)
        : "eax", "ecx", "edx", "memory", "cc"
    );

    str_buf[0] = ADDR_EMPTY_STR;
    str_buf[1] = 0;
    str_buf[2] = 0;

    if (!out_handle) return 0;

    void **slot = (void **)((char *)self + 0x72c);
    if (*slot) {
        int *ref = (int *)((char *)(*slot) + 4);
        if (--(*ref) == 0) {
            typedef void (__thiscall *SoundDtor_t)(void *obj);
            ((SoundDtor_t)(*(void ***)(*slot))[0])(*slot);
        }
    }
    *(void **)((char *)self + 0x728) = ss;
    *slot = out_handle;
    ((StartSound_t)ADDR_START_SOUND)(out_handle);
    *(int *)((char *)self + 0x738) = 1;
    return 1;
}

static void __fastcall hook_play_endgame_music(void *self, void *edx) {
    (void)edx;
    if (*(int *)((char *)self + 0x738) != 0) return;

    void *ss = *(void **)ADDR_SOUND_SYSTEM;
    void *game = *(void **)ADDR_GAME_PTR;
    void *player = (ss && game) ? ((GetLocalPlayer_t)ADDR_GET_LOCAL_PLAYER)(game) : NULL;
    if (!player || *(BYTE *)((char *)player + 0x35) != 1) {
        o_play_endgame_music(self);
        return;
    }

    void *civ = *(void **)((char *)player + 0x1ae4);
    int civ_id = civ ? *(int *)((char *)civ + 8) : -1;
    const char *vsound = (civ_id >= 0) ? wl_civ_get_victory_sound(civ_id) : NULL;
    if (!vsound || !vsound[0]) {
        o_play_endgame_music(self);
        return;
    }

    if (!play_victory_file(self, ss, vsound)) {
        wl_log("victory: failed to load '%s' for civ %d, falling back to vanilla", vsound, civ_id);
        o_play_endgame_music(self);
        return;
    }
    wl_log("victory: playing civ %d victory sound '%s'", civ_id, vsound);
}

void wl_install_victory(void) {
    static const BYTE music_bytes[]  = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE player_bytes[] = { 0x8b, 0x81, 0xc8, 0x00, 0x00, 0x00, 0x8b, 0x88, 0x74, 0x01 };
    static const BYTE play_bytes[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE start_bytes[]  = { 0x56, 0x57, 0x8b, 0xf9 };
    static const BYTE assign_bytes[] = { 0x55, 0x8b, 0xec, 0x53, 0x57 };

    if (memcmp((void *)ADDR_PLAY_ENDGAME_MUSIC, music_bytes, sizeof music_bytes) != 0 ||
        memcmp((void *)ADDR_GET_LOCAL_PLAYER, player_bytes, sizeof player_bytes) != 0 ||
        memcmp((void *)ADDR_PLAY_SOUND, play_bytes, sizeof play_bytes) != 0 ||
        memcmp((void *)ADDR_START_SOUND, start_bytes, sizeof start_bytes) != 0 ||
        memcmp((void *)ADDR_STRING_ASSIGN, assign_bytes, sizeof assign_bytes) != 0) {
        wl_log("victory: unexpected exe bytes, victory sound hook skipped");
        return;
    }

    o_play_endgame_music = (PlayEndgameMusic_t)wl_hook(ADDR_PLAY_ENDGAME_MUSIC, hook_play_endgame_music, sizeof music_bytes);
    if (!o_play_endgame_music) {
        wl_log("victory: hook on 0x6a2e00 failed");
        return;
    }
    wl_log("victory: hook installed on 0x6a2e00");
}
