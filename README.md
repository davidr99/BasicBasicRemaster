# Modern BasicBasic

This repository is an experimental, clean modern reconstruction of Mark
Davidsaver's BasicBasic 1.81. The original 1992-1996 artifacts are treated as
reference material; the new implementation is portable C11 and is intended to
build with an ordinary GCC toolchain.

The initial compiler, `bbasicc`, translates BasicBasic source to C. The
generated C links to a small runtime library. This is simpler to port and test
than recreating the original undocumented bytecode immediately, while leaving
that option open for compatibility research.

## Build a BasicBasic program

The simplest Windows build path is the supplied PowerShell driver. It builds
the modern compiler with GCC, translates the `.BAS` file to C11, and links the
native Windows executable:

```powershell
.\tools\build_program.ps1 K:\dos\bbasic\SAMPLE4.BAS .\SAMPLE4.exe
```

The default compiler is `K:\msys64\mingw64\bin\gcc.exe`; pass `-Gcc` to use a
different MinGW GCC installation. Programs using Windows UI statements are
automatically linked with the Windows GUI subsystem, so they do not open an
extra console window. Use `-WindowsApp` or `-ConsoleApp` to override automatic
detection.

To rebuild every supplied original program:

```powershell
.\tools\build_original_corpus.ps1 -SourceDirectory K:\dos\bbasic
```

This currently produces native executables for all 20 `.BAS` files in the
original directory.

Run the noninteractive language, file, embedded-NUL string, and Win32/GDI
regressions with:

```powershell
.\tools\test.ps1
```

## BasicBasic IDE

The native Windows editor keeps source editing, build diagnostics, compilation,
and running in one window. It follows the workflow recovered from the original
`WBBE.EXE` editor while using standard modern Windows controls and the rebuilt
GCC compiler pipeline.

Build and launch it with:

```powershell
.\tools\build_ide.ps1
.\build-tools\bbasic-ide.exe
```

You can also pass a `.BAS` file on the command line. Use **Compile** (F7) to
build into `build-ide-programs`, or **Run** (F5) to save, compile, and launch.
Compiler output stays in the IDE output pane, and Windows programs are linked
without a console window. Set `BBASIC_GCC` if GCC is not installed at the
default `K:\msys64\mingw64\bin\gcc.exe` path or available on `PATH`.

## Create a deployment package

Build a portable offline Windows package with:

```powershell
.\tools\package.ps1
```

The command creates a versioned folder, ZIP archive, and SHA-256 checksum under
`dist`. By default it bundles the complete MinGW GCC toolchain, so the extracted
package can edit, compile, and run BasicBasic programs on another 64-bit Windows
computer without installing a compiler. The IDE automatically discovers
`toolchain\bin\gcc.exe` beside itself.

Use `-WithoutToolchain` for a much smaller package intended for computers that
already have MinGW GCC on `PATH` or selected through `BBASIC_GCC`. Use `-Version`
and `-OutputDirectory` to control the package name and destination.

## CMake build

With GCC and CMake installed:

```sh
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux or macOS, omit `-G "MinGW Makefiles"`. MSVC is also supported for
development checks, although GCC remains the portability baseline.

## Translate a program

```sh
build/bbasicc program.bas -o program.c
gcc -std=c11 -Iinclude program.c src/bbasic_runtime.c src/bbasic_win32.c -lm -o program
```

With MinGW on Windows, append
`-lgdi32 -luser32 -lcomdlg32 -lwinmm` to the link command.

The implemented corpus surface includes:

- comments plus `WINDOWS NAME` and `WINDOWS SIZE` metacommands
- numeric and string expressions
- `PRINT`
- block and single-line `IF` / `ELSEIF` / `ELSE` / `END IF`
- `DO`, `DO WHILE`, `LOOP`, and `LOOP WHILE`
- `FOR` / `NEXT`, including negative `STEP`
- `GOSUB` / `RETURN`
- numeric and string assignment
- dynamically dimensioned numeric and string arrays (up to four dimensions)
- `DIM`, `DATA`, `READ`, and labelled `RESTORE`
- `STOP` and `END`
- `OSTYPE`, `INKEY$`, `LEN`, `ASC`, `CHR$`, `INT`, `ABS`, and `VAL`
- `LEFT$`, `RIGHT$`, `MID$`, `STR$`, `SPACE$`, `UCASE$`, and `INSTR`
- numeric and named labels plus `GOTO`
- console `INPUT`
- `CLS`, `COLOR`, `LOCATE`, and `BEEP`
- `FREEMEM`, `RND`, `RANDOMIZE`, `TIMER`, `TIME$`, and `DATE$`
- sequential files, random records, `FIELD`, `LSET`, `GET`, `PUT`, `EOF`, and
  `LOC`
- Windows serial ports and `SETCOM`
- `PRINT USING`, `SYSTEM`, `DIR$`, `SLEEP`, `SNDDEV`, and `PLAYSOUND`
- native Win32/GDI windows, graphics, palette operations, mouse input, fonts,
  controls, menus, custom and common dialogs, bitmap operations, and graphics
  printing
- embedded-NUL string behavior required by extended `INKEY$` codes and Windows
  file-dialog filters

All 20 supplied `.BAS` programs translate and link with GCC. The reconstruction
targets source compatibility; it does not reproduce the original appended
bytecode format or claim byte-for-byte executable compatibility.

On systems without legacy COM1/COM2 hardware, serial programs use a disconnected
virtual port so their UI remains usable: reads report no waiting data and writes
are discarded. Set `BBASIC_STRICT_SERIAL=1` to make a missing port fatal, or
`BBASIC_FORCE_VIRTUAL_SERIAL=1` to test the fallback even when hardware exists.

See [docs/ORIGINAL_ARCHITECTURE.md](docs/ORIGINAL_ARCHITECTURE.md) for the
reverse-engineering findings, [docs/WBB_DECOMPILE_MAP.md](docs/WBB_DECOMPILE_MAP.md)
for the compiler-decompile map, and [docs/CORPUS_STATUS.md](docs/CORPUS_STATUS.md)
for current acceptance evidence.
