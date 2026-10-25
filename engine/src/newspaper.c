/* The newspaper's text builder (docs/ui-reports.md, section 3, "The text builder"). Each type's handler follows the
 * spec's table. Engine choices, all marked "chain" below: where the table abbreviates a chain of consecutive texts
 * ("T444-447 with {der} {prof} {P} [des]H8", "T1237 ...") the placeholders are put between the chain's texts in the
 * order the texts' own grammar asks for (the texts are read at run time, so the choice was made by reading them);
 * a space is inserted between a pronoun, a profession, a job name and a following name or article where neither
 * text carries one. A placeholder of the table without a case ({F u24}) takes the dative, as the measured page of
 * day 25 shows ("auf einem Holzbett", "mit Hamburgern"). */
#include "newspaper.h"

#include <string.h>

#include "fixes.h"
#include "sim_internal.h"

#define NEWS_SIZE 70
#define NAMES 0x3BCF
#define NAME_SIZE 30
#define PERSON_SIZE 106
#define STAFF_SIZE 32
#define T_ISSUE 0x16

/* article texts by the gender class of the hospital's name */
enum { A_IM = 2654, A_INS = 2657, A_AM = 2660, A_DES = 2663, A_VOM = 2666, A_DAS = 2669 };
/* pronoun texts by the person's sex */
enum { PR_IHN = 2638, PR_DEN = 2640, PR_DER_CAP = 2642, PR_DER = 2644, PR_ER = 2646, PR_SEIN = 2648, PR_SEINER = 2650,
       PR_SEINE = 2652 };

typedef struct {
    Sim *s;
    Pieces *t;
    const uint8_t *it;
    int item;    /* 0-99 */
    int *number; /* the item number [0x45CF] (1-based), which type 0x27 overwrites */
} C;

static int b(const C *c, int k) { return c->it[k]; }
static int u(const C *c, int k) { return (uint16_t)get16(c->it + k); }
static void txt(C *c, int n) { pc_text(c->t, n); }
static void num(C *c, long v) { pc_num(c->t, v); }
static void sp(C *c) { pc_raw(c->t, " "); }
static int gender(const C *c, int h) {
    if (h < 0 || h > 16) return 0;
    int g = program_u8(c->s->prog, NAMES, (uint32_t)(h * NAME_SIZE + 29));
    return g > 2 ? 2 : g;
}
static void hosp(C *c, int h) {
    if (h >= 0 && h <= 16) pc_hospital(c->t, h);
}
static int english(const C *c) { return program_english(c->s->prog); }
/* the seven name helpers (0x3B4BD-0x3B5D0): article by the name's gender, then the name; the English CD first clamps
 * the index, a negative byte to 1 and 16 or more to 16 (docs/editions.md 7.3, rule E) */
static void art(C *c, int base, int h) {
    if (english(c)) {
        int sb = (int8_t)(uint8_t)h;
        h = sb < 0 ? 1 : sb >= 16 ? 16 : sb;
    }
    txt(c, base + gender(c, h)), hosp(c, h);
}
static const uint8_t *person(const C *c, int i) { return c->s->world->block[BLK_PATIENTS] + (size_t)(i % 1009) * PERSON_SIZE; }
static const uint8_t *staff(const C *c, int i) { return c->s->world->block[BLK_STAFF] + (size_t)(i % 1041) * STAFF_SIZE; }
static void name(C *c, int i) { pc_person(c->t, i); }
static void prof(C *c, int i) { pc_profession(c->t, i); }
static void pron(C *c, int base, int i) { txt(c, base + (person(c, i)[0] ? 1 : 0)); }
/* a pronoun the English CD leaves out (docs/editions.md 7.3, rule C) */
static void pron_de(C *c, int base, int i) {
    if (!english(c)) pron(c, base, i);
}
static void sname(C *c, int i) { pc_staff(c->t, i); }
static void group_dat(C *c, int i) { txt(c, 1292 + staff(c, i)[1]); }
static void group_nom(C *c, int i) { txt(c, 1302 + staff(c, i)[1]); }
static int staff_hospital(const C *c, int i) { return (int8_t)staff(c, i)[3]; }
static void bwh(C *c, int i) { num(c, staff(c, i)[5]), num(c, staff(c, i)[6]), num(c, staff(c, i)[7]); }
/* " (" {bwh} ")" (0x43527): the ")" goes over the third number's trailing space, "(145 64 102)"; the next text
 * brings its own space (docs/ui-reports.md, "{bwh} in parentheses") */
static void bwh_paren(C *c, int i) { pc_raw(c->t, " ("), bwh(c, i), pc_over(c->t, ")"); }
/* {F}/{E}, the item-name helpers 0x43430-0x4350C (docs/editions.md 7.3.1): article by the class byte +7 (0-2
 * singular m/f/n, 3 and more plural), then the noun with the plural "n" step for class 3 and more (step 0: the entry
 * 0x4350C, which has none). German: text 1607 (dative) / 1611 (accusative) + class; the English CD instead prints
 * nothing for class 3 and "a" + "n " or text 386 by the name's first byte for the others, both cases alike (rule A). */
static void thing(C *c, int id, int equipment, int accusative, int step) {
    const uint8_t *rec = equipment ? sim_equipment_item(c->s, id) : sim_furniture_item(c->s, id);
    int cls = rec[7], noun = (equipment ? 3500 : 3700) + id;
    if (!english(c)) txt(c, (accusative ? 1611 : 1607) + (cls > 3 ? 3 : cls)); /* engine choice: classes above 3 take the plural's */
    else if (cls < 3) pc_article_en(c->t, noun);
    int plural = step && cls >= 3;
    /* FIX-10 (docs/fixes.md): original runs the German plural "n" step on English names too ("Bean Soupn"); fixed
     * prints the English name unchanged */
    if (fix_on(10) && english(c)) plural = 0;
    pc_noun(c->t, noun, plural);
}
static void F(C *c, int id) { thing(c, id, 0, 0, 1); }
static void Facc(C *c, int id) { thing(c, id, 0, 1, 1); }
static void Facc_bare(C *c, int id) { thing(c, id, 0, 1, 0); } /* 0x4350C: no plural step */
static void E(C *c, int id) { thing(c, id, 1, 0, 1); }
/* the bed-comfort item 0x1D's equipment: only the name (record dword +0), no article, no plural step */
static void E_name(C *c, int id) { pc_noun(c->t, 3500 + id, 0); }
/* {der} {prof} {P}: pronoun, profession and name of person p */
static void der_prof_name(C *c, int base, int p) { pron(c, base, p), sp(c), prof(c, p), sp(c), name(c, p); }
static void prof_name(C *c, int p) { prof(c, p), sp(c), name(c, p); }
/* {name} of the death items: the copy in the item's bytes 29 on (0x4C8D6), up to '$' or 0; an empty copy prints
 * nothing (docs/ui-reports.md, type 7) */
static void item_name(C *c) { pc_item_name(c->t, c->item); }

/* FIX-8 (docs/fixes.md): the byte before the next text (the name's last, or text `before`'s last for an empty name) and
 * the next text's first byte are both not a space. Without the texts (NULL) a text counts as ending in a space and as
 * starting without one. */
static int text_edge(const C *c, int n, int last, int unknown) {
    size_t len = 0;
    const uint8_t *t = c->s->texts ? textdb_text(c->s->texts, n, &len) : NULL;
    return t && len ? t[last ? len - 1 : 0] : unknown;
}
static int name_needs_space(const C *c, int before, int next) {
    size_t n = 0;
    while (29 + n < NEWS_SIZE && c->it[29 + n] && c->it[29 + n] != '$') n++;
    int end = n ? c->it[29 + n - 1] : text_edge(c, before, 1, ' ');
    return end != ' ' && text_edge(c, next, 0, 'x') != ' ';
}

/* the "room tested" items: headline 0 H8 Ta; 1 Tb [des]H8 Tc */
static int tested_head(C *c, int a, int bb, int cc) {
    if (b(c, 21) == 0) hosp(c, b(c, 8)), txt(c, a);
    else if (b(c, 21) == 1) txt(c, bb), art(c, A_DES, b(c, 8)), txt(c, cc);
    else return 0;
    return 1;
}
/* headline 0 H8 Ta; 1 Tb [im]H8 */
static int tested_head_im(C *c, int a, int bb) {
    if (b(c, 21) == 0) hosp(c, b(c, 8)), txt(c, a);
    else if (b(c, 21) == 1) txt(c, bb), art(c, A_IM, b(c, 8));
    else return 0;
    return 1;
}

static int type3(C *c, int head) {
    int h = b(c, 8);
    const uint8_t *r = hospital(c->s, h < 16 ? h : 0);
    if (h <= 16) sim_rating_record(c->s, hospital(c->s, h));
    switch (b(c, 21)) {
    case 0: txt(c, 213), hosp(c, h), txt(c, 214); return 1;
    case 1: txt(c, 215), hosp(c, h), txt(c, 216); return 1;
    case 2:
        if (head) return hosp(c, h), txt(c, 217), 1;
        txt(c, 218), num(c, (uint16_t)get16(r + 0x2A)), txt(c, 219);
        int d = (uint16_t)get16(r + 0x38);
        if (d == 0) txt(c, 222);
        else if (d == 1) txt(c, 221);
        else num(c, d), txt(c, 220);
        return 1;
    case 3:
        if (head) hosp(c, h), txt(c, 223);
        else txt(c, 224), hosp(c, h), txt(c, 225);
        return 1;
    case 4: txt(c, 212), art(c, A_DES, h); return 1;
    case 5: hosp(c, h), txt(c, 671), num(c, u(c, 22)), txt(c, 672); return 1;
    case 6: hosp(c, h), txt(c, 673), num(c, u(c, 22)), txt(c, 674); return 1;
    default: return 0;
    }
}

static int type4(C *c, int head) { /* failed home treatment */
    int h = b(c, 8), p = u(c, 16), alt = b(c, 22) != 0;
    switch (b(c, 21)) {
    case 0:
        if (head) alt ? (txt(c, 519), hosp(c, h), txt(c, 520)) : (txt(c, 518), art(c, A_IM, h));
        else txt(c, 521), art(c, A_DES, h), txt(c, 522), name(c, p), txt(c, 523), txt(c, 3411 + b(c, 26)), txt(c, 524);
        return 1;
    case 1:
        if (head) alt ? (txt(c, 560), art(c, A_DAS, h)) : (txt(c, 558), art(c, A_DES, h), txt(c, 559));
        else txt(c, 561), name(c, p), txt(c, 562);
        return 1;
    case 2:
        if (head) {
            alt ? (txt(c, 440), art(c, A_IM, h)) : (txt(c, 438), art(c, A_DES, h), txt(c, 439));
            return 1;
        }
        if (b(c, 23) == 0) {
            txt(c, 441), name(c, p), txt(c, 442), art(c, A_DES, h), txt(c, 443);
        } else if (b(c, 23) <= 3) { /* chain: T444-447 / T448-451 / T452-455 */
            int t0 = 444 + 4 * (b(c, 23) - 1);
            txt(c, t0), pron(c, PR_DER, p), txt(c, t0 + 1), prof_name(c, p), txt(c, t0 + 2), art(c, A_DES, h), txt(c, t0 + 3);
        } else {
            return 0;
        }
        return 1;
    case 3:
        if (head) alt ? (txt(c, 565), art(c, A_IM, h)) : (txt(c, 563), art(c, A_IM, h), txt(c, 564));
        else name(c, p), txt(c, 566), art(c, A_INS, h), txt(c, 567), name(c, p), txt(c, 568);
        return 1;
    case 4:
    case 5:
        if (head) alt ? (txt(c, 526), art(c, A_DES, h)) : (txt(c, 525), art(c, A_IM, h));
        else txt(c, 527), art(c, A_DAS, h), txt(c, 528), name(c, p), txt(c, 523), txt(c, 529);
        return 1;
    case 6:
        if (head) alt ? (txt(c, 4110), art(c, A_IM, h)) : (txt(c, 4109), art(c, A_DES, h));
        else txt(c, 4111), art(c, A_DES, h), txt(c, 4112), name(c, p), txt(c, 4113), pron(c, PR_DER, p), txt(c, 4114),
            prof(c, p), txt(c, 4115);
        return 1;
    default: return 0;
    }
}

static int type5(C *c, int head) { /* bills */
    int h = b(c, 8), p = u(c, 16), bill = u(c, 22), alt = b(c, 24) != 0;
    switch (b(c, 21)) {
    case 0:
        if (head) alt ? (txt(c, 677), art(c, A_IM, h)) : (txt(c, 675), hosp(c, h), txt(c, 676));
        else name(c, p), txt(c, 678), num(c, bill), txt(c, 679);
        return 1;
    case 1:
        if (head) alt ? (hosp(c, h), txt(c, 682)) : (txt(c, 680), hosp(c, h), txt(c, 681));
        else txt(c, 683), name(c, p), txt(c, 684), num(c, bill), txt(c, 685), name(c, p), txt(c, 686);
        return 1;
    case 2:
        if (head) txt(c, alt ? 570 : 569);
        else txt(c, 571), art(c, A_DAS, h), txt(c, 572), name(c, p), txt(c, 573), num(c, bill), txt(c, 574),
            pron(c, PR_DER_CAP, p), sp(c), prof(c, p), txt(c, 575), art(c, A_DAS, h), txt(c, 576);
        return 1;
    case 3:
        if (head) txt(c, alt ? 647 : 646);
        else /* chain: T648-654 */
            txt(c, 648), art(c, A_DAS, h), txt(c, 649), name(c, p), txt(c, 650), pron(c, PR_ER, p), txt(c, 651),
                num(c, bill), txt(c, 652), pron(c, PR_DER_CAP, p), sp(c), prof(c, p), txt(c, 653), art(c, A_DAS, h), txt(c, 654);
        return 1;
    case 4:
        if (head) txt(c, alt ? 656 : 655);
        else /* T657-662 with {der} " " {prof} after T660 (0x41F14) */
            txt(c, 657), art(c, A_DAS, h), txt(c, 658), name(c, p), txt(c, 659), num(c, bill), txt(c, 660),
                pron(c, PR_DER, p), sp(c), prof(c, p), txt(c, 661), art(c, A_DAS, h), txt(c, 662);
        return 1;
    case 5:
        if (head) txt(c, alt ? 664 : 663);
        else txt(c, 665), art(c, A_VOM, h), txt(c, 666), num(c, bill), txt(c, 667), name(c, p), txt(c, 668),
            pron(c, PR_DER, p), txt(c, 669), art(c, A_DAS, h), txt(c, 670);
        return 1;
    default: return 0;
    }
}

static int type6(C *c, int head) { /* thug outcomes */
    int h = b(c, 8), v = u(c, 10);
    if (b(c, 21) == 0) { /* a person (u10); the bodies as the table row, {P} the bare name (0x414D8-0x417AC) */
        switch (b(c, 22)) {
        case 0:
            if (head) prof(c, v), txt(c, 737), art(c, A_DES, h), txt(c, 738);
            else
                txt(c, 739), name(c, v), txt(c, 740), art(c, A_DES, h), txt(c, 741), pron(c, PR_DER, v), txt(c, 742),
                    prof(c, v), txt(c, 743);
            return 1;
        case 1:
            if (head) prof(c, v), txt(c, 809);
            else
                txt(c, 810), name(c, v), txt(c, 811), art(c, A_DES, h), txt(c, 812), pron(c, PR_DER, v), txt(c, 813),
                    prof(c, v), txt(c, 814);
            return 1;
        case 2:
            if (head) prof(c, v), txt(c, 815);
            else txt(c, 816), name(c, v), txt(c, 817), art(c, A_DES, h), txt(c, 818), pron(c, PR_IHN, v), txt(c, 819);
            return 1;
        case 3:
            if (head) prof(c, v), txt(c, 820), hosp(c, h);
            else txt(c, 821), name(c, v), txt(c, 822);
            return 1;
        case 4:
            if (head) {
                txt(c, 823);
                if (!person(c, v)[0]) txt(c, 824);
                sp(c), prof(c, v), txt(c, 825);
            } else {
                txt(c, 826), art(c, A_DES, h), txt(c, 827), name(c, v), txt(c, 828);
            }
            return 1;
        case 5:
            if (head) prof(c, v), txt(c, 829);
            else txt(c, 830), name(c, v), txt(c, 831), pron(c, PR_ER, v), txt(c, 832), art(c, A_AM, h), txt(c, 833);
            return 1;
        default: return 0;
        }
    }
    if (b(c, 21) != 1) return 0;
    /* an employee E (u10), the beaten victim (docs/ui-reports.md, "Type 6, b21 = 1 in detail"): the hospital is item
     * byte 8, not E's byte 3; no employee name in any headline; T386 is a single space, so after {group dat} (which ends
     * in a space) the raw headline has two; the pronouns follow E's sex, staff byte 0 */
    int sex = staff(c, v)[0];
    switch (b(c, 22)) {
    case 0:
        if (head) txt(c, 687), group_dat(c, v), txt(c, 688);
        else
            txt(c, 689), sname(c, v), txt(c, 690), group_nom(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 691),
                txt(c, PR_SEINE + sex), txt(c, 692);
        return 1;
    case 1:
        if (head) txt(c, 693), group_dat(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 694);
        else
            txt(c, 695), sname(c, v), txt(c, 696), group_nom(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 697),
                txt(c, PR_SEIN + sex), txt(c, 698), sname(c, v), txt(c, 699);
        return 1;
    case 2:
        if (head) txt(c, 700), group_dat(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 701);
        else
            txt(c, 702), sname(c, v), txt(c, 703), group_nom(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 704),
                txt(c, PR_ER + sex), txt(c, 705), txt(c, PR_SEINE + sex), txt(c, 706);
        return 1;
    case 3:
        if (head) txt(c, 707), group_nom(c, v), txt(c, 386), art(c, A_DES, h);
        else
            txt(c, 708), sname(c, v), txt(c, 709), group_nom(c, v), txt(c, 386), art(c, A_DES, h), txt(c, 710),
                txt(c, PR_SEIN + sex), txt(c, 711), sname(c, v), txt(c, 712);
        return 1;
    default: return 0;
    }
}

static int type7(C *c, int head) { /* death at home */
    int h = b(c, 8), p = u(c, 16);
    if (head) {
        switch (b(c, 21)) {
        case 0: prof(c, p), txt(c, 844); return 1;
        case 1: prof(c, p), txt(c, 845), pron(c, PR_SEINER, p), txt(c, 846); return 1;
        case 2: txt(c, 847); return 1;
        case 3: txt(c, 848); return 1;
        default: return 0;
        }
    }
    if (b(c, 21) > 3) return 0;
    if (b(c, 22) == 0) {
        txt(c, 849), item_name(c), txt(c, 850), pron(c, PR_DER, p), sp(c), prof(c, p), txt(c, 851), item_name(c), txt(c, 852),
            art(c, A_IM, h), txt(c, 853);
    } else if (b(c, 22) == 1) { /* T854-858, {der} " " {prof} after T855 (0x40EA6) */
        txt(c, 854), item_name(c), txt(c, 855), pron(c, PR_DER, p), sp(c), prof(c, p), txt(c, 856), art(c, A_DES, h),
            txt(c, 857), pron(c, PR_ER, p), txt(c, 858);
        /* the German builds stop after 858, whose missing terminator runs on into 859, so the sentence is complete
         * (docs/fixes.md, FIX-9 withdrawn); the English CD goes on with the hospital name and 859 (0x40EA5,
         * docs/editions.md 7.3, rule D) */
        if (english(c)) art(c, A_DES, h), txt(c, 859);
    } else {
        return 0;
    }
    return 1;
}

static int type8(C *c, int head) { /* death in hospital; b22 = the person's state */
    int h = b(c, 8), p = u(c, 16), st = b(c, 22);
    if (head) {
        if (b(c, 21) > 3) return 0;
        txt(c, 860 + b(c, 21)), txt(c, 918 + st);
        if (b(c, 21) == 2) sp(c), art(c, A_DES, h);
        return 1;
    }
    if (b(c, 21) > 3) return 0;
    if (st == 2 || st == 3) { /* chain: T879-883 */
        txt(c, 879), pron(c, PR_DER, p), txt(c, 880), prof(c, p), sp(c), item_name(c), txt(c, 881), item_name(c), sp(c),
            txt(c, 882), pron(c, PR_IHN, p), txt(c, 883);
    } else if (st == 6 || st == 16) { /* chain: T864-867 */
        txt(c, 864), item_name(c), sp(c), art(c, A_IM, h), txt(c, 865), pron(c, PR_DER, p), txt(c, 866), prof(c, p), sp(c),
            item_name(c), txt(c, 867);
    } else if (st == 7) {
        txt(c, 868), art(c, A_DES, h), txt(c, 869), pron(c, PR_DER, p), sp(c), prof(c, p), sp(c), item_name(c),
            txt(c, 870 + (b(c, 23) & 3)), txt(c, 874);
    } else if (st == 9) {
        txt(c, 875), art(c, A_DES, h), txt(c, 876), item_name(c), txt(c, 877), prof(c, p), txt(c, 878);
    } else {
        /* 0x411C8: the English CD drops the "er/sie" after 1070 (engine reading, "he leaving behind"; rule C); 1070
         * follows the second name with no space (docs/ui-reports.md, type 8; SIM-18) */
        txt(c, 1067), art(c, A_IM, h), txt(c, 1068), item_name(c), txt(c, 1069), item_name(c);
        /* FIX-8 (docs/fixes.md): original prints T1070 right after the name ("Xyzwhatte"); fixed puts a space between
         * when neither the name's last byte (T1069's for an empty name) nor T1070's first byte is one */
        if (fix_on(8) && name_needs_space(c, 1069, 1070)) sp(c);
        txt(c, 1070), pron_de(c, PR_ER, p), txt(c, 1071);
    }
    return 1;
}

static int type11(C *c, int head) { /* 0x0B: a thug caught */
    int h = b(c, 8), v = u(c, 10), thug = u(c, 12);
    if (b(c, 21) == 0) { /* a person mission (u10 the person) */
        if (head) {
            switch (b(c, 23)) {
            case 0: txt(c, 726), hosp(c, h), txt(c, 727); return 1;
            case 1: txt(c, 728), hosp(c, h), txt(c, 729); return 1;
            case 2: hosp(c, h), txt(c, 730); return 1;
            case 3: txt(c, 731), art(c, A_DES, h), txt(c, 732); return 1;
            default: return 0;
            }
        }
        txt(c, 733), sname(c, thug), txt(c, 734), der_prof_name(c, PR_DEN, v), txt(c, 735), art(c, A_DES, h), txt(c, 736);
        return 1;
    }
    if (b(c, 21) != 1) return 0;
    int h9 = staff_hospital(c, v); /* a staff mission (u10 the employee) */
    if (head) {
        switch (b(c, 23)) {
        case 0: txt(c, 713), art(c, A_DES, h), txt(c, 714); return 1;
        case 1: txt(c, 715), hosp(c, h), txt(c, 716); return 1;
        case 2: hosp(c, h), txt(c, 717), sname(c, v), txt(c, 718); return 1;
        case 3: hosp(c, h), txt(c, 720); return 1; /* T719 is never printed (docs/ui-reports.md) */
        default: return 0;
        }
    }
    txt(c, 721), sname(c, thug), txt(c, 722), sname(c, v), txt(c, 723), group_nom(c, v), sp(c), art(c, A_DES, h9), txt(c, 724),
        art(c, A_DES, h), txt(c, 725);
    return 1;
}

static int type14(C *c, int head) { /* 0x0E: a new department; b21 = the room category, b22 = the instance */
    int h = b(c, 8), k = b(c, 21), n = b(c, 22), v = b(c, 23);
    if (v > 1) return 0;
    /* the instance is printed + 1 (0x3FA7A-0x3FC38) */
    if (head) {
        if (n == 0 && v == 0) hosp(c, h), txt(c, 456), txt(c, 350 + k), txt(c, 457);
        else if (n == 0) txt(c, 350 + k), txt(c, 459), art(c, A_IM, h);
        else hosp(c, h), txt(c, 456), txt(c, 350 + k), txt(c, 460), num(c, n + 1), txt(c, 458);
        return 1;
    }
    if (n == 0 && v == 0) txt(c, 461), art(c, A_IM, h), txt(c, 462), txt(c, 467 + k), txt(c, 463);
    else if (n == 0) txt(c, 486), txt(c, 467 + k), txt(c, 386), art(c, A_IM, h), txt(c, 487); /* T386 before [im]H8 */
    else if (v == 0) txt(c, 464), art(c, A_DES, h), txt(c, 465), txt(c, 350 + k), txt(c, 460), num(c, n + 1), txt(c, 466),
        art(c, A_DES, h), txt(c, 467);
    else /* chain: T488-490 */
        txt(c, 488), art(c, A_DES, h), txt(c, 489), txt(c, 350 + k), txt(c, 460), num(c, n + 1), txt(c, 490);
    return 1;
}

static int type18(C *c) { /* 0x12: the best employee of a job group */
    int h = b(c, 8), e = u(c, 10);
    switch (b(c, 21)) {
    case 1: sname(c, e), sp(c), art(c, A_VOM, h), txt(c, 1151); return 1;
    case 2: sname(c, e), txt(c, 1152), art(c, A_DES, h), txt(c, 1153); return 1;
    case 3: sname(c, e), txt(c, 386), art(c, A_VOM, h), txt(c, 1154); return 1; /* 3, 6, 7: 0x408CA */
    case 4: txt(c, 1155), sname(c, e), sp(c), art(c, A_VOM, h), txt(c, 1156); return 1;
    case 5: txt(c, 1157), sname(c, e), sp(c), art(c, A_VOM, h), txt(c, 1156); return 1; /* chain */
    case 6: sname(c, e), txt(c, 386), art(c, A_VOM, h), txt(c, 1158); return 1;
    case 7: sname(c, e), txt(c, 386), art(c, A_VOM, h), txt(c, 1159); return 1;
    case 8: txt(c, 1160), sname(c, e), sp(c), art(c, A_VOM, h); return 1;
    case 9: txt(c, 1161), sname(c, e), sp(c), art(c, A_VOM, h), txt(c, 1162); return 1; /* chain */
    case 10: txt(c, 1163), sname(c, e), sp(c), art(c, A_VOM, h), txt(c, 1164); return 1; /* chain */
    default: return 0;
    }
}

static int type21(C *c, int head) { /* 0x15: an advertising campaign */
    int h = b(c, 8), k = b(c, 22), v = get16(c->it + 10);
    if (k < 1 || k > 3) return 0;
    if (head) {
        if (b(c, 21) == 0) {
            static const int KIND[4] = {0, 1181, 1183, 1182};
            hosp(c, h), txt(c, 1180), txt(c, KIND[k]), txt(c, v <= -2 ? 1184 : v >= 2 ? 1185 : 1186);
        } else if (b(c, 21) == 1) {
            static const int FAIL[4] = {0, 1205, 1207, 1206};
            hosp(c, h), txt(c, FAIL[k]);
        } else {
            return 0;
        }
        return 1;
    }
    if (b(c, 21) > 1) return 0;
    static const int START[4] = {0, 1187, 1199, 1202};
    txt(c, START[k]), hosp(c, h), txt(c, 2404 + b(c, 23)), txt(c, 1188);
    if (v <= -2) txt(c, 1189), hosp(c, h), txt(c, 1190), num(c, -v), txt(c, 1191);
    else if (v >= 2) txt(c, 1192), art(c, A_DAS, h), txt(c, 1193), num(c, v), txt(c, 1194);
    else txt(c, 1195), art(c, A_DAS, h), txt(c, 1196);
    static const int END[2][4] = {{0, 1197, 1200, 1203}, {0, 1198, 1201, 1204}};
    txt(c, END[b(c, 21)][k]);
    return 1;
}

static int type22(C *c, int head) { /* 0x16: prices */
    int h = b(c, 8), alt = b(c, 22) == 1;
    if (head && b(c, 22) > 1) return 0; /* b22 0 or 1 picks the headline, any other value none (docs/ui-reports.md) */
    switch (b(c, 21)) {
    case 0:
        if (head) alt ? (hosp(c, h), txt(c, 1209)) : (txt(c, 1208), art(c, A_IM, h));
        else txt(c, 1210), art(c, A_DES, h), txt(c, 1211), art(c, A_VOM, h), txt(c, 1212);
        return 1;
    case 1:
        if (head) alt ? (hosp(c, h), txt(c, 1214)) : (txt(c, 1213), art(c, A_IM, h));
        else txt(c, 1215), art(c, A_VOM, h), txt(c, 1216), art(c, A_DES, h), txt(c, 1217);
        return 1;
    case 2:
        if (head) alt ? (hosp(c, h), txt(c, 1220)) : (txt(c, 1218), art(c, A_IM, h), txt(c, 1219));
        else txt(c, 1221), art(c, A_IM, h), txt(c, 1222);
        return 1;
    case 3:
        if (head) alt ? (hosp(c, h), txt(c, 1224)) : (txt(c, 1223), art(c, A_IM, h));
        else txt(c, 1225), art(c, A_DAS, h), txt(c, 1226);
        return 1;
    case 4:
        if (head) alt ? (hosp(c, h), txt(c, 1228)) : (txt(c, 1227), art(c, A_IM, h));
        else txt(c, 1229), art(c, A_DAS, h), txt(c, 1230), art(c, A_DAS, h), txt(c, 1231);
        return 1;
    default: return 0;
    }
}

static int type23(C *c, int head) { /* 0x17: gave up waiting */
    int h = b(c, 8), p = u(c, 16);
    if (head) {
        switch (b(c, 22)) {
        case 0: prof(c, p), txt(c, 1232), hosp(c, h); return 1;
        case 1: hosp(c, h), txt(c, 1233), prof(c, p), txt(c, 1234); return 1;
        case 2: prof(c, p), txt(c, 1235), hosp(c, h); return 1;
        case 3: prof(c, p), txt(c, 1236), hosp(c, h); return 1;
        default: return 0;
        }
    }
    switch (b(c, 21)) {
    case 0:
        switch (b(c, 23)) { /* chains */
        case 0:
            txt(c, 1237), pron(c, PR_DER, p), txt(c, 1238), prof_name(c, p), sp(c), art(c, A_DAS, h), txt(c, 1239),
                pron(c, PR_IHN, p), txt(c, 1240), pron(c, PR_IHN, p), txt(c, 1241);
            return 1;
        case 1:
            txt(c, 1242), der_prof_name(c, PR_DER, p), txt(c, 1243), art(c, A_DES, h), txt(c, 1244), pron(c, PR_ER, p),
                txt(c, 1245), pron(c, PR_IHN, p), txt(c, 1246), pron(c, PR_ER, p), txt(c, 1247);
            return 1;
        case 2:
            txt(c, 1248), art(c, A_IM, h), txt(c, 1249), name(c, p), txt(c, 1250), pron(c, PR_DER, p), sp(c), prof(c, p),
                txt(c, 1251), pron(c, PR_SEIN, p), txt(c, 1252), pron(c, PR_ER, p), txt(c, 1253), pron(c, PR_ER, p),
                txt(c, 1254);
            return 1;
        case 3:
            txt(c, 1255), pron(c, PR_DER, p), txt(c, 1256), prof_name(c, p), txt(c, 1257), art(c, A_DES, h), txt(c, 1258),
                pron(c, PR_IHN, p), txt(c, 1259), name(c, p), txt(c, 1260);
            return 1;
        default: return 0;
        }
    case 1:
        txt(c, 1261), name(c, p), txt(c, 1262), pron(c, PR_DER, p), txt(c, 1263), prof(c, p), txt(c, 1264), art(c, A_DES, h),
            txt(c, 1265), pron(c, PR_ER, p), txt(c, 1266), pron(c, PR_ER, p), txt(c, 1267), pron_de(c, PR_SEIN, p),
            txt(c, 1268); /* 0x3F667 */
        return 1;
    case 2:
        txt(c, 1269), name(c, p), txt(c, 1270), pron_de(c, PR_DER, p), txt(c, 1271), /* 0x3F6A0 */ art(c, A_IM, h), txt(c, 1272),
            pron(c, PR_IHN, p), txt(c, 1273), pron(c, PR_DER, p), sp(c), prof(c, p), txt(c, 1274);
        return 1;
    default: return 0;
    }
}

static int type36(C *c, int head) { /* 0x24: the team contest */
    int h = b(c, 8), h9 = b(c, 9), a = u(c, 10), bb = u(c, 12), day = u(c, 14);
    if (b(c, 21) == 0) {
        int tied = a == bb;
        if (head) {
            if (tied) txt(c, 1117);
            else hosp(c, h), txt(c, 1116);
        } else if (!tied) {
            txt(c, 1118), num(c, day), txt(c, 1123), art(c, A_DAS, h), txt(c, 1119), num(c, a), txt(c, 1120), art(c, A_DAS, h9),
                txt(c, 1121), num(c, bb), txt(c, 1122), num(c, 25 - day), txt(c, 1124);
        } else {
            txt(c, 1125), num(c, a), txt(c, 1126), hosp(c, h), txt(c, 1127), hosp(c, h9), txt(c, 1128), num(c, day), txt(c, 1129),
                num(c, 25 - day), txt(c, 1130);
        }
        return 1;
    }
    if (b(c, 21) != 1) return 0;
    if (head) hosp(c, h), txt(c, 1131);
    else txt(c, 1132), hosp(c, h), txt(c, 1133), num(c, a), txt(c, 1134), art(c, A_DAS, h9), txt(c, 1135), num(c, bb), txt(c, 1136);
    return 1;
}

static int type37(C *c, int head) { /* 0x25: the driver contest */
    int h = b(c, 8), h9 = b(c, 9), d1 = u(c, 16), d2 = u(c, 18), day = u(c, 14);
    if (b(c, 21) == 0) {
        if (head) sname(c, d1), sp(c), art(c, A_VOM, h), txt(c, 1137);
        else txt(c, 1138), num(c, day), txt(c, 1139), sname(c, d1), sp(c), art(c, A_VOM, h), txt(c, 1140), num(c, u(c, 10)),
            txt(c, 1141), sname(c, d2), sp(c), art(c, A_VOM, h9), txt(c, 1142), num(c, u(c, 12)), txt(c, 1143),
            num(c, 25 - day), txt(c, 1144);
        return 1;
    }
    if (b(c, 21) != 1) return 0;
    if (head) sname(c, d1), sp(c), art(c, A_VOM, h), txt(c, 1145); /* chain */
    else /* chain: T1146-1150 */
        txt(c, 1146), sname(c, d1), sp(c), art(c, A_VOM, h), txt(c, 1147), num(c, u(c, 10)), txt(c, 1148), sname(c, d2), sp(c),
            art(c, A_VOM, h9), txt(c, 1149), num(c, u(c, 12)), txt(c, 1150);
    return 1;
}

static int key_index(int offset) {
    static const int KEYS[6] = {0x20, 0x22, 0x23, 0x24, 0x25, 0x94};
    for (int i = 0; i < 6; i++)
        if (KEYS[i] == offset) return i;
    return -1;
}

/* 0x27: a price key's value. The handler prints b22 by storing it into the item number [0x45CF] (0x3EB52), and the
 * page sets that number once per slot, before the headline, so the slot's body is item b22's (docs/ui-reports.md,
 * the 0x27 row). Engine reading: the store goes with the printed number, so an unhandled offset stores nothing. */
static int type39(C *c) {
    int h = b(c, 8), v = b(c, 22);
    if (key_index(b(c, 21)) >= 0 && c->number) *c->number = v;
    switch (b(c, 21)) {
    case 0x20: txt(c, 1843), hosp(c, h), txt(c, 1844), num(c, v), txt(c, 1845); return 1;
    case 0x22: txt(c, 1846), hosp(c, h), txt(c, 1847), num(c, v), txt(c, 1848); return 1; /* chain */
    case 0x23: txt(c, 1849), num(c, v), txt(c, 1850), art(c, A_IM, h); return 1;
    case 0x24: txt(c, 1851), num(c, v), txt(c, 1852), art(c, A_IM, h); return 1; /* chain */
    case 0x25: hosp(c, h), txt(c, 1853), num(c, v), txt(c, 1854); return 1;
    case 0x94: hosp(c, h), txt(c, 1853), num(c, v), txt(c, 1855); return 1; /* chain: the colon of 0x25 */
    default: return 0;
    }
}

static int build(C *c, int head) {
    int h = b(c, 8), k = b(c, 21);
    switch (b(c, 20)) {
    case 1:
        if (k > 7) return 0;
        txt(c, 196 + k), txt(c, 204 + b(c, 22)), txt(c, 208 + b(c, 23));
        return 1;
    case 2: return k <= 3 ? (txt(c, 164 + 2 * k + !head), 1) : 0;
    case 3: return type3(c, head);
    case 4: return type4(c, head);
    case 5: return type5(c, head);
    case 6: return type6(c, head);
    case 7: return type7(c, head);
    case 8: return type8(c, head);
    case 9: return k <= 3 ? (txt(c, 188 + k), 1) : 0;
    case 0x0A: return k <= 3 ? (txt(c, 172 + 2 * k + !head), 1) : 0;
    case 0x0B: return type11(c, head);
    case 0x0C: return k <= 3 ? (txt(c, 192 + k), 1) : 0;
    case 0x0D: return k <= 3 ? (txt(c, 180 + 2 * k + !head), 1) : 0;
    case 0x0E: return type14(c, head);
    case 0x0F: /* u14 = the receptionist (reception record +8), docs/news-generators.md section 7 */
        if (head) return tested_head(c, 1072, 1073, 1074);
        txt(c, 1075), art(c, A_DES, h), txt(c, 1076), F(c, u(c, 22)), txt(c, 1077), bwh(c, u(c, 14)), txt(c, 1078),
            sname(c, u(c, 14)), txt(c, 1079);
        return 1;
    case 0x10:
        if (head) return tested_head(c, 148, 149, 150);
        txt(c, 151), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 152), F(c, u(c, 24)), txt(c, 153), F(c, u(c, 22)), txt(c, 154),
            sname(c, u(c, 14)), bwh_paren(c, u(c, 14)), txt(c, 155);
        return 1;
    case 0x11:
        if (head) return tested_head(c, 1087, 1088, 1089);
        txt(c, 1090), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 1091), sname(c, u(c, 14)), txt(c, 1092), sname(c, u(c, 10)),
            txt(c, 1093), E(c, u(c, 24)), txt(c, 1094), Facc_bare(c, u(c, 22)), txt(c, 1095);
        return 1;
    case 0x12: return type18(c);
    case 0x13:
        if (head) return tested_head(c, 1096, 1097, 1098);
        txt(c, 1099), hosp(c, h), txt(c, 1100), num(c, b(c, 26)), txt(c, 1101), Facc_bare(c, u(c, 22)), txt(c, 1102),
            sname(c, u(c, 14)), txt(c, 1103), sname(c, u(c, 10)), txt(c, 1104), E(c, u(c, 24)), txt(c, 1105);
        return 1;
    case 0x14:
        if (head) return tested_head(c, 1106, 1107, 1108);
        txt(c, 1109), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 1110), F(c, u(c, 22)), txt(c, 1111), bwh(c, u(c, 14)),
            txt(c, 1112), sname(c, u(c, 14)), txt(c, 1113), sname(c, u(c, 10)), txt(c, 1114), E(c, u(c, 24)), txt(c, 1115);
        return 1;
    case 0x15: return type21(c, head);
    case 0x16: return type22(c, head);
    case 0x17: return type23(c, head);
    case 0x18:
        if (head) return tested_head(c, 1080, 1081, 1082);
        txt(c, 1083), art(c, A_DES, h), txt(c, 1084), Facc_bare(c, u(c, 22)), txt(c, 1085), sname(c, u(c, 14)), txt(c, 1086);
        return 1;
    case 0x19:
        if (head) return tested_head(c, 156, 157, 158);
        txt(c, 159), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 160), F(c, u(c, 24)), txt(c, 161), F(c, u(c, 22)), txt(c, 162),
            sname(c, u(c, 14)), txt(c, 163);
        return 1;
    case 0x1A:
        if (head) {
            if (k == 0) txt(c, 1167), art(c, A_DES, h), txt(c, 1168);
            else if (k == 1) hosp(c, h), txt(c, 1169);
            else return 0;
            return 1;
        }
        txt(c, 1170), art(c, A_DES, h), txt(c, 1171);
        return 1;
    case 0x1B: {
        int p = u(c, 16);
        if (head) {
            if (k == 0) txt(c, 1275), art(c, A_DES, h), txt(c, 1276);
            else if (k == 1) txt(c, 1277), art(c, A_IM, h);
            else return 0;
            return 1;
        }
        /* 0x3F807: the English CD drops the "er/sie" after 1280 (which of the two pronouns 2646 is the one is the
         * engine's reading by the English texts, "only to find he that"; docs/editions.md 7.3, rule C) */
        txt(c, 1278), art(c, A_IM, h), txt(c, 1279), name(c, p), txt(c, 1280), pron_de(c, PR_ER, p), txt(c, 1281),
            pron(c, PR_DER, p), txt(c, 1282), prof(c, p), txt(c, 1283), name(c, p), txt(c, 1284), pron(c, PR_ER, p), sp(c),
            art(c, A_DAS, h), txt(c, 1285);
        return 1;
    }
    case 0x1C: {
        int p = u(c, 16);
        if (head) {
            if (k == 0) hosp(c, h), txt(c, 1286);
            else if (k == 1) txt(c, 1287), art(c, A_IM, h);
            else return 0;
            return 1;
        }
        txt(c, 1288), name(c, p), sp(c), art(c, A_IM, h), txt(c, 1289), pron(c, PR_DER_CAP, p), sp(c), prof(c, p), txt(c, 1290),
            pron(c, PR_ER, p), txt(c, 1291);
        return 1;
    }
    case 0x1D:
        switch (k) {
        case 0: txt(c, 96), E_name(c, u(c, 14)), txt(c, 386), art(c, A_IM, h), txt(c, 97); return 1;
        case 1: txt(c, 98), E_name(c, u(c, 14)), txt(c, 386), art(c, A_IM, h), txt(c, 99); return 1;
        case 2: txt(c, 100), art(c, A_IM, h), txt(c, 101); return 1;
        case 3: txt(c, 102), art(c, A_IM, h); return 1;
        default: return 0;
        }
    case 0x1E: return k <= 7 ? (txt(c, 103 + k), 1) : 0;
    case 0x1F: sname(c, u(c, 16)), sp(c), art(c, A_VOM, h), txt(c, 387); return 1;
    case 0x20: return k <= 7 ? (txt(c, 388 + k), 1) : 0;
    case 0x21:
        if (head) return tested_head(c, 429, 430, 431);
        txt(c, 432), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 433), F(c, u(c, 22)), txt(c, 434), sname(c, u(c, 10)), txt(c, 435),
            E(c, u(c, 24)), txt(c, 436), sname(c, u(c, 14)), txt(c, 437);
        return 1;
    case 0x22:
        if (head) return tested_head(c, 491, 492, 493);
        txt(c, 494), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 495), Facc(c, u(c, 22)), txt(c, 496), sname(c, u(c, 10)),
            txt(c, 497), E(c, u(c, 24)), txt(c, 498), sname(c, u(c, 14)), txt(c, 499);
        return 1;
    case 0x23: { /* the race */
        int p = u(c, 16);
        if (head) hosp(c, h), txt(c, 1059), name(c, p);
        else txt(c, 1060), hosp(c, h), txt(c, 1061), sname(c, u(c, 10)), txt(c, 1062), pron(c, PR_DER_CAP, p), txt(c, 1064),
            name(c, p), txt(c, 1065), sname(c, u(c, 12)), sp(c), art(c, A_VOM, b(c, 9)), txt(c, 1066);
        return 1;
    }
    case 0x24: return type36(c, head);
    case 0x25: return type37(c, head);
    case 0x26: {
        int i = key_index(k);
        if (i < 0) return 0;
        txt(c, 1350 + 2 * i), hosp(c, h), txt(c, 1351 + 2 * i);
        return 1;
    }
    case 0x27: return type39(c);
    case 0x28:
        if (head) return tested_head_im(c, 1894, 1895);
        txt(c, 1896), art(c, A_IM, h), txt(c, 1897), num(c, u(c, 12)), txt(c, 1898), txt(c, 1899), num(c, u(c, 10)), txt(c, 1900),
            art(c, A_DES, h);
        if (u(c, 14) < 2) txt(c, 1901);
        else txt(c, 1902), num(c, u(c, 14)), txt(c, 1903);
        return 1;
    case 0x29:
        if (head) return tested_head(c, 2769, 2770, 2771);
        txt(c, 2772), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2773), Facc(c, u(c, 22)), txt(c, 2774), sname(c, u(c, 10)),
            txt(c, 2775), E(c, u(c, 24)), txt(c, 2776), sname(c, u(c, 14)), txt(c, 2777);
        return 1;
    case 0x2A:
        if (head) return tested_head_im(c, 2927, 2928);
        txt(c, 2929), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2930), F(c, u(c, 24)), txt(c, 2931), F(c, u(c, 22)), txt(c, 2932),
            sname(c, u(c, 14)), bwh_paren(c, u(c, 14)), txt(c, 2933);
        return 1;
    case 0x2B:
        if (head) return tested_head_im(c, 2934, 2935);
        /* chain: T2936-2941 */
        txt(c, 2936), num(c, b(c, 26)), txt(c, 2937), art(c, A_DAS, h), txt(c, 2938), F(c, u(c, 24)), txt(c, 2939), F(c, u(c, 22)),
            txt(c, 2940), sname(c, u(c, 14)), bwh_paren(c, u(c, 14)), txt(c, 2941);
        return 1;
    case 0x2C:
        if (head) return tested_head_im(c, 2948, 2949);
        txt(c, 2950), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2951), F(c, u(c, 24)), txt(c, 2952), E(c, u(c, 22)), txt(c, 2953),
            sname(c, u(c, 14)), txt(c, 2954);
        return 1;
    case 0x2D:
        if (head) return tested_head_im(c, 2955, 2956);
        /* chain: T2957-2961 */
        txt(c, 2957), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2958), F(c, u(c, 24)), txt(c, 2959), sname(c, u(c, 14)),
            txt(c, 2960), E(c, u(c, 22)), txt(c, 2961);
        return 1;
    case 0x2E:
        if (head) {
            if (k > 1) return 0;
            txt(c, 2962 + k), art(c, A_IM, h);
            return 1;
        }
        txt(c, 2964), art(c, A_DES, h), txt(c, 2965), num(c, u(c, 10)), txt(c, 2966), sname(c, u(c, 14)), txt(c, 2967),
            num(c, staff(c, u(c, 14))[5]), txt(c, 2968);
        return 1;
    case 0x2F:
        if (head) return tested_head(c, 2969, 2970, 2971);
        txt(c, 2972), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2973), sname(c, u(c, 14)), txt(c, 2974), Facc_bare(c, u(c, 22)),
            txt(c, 2975), sname(c, u(c, 10)), txt(c, 2976), E(c, u(c, 24)), txt(c, 2977);
        return 1;
    case 0x31:
        if (head) return tested_head(c, 2978, 2979, 2980);
        txt(c, 2981), num(c, b(c, 26)), art(c, A_DES, h), txt(c, 2982), sname(c, u(c, 14)), txt(c, 2983), E(c, u(c, 24)),
            txt(c, 2984), sname(c, u(c, 10)), txt(c, 2985);
        return 1;
    case 0x30:
    case 0x32: {
        int base = b(c, 20) == 0x30 ? 2986 : 2993;
        if (head) return tested_head_im(c, base, base + 1);
        /* chain: T2988-2992 / T2995-2999 */
        txt(c, base + 2), num(c, b(c, 26)), art(c, A_DES, h), txt(c, base + 3), sname(c, u(c, 10)), txt(c, base + 4),
            sname(c, u(c, 14)), txt(c, base + 5), E(c, u(c, 24)), txt(c, base + 6);
        return 1;
    }
    case 0x33:
        if (head) {
            if (k > 1) return 0;
            txt(c, 3000 + 2 * k), art(c, A_IM, h), txt(c, 3001 + 2 * k);
            return 1;
        }
        txt(c, 3004), art(c, A_IM, h), txt(c, 3005), num(c, u(c, 24)), txt(c, 3006);
        return 1;
    case 0x34:
        if (head) {
            if (k > 1) return 0;
            hosp(c, h), txt(c, 3008 + k);
            return 1;
        }
        txt(c, 3010), art(c, A_DAS, h), txt(c, 3011);
        return 1;
    case 0x35: txt(c, k == 2 ? (head ? 3021 : 3022) : (head ? 3019 : 3020)); return 1;
    default: return 0;
    }
}

int news_text_at(Sim *s, int *number, int headline, Pieces *t) {
    pieces_clear(t);
    int n = *number;
    if (n < 1 || n > 100) return 0;
    C c = {s, t, sim_news_item(s, n - 1), n - 1, number};
    if (!build(&c, headline)) {
        pieces_clear(t);
        return 0;
    }
    return 1;
}

int news_text(Sim *s, int number, int headline, Pieces *t) { return news_text_at(s, &number, headline, t); }

int news_issue_ready(Sim *s) {
    const uint8_t *i = s->world->block[WORLD_TAIL] + T_ISSUE;
    return i[0] && i[1] && i[2] && i[3];
}

void news_date(Sim *s, char out[11]) {
    int day = world_tail(s->world, TAIL_DAY), month = world_tail(s->world, TAIL_MONTH);
    int year = world_tail(s->world, TAIL_YEAR);
    if (program_english(s->prog)) { /* MM-DD-YYYY, the year digit year - 1989: a year later (docs/editions.md 7.3, G) */
        memcpy(out, "00-00-1994", 11);
        out[0] = (char)('0' + month / 10 % 10), out[1] = (char)('0' + month % 10);
        out[3] = (char)('0' + day / 10 % 10), out[4] = (char)('0' + day % 10);
        out[9] = (char)('0' + (year - 1989));
        return;
    }
    memcpy(out, "00.00.1994", 11);
    out[0] = (char)('0' + day / 10 % 10), out[1] = (char)('0' + day % 10);
    out[3] = (char)('0' + month / 10 % 10), out[4] = (char)('0' + month % 10);
    out[9] = (char)('0' + (year - 1990)); /* one digit, as the original writes it */
}

size_t news_plural(const uint8_t *t, size_t n, uint8_t *out, size_t cap) {
    size_t o = 0, p = n;
    for (size_t i = 0; i < n; i++)
        if (t[i] == '(') {
            p = i;
            break;
        }
#define PUT(ch) (o < cap ? (void)(out[o++] = (uint8_t)(ch)) : (void)0)
    if (p < n) { /* at the first '(': the byte before it becomes 'n' and a space is inserted, unless p - 2 is n or s */
        uint8_t before2 = p >= 2 ? t[p - 2] : 0; /* engine reading: nothing before the name counts as neither */
        int change = p >= 1 && before2 != 'n' && before2 != 's';
        for (size_t i = 0; i < n; i++) {
            if (change && i == p - 1) {
                PUT('n');
                PUT(' ');
                continue;
            }
            PUT(t[i]);
        }
    } else { /* no '(': an 'n' appended unless the last byte is n, s or ')' */
        for (size_t i = 0; i < n; i++) PUT(t[i]);
        if (n && t[n - 1] != 'n' && t[n - 1] != 's' && t[n - 1] != ')') PUT('n');
    }
#undef PUT
    return o;
}
