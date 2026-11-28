// Markdown (changelog / release notes) -> display lines for the game's TextPanel.
// Pure C (no Windows calls), so it can be tested natively.
#ifndef WL_MDTEXT_H
#define WL_MDTEXT_H

#include <stddef.h>

typedef struct {
    char **line;      // each line: plain text, or "<b>...<b>" for headings (game markup 0x773f70:
                      // "b" is a toggle tag, the same "<b>" opens and closes; "</b>" is an unknown tag)
    int count, cap;
} MdLines;

// Appends the lines of `md` wrapped at `width` characters:
//   "# x" / "## x" / "### x" -> bold line (a blank line before it); `skip_h1` drops "# x" lines.
//   "- x" / "* x" / "+ x"    -> "  - x", nested lists indented further, wrapped lines indented
//   **, __ and ` marks are removed, [text](url) becomes text, '<' becomes '(' and '>' becomes
//   ')' ("->" / "=>" lose the '>'): the game's markup parser 0x773f70 reads '<' as a tag start and
//   any '>' as a tag end. '&' becomes '+' (the game eats it), invalid
//   UTF-8 bytes become '?' (the game's decoder 0x444e40 hangs on them, see FINDINGS).
void md_to_lines(MdLines *out, const char *md, int width, int skip_h1);
void md_add_line(MdLines *out, const char *text);   // one line as it is (already clean)
void md_free(MdLines *l);

#endif
