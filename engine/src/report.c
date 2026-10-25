/* The daily report's rules (docs/ui-reports.md, section 1). Only the player's report is shown (player 0); the day
 * change keeps the baseline of player 1 as well, as the original does. */
#include "report.h"

#include "sim_internal.h"

#define PERSONS 1009
#define PERSON_SIZE 106
#define PERSON_OPINION 20 /* the opinion of hospital 0 */
#define NAMES 0x3BCF /* hospital names, 30 bytes, byte 29 = the gender class */

uint8_t *report_pb(Sim *s, int player) { return s->world->block[player ? BLK_PLAYER1 : BLK_PLAYER0]; }

/* 0x1A5B0 */
void report_baseline(Sim *s, int player) {
    uint8_t *pb = report_pb(s, player), *h = hospital(s, player);
    int32_t worth = get32(h + H_CASH) + get32(h + 0x152);
    put32(pb + RP_WORTH, worth - get32(pb + RP_WORTH_BASE));
    put32(pb + RP_WORTH_BASE, worth);
    static const struct { int field, value, base; } C[3] = {
        {0x26, RP_DISCHARGED, RP_DISCHARGED_BASE}, {0x28, RP_CURED, RP_CURED_BASE}, {0x2A, RP_UNCURED, RP_UNCURED_BASE}};
    for (int i = 0; i < 3; i++) {
        int v = get16(h + C[i].field);
        put16(pb + C[i].value, v - get16(pb + C[i].base));
        put16(pb + C[i].base, v);
    }
    sim_opinion_sum(s);
    sim_score(s);
    int32_t score = get32(h + 0x44);
    put32(pb + RP_SCORE_CHANGE, score - get32(pb + RP_SCORE));
    put32(pb + RP_SCORE, score);
    uint8_t order[16];
    sim_ranking(s, order);
    int rank = 0;
    while (rank < 16 && order[rank] != player) rank++; /* the original's search has no bound; 16 can't miss */
    rank++;
    put32(pb + RP_RANK_CHANGE, get32(pb + RP_RANK) - rank);
    put32(pb + RP_RANK, rank);
    int deaths = get16(h + 0x38);
    put16(pb + RP_DEATHS, deaths - get16(pb + RP_DEATHS_BASE));
    put16(pb + RP_DEATHS_BASE, deaths);
}

void report_new_game(Sim *s) {
    report_baseline(s, 0);
    report_baseline(s, 0); /* the second call stores every value again with a difference of 0; block 29 untouched */
    sim_expected_cash_all(s);
}

/* 0x3DCCB */
void report_day_change(Sim *s) {
    for (int p = 0; p < 2; p++) {
        uint8_t *pb = report_pb(s, p);
        pb[RP_REDRAW] = 0xFF;
        pb[RP_ARMED] = 0xFF;
        report_baseline(s, p);
        uint8_t *h = hospital(s, p);
        put16(h + H_DAYS_PLAYED, get16(h + H_DAYS_PLAYED) + 1);
    }
}

/* ---- the paragraphs (0x68A45) ------------------------------------------------------------------------------- */

static int face(int points, int high, int mid, int base) { /* base = the "high" face; +5 mid, +10 low */
    return points >= high ? base : points >= mid ? base + 5 : base + 10;
}

static int money(Sim *s, Pieces *t) {
    const uint8_t *pb = report_pb(s, 0);
    int pts = 0;
    int32_t worth = get32(pb + RP_WORTH);
    if (worth >= 25) pts += 2, pc_text(t, 1677), pc_num(t, worth), pc_text(t, 1675);
    else if (worth >= -25) pts += 1, pc_text(t, 1676);
    else pc_text(t, 1674), pc_num(t, -(long)worth), pc_text(t, 1675);
    pc_text(t, 1678);
    int32_t rc = get32(pb + RP_RANK_CHANGE), rank = get32(pb + RP_RANK);
    if (rc > 1) pts += 3, pc_text(t, 1679), pc_num(t, rc), pc_text(t, 1680);
    else if (rc == 1) pts += 2, pc_text(t, 1682);
    else if (rc == 0) pts += 1 + (rank == 1 ? 2 : 0), pc_text(t, 1683);
    else if (rc == -1) pc_text(t, 1685);
    else pc_text(t, 1684), pc_num(t, -(long)rc), pc_text(t, 1680);
    pc_num(t, rank), pc_text(t, 1681);
    pc_text(t, 1686);
    int32_t sc = get32(pb + RP_SCORE_CHANGE);
    if (sc > 1) pts += 1, pc_text(t, 1687), pc_num(t, sc), pc_text(t, 1688);
    else if (sc == 1) pc_text(t, 1690);
    else if (sc == 0) pc_text(t, 1691);
    else if (sc == -1) pc_text(t, 1693);
    else pc_text(t, 1692), pc_num(t, -(long)sc), pc_text(t, 1688);
    pc_num(t, get32(pb + RP_SCORE)), pc_text(t, 1689);
    return pts;
}

static int patients(Sim *s, Pieces *t) {
    const uint8_t *pb = report_pb(s, 0);
    int d = get16(pb + RP_DISCHARGED), c = get16(pb + RP_CURED), u = get16(pb + RP_UNCURED), dead = get16(pb + RP_DEATHS);
    int pts = 0;
    pc_text(t, 1694);
    /* engine choice: a negative difference (a counter that fell) takes the "n" branch of each table */
    if (d == 0) pc_text(t, 1696);
    else if (d == 1) pc_text(t, 1695);
    else pts += d >= 40, pc_text(t, 1697), pc_num(t, d), pc_text(t, 1698);
    if (c == 0) {
        pc_text(t, 1701);
        if (u == 0) pc_text(t, 1706);
        else if (u == 1) pc_text(t, 1702), pc_text(t, 1705);
        else pc_text(t, 1702), pc_text(t, 1703), pc_num(t, u), pc_text(t, 1704);
    } else {
        pc_text(t, 1699);
        if (c == 1) pc_text(t, 1700);
        else pts += c >= 35, pc_text(t, 1707), pc_num(t, c), pc_text(t, 1708);
        pc_text(t, 1702);
        if (u == 0) pts += 2, pc_text(t, 1711);
        else if (u == 1) pts += d >= 40, pc_text(t, 1705);
        else if (u == 2) pts += d >= 60, pc_text(t, 1709), pc_num(t, u), pc_text(t, 1710);
        else pc_text(t, 1709), pc_num(t, u), pc_text(t, 1710);
    }
    pts += 1;
    if (dead == 0) pc_text(t, 1714);
    else if (dead == 1) pts -= 2, pc_text(t, 1715);
    else pts -= 3, pc_text(t, 1712), pc_num(t, dead), pc_text(t, 1713);
    return pts;
}

static int nurses(Sim *s, Pieces *t) {
    const uint8_t *h = hospital(s, 0);
    int bust = get16(h + 0x4A), age = h[0x113], fig = h[0x14A] + h[0x14B], pts = 0;
    pc_text(t, 1716), pc_num(t, bust), pc_text(t, 1717);
    static const struct { int at, pts, text; } BUST[6] = {{130, 3, 1718}, {120, 2, 1719}, {110, 1, 1720},
                                                          {100, 1, 1721}, {90, 0, 1722}, {-32768, 0, 1723}};
    for (int i = 0;; i++)
        if (bust >= BUST[i].at) {
            pts += BUST[i].pts, pc_text(t, BUST[i].text);
            break;
        }
    pc_text(t, 1724), pc_num(t, age), pc_text(t, 1725);
    pts += (age < 24) + (age < 22);
    static const struct { int at, pts, text; } FIG[6] = {{165, 0, 1726}, {160, 0, 1727}, {155, 1, 1728},
                                                         {150, 1, 1729}, {145, 2, 1730}, {-1, 3, 1731}};
    for (int i = 0;; i++)
        if (fig >= FIG[i].at) {
            pts += FIG[i].pts, pc_text(t, FIG[i].text);
            break;
        }
    return pts;
}

static int opinion(Sim *s, Pieces *t) {
    sim_opinion_sum(s);
    uint32_t a = (uint32_t)get32(hospital(s, 0) + 0x44) / 1008;
    int pts = 0;
    pc_text(t, 1732), pc_num(t, (long)a), pc_text(t, 1733);
    pts += (a >= 55) + (a >= 52) + (a >= 51) + (a >= 49) - (a < 45) - (a < 40);
    int haters = 0, praisers = 0;
    const uint8_t *p = s->world->block[BLK_PATIENTS] + PERSON_SIZE;
    for (int i = 1; i < PERSONS; i++, p += PERSON_SIZE) {
        haters += p[PERSON_OPINION] < 30;
        praisers += p[PERSON_OPINION] >= 70;
    }
    if (haters == 0) pc_text(t, 1736);
    else if (haters == 1) pc_text(t, 1737);
    else pts -= (haters >= 10) + (haters >= 30) + (haters >= 60), pc_text(t, 1734), pc_num(t, haters), pc_text(t, 1735);
    if (praisers == 0) pc_text(t, 1740);
    else if (praisers == 1) pc_text(t, 1741);
    else /* the bonus counts the haters, not the praisers: a fault of the original, kept */
        pts += (haters >= 10) + (haters >= 30) + (haters >= 60) + (haters >= 100), pc_text(t, 1738), pc_num(t, praisers),
            pc_text(t, 1739);
    return pts;
}

void report_build(Sim *s, Report *r) {
    uint8_t *pb = report_pb(s, 0);
    const uint8_t *h = hospital(s, 0);
    pb[RP_ARMED] = 0;
    pieces_clear(&r->heading);
    pc_text(&r->heading, 1672);
    pc_text(&r->heading, 2663 + program_u8(s->prog, NAMES, 29)); /* 0x3B572: "des " / "der " by the gender */
    pc_hospital(&r->heading, 0);
    pc_text(&r->heading, 1673);
    pc_num(&r->heading, get16(h + H_DAYS_PLAYED));
    r->total = 0;
    for (int i = 0; i < 4; i++) {
        pieces_clear(&r->para[i]);
        r->shown[i] = 1;
        r->points[i] = 0;
        r->face[i] = 0;
    }
    r->points[0] = money(s, &r->para[0]);
    r->face[0] = face(r->points[0], 4, 2, 1291);
    r->points[1] = patients(s, &r->para[1]);
    r->face[1] = face(r->points[1], 3, 1, 1292);
    sim_staff_averages(s);
    if (get16(h + 0x4A)) {
        r->points[2] = nurses(s, &r->para[2]);
        r->face[2] = face(r->points[2], 4, 3, 1293);
    } else {
        r->shown[2] = 0;
    }
    r->points[3] = opinion(s, &r->para[3]);
    r->face[3] = face(r->points[3], 4, 2, 1294);
    for (int i = 0; i < 4; i++) r->total += r->points[i];
    r->overall = face(r->total, 16, 8, 1295);
    r->sound = r->total >= 16 ? 0 : r->total >= 8 ? 1 : 2;
}
