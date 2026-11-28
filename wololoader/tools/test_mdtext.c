// Native test for src/mdtext.c (Markdown -> changelog lines for the game's TextPanel).
//   gcc -O2 -Wall -Wextra -std=c11 -o /tmp/t tools/test_mdtext.c src/mdtext.c && /tmp/t [CHANGELOG.md]
// With a file argument it also prints that file's lines and checks every line is safe for the game.
#include "../src/mdtext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(const MdLines *l, int i, const char *want) {
    const char *got = i < l->count ? l->line[i] : "(missing)";
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d: got [%s] want [%s]\n", i, got, want);
        fails++;
    }
}

// Every line must be valid for the game: no '<' except the "<b>" toggles, no '>' outside them,
// no '&', no control bytes.
static void check_safe(const MdLines *l) {
    for (int i = 0; i < l->count; i++) {
        const char *s = l->line[i];
        for (const char *p = s; *p; p++) {
            if (*p == '<') {
                if (strncmp(p, "<b>", 3) != 0) { printf("FAIL unsafe '<' in [%s]\n", s); fails++; break; }
                p += 2;
                continue;
            }
            if (*p == '>' || *p == '&' || (unsigned char)*p < 0x20) {
                printf("FAIL unsafe char in [%s]\n", s);
                fails++;
                break;
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *md =
        "# Wololoader changelog\n"
        "\n"
        "## 0.7.2\n"
        "\n"
        "- **Multiplayer fixes**:\n"
        "  - The match `keeps` the enemy -> no sudden victory.\n"
        "- Use <b> & [the docs](https://example.com) here\n"
        "\n"
        "## 0.7.1\n"
        "Plain paragraph text that is long enough to be wrapped onto a second line for sure.\n";
    MdLines l = { 0 };
    md_to_lines(&l, md, 40, 1);
    expect(&l, 0, "<b>0.7.2<b>");
    expect(&l, 1, "");
    expect(&l, 2, "  - Multiplayer fixes:");
    expect(&l, 3, "      - The match keeps the enemy - no");
    expect(&l, 4, "        sudden victory.");
    expect(&l, 5, "  - Use (b) + the docs here");
    expect(&l, 6, "");
    expect(&l, 7, "<b>0.7.1<b>");
    expect(&l, 8, "Plain paragraph text that is long enough");
    expect(&l, 9, "to be wrapped onto a second line for");
    expect(&l, 10, "sure.");
    if (l.count != 11) { printf("FAIL count %d\n", l.count); fails++; }
    check_safe(&l);
    md_free(&l);

    // Invalid UTF-8 must not reach the game (its decoder hangs on it).
    md_to_lines(&l, "- bad \xff byte \xc3\xa9", 80, 0);
    expect(&l, 0, "  - bad ? byte \xc3\xa9");
    md_free(&l);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 2; }
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        md_to_lines(&l, buf, 100, 1);
        for (int i = 0; i < l.count; i++) printf("%s\n", l.line[i]);
        check_safe(&l);
        md_free(&l);
    }
    printf(fails ? "%d FAILED\n" : "mdtext: all tests passed\n", fails);
    return fails != 0;
}
