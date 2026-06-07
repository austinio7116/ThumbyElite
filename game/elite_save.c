/*
 * ThumbyElite — save game.
 *
 * Layout: header {magic, version, len, crc32} + payload {galaxy seed,
 * docked addr/station, kills, PlayerState, rep, missions}. Additive
 * schema: bump VERSION on change; loader rejects mismatches (fresh
 * start beats corrupt state).
 */
#include "elite_save.h"
#include "elite_audio.h"
#include <stddef.h>
#include "elite_player.h"
#include "elite_platform.h"
#include "mission.h"
#include <string.h>

#define SAVE_MAGIC   0x454C4954u   /* 'ELIT' */
#define SAVE_VERSION 4   /* v4: four utility bays (v3 loads migrate) */

typedef struct {
    uint32_t magic, version, len, crc;
} SaveHeader;

typedef struct {
    uint32_t galaxy_seed;
    SysAddr  addr;
    uint8_t  station;
    uint8_t  pad[3];
    int32_t  kills;
    PlayerState player;
    int8_t   rep[N_FACTIONS];
    uint8_t  pad2;
    Mission  missions[MAX_MISSIONS];
} SavePayload;

typedef struct {
    SaveHeader h;
    SavePayload p;
} SaveBlob;

static uint32_t crc32_simple(const uint8_t *d, int n) {
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
    }
    return ~c;
}

static bool read_blob(SaveBlob *blob) {
    int n = plat_load((uint8_t *)blob, (int)sizeof *blob);
    if (n < (int)sizeof *blob) return false;
    if (blob->h.magic != SAVE_MAGIC) return false;
    if (blob->h.version == SAVE_VERSION) {
        if (blob->h.len != sizeof(SavePayload)) return false;
    } else if (blob->h.version == 3) {
        /* v3 -> v4: PlayerState grew util_eq[2] -> [4]. The payload on
         * disk is 16 bytes shorter and everything after util_eq sits
         * earlier. Migrate by splitting at the insertion point. */
        if (blob->h.len + 2 * sizeof(WeaponInst) != sizeof(SavePayload))
            return false;
        uint8_t *p = (uint8_t *)&blob->p;
        size_t cut = offsetof(SavePayload, player) +
                     offsetof(PlayerState, util_eq) +
                     2 * sizeof(WeaponInst);
        size_t grow = 2 * sizeof(WeaponInst);
        size_t tail = blob->h.len - cut;
        memmove(p + cut + grow, p + cut, tail);
        memset(p + cut, 0, grow);          /* new bays arrive empty */
    } else {
        return false;
    }
    if (blob->h.crc != crc32_simple((const uint8_t *)&blob->p,
                                    (int)sizeof blob->p))
        return false;
    return true;
}

bool save_exists(void) {
    SaveBlob b;
    return read_blob(&b);
}

bool save_write(SysAddr addr, int station, int kills) {
    SaveBlob b;
    memset(&b, 0, sizeof b);
    b.p.galaxy_seed = galaxy_get_seed();
    b.p.addr = addr;
    b.p.station = (uint8_t)station;
    b.p.kills = kills;
    b.p.player = g_player;
    memcpy(b.p.rep, g_rep, sizeof b.p.rep);
    memcpy(b.p.missions, g_missions, sizeof b.p.missions);
    b.h.magic = SAVE_MAGIC;
    b.h.version = SAVE_VERSION;
    b.h.len = sizeof(SavePayload);
    b.h.crc = crc32_simple((const uint8_t *)&b.p, (int)sizeof b.p);
    return plat_save((const uint8_t *)&b, (int)sizeof b) != 0;
}

/* True if the stored save belongs to the given galaxy — insurance
 * must NOT resurrect a pilot into a previous campaign (NEW GAME never
 * deletes the old save; first dock of the new run overwrites it). */
bool save_matches_galaxy(uint32_t seed) {
    SaveBlob b;
    return read_blob(&b) && b.p.galaxy_seed == seed;
}

bool save_load(SaveMeta *out) {
    SaveBlob b;
    if (!read_blob(&b)) return false;
    galaxy_set_seed(b.p.galaxy_seed);
    g_player = b.p.player;
    sfx_set_laser(g_player.laser_sfx);     /* restore the chosen sfx */
    memcpy(g_rep, b.p.rep, sizeof b.p.rep);
    memcpy(g_missions, b.p.missions, sizeof b.p.missions);
    out->addr = b.p.addr;
    out->station = b.p.station;
    out->kills = b.p.kills;
    return true;
}
