#include "test.h"
#include "../src/fuzzy.h"

#include <string.h>

void test_fuzzy(void) {
    SUITE("fuzzy_score -- basic");

    // empty pattern is defined as 0, not a match failure
    CHECK(fuzzy_score("", "firefox", FUZZY_CTX_NAME) == 0,
          "empty pattern returns 0");

    // kernel threads are always rejected
    CHECK(fuzzy_score("kworker", "[kworker/0:0]", FUZZY_CTX_NAME) == -1,
          "kernel thread returns -1");

    // pattern that cannot match
    CHECK(fuzzy_score("zzz", "firefox", FUZZY_CTX_NAME) == -1,
          "no match returns -1");

    // pattern longer than str with no match
    CHECK(fuzzy_score("firefoxbrowser", "firefox", FUZZY_CTX_NAME) == -1,
          "overlong unmatched pattern returns -1");

    SUITE("fuzzy_score -- scoring order");

    // exact match should outscore a prefix match on the same string
    int exact = fuzzy_score("firefox", "firefox", FUZZY_CTX_NAME);
    int prefix = fuzzy_score("fire", "firefox", FUZZY_CTX_NAME);
    CHECK(exact > prefix, "exact match outscores prefix match");

    // prefix match should outscore a mid-string match
    int mid = fuzzy_score("fox", "firefox", FUZZY_CTX_NAME);
    CHECK(prefix > mid, "prefix match outscores mid-string match");

    // consecutive characters should outscore scattered ones
    int consec = fuzzy_score("fir", "firefox", FUZZY_CTX_NAME);
    int scattered = fuzzy_score("ffx", "firefox", FUZZY_CTX_NAME);
    CHECK(consec > scattered, "consecutive match outscores scattered match");

    SUITE("fuzzy_score -- context bonus");

    // FUZZY_CTX_NAME adds 50, so the same pattern scores higher against name ctx
    int name_score = fuzzy_score("fire", "firefox", FUZZY_CTX_NAME);
    int cmd_score = fuzzy_score("fire", "firefox", FUZZY_CTX_CMDLINE);
    CHECK(name_score > cmd_score, "name ctx scores higher than cmdline ctx");

    SUITE("fuzzy_score -- substitution");

    // substitution only applies for patterns >= 5 chars
    CHECK(fuzzy_score("abcd", "abce", FUZZY_CTX_NAME) == -1,
          "substitution not applied for pattern shorter than 5");

    // 'a' and 'e' are in the same group ("ae") — "firafox" vs "firefox"
    int with_sub = fuzzy_score("firafox", "firefox", FUZZY_CTX_NAME);
    CHECK(with_sub != -1, "substitution applied for pattern >= 5 chars");

    // a clean match should outscore a substitution match
    int clean = fuzzy_score("firefox", "firefox", FUZZY_CTX_NAME);
    CHECK(clean > with_sub, "clean match outscores substitution match");

    SUITE("fuzzy_apply_proc_bonus");

    CHECK(fuzzy_apply_proc_bonus(100, NULL) == 100,
          "NULL proc returns score unchanged");

    process_info_t root_proc = {0};
    strncpy(root_proc.owner, "root", sizeof(root_proc.owner) - 1);
    int root_score = fuzzy_apply_proc_bonus(100, &root_proc);
    CHECK(root_score < 100, "root process receives penalty");
    CHECK(root_score == 70, "root penalty is exactly -30");

    // RAM bonus caps at 20
    process_info_t ram_proc = {0};
    strncpy(ram_proc.owner, "nobody", sizeof(ram_proc.owner) - 1);
    ram_proc.ram = 999999;
    CHECK(fuzzy_apply_proc_bonus(0, &ram_proc) == 20, "RAM bonus capped at 20");

    // zero RAM adds nothing
    process_info_t noram_proc = {0};
    strncpy(noram_proc.owner, "nobody", sizeof(noram_proc.owner) - 1);
    noram_proc.ram = 0;
    CHECK(fuzzy_apply_proc_bonus(50, &noram_proc) == 50, "zero RAM adds no bonus");
}
