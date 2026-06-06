/*
 * ThumbyElite — procedural missions + faction reputation.
 */
#include "mission.h"
#include "elite_player.h"
#include "econ.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

Mission g_missions[MAX_MISSIONS];
int8_t  g_rep[N_FACTIONS];

const char *k_faction_names[N_FACTIONS] = {
    "COALITION", "DOMINION", "FREEHOLDS",
};

static uint32_t s_visit_salt;     /* changes offers between visits */

Faction system_faction(SysAddr a) {
    /* Big contiguous blobs: hash coarse 4x4-sector cells. */
    uint32_t h = (uint32_t)(a.sx >> 2) * 2654435761u ^
                 (uint32_t)(a.sy >> 2) * 668265263u ^
                 (galaxy_get_seed() * 951274213u);
    h ^= h >> 13;
    return (Faction)(h % N_FACTIONS);
}

void missions_init(void) {
    memset(g_missions, 0, sizeof g_missions);
    memset(g_rep, 0, sizeof g_rep);
    s_visit_salt = 0;
}

/* Public wrapper (distress rescues pay rep from elite_game). */
void mission_rep_add_public(int faction, int amt);
static void rep_add(int faction, int amt);
void mission_rep_add_public(int faction, int amt) {
    rep_add(faction, amt);
}

static void rep_add(int faction, int amt) {
    int v = g_rep[faction] + amt;
    if (v > 100) v = 100;
    if (v < -100) v = -100;
    g_rep[faction] = (int8_t)v;
}

/* Find a nearby system with a station (delivery destinations). */
static bool find_dest(SysAddr from, uint32_t salt, SysAddr *out, int *out_st) {
    for (int tries = 0; tries < 24; tries++) {
        uint32_t h = salt * 2654435761u + (uint32_t)tries * 97u;
        h ^= h >> 15;
        int dx = (int)(h % 7u) - 3;
        int dy = (int)((h >> 8) % 7u) - 3;
        if (!dx && !dy) continue;
        SysAddr a = { from.sx + dx, from.sy + dy, 0 };
        int n = galaxy_sector_stars(a.sx, a.sy);
        if (n == 0) continue;
        a.idx = (uint8_t)((h >> 16) % (uint32_t)n);
        SystemInfo si;
        galaxy_generate(a, &si);
        if (si.n_stations == 0) continue;
        *out = a;
        *out_st = (int)((h >> 20) % si.n_stations);
        return true;
    }
    return false;
}

void mission_make_offers(const SystemInfo *si, int station,
                         Mission out[MISSION_OFFERS]) {
    Faction fac = system_faction(si->addr);
    float rep_bonus = 1.0f + 0.004f * (float)g_rep[fac];
    for (int i = 0; i < MISSION_OFFERS; i++) {
        Mission *m = &out[i];
        memset(m, 0, sizeof *m);
        uint32_t h = (uint32_t)(si->seed >> 12) ^ s_visit_salt ^
                     (uint32_t)((station + 1) * 7919) ^
                     (uint32_t)(i * 104729);
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;

        int roll = (int)(h % 100u);
        m->faction = (uint8_t)fac;

        if (roll < 45) {
            /* Delivery: goods the local economy exports. */
            SysAddr dest;
            int dst_st;
            if (!find_dest(si->addr, h, &dest, &dst_st)) continue;
            m->type = MIS_DELIVERY;
            m->good = (uint8_t)((h >> 8) % 16u);
            m->count = (uint8_t)(2 + ((h >> 16) % 6u));
            m->target = dest;
            m->station = (uint8_t)dst_st;
            char dname[14];
            galaxy_system_name(dest, dname);
            float dx = 0, dy = 0, px = 0, py = 0;
            galaxy_star_pos(dest, &dx, &dy);
            galaxy_star_pos(si->addr, &px, &py);
            float dist = sqrtf((dx - px) * (dx - px) + (dy - py) * (dy - py));
            m->reward = (int32_t)((120 + m->count * k_goods[m->good].base / 2 +
                                   (int)(dist * 50)) * rep_bonus);
            snprintf(m->label, sizeof m->label, "%dX %s>%s",
                     m->count, k_goods[m->good].name, dname);
        } else if (roll < 75) {
            m->type = MIS_CULL;
            m->count = (uint8_t)(2 + ((h >> 9) % 4u));
            m->reward = (int32_t)(m->count * 320 * rep_bonus);
            snprintf(m->label, sizeof m->label, "CULL %d PIRATES", m->count);
        } else {
            /* Bounty: a marked pilot waits at a nearby beacon. Tier
             * varies — EASY marks for starter ships, ACE paydays for
             * the brave (user spec). */
            SysAddr dest;
            int dst_st;
            if (!find_dest(si->addr, h ^ 0xB011u, &dest, &dst_st)) continue;
            m->type = MIS_BOUNTY;
            m->target = dest;
            m->tier = (uint8_t)(1 + ((h >> 24) % 4u));
            static const int k_pay[5] = { 0, 600, 1300, 2800, 6500 };
            static const char *k_tag[5] = { "", "EASY", "RISKY", "HARD",
                                            "ACE" };
            m->reward = (int32_t)(k_pay[m->tier] * rep_bonus);
            char dname[14];
            galaxy_system_name(dest, dname);
            snprintf(m->label, sizeof m->label, "%s MARK>%s",
                     k_tag[m->tier], dname);
        }
    }
}

bool mission_accept(const Mission *m) {
    for (int i = 0; i < MAX_MISSIONS; i++) {
        if (g_missions[i].type != MIS_NONE) continue;
        g_missions[i] = *m;
        /* Delivery missions hand you the cargo. */
        if (m->type == MIS_DELIVERY) {
            int room = player_cargo_cap() - player_cargo_total();
            if (room < m->count) { g_missions[i].type = MIS_NONE; return false; }
            g_player.cargo[m->good] += m->count;
        }
        return true;
    }
    return false;
}

void mission_on_kill(int victim_tier, bool was_bounty_mark) {
    for (int i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &g_missions[i];
        if (m->done) continue;
        if (m->type == MIS_CULL && m->count > 0) {
            m->count--;
            if (m->count == 0) m->done = true;
        } else if (m->type == MIS_BOUNTY && was_bounty_mark) {
            m->done = true;
        }
    }
    (void)victim_tier;
}

int mission_bounty_tier_here(SysAddr a) {
    for (int i = 0; i < MAX_MISSIONS; i++)
        if (g_missions[i].type == MIS_BOUNTY && !g_missions[i].done &&
            sysaddr_eq(g_missions[i].target, a))
            return g_missions[i].tier;
    return -1;
}

bool mission_objective_here(SysAddr a) {
    for (int i = 0; i < MAX_MISSIONS; i++) {
        const Mission *m = &g_missions[i];
        if (m->done) continue;
        if ((m->type == MIS_BOUNTY || m->type == MIS_DELIVERY) &&
            sysaddr_eq(m->target, a))
            return true;
    }
    return false;
}

void mission_on_docked(const SystemInfo *si, int station) {
    (void)si; (void)station;
    s_visit_salt += 0x9E3779B9u;     /* fresh offers next visit */
}

int mission_collect(const SystemInfo *si, int station) {
    int paid = 0;
    for (int i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &g_missions[i];
        if (m->type == MIS_NONE) continue;

        /* Deliveries complete at their named station with cargo aboard. */
        if (m->type == MIS_DELIVERY && !m->done) {
            if (sysaddr_eq(m->target, si->addr) && station == m->station &&
                g_player.cargo[m->good] >= m->count) {
                g_player.cargo[m->good] -= m->count;
                m->done = true;
            }
        }
        if (!m->done) continue;

        paid += m->reward;
        g_player.credits += m->reward;
        rep_add(m->faction, (m->type == MIS_BOUNTY) ? 8 : 4);
        g_player.xp_trading += (m->type == MIS_DELIVERY) ? 2 : 0;
        g_player.xp_gunnery += (m->type != MIS_DELIVERY) ? 1 : 0;
        m->type = MIS_NONE;
    }
    return paid;
}
