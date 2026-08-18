# BasicBasic 1.81 compatibility matrix

The goal is source compatibility with the Windows 95 release documented in
`BB.DOC`. Status values mean:

- **Implemented**: compiled and exercised in the modern runtime.
- **Partial**: useful behavior exists, but historical edge cases remain.
- **Planned**: recognized as required but not implemented yet.

## Program control

| Feature | Status |
| --- | --- |
| `IF` / `ELSEIF` / `ELSE` / `END IF` | Implemented |
| `DO` / `LOOP` and `WHILE` conditions | Implemented |
| `GOTO` and numeric/named labels | Implemented |
| `FOR` / `NEXT` | Implemented |
| `GOSUB` / `RETURN` | Implemented |
| `SUB`, `FUNCTION`, `DECLARE`, `CALL` | Planned |
| `EXIT DO`, `EXIT SUB`, `EXIT FUNCTION` | Planned |
| `CHAIN`, `COMMON` | Planned |

## Values and expressions

| Feature | Status |
| --- | --- |
| Single-precision numeric and string variables | Partial |
| `%` integer and `&` long suffixes | Partial |
| Arithmetic, comparison, `AND`, `OR`, `MOD` | Implemented |
| Historical BasicBasic expression-order quirks | Planned |
| One- and multi-dimensional arrays | Implemented (up to four dimensions) |
| `ABS`, `ASC`, `CHR$`, `INT`, `LEN`, `VAL` | Implemented |
| `LEFT$`, `RIGHT$`, `MID$`, `INSTR`, `STR$`, `SPACE$`, `UCASE$` | Implemented |
| `STRING$` | Planned |
| `ATN`, `COS`, `FIX`, `IRND`, `SIN`, `SQR`, `TAN` | Planned |
| `RND`, `RANDOMIZE` | Implemented |

## Console and environment

| Feature | Status |
| --- | --- |
| Console and GDI `PRINT` | Implemented |
| `CLS`, `COLOR`, `LOCATE`, `BEEP` | Implemented |
| `INKEY$`, `OSTYPE`, `FREEMEM`, `TIMER`, `TIME$`, `DATE$` | Implemented |
| Console `INPUT` | Implemented |
| Corpus-used `PRINT USING` formats | Implemented |
| `SCROLLAREA` | Implemented |
| `SLEEP`, `PLAYSOUND`, `SNDDEV` | Implemented |
| `SYSTEM` | Implemented |
| `CSRLIN`, `POS`, `TAB`, `SOUND`, `COMMAND$`, `SHELL` | Planned |

## Data and files

`DIM`, `DATA`, `READ`, labelled `RESTORE`, sequential input/output, random
records, `FIELD`, `LSET`, `GET`, `PUT`, `EOF`, `LOC`, and `DIR$` are
implemented. Windows COM ports and `SETCOM` use native Win32 communications
handles. File locking and unexercised binary-file variants remain planned.

## Windows 95 runtime

The supplied-program Win32/GDI surface is implemented:

- `SCREEN`, `LINE`, `CIRCLE`, `PAINT`, `PSET`, `PRESET`, `GET`, `PUT`,
  and `PALETTE`;
- bitmap creation, loading, storage, selection, copying, and stretching;
- mouse input and persistent backing-store repainting;
- fonts and text measurement;
- main-window naming, sizing, positioning, and output selection;
- menus, controls, buttons, dialogs, message boxes, and file choosers;
- screen and graphics printing through the Windows print dialog;
- serial communications.

Child-window APIs, application-icon metadata, and Windows features documented
in `BB.DOC` but not exercised by the supplied `.BAS` corpus remain future
full-manual compatibility work.

## Sample gates

| Original sample | Current state |
| --- | --- |
| All 20 supplied `.BAS` programs | Translate and link as native Windows executables |
| Core, extended-string, sequential/random file tests | Build and run automatically |
| Win32/GDI/control/bitmap smoke test | Builds, runs, and writes a `PASS` marker |
