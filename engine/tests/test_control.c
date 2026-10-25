#include "control.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    static const char *valid[] = {"status", "quit", "move:0,0", "click:639,439", "wait:0", "wait:30000",
        "key:enter", "key:back", "key:Hello world!", "code:0,ff,1B,48", "save:a b", "shot:C:/a.ppm", "load:x",
        "intro:start", "intro:pre", "intro:pre:first", "intro:pre:again", "intro:relo", "intro:bint",
        "intro:encode", "intro:encode:1", "intro:encode:13:q", "intro:encode:13:d"};
    static const char *bad[] = {"", "status ", "quit:x", "cash:1", "call:123", "wav:a", "move:0", "move:0,",
        "move:,0", "move:640,0", "click:0,440", "move:-1,0", "move:0,0tail", "move:0,0,1", "wait:", "wait:-1",
        "wait:+1", "wait: 1", "wait:30001", "wait:999999999999999999999", "wait:1x", "key:", "key:\t",
        "key:\x7f", "key:\xc3\xa4", "code:", "code:0,", "code:,1", "code:100", "code:0,1z", "code:0x1",
        "save:", "raw:", "load:x\n", "intro:encode:0", "intro:encode:14", "intro:encode:1:x",
        "intro:encode:1:q:x", "intro:pre:first:x", "intro:encode:x", "intro:unknown"};
    for (size_t i = 0; i < sizeof valid / sizeof *valid; i++) CHECK(!control_validate(valid[i], 0));
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(control_validate(bad[i], 1));
    const char *intro_only[] = {"tick:0", "hold:30000", "right:1", "raw:x"};
    for (size_t i = 0; i < sizeof intro_only / sizeof *intro_only; i++) {
        CHECK(control_validate(intro_only[i], 0));
        CHECK(!control_validate(intro_only[i], 1));
    }
    char text[300] = "key:";
    memset(text + 4, 'x', 129);
    text[132] = 0;
    CHECK(!control_validate(text, 0));
    text[132] = 'x'; text[133] = 0;
    CHECK(control_validate(text, 0));
    strcpy(text, "code:");
    for (int i = 0; i < 128; i++) strcat(text, i ? ",f" : "f");
    CHECK(!control_validate(text, 0));
    strcat(text, ",f");
    CHECK(control_validate(text, 0));

    ControlLine line = {0};
    const char *command = "wait:12\r\n";
    for (size_t i = 0; i < strlen(command); i++) CHECK(control_feed(&line, (unsigned char)command[i]) == (command[i] == '\n'));
    CHECK(!line.invalid && !strcmp(line.text, "wait:12"));
    memset(&line, 0, sizeof line);
    for (int i = 0; i < CONTROL_LINE_MAX; i++) CHECK(!control_feed(&line, 'x'));
    CHECK(control_feed(&line, '\n') && !line.invalid && strlen(line.text) == CONTROL_LINE_MAX);
    CHECK(!control_feed(&line, 'y') && line.invalid);
    CHECK(control_feed(&line, '\n') && line.invalid);
    memset(&line, 0, sizeof line);
    control_feed(&line, 0);
    CHECK(control_feed(&line, '\n') && line.invalid);
    memset(&line, 0, sizeof line);
    CHECK(control_feed(&line, '\n') && !line.invalid && !line.used);

    FILE *out = tmpfile();
    CHECK(out != NULL);
    if (out) {
        ControlState state = {3, 42, 59, 18, 3, 4, 1, 0};
        CHECK(!control_response(out, &state, 1, 1, "bad\n\"\\"));
        rewind(out);
        char result[512];
        CHECK(fgets(result, sizeof result, out) != NULL);
        CHECK(!strcmp(result, "{\"event\":\"ready\",\"ok\":false,\"seq\":3,\"frames\":42,\"screen\":59,\"day\":18,\"hour\":3,\"minute\":4,\"quit\":true,\"intro_active\":false,\"protocol\":1,\"window\":true,\"error\":\"bad\\u000a\\\"\\\\\"}\n"));
        fclose(out);
    }
    return failures != 0;
}
