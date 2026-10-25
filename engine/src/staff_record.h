/* Checked views for the working-mood path only (docs/sim-staff2.md, "Mood, working rules").
 * BIING_RECORD_CHECKS enables diagnostics independently of NDEBUG; ordinary builds keep the same byte reads. */
#ifndef BIING_STAFF_RECORD_H
#define BIING_STAFF_RECORD_H
#include "world.h"

typedef struct { int value; } StaffId;
typedef struct { int hospital, slot; } StaffSlotId;
typedef enum { STAFF_RECORD, STAFF_SLOT_RECORD } StaffRecordKind;
typedef struct {
    uint8_t *data;
    size_t length;
    StaffRecordKind kind;
#ifdef BIING_RECORD_CHECKS
    int staff_id;
    StaffSlotId slot_id;
#endif
} StaffRecordView;
typedef struct { StaffRecordKind kind; size_t offset, width; } StaffField;

#define STAFF_MOOD_WAGE ((StaffField){STAFF_RECORD, 19, 1})
#define STAFF_MOOD_DEMAND ((StaffField){STAFF_RECORD, 18, 1})
#define STAFF_MOOD_STAMINA ((StaffField){STAFF_RECORD, 14, 1})
#define STAFF_SLOT_HOURS ((StaffField){STAFF_SLOT_RECORD, 10, 1})
#define STAFF_SLOT_EMPLOYEE ((StaffField){STAFF_SLOT_RECORD, 0, 2})

#ifdef BIING_RECORD_CHECKS
StaffRecordView staff_record_at(World *w, StaffId id, const char *caller);
StaffRecordView staff_slot_at(World *w, StaffSlotId id, const char *caller);
uint8_t staff_read_u8(StaffRecordView view, StaffField field, const char *caller);
uint16_t staff_read_u16(StaffRecordView view, StaffField field, const char *caller);
void staff_link_at(StaffId employee, StaffRecordView slot, const char *caller);
#define staff_record_view(...) staff_record_at(__VA_ARGS__, __func__)
#define staff_slot_view(...) staff_slot_at(__VA_ARGS__, __func__)
#define staff_field_u8(...) staff_read_u8(__VA_ARGS__, __func__)
#define staff_field_u16(...) staff_read_u16(__VA_ARGS__, __func__)
#define staff_record_link(...) staff_link_at(__VA_ARGS__, __func__)
#else
/* Keep these inline and descriptors constant so checks-off builds fold to the original pointer/byte accesses. */
static inline StaffRecordView staff_record_view(World *w, StaffId id) {
    return (StaffRecordView){w->block[BLK_STAFF] + (size_t)id.value * 32, 32, STAFF_RECORD};
}
static inline StaffRecordView staff_slot_view(World *w, StaffSlotId id) {
    return (StaffRecordView){w->block[BLK_STAFF_SLOTS] + ((size_t)id.hospital * 200 + (size_t)id.slot) * 20, 20, STAFF_SLOT_RECORD};
}
static inline uint8_t staff_field_u8(StaffRecordView v, StaffField f) { return v.data[f.offset]; }
static inline uint16_t staff_field_u16(StaffRecordView v, StaffField f) {
    const uint8_t *p = v.data + f.offset;
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static inline void staff_record_link(StaffId employee, StaffRecordView slot) { (void)employee; (void)slot; }
#endif
#endif
