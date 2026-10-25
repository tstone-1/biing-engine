#include "staff_record.h"

#ifdef BIING_RECORD_CHECKS
#include <stdio.h>
#include <stdlib.h>

static void fail_index(const char *reason, const char *caller, int employee, int hospital, int slot) {
    fprintf(stderr, "[ERROR] record check: %s (caller=%s staff=%d hospital=%d slot=%d)\n", reason, caller, employee, hospital, slot);
    exit(EXIT_FAILURE);
}
static void fail_field(const char *reason, const char *caller, StaffRecordView v, StaffField f, size_t width) {
    fprintf(stderr, "[ERROR] record check: %s (caller=%s staff=%d hospital=%d slot=%d kind=%d expected_kind=%d "
                    "length=%zu offset=%zu width=%zu expected_width=%zu)\n",
            reason, caller, v.staff_id, v.slot_id.hospital, v.slot_id.slot, (int)v.kind, (int)f.kind, v.length, f.offset, width, f.width);
    exit(EXIT_FAILURE);
}

StaffRecordView staff_record_at(World *w, StaffId id, const char *caller) {
    if (id.value < 0 || id.value >= STAFF_COUNT) fail_index("staff index", caller, id.value, -1, -1);
    size_t at = (size_t)id.value * 32;
    if (!w->block[BLK_STAFF] || at > w->size[BLK_STAFF] || w->size[BLK_STAFF] - at < 32)
        fail_index("staff storage", caller, id.value, -1, -1);
    return (StaffRecordView){w->block[BLK_STAFF] + at, 32, STAFF_RECORD, id.value, {-1, -1}};
}

StaffRecordView staff_slot_at(World *w, StaffSlotId id, const char *caller) {
    if (id.hospital < 0 || id.hospital >= 16 || id.slot < 0 || id.slot >= 200)
        fail_index("slot index", caller, -1, id.hospital, id.slot);
    size_t at = ((size_t)id.hospital * 200 + (size_t)id.slot) * 20;
    if (!w->block[BLK_STAFF_SLOTS] || at > w->size[BLK_STAFF_SLOTS] || w->size[BLK_STAFF_SLOTS] - at < 20)
        fail_index("slot storage", caller, -1, id.hospital, id.slot);
    return (StaffRecordView){w->block[BLK_STAFF_SLOTS] + at, 20, STAFF_SLOT_RECORD, -1, id};
}

static const uint8_t *field_at(StaffRecordView v, StaffField f, size_t width, const char *caller) {
    if (v.kind != f.kind) fail_field("field kind", caller, v, f, width);
    if (f.width != width) fail_field("field width", caller, v, f, width);
    if (!v.data || v.length != (v.kind == STAFF_RECORD ? 32u : 20u)) fail_field("record length", caller, v, f, width);
    if (f.offset > v.length || width > v.length - f.offset) fail_field("field bounds", caller, v, f, width);
    return v.data + f.offset;
}
uint8_t staff_read_u8(StaffRecordView view, StaffField field, const char *caller) { return *field_at(view, field, 1, caller); }
uint16_t staff_read_u16(StaffRecordView view, StaffField field, const char *caller) {
    const uint8_t *p = field_at(view, field, 2, caller);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
void staff_link_at(StaffId employee, StaffRecordView slot, const char *caller) {
    if (staff_read_u16(slot, STAFF_SLOT_EMPLOYEE, caller) != employee.value)
        fail_index("staff-slot mismatch", caller, employee.value, slot.slot_id.hospital, slot.slot_id.slot);
}
#endif
