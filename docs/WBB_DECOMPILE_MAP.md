# `WBB.EXE` decompile map

## Provenance

The analyzed compiler is the 140,800-byte `WBB.EXE` supplied with BasicBasic
1.81. Its SHA-256 is:

```text
C43FA6A6ECB6086982FE5771275A91F812651F4B9CA60EC9E30F5ACFE99A4573
```

The binary identifies itself as `BasicBasic version 1.81`. It is a 16-bit NE
Windows application linked for Windows 3.0 and used by the package under both
Windows 3.x and Windows 95.

The decompiler output is not compilable source. Its guessed types lose many
16:16 far pointers and stack arguments, and its global declarations overlap.
The executable and analysis database must therefore be retained alongside the
C export.

## High-confidence compiler functions

| Decompiled function | Recovered role | Evidence |
| --- | --- | --- |
| `FUN_1010_016c` | Opens the `.BAS` source and optional `.TMP` debug listing | Calls `_LOPEN`; constructs the `.tmp` extension and calls `_LCREAT` |
| `FUN_1010_024e` | Initializes compiler arenas and capacity limits | Allocates the source/token, symbol, string, numeric, and output buffers |
| `FUN_1018_1bb9` | Main statement/token parser | Reads a token byte from the compiler buffer and dispatches cases `0x00` through `0xFC` |
| `FUN_1018_1311` | Typed expression/operand dispatcher | Dispatches according to value/type codes and expression operator codes |
| `FUN_1018_1749` | Array declaration/allocation processing | Reads element type, dimension count, bounds, and calculates storage |
| `FUN_1018_1926` | Retrieves array dimension metadata | Reads the dimension count from the symbol record |
| `FUN_1018_19f0` | Parses/checks an array reference | Reads subscripts and checks them against stored dimension metadata |
| `FUN_1018_0000` | Fatal compiler error reporting | Formats the error number with the current compiler offset, shows a message, and terminates compilation |
| `FUN_1010_09a9` | Warning reporter | Increments warning counters and writes diagnostic records when separate compilation is active |
| `FUN_1010_0af1` | Compiler error reporter | Loads error text, increments the error count, and writes error records |
| `FUN_1008_617c` | Serializes the compiled application payload | Writes configuration, tables, literals, strings, and instruction bytes through `_LWRITE` |
| `FUN_1008_68d4` | Constructs the destination `.EXE` filename | Replaces the source extension and performs the Windows filename adjustment |

`DAT_1030_390f` is the far pointer to the compiler's tokenized/intermediate
buffer and `DAT_1030_390d` is its current cursor. Those names should be applied
in the interactive analysis database before deeper work.

## Recovered extended statement token map

The executable contains a contiguous keyword table. Correlating its order with
the `0xC3` through `0xFC` cases in `FUN_1018_1bb9` gives this exact map:

| Token | Statement | Token | Statement |
| --- | --- | --- | --- |
| `C3` | `LSET` | `E0` | `SELECTPRINT` |
| `C4` | `LPRINT` | `E1` | `SELECTDISPLAY` |
| `C5` | `PRINT` | `E2` | `STOREBITMAP` |
| `C6` | `WRITE` | `E3` | `CIRCLE` |
| `C7` | `INPUT` | `E4` | `POSITION` |
| `C8` | `SUB` | `E5` | `PSET` |
| `C9` | `DECLARE` | `E6` | `PRESET` |
| `CA` | `FUNCTION` | `E7` | `PAINT` |
| `CB` | `CALL` | `E8` | `PRINTCONTROL` |
| `CC` | `DO` | `E9` | `CREATEWINDOW` |
| `CD` | `LOOP` | `EA` | `DESTROYWINDOW` |
| `CE` | `EXIT` | `EB` | `MAINMENU` |
| `CF` | file `LINE INPUT` | `EC` | `ADDSUBMENU` |
| `D0` | `READ` | `ED` | `SCREEN` |
| `D1` | `FOR` | `EE` | `PALETTE` |
| `D2` | `PUT` | `EF` | `CREATEFONT` |
| `D3` | `GET` | `F0` | `SELECTFONT` |
| `D4` | `OPEN` | `F1` | `MENUITEMGRAY` |
| `D5` | `SCROLLAREA` | `F2` | `MENUITEMON` |
| `D6` | `LOCK` | `F3` | `CBUTTON` |
| `D7` | `UNLOCK` | `F4` | `DBUTTON` |
| `D8` | graphics `LINE` | `F5` | `DCONTROL` |
| `D9` | `RANDOMIZE` | `F6` | `CONTROL` |
| `DA` | `LOADBITMAP` | `F7` | `SETCTEXT` |
| `DB` | `CREATEBITMAP` | `F8` | `RADIOON` |
| `DC` | `COPYBITS` | `F9` | `RADIOOFF` |
| `DD` | `STRETCHBITS` | `FA` | `CHECKON` |
| `DE` | `SELECTBITMAP` | `FB` | `CHECKOFF` |
| `DF` | `SELECTWINDOW` | `FC` | `SETCOM` |

The core token range (`0x00`-`0x7B`) is also visible in the main dispatcher but
still needs names correlated with the earlier keyword and operator tables.

## Payload layout evidence

`FUN_1008_617c` confirms the previously inferred layout rather than merely
suggesting it. It writes, in order:

1. fixed application and Windows configuration fields;
2. menu/font and related metadata;
3. string table counts and string data;
4. numeric literal counts and formatted numeric data;
5. compiler data/symbol buffers;
6. the generated instruction stream from `DAT_1030_390f`.

The compiler creates the destination from `BB181.STB`, then appends this
serialized payload. This explains why every original generated application has
an identical 19,793-byte prefix.

## How this changes the modern implementation

The modern C backend remains the primary route, but the compiler decompile can
now be used to make it behaviorally precise:

- recover the exact token grammar and statement parameter rules;
- reproduce the original type codes and conversions;
- reproduce array bounds and memory-layout behavior;
- map compiler error numbers to the same source conditions;
- implement the documented unusual expression ordering accurately;
- build differential fixtures from original payload bytes;
- optionally add an original-bytecode backend after the instruction format is
  fully mapped.
