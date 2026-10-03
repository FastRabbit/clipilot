# Tab completion

Implemented in [`src/cli_complete.c`](../src/cli_complete.c), invoked from the
`TAB` binding in the line editor.

## What gets completed

| Position on the line | Candidates |
|---|---|
| first word | application commands + built-ins |
| a word directly after a node that has subcommands | that node's subcommand names |
| an argument position whose spec is `CLI_ARG_ENUM` | that spec's `enum_values` |
| anything else | none |

Completion deliberately does **not** guess values for `int`, `str`, `hex` or
`float` arguments — there is nothing sensible to offer, and a beep is more
informative than a wrong insertion. Making an argument an enum is the way to
make it completable.

## Algorithm

1. Scan left from the cursor to the preceding space; the text between is the
   **prefix**, and everything before it is split into complete **words**.
   Completion therefore works mid-line, not just at the end — the text to the
   right of the cursor is ignored.
2. `index` = number of complete words before the prefix. `index == 0` means the
   command position.
3. For `index > 0`, walk the tree from `words[0]`, descending while the node
   has subcommands and the next word matches one. `k` ends up as the number of
   words consumed by the walk.
   * If the walk fails to match, there are no candidates.
   * If `k == index` and the node has subcommands, offer them.
   * Otherwise the prefix is at argument position `index - k`; offer the enum
     values if that spec (or the trailing variadic spec) is an enum.
4. Filter candidates by the prefix, capped at `CLI_MAX_COMPLETIONS`.

Candidate matching uses `memcmp` over the prefix length and is
case-**sensitive**, unlike enum *conversion*, which is case-insensitive. So
`ID<TAB>` will not complete `idle`, but typing `IDLE` still converts fine.

## Acting on the candidate set

| Count | Behaviour |
|---|---|
| 0 | beep (`\a`), line unchanged |
| 1 | insert the remainder **plus a trailing space**, ready for the next word |
| >1, longest common prefix is longer than what was typed | insert the LCP only |
| >1, LCP adds nothing | list the candidates, then repaint the line |

That last row is the subtle one. With commands `flag` and `fail`, typing `f`
and pressing TAB has an LCP of exactly `f` — nothing can be inserted. The
correct response is to **list**, not to beep: candidates do exist. Beeping is
reserved for a genuinely empty candidate set. Getting this backwards produces a
CLI that appears to have no completion for exactly the cases where the user
needs it most.

Listing prints the names two spaces apart, wrapping at `CLI_LIST_WIDTH` (72)
columns, then calls `cli_edit_refresh()` so the prompt and the partially typed
line reappear below.

## Limits

* `CLI_MAX_COMPLETIONS` bounds the candidate array, which is a stack array of
  `const char *`. Extra matches beyond the cap are dropped silently, which can
  make the listed set incomplete — raise the cap if a table is large.
* A line already at `CLI_MAX_TOKENS` words does nothing.
* Insertion goes through `cli_edit_insert()`, so it respects
  `CLI_MAX_LINE_LEN` and beeps if the completion does not fit.
