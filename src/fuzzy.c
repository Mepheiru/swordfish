#include "fuzzy.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

#if defined(__x86_64__)
#  define PREFETCH(p) __asm__ volatile("prefetcht0 %0"::"m"(*(const char*)(p)))
#else
#  define PREFETCH(p) ((void)0)
#endif

static inline char to_lower(char c) {
    return (unsigned char)c >= 'A' && (unsigned char)c <= 'Z' ? c + 32 : c;
}

static uint32_t group_bits[128];
static bool groups_ready = false;

static void init_groups(void) {
    static const char *groups[] = {
        "io", "ou", "ei", "ae",
        "qwa", "wqse", "ewrd", "rewft", "trgy",
        "yuhi", "uijo", "iokp", "op",
        "azsq", "sazdx", "dxcfe", "fvgd", "gbhf",
        "hnjg", "jmkh", "knlj", "lk",
        "zxas", "xcvz", "cvbx", "vbnc", "bnm",
        NULL
    };
    for (int g = 0; groups[g]; g++) {
        uint32_t bit = 1u << g;
        for (const char *c = groups[g]; *c; c++)
            group_bits[(unsigned char)*c] |= bit;
    }
    groups_ready = true;
}

static bool is_word_boundary(const char *s) {
    char prev = *(s - 1);
    return prev == '_' || prev == '-' || prev == ' ' || (to_lower(prev) != prev && to_lower(*s) == *s);
}

static bool chars_similar(char a, char b) {
    a = to_lower(a);
    b = to_lower(b);
    if (a == b) return true;
    if ((unsigned char)a >= 128 || (unsigned char)b >= 128) return false;
    return (group_bits[(unsigned char)a] & group_bits[(unsigned char)b]) != 0;
}

static const char *current_username(void) {
    static char buf[64] = {0};
    if (buf[0]) return buf;
    struct passwd *pw = getpwuid(getuid());
    if (pw) strncpy(buf, pw->pw_name, sizeof(buf) - 1);
    return buf;
}

__attribute__((hot))
int fuzzy_score(const char *pattern, const char *str, fuzzy_ctx_t ctx) {
    if (!pattern || !str || !*pattern) return 0;
    if (str[0] == '[') return -1;

    if (!groups_ready) init_groups();

    // pre-lowercase pattern once so the hot loop only lowercases *s per iteration
    char pat_low[256];
    int pat_len = 0;
    for (; pattern[pat_len] && pat_len < 255; pat_len++)
        pat_low[pat_len] = to_lower(pattern[pat_len]);
    pat_low[pat_len] = '\0';

    int str_len = (int)strlen(str);

    int score = 0;
    int consecutive = 0;
    int substitutions_left = 1;
    bool had_substitution = false;
    const char *p = pat_low;
    const char *s = str;
    bool first = true;

    while (*p && *s) {
        char sl = to_lower(*s);
        if (*p == sl) {
            score += 10;
            score += consecutive * 5;
            if (first)
                score += 20;
            else if (s > str && is_word_boundary(s))
                score += 15;
            consecutive++;
            p++;
            first = false;
        } else if (chars_similar(*p, *s) && substitutions_left > 0 && pat_len >= 5) {
            had_substitution = true;
            substitutions_left--;
            consecutive = 0;
            p++;
            first = false;
        } else {
            consecutive = 0;
            first = false;
        }
        s++;
    }

    if (*p != '\0') return -1;

    if (pat_len == str_len && strncasecmp(pat_low, str, pat_len) == 0)
        score += 80;

    score -= str_len;

    if (ctx == FUZZY_CTX_NAME)
        score += 50;

    if (had_substitution)
        score /= 3;

    return score;
}

/* Per-process bonuses — applied after the hot loop, only on matched results. */
int fuzzy_apply_proc_bonus(int score, const process_info_t *proc) {
    if (!proc) return score;

    if (strcmp(proc->owner, "root") == 0)
        score -= 30;

    const char *me = current_username();
    if (me[0] && strcmp(proc->owner, me) == 0)
        score += 20;

    if (proc->ram > 0) {
        int ram_bonus = (int)(proc->ram / 100);
        if (ram_bonus > 20) ram_bonus = 20;
        score += ram_bonus;
    }

    if (proc->start_time > 0) {
        int recency = (int)(proc->start_time / 1000000) % 10;
        score += recency;
    }

    return score;
}
