#include "control.h"
#include <inttypes.h>
#include <string.h>

int control_feed(ControlLine *line, unsigned char c) {
    if (c == '\n') {
        if (line->used && line->text[line->used - 1] == '\r') line->used--;
        line->text[line->used] = 0;
        return 1;
    }
    if (!c || line->used == CONTROL_LINE_MAX) line->invalid = 1;
    else line->text[line->used++] = (char)c;
    return 0;
}

static int number(const char **p, unsigned max) {
    unsigned value = 0;
    const char *s = *p;
    if (*s < '0' || *s > '9') return -1;
    do {
        unsigned digit = (unsigned)(*s++ - '0');
        if (value > max / 10 || (value == max / 10 && digit > max % 10)) return -1;
        value = value * 10 + digit;
    } while (*s >= '0' && *s <= '9');
    *p = s;
    return (int)value;
}

const char *control_validate(const char *s, int intro_active) {
    if (!strcmp(s, "status") || !strcmp(s, "quit")) return NULL;
    if (!strncmp(s, "move:", 5) || !strncmp(s, "click:", 6)) {
        const char *p = strchr(s, ':') + 1;
        if (number(&p, 639) < 0 || *p++ != ',' || number(&p, 439) < 0 || *p)
            return "expected coordinates x=0..639,y=0..439";
        return NULL;
    }
    if (!strncmp(s, "wait:", 5) || !strncmp(s, "tick:", 5) ||
        !strncmp(s, "hold:", 5) || !strncmp(s, "right:", 6)) {
        const char *p = strchr(s, ':') + 1;
        if (number(&p, CONTROL_ADVANCE_MAX) < 0 || *p) return "expected frame count 0..30000";
        return s[0] != 'w' && !intro_active ? "command requires an active intro" : NULL;
    }
    if (!strncmp(s, "key:", 4)) {
        size_t n = strlen(s + 4);
        if (!n || n > 128) return "expected 1..128 printable ASCII characters";
        for (const unsigned char *p = (const unsigned char *)s + 4; *p; p++)
            if (*p < 32 || *p > 126) return "expected 1..128 printable ASCII characters";
        return NULL;
    }
    if (!strncmp(s, "code:", 5)) {
        const char *p = s + 5;
        unsigned count = 0;
        do {
            unsigned n = 0;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) p++, n++;
            if (!n || n > 2 || ++count > 128 || (*p && *p != ',')) return "expected 1..128 comma-separated hex bytes";
            if (!*p) return NULL;
            p++;
        } while (*p);
        return "expected 1..128 comma-separated hex bytes";
    }
    if (!strncmp(s, "shot:", 5) || !strncmp(s, "save:", 5) || !strncmp(s, "load:", 5) || !strncmp(s, "raw:", 4)) {
        const unsigned char *p = (const unsigned char *)strchr(s, ':') + 1;
        if (!*p) return "expected a nonempty path";
        for (; *p; p++) if (*p < 32 || *p == 127) return "path contains a control character";
        return s[0] == 'r' && !intro_active ? "command requires an active intro" : NULL;
    }
    if (!strncmp(s, "intro:", 6)) {
        const char *p = s + 6;
        if (!strcmp(p, "start") || !strcmp(p, "pre") || !strcmp(p, "pre:first") || !strcmp(p, "pre:again") ||
            !strcmp(p, "relo") || !strcmp(p, "bint") || !strcmp(p, "encode")) return NULL;
        if (!strncmp(p, "encode:", 7)) {
            p += 7;
            if (number(&p, 13) > 0 && (!*p || !strcmp(p, ":q") || !strcmp(p, ":d"))) return NULL;
        }
        return "unknown intro program or scene";
    }
    return "unknown command";
}

int control_response(FILE *out, const ControlState *s, int ready, int window, const char *error) {
    fprintf(out, "{\"event\":\"%s\",\"ok\":%s,\"seq\":%" PRIu64 ",\"frames\":%" PRIu64
            ",\"screen\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,\"quit\":%s,\"intro_active\":%s",
            ready ? "ready" : "response", error ? "false" : "true", s->seq, s->frames,
            s->screen, s->day, s->hour, s->minute, s->quit ? "true" : "false", s->intro_active ? "true" : "false");
    if (ready) fprintf(out, ",\"protocol\":1,\"window\":%s", window ? "true" : "false");
    if (error) {
        fputs(",\"error\":\"", out);
        for (const unsigned char *p = (const unsigned char *)error; *p; p++) {
            if (*p < 32) fprintf(out, "\\u%04x", *p);
            else { if (*p == '"' || *p == '\\') fputc('\\', out); fputc(*p, out); }
        }
        fputc('"', out);
    }
    fputs("}\n", out);
    return fflush(out) || ferror(out);
}
