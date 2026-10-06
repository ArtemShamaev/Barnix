#include <assert.h>
#include <stdio.h>
#include "font.c"
int main(void)
{
    unsigned int previous = 0;
    for (unsigned int y = 0; y < 7; y++) {
        unsigned int slash = latin_row('/', y);
        unsigned int backslash = latin_row('\\', 6 - y);
        /* A continuous diagonal: one pixel per row, no clipped/stray dots,
         * moving at most one column, and mirrored for the backslash. */
        assert(slash && !(slash & (slash - 1)));
        assert(slash == backslash);
        if (y) assert(slash == previous || slash == previous * 2);
        previous = slash;
    }
    assert(latin_row('/', 0) < latin_row('/', 6));
    puts("Slash glyph tests passed");
    return 0;
}
