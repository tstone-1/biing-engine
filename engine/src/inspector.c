/* Field meanings from docs/economy.md, formats.md, sim-patients.md, sim-staff.md
 * and sim-rooms.md. Relationship fields stay read-only: changing an assignment
 * needs the game's own transaction, not a write to one of its mirrored records.
 * Edits are diagnostic scalar writes, not purchases, cures, hiring or game actions. */
#include "inspector.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    unsigned offset, width;
    int sign, writable;
    int64_t min, max;
} Field;
#define R(n,o,w,s) {n,o,w,s,0,0,0}
#define E(n,o,w,s,lo,hi) {n,o,w,s,1,lo,hi}
#define U(n,o) E(n,o,1,0,0,255)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const Field hospitals[] = {
    E("Cash",0x16,4,1,INT32_MIN,INT32_MAX),
    R("In business flag",0x15,1,0), R("Owned plots",0x1A,2,0),
    R("Rented plots",0x1C,2,0), R("Built plots",0x1E,2,0),
    E("Ward price divisor",0x20,1,0,1,255), E("Intensive care price divisor",0x21,1,0,1,255),
    E("Operation price divisor",0x22,1,0,1,255), E("Equipment price divisor",0x23,1,0,1,255),
    E("Doctor price divisor",0x24,1,0,1,255), E("Nurse price divisor",0x25,1,0,1,255),
    E("Material price divisor",0x94,1,0,1,255), U("Invoice base fee",0x95),
    R("Price index (cached)",0x10A,2,0), R("Opinion sum / score (shared cache)",0x44,4,1),
    R("Discharged and billed",0x26,2,0), R("Cured patients",0x28,2,0), R("Uncured / pathology cases",0x2A,2,0),
    R("Operations started",0x32,2,0), R("Deaths counter",0x38,2,0), R("Donors",0x3E,2,0),
    R("Blood donated",0x40,4,1), R("Blood transfused",0x5A,4,1),
    R("Treatment revenue today",0x8C,4,1), R("Treatment revenue total",0x90,4,1),
    R("Room rent today",0x96,4,1), R("Room rent total",0x9A,4,1),
    R("Plot rent today",0x9E,4,1), R("Plot rent total",0xA2,4,1),
    R("Personnel costs today",0xA6,4,1), R("Personnel costs total",0xAA,4,1),
    R("Investment today",0x14E,4,1), R("Investment total",0x152,4,1),
    R("Days played",0x162,2,0), R("Opinions above 69",0x164,2,0), R("Opinions below 30",0x166,2,0),
};
static const Field staff[] = {
    R("Sex",0,1,0), R("Job group",1,1,0), E("Training level",2,1,0,0,11),
    R("Hospital (-1 = applicant)",3,1,1), R("Duty state",4,1,0),
    U("Bust / job-specific attribute",5), U("Waist / golf handicap",6), U("Hips / male hourly attribute",7),
    E("Age",8,1,0,18,100), E("Height",9,1,0,100,250), E("Weight",10,1,0,30,250),
    R("Independent work flag",11,1,0), U("Specific course points",12), U("General course points",13),
    E("Diligence",14,1,0,0,100), E("IQ",15,1,0,0,100), E("Experience",16,2,0,0,65535),
    U("Salary demand",18), R("Current wage (mirrored in employment slot)",19,1,0),
    E("Mood",21,1,0,1,100), R("Dismissed by hospital",22,1,1), R("Portrait",24,2,0),
    R("Room / course index",26,1,0), R("Reserved job opening",27,1,1),
    R("Qualification text",28,2,0), R("Peculiarity text",30,2,0),
};
static const Field patients[] = {
    R("Sex",0,1,0), R("Profession",1,1,0), R("Hospital (-1 = home)",3,1,1), R("Hospital state",4,1,0),
    E("Health",44,1,0,0,100), E("Baseline health",78,1,0,0,100), E("Blood",94,2,1,0,700),
    R("Ailment",41,1,0), R("Diagnosed illness",42,1,0), R("Treatment",43,1,0), R("Actual illness",56,1,0),
    R("Cosmetic wish",57,1,0), R("In treatment flag",79,1,0), R("Treating hospital",81,1,1),
    R("Treatment rank",82,1,0), R("Treatment rounds",103,1,0), R("Death counter",104,1,0),
    R("Treatment days remaining",105,1,0),
    E("Opinion of hospital 0",20,1,0,0,100), E("Opinion of hospital 1",21,1,0,0,100),
    E("Opinion of hospital 2",22,1,0,0,100), E("Opinion of hospital 3",23,1,0,0,100),
    E("Opinion of hospital 4",24,1,0,0,100), E("Opinion of hospital 5",25,1,0,0,100),
    E("Opinion of hospital 6",26,1,0,0,100), E("Opinion of hospital 7",27,1,0,0,100),
    E("Opinion of hospital 8",28,1,0,0,100), E("Opinion of hospital 9",29,1,0,0,100),
    E("Opinion of hospital 10",30,1,0,0,100), E("Opinion of hospital 11",31,1,0,0,100),
    E("Opinion of hospital 12",32,1,0,0,100), E("Opinion of hospital 13",33,1,0,0,100),
    E("Opinion of hospital 14",34,1,0,0,100), E("Opinion of hospital 15",35,1,0,0,100),
};
static const Field employment[] = {
    R("Staff index (0 = empty)",0,2,0), R("Job group",2,1,0), R("Duty state",3,1,0), R("Room / course",4,1,0),
    R("Wage",6,1,0), R("Hours since hired",7,1,0), R("Days employed",8,2,0),
    R("Hours on duty",10,1,0), R("Hours off duty",11,1,0), R("Mission target",12,2,0),
    R("Mission minutes",14,1,0), R("Return category",16,1,0), R("Return room",17,1,0), R("Rest countdown",19,1,0),
};
static const Field roomslots[] = {R("Room catalog type (255 = empty)",0,1,0), R("Rented flag",1,1,0)};
static const Field wards[] = {
    R("Beds",10,2,0), R("Occupied beds",12,2,0),
};
static const Field reception[] = {
    R("Assigned staff",8,2,0), R("Waiting patient 1",10,2,0), R("Waiting patient 2",14,2,0),
    R("Waiting patient 3",18,2,0), R("Waiting patient 4",22,2,0),
};
static const Field bank[] = {R("Blood stock",12,4,1), R("Blood capacity",16,4,1)};
static const Field store[] = {
    R("Clerk",4,2,0), R("Capacity",7,1,0), R("Furniture held",8,1,0), R("Equipment held",9,1,0),
};
typedef struct {
    const char *name, *source;
    unsigned block, stride, per_hospital;
    const Field *fields;
    size_t nfields;
} Group;
#define G(n,s,b,w,h,f) {n,s,b,w,h,f,COUNT(f)}
static const Group groups[] = {
    G("Hospitals","docs/economy.md",BLK_HOSPITALS,500,1,hospitals),
    G("Staff","docs/formats.md; docs/sim-staff.md",BLK_STAFF,32,0,staff),
    G("Patients","docs/formats.md; docs/sim-patients.md",BLK_PATIENTS,106,0,patients),
    G("Employment slots","docs/sim-staff.md",BLK_STAFF_SLOTS,20,200,employment),
    G("Room slots","docs/economy.md",BLK_ROOM_SLOTS,2,200,roomslots),
    G("Receptions","docs/sim-patients.md",3,174,1,reception),
    G("Bed wards","docs/sim-patients.md",16,240,10,wards),
    G("Neurology wards","docs/sim-patients.md",17,240,10,wards),
    G("Blood banks","docs/economy.md",5,178,1,bank),
    G("Stores","docs/sim-staff.md",12,730,1,store),
};

/* Game text pools are Latin-1. Escape bytes as JSON codepoints, never print them
 * as UTF-8 or let an embedded quote become protocol structure. */
static void string_bytes(FILE *out, const uint8_t *s, size_t n) {
    fputc('"', out);
    for (size_t i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c < 32 || c >= 127 || c == '"' || c == '\\') fprintf(out, "\\u%04x", c);
        else fputc((int)c, out);
    }
    fputc('"', out);
}
static void string(FILE *out, const char *s) { string_bytes(out, (const uint8_t *)s, strlen(s)); }
static int64_t value(const uint8_t *p, const Field *f) {
    uint32_t v = 0;
    for (unsigned k = 0; k < f->width; k++) v |= (uint32_t)p[f->offset + k] << (8 * k);
    if (f->sign) {
        int64_t modulus = INT64_C(1) << (8 * f->width);
        if ((int64_t)v >= modulus / 2) return (int64_t)v - modulus;
    }
    return v;
}
static size_t records(const Biing *b, const Group *g) { return b->world.size[g->block] / g->stride; }
static uint8_t *record(Biing *b, const Group *g, int64_t i) {
    if (i < 0 || (uint64_t)i >= records(b, g) || !b->world.block[g->block]) return NULL;
    return b->world.block[g->block] + (size_t)i * g->stride;
}
static int hospital_of(const Group *g, size_t i, const uint8_t *p) {
    if (g->per_hospital) return (int)(i / g->per_hospital);
    return p[3] == 255 ? -1 : p[3];
}
static void name(FILE *out, const Biing *b, unsigned group, size_t i) {
    if (group == 0 && i < 17) {
        /* docs/economy.md and docs/desktop.md: 29 name bytes, then an article
         * flag. '$' terminates; '_' is an invisible discretionary hyphen. */
        uint8_t label[29];
        size_t n = 0;
        for (unsigned k = 0; k < sizeof label; k++) {
            int c = program_u8(&b->prog, 0x3BCF, (uint32_t)i * 30 + k);
            if (!c || c == '$') break;
            if (c != '_') label[n++] = (uint8_t)c;
        }
        if (n) { string_bytes(out, label, n); return; }
    }
    if (group == 1 || group == 2) {
        size_t n = 0;
        const uint8_t *s = datadb_pool_entry(&b->people, group == 1 ? DAT_STAFF_NAMES : DAT_PATIENT_NAMES, (int)i, &n);
        if (s && n) {
            size_t len = 0;
            while (len < n && s[len] && s[len] != '$') len++;
            if (len) { string_bytes(out, s, len); return; }
        }
    }
    char label[96];
    snprintf(label, sizeof label, "%s %zu", groups[group].name, i);
    string(out, label);
}
static void header(Inspector *in, Biing *b, FILE *out, const char *error, int ready) {
    fprintf(out, "{\"event\":\"%s\",\"protocol\":1,\"inspector\":true,\"window\":true,"
        "\"seq\":%" PRIu64 ",\"frames\":%" PRIu64 ",\"ok\":%s,\"paused\":%s,\"steps\":%u,"
        "\"edits\":%" PRIu64 ",\"screen\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,"
        "\"quit\":%s,\"intro_active\":false,\"sim_tick\":%d,\"clock_stopped\":%s,\"cash\":%ld",
        ready ? "ready" : "response", in->seq, in->frames, error ? "false" : "true",
        in->paused ? "true" : "false", in->steps, in->edits, b->ui.current,
        biing_clock(b, TAIL_DAY), biing_clock(b, TAIL_HOUR), biing_clock(b, TAIL_MINUTE),
        b->quit ? "true" : "false", b->sim.tick, b->clock_stopped ? "true" : "false", biing_cash(b));
    if (error) { fputs(",\"error\":", out); string(out, error); }
}
static int finish(FILE *out) { fputs("}\n", out); return fflush(out) || ferror(out); }
int inspector_error(Inspector *in, Biing *b, const char *error, FILE *out) {
    in->seq++;
    header(in, b, out, error, 0);
    return finish(out);
}
/* Strict signed decimal parsing: no whitespace, suffixes, empty values or overflow. */
static int numbers(const char *s, int64_t *v, size_t n) {
    for (size_t k = 0; k < n; k++) {
        const char *digit = *s == '-' ? s + 1 : s;
        if (*digit < '0' || *digit > '9') return 0;
        char *end;
        errno = 0;
        long long x = strtoll(s, &end, 10);
        if (errno || (k + 1 == n ? *end != 0 : *end != ':')) return 0;
        v[k] = (int64_t)x;
        s = end + (k + 1 < n);
    }
    return 1;
}
int inspector_command(Inspector *in, Biing *b, const char *cmd, FILE *out) {
    if (!cmd) { header(in, b, out, NULL, 1); return finish(out); }
    const char *error = NULL;
    int64_t a[5] = {0};
    int action = 0; /* 1 schema, 2 list, 3 read, 4 raw, 5 set */
    const Group *g = NULL;
    uint8_t *p = NULL;
    if (!strcmp(cmd, "status")) {}
    else if (!strcmp(cmd, "pause")) { in->paused = 1; in->steps = 0; }
    else if (!strcmp(cmd, "resume")) { in->paused = 0; in->steps = 0; }
    else if (!strcmp(cmd, "quit")) b->quit = 1;
    else if (!strcmp(cmd, "schema")) action = 1;
    else if (!strncmp(cmd, "step:", 5)) {
        if (!numbers(cmd + 5, a, 1) || a[0] < 1 || a[0] > 50) error = "Expected 1..50 frames";
        else if (!in->paused || in->steps) error = "Pause and wait for the previous step first";
        else in->steps = (unsigned)a[0];
    } else if (!strncmp(cmd, "save:", 5)) {
        if (!in->paused || in->steps) error = "Pause before saving a checkpoint";
        else if (!cmd[5] || world_save_edition(&b->world, cmd + 5, &b->prog)) error = "Checkpoint could not be written";
    } else if (!strncmp(cmd, "load:", 5)) {
        if (!in->paused || in->steps) error = "Pause before restoring a checkpoint";
        else if (!cmd[5] || biing_restore_checkpoint(b, cmd + 5)) error = "Checkpoint could not be restored";
        else in->edits = 0;
    } else {
        size_t n = 0;
        const char *args = NULL;
        if (!strncmp(cmd, "list:", 5)) { action = 2; n = 1; args = cmd + 5; }
        if (!strncmp(cmd, "read:", 5)) { action = 3; n = 2; args = cmd + 5; }
        if (!strncmp(cmd, "raw:", 4)) { action = 4; n = 2; args = cmd + 4; }
        if (!strncmp(cmd, "set:", 4)) { action = 5; n = 5; args = cmd + 4; }
        if (!args || !numbers(args, a, n)) error = "Unknown command or invalid integer arguments";
        else if (action == 4) {
            if (a[0] < 0 || a[0] > WORLD_TAIL || a[1] < 0 || (uint64_t)a[1] >= b->world.size[a[0]] ||
                !b->world.block[a[0]]) error = "Raw block or offset out of range";
        } else if (a[0] < 0 || (uint64_t)a[0] >= COUNT(groups)) error = "Group out of range";
        else {
            g = &groups[a[0]];
            if (action != 2 && !(p = record(b, g, a[1]))) error = "Record out of range";
            else if (action == 5) {
                if (!in->paused || in->steps) error = "Pause before editing";
                else if (a[2] < 0 || (uint64_t)a[2] >= g->nfields) error = "Field out of range";
                else {
                    const Field *f = &g->fields[a[2]];
                    if (!f->writable) error = "Read-only field: use the game for structural changes";
                    else if (a[4] < f->min || a[4] > f->max) error = "Value outside this field's allowed range";
                    else if (value(p, f) != a[3]) error = "Value changed since it was read; refresh before editing";
                    else {
                        uint32_t bits = (uint32_t)a[4];
                        for (unsigned k = 0; k < f->width; k++) p[f->offset + k] = (uint8_t)(bits >> (8 * k));
                        in->edits++;
                    }
                }
            }
        }
    }
    in->seq++;
    header(in, b, out, error, 0);
    if (error) return finish(out);
    if (action == 1) {
        fputs(",\"groups\":[", out);
        for (size_t k = 0; k < COUNT(groups); k++) {
            g = &groups[k];
            fprintf(out, "%s{\"id\":%zu,\"name\":", k ? "," : "", k); string(out, g->name);
            fputs(",\"source\":", out); string(out, g->source);
            fprintf(out, ",\"count\":%zu,\"block\":%u,\"stride\":%u,\"fields\":[", records(b,g), g->block, g->stride);
            for (size_t j = 0; j < g->nfields; j++) {
                const Field *f = &g->fields[j];
                fprintf(out, "%s{\"id\":%zu,\"name\":", j ? "," : "", j); string(out, f->name);
                fprintf(out, ",\"offset\":%u,\"width\":%u,\"signed\":%s,\"editable\":%s,\"min\":%" PRId64 ",\"max\":%" PRId64 "}",
                    f->offset, f->width, f->sign ? "true" : "false", f->writable ? "true" : "false", f->min, f->max);
            }
            fputs("]}", out);
        }
        fputs("],\"blocks\":[", out);
        for (int k = 0; k <= WORLD_TAIL; k++) fprintf(out, "%s%zu", k ? "," : "", b->world.size[k]);
        fputc(']', out);
    } else if (action == 2) {
        fputs(",\"records\":[", out);
        for (size_t i = 0; i < records(b,g); i++) {
            p = record(b,g,(int64_t)i);
            if (!p) break;
            fprintf(out, "%s{\"id\":%zu,\"hospital\":%d,\"name\":", i ? "," : "", i, hospital_of(g,i,p));
            name(out,b,(unsigned)a[0],i);
            fputc('}', out);
        }
        fputc(']', out);
    } else if (action == 3 || action == 5) {
        fprintf(out, ",\"group\":%" PRId64 ",\"record\":%" PRId64 ",\"hospital\":%d,\"name\":",
            a[0],a[1],hospital_of(g,(size_t)a[1],p));
        name(out,b,(unsigned)a[0],(size_t)a[1]);
        fputs(",\"values\":[",out);
        for (size_t j = 0; j < g->nfields; j++) fprintf(out, "%s%" PRId64, j ? "," : "", value(p,&g->fields[j]));
        fputs("],\"bytes\":[",out);
        for (unsigned j = 0; j < g->stride; j++) fprintf(out, "%s%u", j ? "," : "", p[j]);
        fputc(']',out);
    } else if (action == 4) {
        fputs(",\"bytes\":[",out);
        size_t available = b->world.size[a[0]] - (size_t)a[1];
        if (available > 256) available = 256;
        for (size_t j = 0; j < available; j++) fprintf(out, "%s%u", j ? "," : "", b->world.block[a[0]][(size_t)a[1] + j]);
        fputc(']',out);
    }
    return finish(out);
}
