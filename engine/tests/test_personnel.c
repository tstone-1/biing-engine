/* The player's personnel rules (docs/ui-personnel.md). Synthetic world and program image (see AGENTS.md); the
 * values are chosen so that the results are the oracle numbers of the spec, e.g. person 844 (demand 7) hired
 * through a nurse ad of wage 10 with 983 Lümmel leaves 973. */
#include <stdio.h>
#include <string.h>

#include "personnel.h"
#include "sim_internal.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static World g_world;
static Program g_prog;
static uint8_t g_image[0x10000];
static Sim g_sim;
static Sim *s = &g_sim;

static uint8_t *h(void) { return world_hospital(&g_world, 0); }
static int32_t cash(void) { return world_cash(&g_world, 0); }
static uint8_t *st(int i) { return personnel_staff(s, i); }
static uint8_t *sl(int i) { return personnel_slot(s, i); }
static uint8_t *op(int j) { return personnel_opening(s, j); }
static uint8_t *pb(void) { return personnel_pb(s); }
static uint8_t *course(int e) { return personnel_course(s, e); }

/* every person in the pool, no reservation, never dismissed; no openings, no courses */
static void fresh(int32_t money) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 7);
    for (int i = 0; i < 1041; i++) {
        uint8_t *p = st(i);
        p[3] = p[4] = p[22] = p[27] = 0xFF;
        p[21] = 50;
    }
    for (int j = 0; j < 200; j++) op(j)[0] = 0xFF;
    for (int e = 0; e < 20; e++) course(e)[0] = 0xFF;
    world_set_cash(&g_world, 0, money);
    /* the start values of the remembered ads */
    put16(pb() + PB_NURSE_BUST, 110), put16(pb() + PB_NURSE_WAGE, 10), put16(pb() + PB_NURSE_TRAINING, 5);
    put16(pb() + PB_DOCTOR_TRAINING, 5), put16(pb() + PB_DOCTOR_CRITERION, 70);
}

static int pb16(int off) { return get16(pb() + off); }

/* a person of the pool: sex, group, training, demand */
static void person(int i, int sex, int group, int training, int demand) {
    uint8_t *p = st(i);
    p[0] = (uint8_t)sex, p[1] = (uint8_t)group, p[2] = (uint8_t)training, p[18] = (uint8_t)demand;
}

/* an employee of hospital 0 in slot k: staff index i, group, duty state */
static void employ(int k, int i, int group, int state) {
    put16(sl(k), i);
    sl(k)[2] = (uint8_t)group;
    sl(k)[3] = (uint8_t)state;
    st(i)[1] = (uint8_t)group;
    st(i)[3] = 0;
    st(i)[4] = (uint8_t)state;
    sl(k)[6] = st(i)[19] = 10;
}

/* ---- job ads ------------------------------------------------------------------------------------- */

static void test_ad_form(void) {
    fresh(984);
    personnel_ad_defaults(s, 1);
    CHECK(pb16(PB_WAGE) == 10 && pb16(PB_BUST) == 110 && pb16(PB_TRAINING) == 5 && pb16(PB_CRITERION) == 0);
    CHECK(pb16(PB_GROUP) == 1);
    for (int i = 0; i < 50; i++) personnel_ad_arrow(s, AD_WAGE, 1);
    CHECK(pb16(PB_WAGE) == 40 && pb16(PB_NURSE_WAGE) == 40);
    for (int i = 0; i < 50; i++) personnel_ad_arrow(s, AD_WAGE, 0);
    CHECK(pb16(PB_WAGE) == 1); /* -1 while >= 2 */
    for (int i = 0; i < 50; i++) personnel_ad_arrow(s, AD_SECOND, 1);
    CHECK(pb16(PB_BUST) == 150 && pb16(PB_NURSE_BUST) == 150);
    for (int i = 0; i < 90; i++) personnel_ad_arrow(s, AD_SECOND, 0);
    CHECK(pb16(PB_BUST) == 69); /* floor 69 */
    for (int i = 0; i < 99; i++) personnel_ad_arrow(s, AD_TRAINING, 1);
    CHECK(pb16(PB_TRAINING) == 90 && pb16(PB_NURSE_TRAINING) == 90 && pb16(PB_DOCTOR_TRAINING) == 5);
    for (int i = 0; i < 99; i++) personnel_ad_arrow(s, AD_TRAINING, 0);
    CHECK(pb16(PB_TRAINING) == 0);
    /* the remembered nurse ad comes back */
    personnel_ad_defaults(s, 3);
    CHECK(pb16(PB_WAGE) == 5 && pb16(PB_TRAINING) == 0 && pb16(PB_CRITERION) == 29 && pb16(PB_GROUP) == 3);
    personnel_ad_arrow(s, AD_SECOND, 0);
    CHECK(pb16(PB_CRITERION) == 29); /* the default 29 cannot go down */
    personnel_ad_arrow(s, AD_SECOND, 1);
    CHECK(pb16(PB_CRITERION) == 34 && pb16(PB_DOCTOR_CRITERION) == 34);
    for (int i = 0; i < 99; i++) personnel_ad_arrow(s, AD_WAGE, 1);
    CHECK(pb16(PB_WAGE) == 50 && pb16(PB_NURSE_WAGE) == 1); /* other groups: 50, not remembered */
    personnel_ad_arrow(s, AD_TRAINING, 1);
    CHECK(pb16(PB_DOCTOR_TRAINING) == 1 && pb16(PB_NURSE_TRAINING) == 0);
    personnel_ad_defaults(s, 1);
    CHECK(pb16(PB_WAGE) == 1 && pb16(PB_BUST) == 69 && pb16(PB_TRAINING) == 0);
    /* doctors 5: 70 + 5 steps to 140, then stop; group 2 reads back the remembered values */
    personnel_ad_defaults(s, 5);
    CHECK(pb16(PB_WAGE) == 10 && pb16(PB_TRAINING) == 5 && pb16(PB_CRITERION) == 70);
    for (int i = 0; i < 30; i++) personnel_ad_arrow(s, AD_SECOND, 1);
    CHECK(pb16(PB_CRITERION) == 140);
    personnel_ad_defaults(s, 2);
    CHECK(pb16(PB_WAGE) == 9 && pb16(PB_TRAINING) == 1 && pb16(PB_CRITERION) == 140);
    personnel_ad_defaults(s, 9);
    CHECK(pb16(PB_WAGE) == 9 && pb16(PB_TRAINING) == 4 && pb16(PB_CRITERION) == 72);
    CHECK(personnel_ad_next_group(10) == 1 && personnel_ad_next_group(3) == 4);
}

static void test_publish(void) {
    fresh(984);
    op(0)[0] = 3; /* an AI opening in use */
    personnel_ad_defaults(s, 1);
    CHECK(personnel_publish(s) == 1);
    static const uint8_t AD[7] = {0x00, 0x01, 0x0A, 0x6E, 0x00, 0x05, 0x00};
    CHECK(memcmp(op(1), AD, 7) == 0);
    CHECK(cash() == 984 && get32(h() + 0xA6) == 0); /* nothing paid now */
    personnel_ad_defaults(s, 3);
    CHECK(personnel_publish(s) == 2 && op(2)[1] == 3 && op(2)[4] == 29 && op(2)[3] == 0);
    for (int j = 0; j < 200; j++) op(j)[0] = 5;
    CHECK(personnel_publish(s) == -1); /* text 555 */
    /* one hour of the ad costs 1 (0x2B6C3, the simulation) */
    fresh(984);
    personnel_ad_defaults(s, 1);
    personnel_publish(s);
    sim_call(s, 0x2B6C3);
    CHECK(cash() == 983 && get32(h() + 0xA6) == 1 && get32(h() + 0xAA) == 1);
}

static void test_ad_expiry(void) {
    fresh(984);
    personnel_ad_defaults(s, 1);
    int j = personnel_publish(s);
    op(j)[6] = 60;
    person(51, 1, 1, 6, 7);
    st(51)[27] = (uint8_t)j;
    s->on_applications = 1; /* the applications are open: the ad stays */
    sim_call(s, 0x1F2A8);
    CHECK(op(j)[0] == 0 && st(51)[27] == j);
    s->on_applications = 0;
    sim_call(s, 0x1F2A8);
    CHECK((int8_t)op(j)[0] < 0 && st(51)[27] == 0xFF);
}

/* ---- applications ---------------------------------------------------------------------------------- */

static void test_shuffle(void) {
    fresh(0);
    static uint16_t list[PERSONNEL_LIST];
    personnel_shuffle(s, list);
    static int seen[1041];
    memset(seen, 0, sizeof seen);
    int moved = 0;
    for (int i = 0; i <= 1040; i++) seen[list[i]]++, moved += list[i] != i + 1;
    int ok = list[0] == 1 && seen[0] == 1;
    for (int i = 1; i <= 1040; i++) ok &= seen[i] == 1;
    CHECK(ok && moved > 900);
}

static uint16_t g_list[PERSONNEL_LIST];

static void applicant_world(void) {
    fresh(983);
    for (int i = 0; i < PERSONNEL_LIST; i++) g_list[i] = (uint16_t)(i <= 1040 ? i : 0); /* position p = person p */
    personnel_ad_defaults(s, 1);
    int j = personnel_publish(s); /* opening 0 */
    CHECK(j == 0);
    person(844, 1, 1, 6, 7);
    person(51, 1, 1, 6, 5);
    st(844)[27] = st(51)[27] = 0;
    op(1)[0] = 4, op(1)[1] = 1; /* an AI ad */
    person(300, 1, 1, 6, 5);
    st(300)[27] = 1; /* reserved the AI's ad: not an applicant of the player */
}

static void test_applicants(void) {
    applicant_world();
    CHECK(personnel_is_applicant(s, 844) && personnel_is_applicant(s, 51) && !personnel_is_applicant(s, 300));
    CHECK(!personnel_is_applicant(s, 0));
    CHECK(personnel_next_applicant(s, g_list, 0) == 51);
    CHECK(personnel_next_applicant(s, g_list, 51) == 844);
    CHECK(personnel_next_applicant(s, g_list, 844) == 51); /* wraps after 1,040 */
    person(1, 1, 1, 6, 5);
    st(1)[27] = 0;
    CHECK(personnel_next_applicant(s, g_list, 844) == 1 && personnel_prev_applicant(s, g_list, 51) == 1);
    CHECK(personnel_prev_applicant(s, g_list, 1) == 844); /* below 1 back to 1,040 */
    st(1)[27] = 0xFF;
    CHECK(personnel_prev_applicant(s, g_list, 51) == 844);
    CHECK(personnel_prev_applicant(s, g_list, 844) == 51);
    st(844)[3] = 2; /* employed elsewhere */
    CHECK(personnel_next_applicant(s, g_list, 51) == 51);
    st(51)[27] = 0xFF;
    CHECK(personnel_next_applicant(s, g_list, 0) == -1); /* text 1371 */
}

static void test_hire(void) {
    applicant_world();
    put16(sl(0), 1); /* slot 0 taken */
    memset(sl(1) + 7, 0x5A, 13); /* what the free slot had in +7 ... +19 */
    int k = -1;
    CHECK(personnel_hire(s, 844, &k) == 0 && k == 1);
    static const uint8_t SLOT[7] = {0x4C, 0x03, 0x01, 0x00, 0x00, 0x00, 0x0A};
    CHECK(memcmp(sl(1), SLOT, 7) == 0 && sl(1)[18] == 0xFF);
    CHECK(sl(1)[7] == 0x5A && sl(1)[17] == 0x5A && sl(1)[19] == 0x5A); /* the rest left as they were */
    CHECK(st(844)[19] == 10); /* the ad's wage, not demand 7 + 2 */
    CHECK(cash() == 973 && get32(h() + 0xA6) == 10 && get32(h() + 0xAA) == 10);
    CHECK(st(844)[3] == 0 && st(844)[4] == 0 && st(844)[27] == 0xFF);
    CHECK(st(51)[27] == 0xFF && st(300)[27] == 1); /* the other applicant is released, the AI's kept */
    CHECK((int8_t)op(0)[0] < 0 && op(1)[0] == 4);
    /* below 20 Lümmel: text 750, nothing changes */
    applicant_world();
    world_set_cash(&g_world, 0, 19);
    CHECK(personnel_hire(s, 844, &k) == PERS_BROKE && cash() == 19 && get16(sl(0)) == 0 && op(0)[0] == 0);
    world_set_cash(&g_world, 0, 20);
    CHECK(personnel_hire(s, 844, &k) == 0 && cash() == 10);
    /* no free slot: text 751 */
    applicant_world();
    for (int i = 0; i < 200; i++) put16(sl(i), 900 + i);
    CHECK(personnel_hire(s, 844, &k) == PERS_ENOUGH_STAFF && cash() == 983 && st(844)[27] == 0);
}

static void test_tear_up(void) {
    applicant_world();
    personnel_tear_up(s, 844);
    CHECK(st(844)[27] == 0xFF && st(844)[22] == 0 && op(0)[0] == 0 && st(51)[27] == 0);
    /* byte 22 = 0 bars the person from the player's ads (the pairing 0x1F1F3 compares it with +0) */
    CHECK(!personnel_is_applicant(s, 844));
}

/* ---- the sheet ------------------------------------------------------------------------------------- */

static void test_search(void) {
    fresh(0);
    employ(5, 10, 1, 0);
    employ(7, 11, 2, 0x13);
    employ(9, 12, 1, 6);
    employ(11, 13, 8, 0x14);
    employ(12, 14, 1, 0x19);
    employ(13, 15, 1, 0x18);
    CHECK(personnel_search(s, 0, 1, 210, FILTER_IDLE, 0) == 5);
    CHECK(personnel_search(s, 6, 1, 210, FILTER_IDLE, 0) == 5); /* wraps */
    CHECK(personnel_search(s, 0, 1, 210, FILTER_DISMISS, 0) == 5 && personnel_search(s, 6, 1, 210, FILTER_DISMISS, 0) == 7);
    CHECK(personnel_search(s, 0, 1, 210, FILTER_ROOMS, 0) == 9 && personnel_search(s, 10, 1, 210, FILTER_ROOMS, 0) == 11);
    CHECK(personnel_search(s, 0, 1, 210, FILTER_HOME, 0) == 7 && personnel_search(s, 8, 1, 210, FILTER_HOME, 0) == 13);
    CHECK(personnel_search(s, 0, 1, 210, FILTER_AWAY, 0) == 12);
    CHECK(personnel_search(s, 0, 1, 210, FILTER_DISMISS, 2) == 7 && personnel_search(s, 0, 1, 210, FILTER_IDLE, 2) == -1);
    CHECK(personnel_search(s, 4, -1, 210, FILTER_IDLE, 1) == 5); /* backwards, wrapping below 0 */
    CHECK(personnel_nobody_text(FILTER_IDLE) == 786 && personnel_nobody_text(FILTER_DISMISS) == 787 &&
          personnel_nobody_text(FILTER_ROOMS) == 788 && personnel_nobody_text(FILTER_HOME) == 789 &&
          personnel_nobody_text(FILTER_AWAY) == 790);
    world_set_tail(&g_world, TAIL_HOUR, 3);
    CHECK(personnel_dismissal_allowed(s) == 0);
    world_set_tail(&g_world, TAIL_HOUR, 4);
    CHECK(personnel_dismissal_allowed(s) == PERS_DISMISSAL_HOURS);
    world_set_tail(&g_world, TAIL_HOUR, 13);
    CHECK(personnel_dismissal_allowed(s) == PERS_DISMISSAL_HOURS);
}

static void test_wages_and_dismissal(void) {
    fresh(973);
    employ(0, 844, 1, 0);
    personnel_raise(s, 0);
    CHECK(sl(0)[6] == 11 && st(844)[19] == 11);
    for (int i = 0; i < 14; i++) personnel_cut(s, 0);
    CHECK(sl(0)[6] == 2 && st(844)[19] == 2); /* floor 2 */
    for (int i = 0; i < 40; i++) personnel_raise(s, 0);
    CHECK(sl(0)[6] == 30 && st(844)[19] == 30); /* cap 30 */
    employ(1, 845, 2, 5);
    CHECK(personnel_dismiss(s, 1) == 0 && get16(sl(1)) == 845 && cash() == 973); /* in a room: nothing */
    st(844)[11] = st(844)[23] = 0xFF, st(844)[20] = 3, st(844)[26] = 4;
    sl(0)[8] = 0x77;
    CHECK(personnel_dismiss(s, 0) == 1);
    CHECK(cash() == 943 && get32(h() + 0xA6) == 30 && get32(h() + 0xAA) == 30);
    CHECK(get16(sl(0)) == 0 && sl(0)[8] == 0x77 && sl(0)[6] == 30); /* the other slot bytes stay */
    CHECK(st(844)[3] == 0xFF && st(844)[4] == 0xFF && st(844)[26] == 0 && st(844)[20] == 0 && st(844)[22] == 0 &&
          st(844)[27] == 0xFF && st(844)[11] == 0 && st(844)[23] == 0);
    employ(2, 846, 1, 0x13);
    CHECK(personnel_dismiss(s, 2) == 1 && cash() == 913); /* off duty can go too */
}

static void test_independence(void) {
    fresh(0);
    employ(0, 844, 1, 0);
    uint8_t *p = st(844);
    p[0] = 1, p[2] = 6, p[5] = 100, p[6] = 60, p[7] = 90;
    CHECK(personnel_independence(p) == 1052); /* the newly hired nurse of the oracle */
    CHECK(personnel_toggle_independent(s, 0) == PERS_NOT_YET && p[11] == 0);
    p[5] = 130, p[6] = 51, p[7] = 100, p[12] = 4, p[13] = 3, put16(p + 16, 30);
    CHECK(personnel_independence(p) == 1100 - 48 - 896 - 15 - 16 - 24 - 30);
    put16(p + 16, 200);
    CHECK(personnel_independence(p) < 0);
    CHECK(personnel_toggle_independent(s, 0) == PERS_NOW_INDEPENDENT && p[11] == 0xFF);
    CHECK(personnel_toggle_independent(s, 0) == PERS_NOW_DEPENDENT && p[11] == 0);
    /* men: 1,200 - 8 x training - 128 x courses - byte 15 - experience */
    employ(1, 845, 2, 0);
    p = st(845);
    p[0] = 0, p[2] = 10, p[12] = 2, p[15] = 80, put16(p + 16, 5);
    CHECK(personnel_independence(p) == 1200 - 80 - 256 - 80 - 5);
    put16(p + 16, 30000);
    CHECK(personnel_independence(p) == (int16_t)(1200 - 80 - 256 - 80 - 30000));
    p = st(844); /* 16-bit arithmetic: a large negative score wraps to a positive one */
    p[12] = p[13] = 119, put16(p + 16, 30000);
    CHECK(personnel_independence(p) == (int16_t)(1100 - 48 - 128 * 238 - 15 - 16 - 24 - 30000) &&
          personnel_independence(p) > 0);
}

/* ---- sending home, releasing, room records --------------------------------------------------------- */

static uint8_t *room(int cat, int i) { return sim_room_record(s, 0, cat, i); }

static void in_room(int k, int i, int group, int cat, int inst, int off) {
    employ(k, i, group, cat);
    sl(k)[4] = st(i)[26] = (uint8_t)inst;
    put16(room(cat, inst) + off, i);
}

static void test_send_home(void) {
    fresh(0);
    in_room(0, 844, 1, 2, 0, 8);
    room(2, 0)[0x45] = 9;
    sl(0)[7] = 5, sl(0)[10] = 6, sl(0)[11] = 7, sl(0)[16] = 3, sl(0)[17] = 1;
    put16(room(2, 0) + 0x4E, 77); /* a patient queued at the desk */
    CHECK(personnel_send_home(s, 0) == PERS_BUSY && st(844)[4] == 2 && get16(room(2, 0) + 8) == 844);
    put16(room(2, 0) + 0x4E, 0);
    CHECK(personnel_send_home(s, 0) == 0);
    CHECK(get16(room(2, 0) + 8) == 0 && room(2, 0)[0x45] == 0);
    CHECK(sl(0)[3] == 0x13 && st(844)[4] == 0x13 && sl(0)[4] == 0 && st(844)[26] == 0);
    CHECK(sl(0)[7] == 0 && sl(0)[10] == 0 && sl(0)[11] == 0 && sl(0)[16] == 0 && sl(0)[17] == 0);
    CHECK(personnel_send_home(s, 0) == PERS_ALREADY_HOME);
    /* works independently: the room is remembered for the return after the rest */
    in_room(1, 845, 1, 6, 3, 8);
    st(845)[11] = 0xFF;
    room(6, 3)[0x0F] = 4;
    CHECK(personnel_send_home(s, 1) == 0 && sl(1)[16] == 6 && sl(1)[17] == 3 && room(6, 3)[0x0F] == 0 &&
          get16(room(6, 3) + 8) == 0);
    /* an idle employee goes home without a room */
    employ(2, 846, 1, 0);
    CHECK(personnel_send_home(s, 2) == 0 && st(846)[4] == 0x13);
    /* thug on a mission (0x14): refused */
    employ(3, 847, 8, 0x14);
    CHECK(personnel_send_home(s, 3) == PERS_BUSY && st(847)[4] == 0x14);
}

static void test_release(void) {
    fresh(0);
    employ(0, 844, 1, 0);
    CHECK(personnel_release(s, 0) == PERS_NOT_ASSIGNED);
    employ(3, 847, 1, 1); /* state 1 is below 2 too */
    CHECK(personnel_release(s, 3) == PERS_NOT_ASSIGNED && st(847)[4] == 1);
    employ(1, 845, 1, 0x13);
    CHECK(personnel_release(s, 1) == PERS_ALREADY_HOME);
    in_room(2, 846, 2, 5, 1, 0x0A);
    sl(2)[7] = 9, sl(2)[10] = 8, sl(2)[11] = 7;
    put16(room(5, 1) + 0x0C, 500); /* a patient in the room */
    CHECK(personnel_release(s, 2) == PERS_BUSY && get16(room(5, 1) + 0x0A) == 846);
    put16(room(5, 1) + 0x0C, 0);
    CHECK(personnel_release(s, 2) == 0 && get16(room(5, 1) + 0x0A) == 0);
    CHECK(sl(2)[3] == 0 && st(846)[4] == 0 && sl(2)[4] == 0 && st(846)[26] == 0 && sl(2)[7] == 0);
    CHECK(sl(2)[10] == 8 && sl(2)[11] == 7); /* idle, not off duty; +10/+11 kept */
}

static void test_room_release_table(void) {
    fresh(0);
    in_room(0, 844, 1, 11, 0, 8); /* dental: nurse +8 and byte +0x19 */
    room(11, 0)[0x19] = 5;
    CHECK(personnel_room_release(s, 0) == 0 && get16(room(11, 0) + 8) == 0 && room(11, 0)[0x19] == 0);
    in_room(1, 845, 1, 7, 2, 8); /* OP: nurse +8 and byte +0x17 */
    room(7, 2)[0x17] = 5;
    CHECK(personnel_room_release(s, 1) == 0 && get16(room(7, 2) + 8) == 0 && room(7, 2)[0x17] == 0);
    in_room(2, 846, 2, 7, 2, 6); /* OP: doctor +6 */
    CHECK(personnel_room_release(s, 2) == 0 && get16(room(7, 2) + 6) == 0);
    in_room(3, 847, 5, 7, 2, 0x0A); /* OP: surgeon +0xA */
    CHECK(personnel_room_release(s, 3) == 0 && get16(room(7, 2) + 0x0A) == 0);
    in_room(4, 848, 4, 13, 0, 6); /* kitchen */
    room(13, 0)[5] = 1, put32(room(13, 0) + 0x0A, 99), put16(room(13, 0) + 0x0E, 5);
    CHECK(personnel_room_release(s, 4) == 0 && get16(room(13, 0) + 6) == 0 && room(13, 0)[5] == 0 &&
          get32(room(13, 0) + 0x0A) == 0 && get16(room(13, 0) + 0x0E) == 0);
    in_room(5, 849, 6, 10, 0, 8); /* garage: +6 if it holds the person, else +8 */
    put16(room(10, 0) + 6, 850);
    CHECK(personnel_room_release(s, 5) == 0 && get16(room(10, 0) + 8) == 0 && get16(room(10, 0) + 6) == 850);
    room(10, 0)[0x0C] = 1;
    in_room(6, 850, 6, 10, 0, 6);
    CHECK(personnel_room_release(s, 6) == 1 && get16(room(10, 0) + 6) == 850);
    room(10, 0)[0x0C] = 0;
    CHECK(personnel_room_release(s, 6) == 0 && get16(room(10, 0) + 6) == 0);
    in_room(7, 851, 2, 12, 0, 2); /* golf room: only group 1 can be released */
    CHECK(personnel_room_release(s, 7) == 1);
    in_room(8, 852, 1, 12, 0, 2);
    room(12, 0)[5] = 2;
    CHECK(personnel_room_release(s, 8) == 1);
    room(12, 0)[5] = 0;
    CHECK(personnel_room_release(s, 8) == 0 && get16(room(12, 0) + 2) == 0);
    in_room(9, 853, 1, 14, 0, 4); /* club: +2 if it holds the person, else +4 */
    put16(room(14, 0) + 2, 1);
    CHECK(personnel_room_release(s, 9) == 0 && get16(room(14, 0) + 4) == 0 && get16(room(14, 0) + 2) == 1);
    room(14, 0)[0x19] = 1;
    CHECK(personnel_room_release(s, 9) == 1);
    in_room(10, 854, 7, 17, 0, 0x0A); /* blood bank: pathologist +0xA, no refusal */
    put16(room(17, 0) + 0x0C, 3);
    CHECK(personnel_room_release(s, 10) == 0 && get16(room(17, 0) + 0x0A) == 0);
    in_room(11, 855, 7, 18, 0, 0x0A); /* pathology: refused with a body on the table */
    put16(room(18, 0) + 0x0C, 3);
    CHECK(personnel_room_release(s, 11) == 1);
    put16(room(18, 0) + 0x0C, 0);
    CHECK(personnel_room_release(s, 11) == 0 && get16(room(18, 0) + 0x0A) == 0);
    in_room(12, 856, 3, 4, 0, 4); /* store room */
    CHECK(personnel_room_release(s, 12) == 0 && get16(room(4, 0) + 4) == 0);
    in_room(13, 857, 1, 3, 2, 8); /* waiting room */
    room(3, 2)[0x0F] = 1;
    CHECK(personnel_room_release(s, 13) == 0 && get16(room(3, 2) + 8) == 0 && room(3, 2)[0x0F] == 0);
    employ(14, 858, 1, 0x18);
    CHECK(personnel_room_release(s, 14) == 1);
    employ(15, 859, 1, 1);
    CHECK(personnel_room_release(s, 15) == 0);
}

/* ---- courses ---------------------------------------------------------------------------------------- */

static void plant_course(int e, int group, int start, int left, int id, int price) {
    uint8_t *c = course(e);
    c[0] = (uint8_t)group, c[1] = (uint8_t)start, c[2] = c[3] = (uint8_t)left, c[4] = (uint8_t)id;
    g_image[0x40D9 + 22 * id + 9] = (uint8_t)price;
}

static void test_courses(void) {
    fresh(973);
    employ(0, 844, 1, 0);
    plant_course(1, 2, 3, 5, 6, 30); /* doctors' course */
    plant_course(4, 1, 3, 5, 2, 40); /* the oracle's planted course */
    plant_course(6, 0, 0, 5, 0, 10); /* running already */
    CHECK(personnel_course_find(s, 0, 20, 1) == 4 && personnel_course_find(s, 0, 20, 2) == 1);
    CHECK(personnel_course_find(s, 5, 29, 1) == 4); /* wraps */
    CHECK(personnel_course_in_use(s, 0, 20) == 1 && personnel_course_in_use(s, 2, 29) == 4 &&
          personnel_course_in_use(s, 5, 29) == 6);
    CHECK(personnel_course_enrol(s, 0, 1) == PERS_DEADLINE); /* another group */
    CHECK(personnel_course_enrol(s, 0, 6) == PERS_DEADLINE); /* started */
    world_set_cash(&g_world, 0, 40);
    CHECK(personnel_course_enrol(s, 0, 4) == PERS_NO_FEE && cash() == 40); /* price not below the cash */
    world_set_cash(&g_world, 0, 973);
    sl(0)[7] = sl(0)[10] = sl(0)[11] = 3;
    CHECK(personnel_course_enrol(s, 0, 4) == 0);
    CHECK(cash() == 933 && get32(h() + 0x11C) == 40 && get32(h() + 0x13E) == 40 && get32(h() + 0xA6) == 0);
    CHECK(sl(0)[3] == 0x19 && sl(0)[4] == 4 && st(844)[4] == 0x19 && st(844)[26] == 4);
    CHECK(sl(0)[7] == 0 && sl(0)[10] == 0 && sl(0)[11] == 0);
    CHECK(personnel_course_enrol(s, 0, 4) == PERS_NOBODY_THERE); /* not idle any more */
    plant_course(8, 0, 2, 4, 11, 5); /* any group */
    employ(1, 845, 3, 0);
    CHECK(personnel_course_find(s, 0, 20, 3) == 8 && personnel_course_enrol(s, 1, 8) == 0 && cash() == 928);
}

/* ---- room staff ------------------------------------------------------------------------------------- */

static void test_roster(void) {
    fresh(973);
    g_world.block[BLK_ROOM_COUNTS][2] = 1;
    const RosterStub *rec = personnel_roster_stub(0x1D2AB);
    CHECK(rec && rec->cat == 2 && rec->group == 1 && rec->field == 8 && rec->back == 0x33D58);
    int n = 0;
    for (int i = 0; i < 26; i++) n += personnel_roster_stub(PERSONNEL_ROSTER[i].stub) == &PERSONNEL_ROSTER[i];
    CHECK(n == 26 && !personnel_roster_stub(0x1D2E6));
    /* nobody idle and the desk empty: the stub's box */
    int occ = 5;
    CHECK(personnel_roster_enter(s, rec, &occ) == -1 && occ == -1);
    /* the oracle: the hired nurse in slot 0 */
    employ(0, 844, 1, 0);
    employ(1, 845, 1, 0);
    st(845)[21] = 9; /* mood below 10: not offered first, but browsable */
    employ(2, 846, 2, 0);
    put16(pb() + PB_ROOM_SHOWN, 7);
    CHECK(personnel_roster_enter(s, rec, &occ) == 0 && occ == -1);
    CHECK(pb16(PB_INDEX) == 0 && pb16(PB_ROSTER_GROUP) == 1 && pb16(PB_ROSTER_CAT) == 2 && pb16(PB_ROSTER_ROOM) == 0);
    CHECK(pb16(PB_ROOM_SHOWN) == 0 && (uint32_t)get32(pb() + 6) == 0x33D58);
    personnel_roster_step(s, 1, occ);
    CHECK(pb16(PB_INDEX) == 1); /* no mood test while browsing */
    personnel_roster_step(s, 1, occ);
    CHECK(pb16(PB_INDEX) == 0); /* the doctor in slot 2 is skipped, wraps */
    personnel_roster_step(s, -1, occ);
    CHECK(pb16(PB_INDEX) == 1);
    personnel_roster_step(s, 1, occ);
    int d;
    CHECK(personnel_assign(s, occ, &d) == ROSTER_ASSIGNED && d == 0);
    CHECK(get16(room(2, 0) + 8) == 844 && sl(0)[3] == 2 && st(844)[4] == 2 && sl(0)[4] == 0 && st(844)[26] == 0);
    CHECK(pb16(PB_ROOM_SHOWN) == -1);
    /* again: the occupant is shown first; assigning the occupant changes nothing */
    CHECK(personnel_roster_enter(s, rec, &occ) == 0 && occ == 0 && pb16(PB_INDEX) == 0);
    CHECK(personnel_assign(s, occ, &d) == ROSTER_BACK && get16(room(2, 0) + 8) == 844);
    sl(0)[3] = st(844)[4] = 0; /* even an occupant that reads as idle is not re-assigned */
    CHECK(personnel_assign(s, occ, &d) == ROSTER_BACK && sl(0)[3] == 0 && d == 0);
    sl(0)[3] = st(844)[4] = 2;
    /* a replacement: the old one becomes idle at once */
    personnel_roster_step(s, 1, occ);
    CHECK(pb16(PB_INDEX) == 1);
    sl(0)[7] = 6;
    CHECK(personnel_assign(s, occ, &d) == ROSTER_ASSIGNED && d == 844 && get16(room(2, 0) + 8) == 845);
    CHECK(sl(0)[3] == 0 && sl(0)[4] == 0 && sl(0)[7] == 0 && st(844)[4] == 0 && st(844)[26] == 0);
    CHECK(sl(1)[3] == 2 && st(845)[4] == 2);
    /* a candidate who is no longer idle is not assigned */
    CHECK(personnel_roster_enter(s, rec, &occ) == 0 && occ == 1);
    put16(pb() + PB_INDEX, 0);
    st(844)[4] = 0x13;
    CHECK(personnel_assign(s, occ, &d) == ROSTER_BACK && get16(room(2, 0) + 8) == 845);
    st(844)[4] = 0;
    put16(pb() + PB_INDEX, -1);
    CHECK(personnel_assign(s, occ, &d) == ROSTER_NOBODY);
    /* release: refused while a patient waits, then idle */
    put16(room(2, 0) + 0x0A, 3);
    CHECK(personnel_roster_release(s, occ) == PERS_BUSY && get16(room(2, 0) + 8) == 845);
    put16(room(2, 0) + 0x0A, 0);
    CHECK(personnel_roster_release(s, occ) == 0 && get16(room(2, 0) + 8) == 0 && sl(1)[3] == 0 && st(845)[4] == 0);
    CHECK(personnel_roster_release(s, occ) == PERS_NOT_ASSIGNED);
    sl(1)[3] = 2, st(845)[4] = 1; /* staff byte 4 below 2 */
    CHECK(personnel_roster_release(s, occ) == PERS_NOT_ASSIGNED);
    sl(1)[3] = 0, st(845)[4] = 2; /* slot +3 = 0 */
    CHECK(personnel_roster_release(s, occ) == PERS_NOT_ASSIGNED);
    CHECK(personnel_roster_release(s, -1) == ROSTER_BACK);
    st(845)[4] = 0x13, sl(1)[3] = 0x13;
    CHECK(personnel_roster_release(s, occ) == ROSTER_BACK);
}

static void test_roster_fields(void) {
    fresh(973);
    g_world.block[BLK_ROOM_COUNTS][5] = 3;
    employ(0, 844, 2, 0);
    put16(pb() + PB_ROOM_SHOWN, 2); /* the treatment room screen shows instance 2: kept */
    const RosterStub *doc = personnel_roster_stub(0x1D14F);
    int occ, d;
    CHECK(personnel_roster_enter(s, doc, &occ) == 0 && pb16(PB_ROSTER_ROOM) == 2);
    CHECK(personnel_assign(s, occ, &d) == ROSTER_ASSIGNED && get16(room(5, 2) + 0x0A) == 844);
    CHECK(sl(0)[4] == 2 && st(844)[26] == 2 && pb16(PB_ROOM_SHOWN) == 2); /* a room number in the message */
    /* the OP stubs set pb+0x26 = their field */
    employ(1, 845, 5, 0);
    const RosterStub *op_surgeon = personnel_roster_stub(0x1CFE1);
    put16(pb() + PB_ROOM_SHOWN, 1);
    CHECK(personnel_roster_enter(s, op_surgeon, &occ) == 0 && pb16(PB_ROSTER_FIELD) == 0x0A);
    CHECK(personnel_assign(s, occ, &d) == ROSTER_ASSIGNED && get16(room(7, 1) + 0x0A) == 845 && st(845)[4] == 7);
    /* the kitchen: single room, +6, no number */
    employ(2, 846, 4, 0);
    put16(pb() + PB_ROOM_SHOWN, 4);
    CHECK(personnel_roster_enter(s, personnel_roster_stub(0x1CD81), &occ) == 0 && pb16(PB_ROSTER_ROOM) == 0);
    CHECK(personnel_assign(s, occ, &d) == ROSTER_ASSIGNED && get16(room(13, 0) + 6) == 846 && pb16(PB_ROOM_SHOWN) == -1);
}

int main(void) {
    test_ad_form();
    test_publish();
    test_ad_expiry();
    test_shuffle();
    test_applicants();
    test_hire();
    test_tear_up();
    test_search();
    test_wages_and_dismissal();
    test_independence();
    test_send_home();
    test_release();
    test_room_release_table();
    test_courses();
    test_roster();
    test_roster_fields();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
