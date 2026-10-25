/* Synthetic exact-sized World records; failure cases run as separate portable CTest child processes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_internal.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void positive(World *w, Sim *s) {
    StaffRecordView first = staff_record_view(w, (StaffId){0}), last = staff_record_view(w, (StaffId){1040});
    StaffRecordView a = staff_slot_view(w, (StaffSlotId){0, 0}), b = staff_slot_view(w, (StaffSlotId){15, 199});
    CHECK(first.data == w->block[BLK_STAFF] && first.length == 32 && first.kind == STAFF_RECORD);
    CHECK(last.data + last.length == w->block[BLK_STAFF] + w->size[BLK_STAFF]);
    CHECK(a.data == w->block[BLK_STAFF_SLOTS] && a.length == 20 && a.kind == STAFF_SLOT_RECORD);
    CHECK(b.data + b.length == w->block[BLK_STAFF_SLOTS] + w->size[BLK_STAFF_SLOTS]);
    /* The last legal two-byte field and byte are accepted; a one-byte overrun is tested separately. */
    put16(b.data + 18, 0xA581);
    CHECK(staff_field_u16(b, (StaffField){STAFF_SLOT_RECORD, 18, 2}) == 0xA581);
    CHECK(staff_field_u8(b, (StaffField){STAFF_SLOT_RECORD, 19, 1}) == 0xA5);
    put16(b.data + 18, 0);
    put16(b.data, 1040);
    static const struct { int wage, hours, stamina, fix, result; size_t draws; } cases[] = {
        {9, 0, 0, 0, 1, 2}, {9, 200, 16, 0, 0, 2},
        {10, 0, 0, 0, 2, 1}, {10, 3, 0, 0, 0, 2}, {10, 127, 0, 0, 1, 3},
        {10, 128, 0, 0, 2, 1}, {10, 200, 0, 0, 2, 1}, {10, 128, 0, 1, 1, 3}, {10, 200, 0, 1, 1, 3},
        {11, 0, 0, 0, 2, 0}, {11, 3, 0, 0, 1, 2}, {11, 127, 0, 0, 1, 0}, {11, 200, 0, 1, 1, 0},
    };
    static const uint32_t zeros[3] = {0};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        last.data[19] = (uint8_t)cases[i].wage, last.data[18] = 10, last.data[14] = (uint8_t)cases[i].stamina;
        b.data[10] = (uint8_t)cases[i].hours;
        fixes_set(cases[i].fix ? 1u << 28 : 0);
        SimScript script = {zeros, 3, 0, 0, NULL};
        s->rng_script = &script;
        CHECK(sim_mood_working(s, (StaffId){1040}, (StaffSlotId){15, 199}) == cases[i].result);
        CHECK(script.used == cases[i].draws && script.extra == 0);
        s->rng_script = NULL;
    }
    fixes_set(0);
    /* Actual minute dispatcher selects the last slot and employee. Invalid stored IDs keep being skipped. */
    put16(a.data, -1), put16(a.data + 20, STAFF_COUNT);
    last.data[1] = 1, last.data[4] = 5, last.data[19] = 11, last.data[18] = 10, last.data[21] = 50;
    b.data[3] = 5, b.data[10] = 24;
    SimScript script = {zeros, 3, 0, 0, NULL};
    s->rng_script = &script;
    CHECK(sim_staff_run(s, 0x2FC77) == 1);
    CHECK(last.data[21] == 49 && first.data[21] == 0 && script.used == 0 && script.extra == 0);
    s->rng_script = NULL;
}

static void negative(const char *which, World *w, Sim *s) {
    StaffId employee = {1};
    StaffSlotId place = {0, 0};
    StaffRecordView slot = staff_slot_view(w, place);
    put16(slot.data, employee.value);
    if (!strcmp(which, "kind")) (void)staff_field_u8(slot, STAFF_MOOD_WAGE);
    else if (!strcmp(which, "width")) (void)staff_field_u16(slot, STAFF_SLOT_HOURS);
    else if (!strcmp(which, "bounds")) (void)staff_field_u16(slot, (StaffField){STAFF_SLOT_RECORD, 19, 2});
    else if (!strcmp(which, "offset")) (void)staff_field_u8(slot, (StaffField){STAFF_SLOT_RECORD, 21, 1});
    else if (!strcmp(which, "length")) { slot.length = 19; (void)staff_field_u8(slot, STAFF_SLOT_HOURS); }
    else if (!strcmp(which, "null")) { slot.data = NULL; (void)staff_field_u8(slot, STAFF_SLOT_HOURS); }
    else {
        if (!strcmp(which, "staff-negative")) employee.value = -1;
        else if (!strcmp(which, "staff-past")) employee.value = STAFF_COUNT;
        else if (!strcmp(which, "slot-negative")) place.slot = -1;
        else if (!strcmp(which, "slot-past")) place.slot = 200;
        else if (!strcmp(which, "hospital-negative")) place.hospital = -1;
        else if (!strcmp(which, "hospital-past")) place.hospital = 16;
        else if (!strcmp(which, "staff-missing")) { free(w->block[BLK_STAFF]); w->block[BLK_STAFF] = NULL; }
        else if (!strcmp(which, "slot-missing")) { free(w->block[BLK_STAFF_SLOTS]); w->block[BLK_STAFF_SLOTS] = NULL; }
        else if (!strcmp(which, "staff-short")) w->size[BLK_STAFF] = 63;
        else if (!strcmp(which, "staff-storage-past")) w->size[BLK_STAFF] = 31;
        else if (!strcmp(which, "slot-short")) w->size[BLK_STAFF_SLOTS] = 19;
        else if (!strcmp(which, "slot-storage-past")) { place.slot = 1; w->size[BLK_STAFF_SLOTS] = 19; }
        else if (!strcmp(which, "link")) put16(slot.data, 2);
        else { fprintf(stderr, "unknown case: %s\n", which); exit(2); }
        (void)sim_mood_working(s, employee, place);
    }
    puts("[FAIL] expected record diagnostic was not raised");
}

int main(int argc, char **argv) {
    World world = {0};
    Program program = {0};
    Sim sim;
    if (world_alloc(&world)) return 2;
    sim_init(&sim, &world, &program, 7);
    if (argc == 1) positive(&world, &sim);
    else negative(argv[1], &world, &sim);
    world_free(&world);
    if (failures) return 1;
    puts("[OK] staff record views");
    return 0;
}
