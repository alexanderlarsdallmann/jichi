/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* pd_core.c - pure core of ptydrive: script parsing, escape decoding, and
 * the streaming substring matcher. Libc-only; the PTY shell is ptydrive.c.
 * Unit-tested in tests/test_ttools.c. */

#include "pd_core.h"
#include "jc_snprintf.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

static int pd_err(char *err, size_t errcap, int line, const char *msg)
{
    if (err != NULL && errcap > 0)
        jc_snprintf(err, errcap, "line %d: %s", line, msg);
    return -1;
}

static const char *pd_skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

int pd_unescape(const char *in, char *out, size_t cap, size_t *outlen)
{
    size_t n = 0;
    while (*in != '\0') {
        char c = *in++;
        if (c == '\\') {
            char e = *in++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '\\': c = '\\'; break;
            case '"': c = '"'; break;
            case 'e': c = '\033'; break;
            case 'x': {
                int hi, lo;
                hi = (unsigned char)*in;
                if (hi >= '0' && hi <= '9') hi -= '0';
                else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
                else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
                else return -1;
                in++;
                lo = (unsigned char)*in;
                if (lo >= '0' && lo <= '9') lo -= '0';
                else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
                else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
                else return -1;
                in++;
                c = (char)((hi << 4) | lo);
                break;
            }
            default:
                return -1;      /* unknown escape (incl. trailing '\') */
            }
        }
        if (n + 1 >= cap)
            return -1;
        out[n++] = c;
    }
    out[n] = '\0';
    if (outlen != NULL)
        *outlen = n;
    return 0;
}

const char *pd_match(const char *buf, size_t len,
                     const char *pat, size_t patlen)
{
    size_t i;
    if (patlen == 0 || patlen > len)
        return NULL;
    for (i = 0; i + patlen <= len; i++) {
        if (buf[i] == pat[0] && memcmp(buf + i, pat, patlen) == 0)
            return buf + i;
    }
    return NULL;
}

int pd_signal_from_name(const char *name)
{
    if (strcmp(name, "TERM") == 0)
        return SIGTERM;
    if (strcmp(name, "INT") == 0)
        return SIGINT;
    if (strcmp(name, "HUP") == 0)
        return SIGHUP;
    if (strcmp(name, "KILL") == 0)
        return SIGKILL;
    return -1;
}

/* Extract a quoted, escaped argument. On success returns a malloc'd decoded
 * buffer (length in *outlen) and advances *p past the closing quote. */
static char *pd_quoted(const char **p, size_t *outlen)
{
    const char *s = *p;
    const char *end;
    char *raw;
    char *dec;
    size_t rawlen;

    if (*s != '"')
        return NULL;
    s++;
    /* find the closing quote, honouring backslash escapes */
    end = s;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && end[1] != '\0')
            end++;
        end++;
    }
    if (*end != '"')
        return NULL;
    rawlen = (size_t)(end - s);
    raw = (char *)malloc(rawlen + 1);
    if (raw == NULL)
        return NULL;
    memcpy(raw, s, rawlen);
    raw[rawlen] = '\0';
    dec = (char *)malloc(rawlen + 1);
    if (dec == NULL) {
        free(raw);
        return NULL;
    }
    if (pd_unescape(raw, dec, rawlen + 1, outlen) != 0) {
        free(raw);
        free(dec);
        return NULL;
    }
    free(raw);
    *p = end + 1;
    return dec;
}

static int pd_push(struct pd_script *s, const struct pd_cmd *c)
{
    struct pd_cmd *n = (struct pd_cmd *)realloc(s->cmds,
            (size_t)(s->ncmds + 1) * sizeof(*n));
    if (n == NULL)
        return -1;
    s->cmds = n;
    s->cmds[s->ncmds++] = *c;
    return 0;
}

int pd_script_parse(const char *text, struct pd_script *out,
                    char *err, size_t errcap)
{
    const char *p = text;
    int line = 0;

    out->cmds = NULL;
    out->ncmds = 0;
    if (err != NULL && errcap > 0)
        err[0] = '\0';

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t linelen = (eol != NULL) ? (size_t)(eol - p) : strlen(p);
        char linebuf[1024];
        const char *s;
        struct pd_cmd c;

        line++;
        if (linelen >= sizeof(linebuf)) {
            pd_script_free(out);
            return pd_err(err, errcap, line, "line too long");
        }
        memcpy(linebuf, p, linelen);
        linebuf[linelen] = '\0';
        p = (eol != NULL) ? eol + 1 : p + linelen;

        s = pd_skip_ws(linebuf);
        if (*s == '\0' || *s == '#')
            continue;

        memset(&c, 0, sizeof(c));
        c.line = line;

        if (strncmp(s, "expect ", 7) == 0) {
            const char *q = pd_skip_ws(s + 7);
            size_t plen = 0;
            char *pat = pd_quoted(&q, &plen);
            long secs = 10;
            if (pat == NULL || plen == 0) {
                free(pat);
                pd_script_free(out);
                return pd_err(err, errcap, line,
                              "expect needs a non-empty quoted pattern");
            }
            q = pd_skip_ws(q);
            if (*q != '\0' && *q != '#')
                secs = strtol(q, NULL, 10);
            if (secs < 1) {
                free(pat);
                pd_script_free(out);
                return pd_err(err, errcap, line, "expect timeout < 1s");
            }
            c.kind = PD_CMD_EXPECT;
            c.text = pat;
            c.text_len = plen;
            c.a = secs;
        } else if (strncmp(s, "send ", 5) == 0) {
            const char *q = pd_skip_ws(s + 5);
            size_t blen = 0;
            char *bytes = pd_quoted(&q, &blen);
            if (bytes == NULL || blen == 0) {
                free(bytes);
                pd_script_free(out);
                return pd_err(err, errcap, line,
                              "send needs a non-empty quoted string");
            }
            c.kind = PD_CMD_SEND;
            c.text = bytes;
            c.text_len = blen;
        } else if (strncmp(s, "delay ", 6) == 0 ||
                   strncmp(s, "drain ", 6) == 0) {
            long ms = strtol(s + 6, NULL, 10);
            if (ms < 1) {
                pd_script_free(out);
                return pd_err(err, errcap, line, "duration must be >= 1ms");
            }
            c.kind = (s[1] == 'e') ? PD_CMD_DELAY : PD_CMD_DRAIN;
            c.a = ms;
        } else if (strncmp(s, "winsize ", 8) == 0) {
            char *end = NULL;
            long rows = strtol(s + 8, &end, 10);
            long cols = (end != NULL) ? strtol(end, NULL, 10) : 0;
            if (rows < 1 || cols < 1) {
                pd_script_free(out);
                return pd_err(err, errcap, line, "winsize needs ROWS COLS");
            }
            c.kind = PD_CMD_WINSIZE;
            c.a = rows;
            c.b = cols;
        } else if (strncmp(s, "signal ", 7) == 0) {
            int sig = pd_signal_from_name(pd_skip_ws(s + 7));
            if (sig < 0) {
                pd_script_free(out);
                return pd_err(err, errcap, line,
                              "signal needs TERM|INT|HUP|KILL");
            }
            c.kind = PD_CMD_SIGNAL;
            c.a = sig;
        } else if (strncmp(s, "waitexit", 8) == 0 &&
                   (s[8] == '\0' || s[8] == ' ')) {
            long secs = 10;
            const char *q = pd_skip_ws(s + 8);
            if (*q != '\0' && *q != '#')
                secs = strtol(q, NULL, 10);
            if (secs < 1) {
                pd_script_free(out);
                return pd_err(err, errcap, line, "waitexit timeout < 1s");
            }
            c.kind = PD_CMD_WAITEXIT;
            c.a = secs;
        } else if (strncmp(s, "assertexit ", 11) == 0) {
            c.kind = PD_CMD_ASSERTEXIT;
            c.a = strtol(s + 11, NULL, 10);
        } else {
            pd_script_free(out);
            return pd_err(err, errcap, line, "unknown command");
        }

        if (pd_push(out, &c) != 0) {
            free(c.text);
            pd_script_free(out);
            return pd_err(err, errcap, line, "out of memory");
        }
    }

    if (out->ncmds == 0)
        return pd_err(err, errcap, line, "empty script");
    return 0;
}

void pd_script_free(struct pd_script *s)
{
    int i;
    if (s == NULL || s->cmds == NULL) {
        if (s != NULL)
            s->ncmds = 0;
        return;
    }
    for (i = 0; i < s->ncmds; i++)
        free(s->cmds[i].text);
    free(s->cmds);
    s->cmds = NULL;
    s->ncmds = 0;
}
