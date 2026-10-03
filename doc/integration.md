# Integrating clipilot into another project

clipilot is a source library: five `.c` files and two headers, C11, no
dependencies beyond a freestanding libc. There is nothing to install and no
binary to distribute — you add the sources to your build.

> **Read [the configuration warning](#the-one-real-trap) before you override any
> `CLI_*` macro.** It is the only way to break an integration silently, and it
> is easy to do by accident.

## Choosing a method

| Method | Use when | Upgrades |
|---|---|---|
| [Git submodule](#git-submodule) | you want a pinned, auditable version and your team is comfortable with submodules | `git -C ext/clipilot pull` |
| [Git subtree](#git-subtree) | you want the sources committed in your repo so clones need no extra steps | `git subtree pull` |
| [CMake FetchContent](#cmake-fetchcontent) | the consumer is CMake-based and you prefer no repository plumbing | edit the tag |
| [Vendored copy](#vendored-copy) | the build is an IDE project (STM32CubeIDE, IAR, Keil) or a plain Makefile | manual |

All four end up compiling the same five files. The difference is only how the
sources arrive and how you get updates.

`find_package(clipilot)` is **not** supported — the project deliberately ships
no `install()` rules, because an MCU library is consumed as source, compiled
with the firmware's own flags.

## Git submodule

The version is pinned by commit, and your repository records which one.

```sh
git submodule add https://<host>/clipilot.git external/clipilot
git commit -m "build: add clipilot as a submodule"
```

Anyone cloning afterwards needs:

```sh
git clone --recurse-submodules <your-repo>
# or, in an existing clone:
git submodule update --init --recursive
```

That extra step is the one drawback — a forgotten `--recurse-submodules`
produces an empty directory and a confusing build error. To update:

```sh
git -C external/clipilot fetch
git -C external/clipilot checkout <tag-or-commit>
git add external/clipilot && git commit -m "build: update clipilot"
```

## Git subtree

The sources are committed into your repository, so a plain `git clone` is
enough and CI needs no submodule handling. The cost is a larger repository and
a slightly more involved update.

```sh
git subtree add --prefix external/clipilot \
    https://<host>/clipilot.git main --squash
```

Update later with the same `--prefix`:

```sh
git subtree pull --prefix external/clipilot \
    https://<host>/clipilot.git main --squash
```

Prefer this when the firmware repository must be self-contained — for example
when builds run on an isolated machine with no access to the clipilot remote.

## CMake FetchContent

No repository plumbing at all; CMake clones at configure time.

```cmake
include(FetchContent)

FetchContent_Declare(clipilot
    GIT_REPOSITORY https://<host>/clipilot.git
    GIT_TAG        <commit-or-tag>        # pin it; never use a branch name
)

set(CLI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CLI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clipilot)

target_link_libraries(my_firmware PRIVATE clipilot::clipilot)
```

Pin `GIT_TAG` to a commit or tag. A branch name makes the build
non-reproducible: two engineers can compile different code from identical
sources.

Note this requires network access at configure time, which some build
environments forbid.

## Vendored copy

Copy `src/` and `include/` into your tree. Appropriate for IDE-managed
projects, and the least machinery — at the cost of no automated upgrade path.
Record the version you copied in a README so a future upgrade is possible.

## Wiring it into the build

### CMake

Both the submodule and subtree layouts use `add_subdirectory`:

```cmake
set(CLI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CLI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/clipilot)

target_link_libraries(my_firmware PRIVATE clipilot::clipilot)
```

Turn the two options off so the consumer build does not try to build the host
demo and the test suite for your MCU target — they are host programs and will
not link for a cross build.

Linking `clipilot::clipilot` brings the include directory with it; no
`target_include_directories` is needed.

### Plain Makefile

```make
CLIPILOT := external/clipilot
CFLAGS   += -I$(CLIPILOT)/include
SRCS     += $(CLIPILOT)/src/cli.c \
            $(CLIPILOT)/src/cli_parse.c \
            $(CLIPILOT)/src/cli_edit.c \
            $(CLIPILOT)/src/cli_complete.c \
            $(CLIPILOT)/src/cli_builtin.c
```

### STM32CubeIDE

1. Copy or submodule the project under the firmware project directory.
2. **Project → Properties → C/C++ General → Paths and Symbols → Includes** —
   add `clipilot/include`.
3. **Source Location** — add `clipilot/src`, or ensure the folder is not
   excluded from the build.
4. Put any `CLI_*` overrides under **Symbols** on the same page, so they apply
   to the whole project.

Step 4 matters — see below.

## The one real trap

**Every translation unit must see identical `CLI_*` macro values**, because
they determine `sizeof(cli_t)`. If the library is compiled with the defaults
and your application with overrides, the two disagree about the size of the
instance you allocate — and `cli_init()` zeroes it using the *library's* idea
of the size, overrunning your object.

This is easy to do accidentally. The natural-looking CMake below is **wrong**:

```cmake
add_subdirectory(external/clipilot)
add_executable(my_firmware src/main.c)
target_link_libraries(my_firmware PRIVATE clipilot::clipilot)

# WRONG: applies to my_firmware only, not to the clipilot sources
target_compile_definitions(my_firmware PRIVATE CLI_HISTORY_DEPTH=4)
```

Compiled exactly as written, the application sees `sizeof(cli_t) == 1200` while
the library sees `1716`. A `static cli_t` in the application is then 516 bytes
too small, and the `memset()` inside `cli_init()` corrupts whatever follows it.
Nothing warns; the symptom is unrelated data changing at start-up.

Put the definitions on the **library** target as `PUBLIC` instead, so they
apply to the library's own sources *and* propagate to everything that links it:

```cmake
add_subdirectory(external/clipilot)

# RIGHT: library and all consumers agree
target_compile_definitions(clipilot PUBLIC
    CLI_HISTORY_DEPTH=4
    CLI_ENABLE_FLOAT=0
)
```

The equivalents elsewhere: a global `CFLAGS` entry in a Makefile, and the
project-wide **Symbols** page in an IDE — never a per-file setting.

If you would rather not rely on getting the build right, edit
`include/cli_config.h` directly in a vendored copy. The macros are all
`#ifndef`-guarded, so changing the defaults there is supported and applies
everywhere by construction.

## Verifying the integration

After wiring it up, check that the two views agree:

```c
printf("sizeof(cli_t)=%u\n", (unsigned)sizeof(cli_t));
```

Compare against the figures in [configuration](configuration.md) — 1716 bytes
for the defaults on a 64-bit host, less on a 32-bit target. If you overrode
macros and this number did not change, they are not reaching the library.

Then confirm the console itself works with a minimal command, as shown in
[usage](usage.md#a-first-command).

## Licence and provenance

Record the upstream URL and the pinned commit in your firmware documentation.
For a submodule, `git submodule status` reports it; for a vendored copy,
nothing does unless you write it down.
