# Original `.BAS` corpus status

The acceptance corpus is the 20 BasicBasic programs in `K:\dos\bbasic`. The
authoritative rebuild command is:

```powershell
.\tools\build_original_corpus.ps1 -SourceDirectory K:\dos\bbasic
```

On 2026-08-17 this command completed successfully with the message
`Built 20 original BasicBasic programs.` Every source translated without a
diagnostic and every generated C file linked with MinGW GCC.

| Program | Translation | GCC link |
| --- | --- | --- |
| `ALBUM.BAS` | Pass | Pass |
| `CARD.BAS` | Pass | Pass |
| `SAMPLE1.BAS` | Pass | Pass |
| `SAMPLE2.BAS` | Pass | Pass |
| `SAMPLE3.BAS` | Pass | Pass |
| `SAMPLE4.BAS` | Pass | Pass |
| `SAMPLE5.BAS` | Pass | Pass |
| `SAMPLE6.BAS` | Pass | Pass |
| `SAMPLE7.BAS` | Pass | Pass |
| `SAMPLE8.BAS` | Pass | Pass |
| `SAMPLE10.BAS` | Pass | Pass |
| `SAMPLE11.BAS` | Pass | Pass |
| `SAMPLE12.BAS` | Pass | Pass |
| `SAMPLE13.BAS` | Pass | Pass |
| `SAMPLE14.BAS` | Pass | Pass |
| `SAMPLE15.BAS` | Pass | Pass |
| `SAMPLE16.BAS` | Pass | Pass |
| `SAMPLEW1.BAS` | Pass | Pass |
| `SAMPLEW2.BAS` | Pass | Pass |
| `SAMPLEW3.BAS` | Pass | Pass |

Executable regression programs additionally verify:

- expressions, branching, arrays, forward/reverse `FOR`, and `GOSUB`;
- `DATA`, labelled `RESTORE`, and embedded-NUL/extended-key strings;
- sequential files and fixed-width random records;
- Win32 window creation, GDI text and graphics, controls, fonts, memory
  bitmaps, and bit-block copying.

These focused checks are repeatable with `.\tools\test.ps1`.

Most originals are intentionally interactive or perpetual-loop demonstrations,
so a successful link is not presented as proof that every possible user event
path was automatically exercised. Their complete source-used API surface is
implemented in the runtime, while the focused regressions provide repeatable
noninteractive behavioral evidence.
