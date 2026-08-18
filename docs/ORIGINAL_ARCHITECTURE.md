# Original BasicBasic 1.81 architecture

## What the supplied files are

The original manual identifies these distinct components:

| File | Role |
| --- | --- |
| `BB.EXE` | Standalone DOS compiler |
| `WBB.EXE` | Standalone Windows compiler |
| `BBE.EXE` / `WBBE.EXE` | DOS and Windows development editors |
| `BB181.STB` | Launcher stub copied verbatim into every generated application |
| `BBL181.EXE` | DOS runtime module |
| `WBBL181.EXE` | Windows 3.x runtime module |
| `WBB5181.EXE` | Windows 95 runtime module with long-filename support |

This means `wbb5181.exe.c` is a decompilation of the Windows 95 runtime and
interpreter, not of the compiler front end.

## Verified executable packaging

`BB181.STB` is 19,793 bytes. A byte-for-byte comparison with a generated
`SAMPLE16.EXE` shows that all 19,793 bytes are an identical prefix, with a
445-byte application payload appended. Other generated executables have the
same prefix and different appended payload sizes.

The payload contains:

- fixed-size application/window configuration fields;
- the original application name or path;
- variable and literal tables;
- a compact instruction stream.

The `.TMP` files are not compiler intermediate source. The manual describes
them as debug listings, and inspection confirms that the leading value on each
line is the runtime instruction address for that source line. For example,
`SAMPLE16.TMP` ends at runtime address 144 while the application payload is 445
bytes because the payload also contains headers and data tables.

The likely original execution flow is:

1. The compiler copies `BB181.STB` to the destination `.EXE` unchanged.
2. It appends configuration, symbol/literal data, and compiled instructions.
3. The launcher chooses the DOS or Windows runtime module.
4. `BBL181.EXE`, `WBBL181.EXE`, or `WBB5181.EXE` loads and interprets the
   appended program.

## Language characteristics confirmed by the manual

BasicBasic is documented as a subset of Microsoft Basic with integer (`%`),
long integer (`&`), single precision (no suffix), and string (`$`) values. It
does not support double precision. Arrays must always be dimensioned.

One important compatibility trap is its unusual expression evaluation. The
manual says expressions scan left-to-right, but all multiplication operations
are checked before divisions and all subtraction operations before additions.
A full compatibility mode must reproduce this behavior; the initial modern
compiler uses conventional operator precedence and will make the historical
mode explicit later.

The language also includes a large Windows-specific runtime surface: graphics,
fonts, child windows, controls, dialogs, menus, bitmap operations, printing,
serial communications, sound, and Windows metacommands.

## Reconstruction strategy

The recommended primary target is source compatibility, not binary bytecode
compatibility:

```text
BasicBasic source -> lexer/parser -> typed IR -> C11 -> GCC -> native program
                                      |
                                      +---- portable BasicBasic runtime
```

Advantages:

- produces native 32/64-bit programs with a stock compiler;
- avoids depending on 16-bit Windows APIs and segmented memory;
- allows each historical feature to receive a tested modern implementation;
- preserves a clean boundary between language semantics and GUI/runtime APIs.

Exact old-bytecode support can be added as a second backend after its format is
mapped. A decompile of `WBB.EXE` is now available and confirms that it contains
tokenization, parsing, fixups, and payload serialization. The recovered entry
points and extended token map are recorded in
[`WBB_DECOMPILE_MAP.md`](WBB_DECOMPILE_MAP.md).

## Staged compatibility plan

1. Core console language: expressions, variables, flow control, functions,
   arrays, console input/output, DATA/READ, and subroutines.
2. Files: sequential, random, and binary I/O plus FIELD/LSET semantics.
3. Portable graphics and input, preferably behind SDL or a small native Win32
   backend.
4. Windows controls, dialogs, menus, bitmaps, fonts, and printing.
5. Historical expression mode, numeric-width behavior, error codes, and other
   quirks discovered through differential tests against the original runtime.
6. Optional reader/interpreter for original appended bytecode.
