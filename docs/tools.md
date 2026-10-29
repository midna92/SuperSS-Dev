# Tools (`Tools/`) and Cheat Engine table (`CE Trainer/`)

None of these are part of the server runtime. They are stand-alone helpers written by the project author for working with **PangYa client files** (game data, models, fonts, maps, UCC images) and for debugging the original client. There are no project files, makefiles or `.sln` for them; each `.cpp` is a single translation unit you compile by hand. All sources are in the author's Portuguese-origin style; source comments have been translated, runtime strings are mostly still Portuguese.

| Tool | Platform | Kind | Handles |
|---|---|---|---|
| `Tools/lzpak.cpp` | Win/Linux/macOS (C++17, `<filesystem>`) | CLI | `.pak` archives (LZPak) |
| `Tools/CryptXTEA.{h,cpp}` | any | library only (no `main`) | XTEA block cipher for `updatelist` files |
| `Tools/MakeFont.cpp` | Windows only (GDI+) | CLI | produces `.wft` bitmap font files |
| `Tools/gbin_to_text.cpp` | any | CLI dumper | `.gbin`, `.sgbin`, `.aibin` |
| `Tools/sbin_to_text.cpp` | any | CLI dumper | `.sbin` |
| `Tools/puppet_to_text.cpp` | any | CLI dumper + OBJ exporter | `.pet`, `.mpet`, `.bpet`, `.apet` |
| `Tools/UCCVIEW(SD)/` | any browser | HTML + JS viewer | UCC (user created content) raw image files |
| `CE Trainer/*.CT` | Windows + Cheat Engine | Cheat Engine table + Lua | live game client memory (JP/USA/TH) |

Nothing in `Server Lib/` includes or links these files (checked by search for `CryptXTEA`/`EncryptUpdatelist` outside `Tools/`).

---

## `Tools/lzpak.cpp` - LZPak (`.pak`) packer/unpacker

PangYa's data archives (`.pak`) are "LZPak". The tool both **extracts** and **creates** them. Header comment gives the build lines:

```
g++ lzpak.cpp -o lzpak.exe
cl /EHsc /std:c++17 lzpak.cpp /Fe:lzpak.exe
```

(`g++` needs `-std=c++17` or newer for `std::filesystem` unless it is the default.)

### File format as implemented

Packed structs (`#pragma pack(1)`):

- **Footer header** `sLZPakHeader` (the *last* 9 bytes of the file): `u32 offset_file_entry`, `u32 num_file_entry`, `u8 version`. Expected `version == 0x12` (`kLZPakVersion`); a different value only prints a warning.
- **File entry table** starts at `offset_file_entry`; each entry is `sLZPakFileEntryHeader { u8 name_length; u8 type:4; u8 version:4; }` followed by `u32 offset`, `u32 compress_size`, `u32 size`, and `name[name_length]` (+1 byte padding for entry versions 1, 2 and RAW).
- Entry **type**: `0` RAW, `1` LZ77, `2` DIRECTORY, `3` LZ772 (a second LZ variant; implemented by `LZ772_Compress/Decompress`).
- Entry **version** (how the entry metadata is obfuscated): `0` = v1 and `1` = v2: `size` XOR `0x71` and every name byte XOR `0x71`; `2` = v3: `size`/`offset` (as one 64-bit block) and name (8-byte blocks) are **XTEA-encrypted** with a per-region key; `0xF` = RAW (no obfuscation).
- Optional **author block** sits between the last file's data and the entry table: name bytes followed by a 2-byte big-endian length (read as "Autor do LZPak"; defaults to "Desconhecido" = "Unknown" if absent). Packs created by this tool carry author `"SuperSS"`.
- XTEA keys per region (`kKeys_*`), all 4 x u32: GB (Global), TH (Thailand), JP (Japan), KR (Korea), ID (Indonesia), EU (Europe). When reading a v3 pack, the tool tries each known key on the first entry (the key is accepted only if the decrypted `offset` of the first entry is 0, i.e. `(decrypted & 0xFFFFFFFF) == 0`; this assumes the first file starts at offset 0) and, if none works, asks interactively: choice 0-5 or 6 = custom key typed as `hex,hex,hex,hex`.

### Usage

```
lzpak <file.pak>                       # list entries (prints header, entries, author)
lzpak <file.pak> <pattern> [outdir]    # extract entries whose name matches the pattern
lzpak <dir-or-file>                    # create newpak.pak, interactive prompts
lzpak <dir-or-file> -c                 # same, interactive
lzpak <dir-or-file> <ver> <type> <lvl> # create non-interactively
```

Argument logic (`main()`, line ~1047):

- If `argv[1]` does not end in `.pak`, or `argv[2] == "-c"`, it is **create** mode. Otherwise it is **list/extract** mode.
- Extract: `argv[2]` is a pattern where `*` becomes `.*` and is used with `std::regex_search` on the entry path (backslashes normalised to `/`). `argv[3]` = output directory (default `./`). Directory structure inside the pack is recreated. RAW, LZ77 and LZ772 entries are decompressed. With only `argv[1]`, entries are only listed (the extraction branch is `if (_argc > 2)`).
- Create: input can be a directory (recursive) or a single file. Output is always `newpak.pak` in the **current directory**. Prompts (when interactive) offer entry version 1/2/3/Raw (default 3), compression Raw/LZ77/LZ772 (default LZ772), compression level 0-5 (capped at 5), and, for v3, the region key (0 Global ... 5 Europe, or custom). Non-interactive: `<ver>` is the enum value (0=v1, 1=v2, 2=v3, 15=raw), `<type>` the enum (0 raw, 1 LZ77, 3 LZ772), `<lvl>` default 5. Non-interactive create without region prompt defaults to the **JP** key (`gLocationKeys = kKeys_JP`).
- Return codes 1-17 are distinct failures (e.g. 2 cannot open, 3 too small, 9 bad XTEA key).

---

## `Tools/CryptXTEA.{h,cpp}` - XTEA for `updatelist`

Class `stdA::CryptXTEA`: `OpenFile(buffer, path, &size, option)` (0 read / 1 write whole file), `EncryptXTEA`/`DecryptXTEA` (one 8-byte block, 4 outer iterations of 4 rounds written out manually, delta constant `0x61C88647`), and `EncryptUpdatelist` / `DecryptUpdatelist(dst, src, size, option)` which process the buffer in 8-byte blocks (the trailing `size % 8` bytes are copied unencrypted - the loop is `size / 8`). Key selection (`option`): `0` = TH (S4.9/S9 Thai), `1` or default = GB (S9 Global "FRESH UP"), `2` = JP (S9 JP). These are the same keys as `lzpak.cpp` (GB/TH/JP). There is no `main`, so it is a code library to be linked into your own tool; "updatelist" is the game updater's file list (name inferred from function names; the repo does not otherwise document it).

Build: add `CryptXTEA.cpp` to your own project (`#include "CryptXTEA.h"`; plain C++ with `<iostream>/<fstream>`).

---

## `Tools/MakeFont.cpp` - `.wft` bitmap font generator

"makefont.exe Version 1.0 by Acrisio SuperSS Dev". Renders glyphs of a system font with **GDI+** into a monochrome bitmap font file for PangYa (comment: "based on the HL code" - i.e. Half-Life's `makefont`). Windows only: `windows.h`, `gdiplus.h`, `Winmm.lib` (`#pragma comment(lib, ...)`, so build with MSVC: `cl /EHsc MakeFont.cpp`).

```
makefont [-font "fontname"] [-italic] [-underline] [-bold] [-antialiasing]
         [-startletter code] [-pointsize size] [-scale size] [-paddingleft px] [-one] outfile
```

- Defaults: font name unset (constant `DEFAULT_FONT = "Arial"` exists), point size `MIN_FONT_SIZE = 4`, start code point `0x20` (values below are clamped to 0x20), antialiasing work size 22, scale 1.
- `-scale n` with n > 1 is interpreted as a target pixel size and divided by `pointsize`.
- `-one` renders only the first letter and saves `letter3.bmp` as a test.
- Output: `outfile` with extension forced to **`.wft`** (`StripExtension` + `DefaultExtension`). It also writes `log.txt` with the measured box (X/Y/Width/Height) of the last letter, and prints the elapsed time.
- The binary layout of `.wft` is produced by `makeMyFont()` (bit-packed rows per glyph, followed by a `u16` glyph width). I did not decode it further.

---

## `Tools/gbin_to_text.cpp` - `.gbin` / `.sgbin` / `.aibin` dumper

Header: "translator for PangYa PC .gbin, .sgbin and .aibin files, all versions". Parses the binary and writes a **human-readable text log**; it does not convert back.

- Run: `gbin_to_text <file>` (needs at least 1 argument; exit codes -1 cannot open, -2 no args, -3 empty, -4 parse failure).
- Output: a file in the current directory named `Log Gbin(D-M-YYYY H-M-S).log` (class `FileLog`; `DISABLE_LOG` define is `false`). Nothing but a few lines goes to the console.
- Format handled (`loadGBin`): header `HeaderV70` (+`HeaderV72` for newer), magic **`"WEPX"`**, `version` (versions > `0x71` have an extra `num_new_element`); then cameras, lights, sound boxes, textures, nodes (`NodeV70`/`NodeV72`), elements (`ElementV70/71/72`, `NewElementV72`), `MapCheck`, `PointXZ`, `MapColorVtx` and `ElementBaseFaceMapColorVtx`. These are the course/scene geometry & map-check structures (the names suggest course/hole data; the code does not say which in-game file is which - **unverified**).
- Compile: `g++ -std=c++17 gbin_to_text.cpp -o gbin_to_text` (pure STL, no Windows dependencies; `-std=c++11` or higher is likely enough but unverified).

## `Tools/sbin_to_text.cpp` - `.sbin` dumper

Header: "translator for the PangYa PC .sbin file, all versions". Same CLI contract as above: `sbin_to_text <file>` -> `Log Sbin(<timestamp>).log`.

- Format: leading 4-byte `Version` (major.minor; if `(v & 0xFFFE0000) == 0xFFFE0000` and >= 1.0 the file starts with a bone count then, per bone, a fixed string bone name and a block; otherwise it is treated as an older "Beta version" single block).
- Per-bone block `SBINCtx` = `InFragMesh` (mesh infos, vertices `Vector3d`, UVs, global and two typed **DDS** texture sets with sizes) + `OutFragMesh`. Exit codes -1..-7 for failures.
- Compile: `g++ -std=c++17 sbin_to_text.cpp -o sbin_to_text`.

## `Tools/puppet_to_text.cpp` - "puppet" model files `.pet`/`.mpet`/`.bpet`/`.apet`

Header: "translator for PangYa PC .pet, .mpet, .bpet and .apet files, all versions". "Puppet" is the client's 3D model container: a sequence of 4-character **blocks** (`VERS`, `MESH`, `SMTL`, `FRAM`, `TEXT`, `BONE`, `ANIM`, `MOTI`, `COLL`, `FANM`, `EXTR`).

- Run: `puppet_to_text [file]` (default `golf_ball_01.pet`). The extension selects which blocks are expected (`setFileType`): `.pet` = all; `.mpet` = TEXT+BONE+MESH+FANM+SKINLIST+SMTL (mesh/material); `.apet` = BONE+ANIM+FRAM+MOTI (animation); `.bpet` = COLL+BONE+EXTR (collision/extra). Unknown extension = all (`FT_ALL`).
- Output 1: `Log Puppet(<timestamp>).log` with each block decoded (version, bones, textures with specular-material parameters, motion, ...).
- Output 2: when a `MESH` block is present, the tool **also writes Wavefront OBJ/MTL files with fixed names `pippin2.obj` and `pippin2.mtl`** in the current directory (vertices skinned by the first bone's matrix and weight, `vn`, `vt` with V flipped, faces grouped by `usemtl`). The names are hard-coded, so the next run overwrites them. It is a rough exporter (note the debugging name and the commented-out `calc_vtx_and_index` call).
- Compile: `g++ -std=c++17 puppet_to_text.cpp -o puppet_to_text` (uses `strcasecmp` on non-Windows via `MyStricmp`).

---

## `Tools/UCCVIEW(SD)/` - UCC viewer

Files: `uccviewer.htm` (empty page, title "UCC VIEWER", loads `uccview.js`) and `uccview.js` ("UCC-VIEW do pangya", created 2016-07-27). UCC = the client's **User Created Content** (custom designs players paint on clothes/parts), stored as raw pixel buffers.

- Run: open `uccviewer.htm` in a browser (no server or build needed), use the file chooser.
- The script creates a 400x400 `<canvas>` and a file input, reads the chosen file as a binary string, and renders by **file name**:
  - name contains `front` or `back` (clothing parts): **BGR, 3 bytes per pixel**, 128x256, or 256x256 if the file is larger than 100000 bytes (dresses); alpha forced to 0xFF.
  - name contains `icon`: **BGRA, 4 bytes per pixel**, 64x84.
  - any other name: only clears the canvas (nothing drawn).
- Input: raw uncompressed pixel dump (no header) as stored by the client; output: only on-screen canvas (no export). Missing bytes are filled with random values.

---

## `CE Trainer/` - Cheat Engine table

File: `CE Trainer/Trainer edit e view pangya fresh up [jp-usa-th] by Acrisio SuperSS Dev.CT` (Cheat Engine table version 19, XML). It contains:

- one form (`EditView`, a compressed Ascii85 `TCEForm` layout) and
- a ~40 KB **Lua script** ("Script Edit e View PangYa Fresh UP Ver. [JP/USA/TH]", created 2019-11-10, updated 2019-11-22 to add freezes for "Spin, Curva (curve), Vento (wind), Angulo (angle), Slope X and Y", and 2024-08-19 to add TH addresses).

It is a **client-side tool** to view and edit in-game shot parameters of the original PangYa **Fresh Up** client for the JP, USA and TH builds; it has nothing to do with the emulator servers. Addresses are hard-coded per region in the Lua (e.g. `MapSearch.const_addr = 0x00E6C934` with comments `USA 0x00E3CFD4, JP 0x00E73E60, TH 0x00E6C934`; power gauge base `TH 0x00E4090C`; classic wind pointer `TH 0x00E6FF1C`; some struct offsets differ per region, such as the grid `+0x3064` USA/TH vs `+0x6944` JP). The script walks the client's internal object map (a linked list of named entries such as `'Screen'`, `'Grid'`, `'Spin'`), then reads/writes floats:

- View: ground (terrain) slope/percentage, distance (yards), height (m), PB (power/break?) value, aim degree, slope break, wind speed/direction, tee/pin/ball positions.
- Edit/freeze (timers writing the value back): spin, curve, wind and wind angle, slope X/Y, wind-rotate, ball-line rotate, power gauge "full" (99.0); a hotkey `Ctrl + Numpad +` increases the wind-rotate value.

Use: open Cheat Engine, attach to the game process, load the table (`File > Load`), enable the script (Cheat Engine asks about Lua execution); the `EditView` form appears. Note this affects only your own client's local display/aim and is a cheating aid for the real game; the repo author ships it alongside the emulator for client investigation. Unverified: exact semantics of some labels (for example "PB") - the label text is in the compressed form data.

## Which file formats, at a glance

| Extension | Where handled | Notes |
|---|---|---|
| `.pak` | `lzpak.cpp` | LZPak archive; entry compression RAW/LZ77/LZ772; XTEA key by region |
| `updatelist` | `CryptXTEA.cpp` | XTEA ECB over 8-byte blocks (library) |
| `.wft` | `MakeFont.cpp` | generated font |
| `.gbin/.sgbin/.aibin` | `gbin_to_text.cpp` | magic `WEPX`, versions 0x70-0x72+ |
| `.sbin` | `sbin_to_text.cpp` | versioned, per-bone mesh + DDS textures |
| `.pet/.mpet/.bpet/.apet` | `puppet_to_text.cpp` | block container; exports OBJ/MTL |
| UCC `*front*`, `*back*`, `*icon*` | `UCCVIEW(SD)` | raw BGR/BGRA |
| `.iff` | `Server Lib/IFF Manager` | see `docs/iff-manager.md` |
