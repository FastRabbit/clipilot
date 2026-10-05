---
description: Branch naming and commit message rules enforced by the global git hooks. Load before creating any branch or commit in this repository.
---

# Git Workflow

A global hook path is configured (`core.hooksPath = C:/DevProg/.githooks`) with
`pre-commit` and `commit-msg` hooks that enforce Siemens conventional-changelog
rules. They reject non-compliant input, so get the format right the first time.
**Never bypass them with `--no-verify`** — fix the message or the branch name.

## Branch names

`pre-commit` allows `main`/`master` only for the very first commit of an empty
repository. Every later commit must be on a branch matching:

```
<type>/[RTC123456-|123456-|1.2.3-]<lowercase-description-with-dashes>
```

Types: `feat`, `fix`, `devel`, `docs`, `style`, `refactor`, `perf`, `test`,
`chore`, `build`, `ci`, `revert`. Max 72 characters.

```
feat/RTC123456-add-uart-command     good
fix/123456-completion-off-by-one    good
docs/update-porting-guide           good
feature/add-stuff                   rejected - invalid type
feat/Add_Stuff                      rejected - underscore, uppercase start
```

## Commit messages

`commit-msg` checks three things:

1. **Title ≤ 50 characters.**
2. **Title matches** `^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-z0-9-]+\))?: [a-z][A-Za-z0-9 ._-]*$`
   — optional lowercase scope, subject starts lowercase, no trailing period,
   and **no characters outside `[A-Za-z0-9 ._-]`** in the subject (so no commas,
   parentheses, colons or slashes after the type).
3. **Every body line ≤ 72 characters.** This is the rule that bites most often;
   bullet lists and pasted log output overflow easily.

Separate title and body with a blank line. Describe the *why*, not a file list.

```
feat: add embedded CLI library for MCUs

Introduce clipilot, a command line interface for microcontrollers such
as the STM32 family. The library is allocation free and needs only two
hooks: a non-blocking character source and a character sink.

Features:

* Commands with arbitrarily nested subcommands and typed arguments.
* Line editing, history and tab completion over commands and
  subcommands.

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

Include the `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`
trailer on agent-authored commits. Trailer lines are exempt from the 72-column
limit in practice only if they fit — keep them as the last lines of the body.

## Landing work on main

You cannot commit on `main`, and you also cannot *merge* onto it normally: a
merge commit is still a commit on `main`, so `pre-commit` rejects it. Use a
**fast-forward**, which moves the branch pointer without creating one:

```powershell
git checkout -b docs/my-change      # or feat/..., fix/...
# ... edit, then commit with -F as above ...
git checkout main
git merge --ff-only docs/my-change
git branch -d docs/my-change
```

This keeps history linear and is the workflow every commit in this repo has
used. If `--ff-only` refuses, `main` has moved on — rebase the branch rather
than reaching for a merge commit.

## Remote

`origin` is a bare repository on a local drive: `E:/clipilot.bare`.

That drive does not record ownership, so the first push fails with *"detected
dubious ownership"*. The fix is the one git prints, and it is already applied
on this machine:

```powershell
git config --global --add safe.directory E:/clipilot.bare
```

Pushes emit an unrelated `safe.directory ''e:\git-repos\peaky-II.git\'' not
absolute` warning from a pre-existing global-config entry. It is harmless and
not ours to fix.

Because `origin` is a plain directory, adding it as a submodule elsewhere on
this machine needs `git -c protocol.file.allow=always submodule add ...`;
git refuses `file://` transport for submodules by default.

## Practical tips

* Write the message to a file and commit with `git commit -F <file>` rather
  than fighting PowerShell quoting with `-m`. Put the file outside the repo.
* Pre-flight the length rules before invoking git. Skip the last line so the
  84-character `Co-authored-by` trailer does not raise a false positive:

  ```powershell
  $l = Get-Content $msg
  "title: " + $l[0].Length
  $l | Select-Object -SkipLast 1 | ForEach-Object {
      if ($_.Length -gt 72) { "TOO LONG ($($_.Length)): $_" }
  }
  ```

  Re-run it after every edit. Rewrapping one line easily pushes the next over.
* Always disable the pager: `git --no-pager log`, `git --no-pager status`.
* `build/` is git-ignored; never stage build output or scratch files.
