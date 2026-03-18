#include "fuzzy.h"

#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#define NAME_BONUS    50
#define BASE_MATCH    10
#define CONSEC_BONUS   5
#define PREFIX_BONUS  20

int fuzzy_score(const char *pattern, const char *str, fuzzy_ctx_t ctx) {
    if (!pattern || !str || !*pattern) return 0;

    int score       = 0;
    int consecutive = 0;
    const char *p   = pattern;
    const char *s   = str;
    bool first      = true;

    while (*p && *s) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            score += BASE_MATCH;
            score += consecutive * CONSEC_BONUS;
            if (first) score += PREFIX_BONUS;
            consecutive++;
            p++;
            first = false;
        } else {
            consecutive = 0;
            first = false;
        }
        s++;
    }

    if (*p != '\0') return -1;
    score -= (int)strlen(str);

    if (ctx == FUZZY_CTX_NAME)
        score += NAME_BONUS;

    return score;
}
