#include "inspector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"[FAIL] line %d: %s\n",__LINE__,#x); failures++; } } while (0)
static char response[1048576];
static int command(Inspector *in, Biing *b, const char *cmd) {
    FILE *out = tmpfile();
    if (!out) { failures++; return 0; }
    CHECK(!inspector_command(in,b,cmd,out));
    rewind(out);
    size_t got = fread(response,1,sizeof response-1,out);
    response[got] = 0;
    CHECK(!ferror(out));
    fclose(out);
    return strstr(response,"\"ok\":true") != NULL;
}
int main(void) {
    static Biing b;
    Inspector in = {0};
    CHECK(!world_alloc(&b.world));
    world_set_cash(&b.world,0,1234);
    CHECK(command(&in,&b,NULL));
    CHECK(strstr(response,"\"inspector\":true") && strstr(response,"\"event\":\"ready\""));
    CHECK(!command(&in,&b,"set:0:0:0:1234:9000")); /* Running writes must fail. */
    CHECK(world_cash(&b.world,0) == 1234 && in.edits == 0);
    CHECK(command(&in,&b,"pause"));
    CHECK(in.paused);
    /* Zero is within a read-only field's descriptor bounds. An out-of-range
     * write would only exercise the range check if the read-only guard broke. */
    world_hospital(&b.world,0)[0x15] = 1;
    CHECK(!command(&in,&b,"set:0:0:1:1:0"));
    CHECK(strstr(response,"Read-only field"));
    CHECK(world_hospital(&b.world,0)[0x15] == 1 && in.edits == 0);
    CHECK(command(&in,&b,"schema"));
    CHECK(strstr(response,"\"name\":\"Mood\"") && strstr(response,"\"name\":\"Room slots\""));
    /* Synthetic translated table at a mapped address, including Latin-1 and
     * discretionary hyphens. No names or bytes from the purchased game. */
    uint8_t names[17 * 30] = {0};
    memcpy(names + 10 * 30, "Test_Klinik \334$hidden", 19);
    memcpy(names + 16 * 30, "Test Dealer$", 12);
    ProgramSegment segment = {0x3BCF, 0x3BCF + sizeof names, 0};
    ProgramBuild build = {.segments = &segment, .segment_count = 1};
    b.prog.image.data = names;
    b.prog.image.size = sizeof names;
    b.prog.build = &build;
    CHECK(command(&in,&b,"list:0"));
    CHECK(strstr(response,"\"name\":\"TestKlinik \\u00dc\""));
    CHECK(strstr(response,"\"name\":\"Test Dealer\""));
    CHECK(command(&in,&b,"read:0:10"));
    CHECK(strstr(response,"\"name\":\"TestKlinik \\u00dc\""));
    b.prog.image.data = NULL;
    b.prog.image.size = 0;
    b.prog.build = NULL;
    CHECK(command(&in,&b,"list:1"));
    CHECK(strstr(response,"\"id\":1040,"));
    uint8_t before[500];
    memcpy(before,world_hospital(&b.world,0),sizeof before);
    CHECK(command(&in,&b,"set:0:0:0:1234:-500"));
    CHECK(world_cash(&b.world,0) == -500 && in.edits == 1);
    for (int i = 0; i < 500; i++) if (i < 0x16 || i >= 0x1A) CHECK(before[i] == world_hospital(&b.world,0)[i]);
    const char *bad[] = {"set:0:0:0:1234:42", "set:0:0:0:-500:2147483648", "set:0:0:0:-500:-2147483649",
        "set:0:0:1:0:255", "set:0:0:5:0:0", "set:0:17:0:0:1", "set:0:-1:0:0:1", "set:0:0:999:0:1",
        "set:1:1041:0:0:1", "set:2:1009:0:0:1", "set:0:0:0:-500:5tail", "set:0:0:0:-500:5:6",
        "set:0:0:0:-500:99999999999999999999999", "read:0:+0", "read:0: 0", "read:0:0:",
        "read:10:0", "raw:37:0", "raw:0:-1", "raw:0:106954", "step:0", "step:51", "step:-1"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        CHECK(!command(&in,&b,bad[i])); CHECK(world_cash(&b.world,0) == -500 && in.edits == 1);
    }
    CHECK(command(&in,&b,"set:0:16:0:0:2147483647"));
    CHECK(world_cash(&b.world,16) == INT32_MAX);
    CHECK(command(&in,&b,"set:0:16:0:2147483647:-2147483648"));
    CHECK(world_cash(&b.world,16) == INT32_MIN);
    CHECK(command(&in,&b,"set:1:1040:16:0:65535")); /* Experience is unsigned LE16. */
    CHECK(b.world.block[BLK_STAFF][1040*32+16] == 255 && b.world.block[BLK_STAFF][1040*32+17] == 255);
    CHECK(command(&in,&b,"read:1:1040"));
    CHECK(strstr(response,"65535"));
    b.world.block[BLK_STAFF][1040*32+3] = 255;
    CHECK(command(&in,&b,"read:1:1040"));
    CHECK(strstr(response,"\"values\":[0,0,0,-1,"));
    b.world.block[BLK_PATIENTS][1008*106+94] = 255;
    b.world.block[BLK_PATIENTS][1008*106+95] = 255;
    CHECK(command(&in,&b,"read:2:1008"));
    CHECK(strstr(response,"\"values\":[0,0,0,0,0,0,-1,"));
    CHECK(command(&in,&b,"set:2:1008:4:0:100"));
    CHECK(b.world.block[BLK_PATIENTS][1008*106+44] == 100);
    CHECK(command(&in,&b,"raw:36:93"));
    CHECK(strstr(response,"\"bytes\":[0]"));
    CHECK(command(&in,&b,"step:50")); CHECK(in.steps == 50);
    CHECK(!command(&in,&b,"set:0:0:0:-500:10"));
    CHECK(!command(&in,&b,"step:1"));
    CHECK(command(&in,&b,"pause")); CHECK(!in.steps);
    CHECK(command(&in,&b,"resume")); CHECK(!in.paused);
    CHECK(!command(&in,&b,"step:1"));
    world_free(&b.world);
    if (!failures) puts("[PASS] Inspector schema, bounds, pause, stale-write protection and exact scalar edits");
    return failures != 0;
}
