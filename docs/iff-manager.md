# IFF Manager and the `.iff` data tables

## Purpose

`.iff` files are PangYa's item/data tables (characters, parts, clubs, balls, caddies, cards, courses, Grand Prix definitions, gacha tables, error-code text, ...). The game client ships them in `data/pangya_jp.iff`. Every C++ server loads the same file at runtime to know prices, shop flags, stats and rewards.

This doc covers three things:

1. The on-disk format of `pangya_jp.iff`, as implemented in `Server Lib/Projeto IOCP/TYPE/data_iff.h` (record structs) and `Server Lib/Projeto IOCP/UTIL/iff.{h,cpp}` (reader).
2. The **IFF Manager** console tool (`Server Lib/IFF Manager`), which links the same reader plus a writer (`iff_writer`) to bulk-edit the file and to generate SQL/HTML snippets.
3. The bundled `data/` variants of the file.

## How it fits in the system

- `Server Lib/Projeto IOCP/UTIL/iff.{h,cpp}` defines class `stdA::iff`. `typedef Singleton<iff> sIff;` (`iff.h` line ~602) is the global instance.
- The Game Server uses it heavily: `Game Server/Game Server/game_server.cpp` calls `sIff::getInstance().load()` on startup if `!isLoad()`, and again from `game_server::reload_systems()`. The reload command (`_tipo` 1 = "IFF", 0 = all systems) re-reads the file without restarting. Other users: `Game Server/GAME/channel.cpp`, `room.cpp`, `UTIL/map.cpp`, `sys_achievement.cpp`, `mgr_daily_quest.cpp`.
- The Login, Message and Rank servers each also ship a `data/pangya_jp.iff` next to the binary (`Server Lib/{Login,Message,Rank} Server/*/data/`). The Linux tree has copies under `Server Lib/Linux Builds/<Server>/data/`.
- Servers do **not** have an ini key for the file location. The path is a compile-time constant: `#define PATH_PANGYA_IFF "data/pangya_jp.iff"` in `Server Lib/Projeto IOCP/UTIL/iff.h`. It is relative to the **current working directory** of the process, so start each server from the directory that contains its `data/` folder.
- The `.iff` is ordinary **ZIP** (`file` reports "Zip archive data, deflate"). The reader uses libzip (`zip_open`, `zip_fopen_index`, `zip_fread`). On Windows it uses the bundled `Server Lib/Projeto IOCP/ZIP/` (x64 header under `ZIP/x64/`, `.lib` under `ZIP/lib/`); on Linux it uses system `<zip.h>` (`pkg-config libzip`).

## File layout / format

### Container

`pangya_jp.iff` = a ZIP archive with **57 entries** (verified by `unzip -l` on the bundled copy). Each entry is one table, named after the record type (`Character.iff`, `Part.iff`, `Item.iff`, `SetItem.iff`, `Desc.iff`, `GrandPrixData.iff`, ...). Two entries have a `.sff` extension rather than `.iff`: `GrandPrixAIOptionalData.sff` and `MemorialShopCoinItem.sff`. The loader looks them up by exactly that name (`iff.cpp` lines ~400, ~420), so the odd extension is required.

### Entry layout

Each entry is a flat array of packed (`#pragma pack(1)`) structs with an 8-byte header:

```
struct IFF::Head {            // data_iff.h
    uint16 count_element;     // number of records
    uint16 flag_ligacao;      // "link flag to other IFFs" (not interpreted by the reader)
    uint32 version;           // must be IFF_VERSION = 0x0D (13)
};
// followed by count_element * sizeof(RecordType) bytes
```

Validation done by the reader macros `MAKE_UNZIP_MAP` / `MAKE_UNZIP_VECTOR` (`iff.cpp`):

- `head.version != 13` throws `exception` (error type `_IFF`, code 4, "version of the IFF file differs").
- `count_element * sizeof(type) + 8 != uncompressed entry size` throws (code 5, "structure size does not match"). Practical consequence: if you change a struct in `data_iff.h` the file no longer loads, because record sizes must match the client's tables exactly.
- ZIP cannot be opened: code 1; entry cannot be opened: code 2; entry smaller than the header: code 3.
- A table that is missing/empty only logs `[E_LOAD_IFF] File <name> not found or empty`.

Records are stored either in a `std::map<uint32_t, T>` keyed by `_typeid` (or `tipo`/`id` for some tables) or in a `std::vector<T>` (tables without a unique key). For map loading, a duplicate key is overwritten and logged to file only; key 0 is accepted only once.

### Common record header: `IFF::Base`

Most item tables inherit `IFF::Base` (`data_iff.h` ~line 144):

| Field | Meaning |
|---|---|
| `active` (u32) | active flag |
| `_typeid` (u32) | item id, see below |
| `name[64]` | internal name |
| `level` | 7-bit level + `is_max` bit |
| `icon[43]` | icon file name |
| `ShopDados shop` | `price`, `sale_price`, `sell_price`, `flag_shop` (16-bit union), `time_shop` (`active`, `dia` days) |
| `TikiShopDados tiki` | Tiki-shop points/pang/bonus data |
| `DateDados date` | `active_date` bit + two Windows `SYSTEMTIME` (start/end) |

Shop flag bits (`FlagShop::stFlagShop`): `is_cash`, `can_send_mail_and_personal_shop`, `can_dup`, `block_mail_and_personal_shop`, `is_saleable` (buyable with Pang), `is_giftable`, `only_display`; second byte holds `is_new` / `is_hot` icons. Time-limited item prices are in `Item::c[5]` (1, 7, 15, 30, 365 days) and `Item::point` (no-limit price).

### `_typeid` decoding

Helpers in `iff.cpp` (~line 507):

- group = `(typeid & 0xFC000000) >> 26`, matching `enum iff::IFF_GROUP_ID`: CHARACTER=1, PART, CLUB, CLUBSET, BALL, ITEM=6, CADDIE, CAD_ITEM, SET_ITEM, COURSE, MATCH, TITLE, ENCHANT, SKIN, HAIR_STYLE, MASCOT, CHILDITEM, FURNITURE, ACHIEVEMENT, COUNTER_ITEM=27, AUX_PART, QUEST_STUFF, QUEST_ITEM, CARD=31.
- `getItemSubGroupIdentify24/22/21` = bits below the group shifted by 24/22/21.
- `getItemCharIdentify` = `(typeid & 0x03FF0000) >> 18`; `getItemCharPartNumber` = `(& 0x0003FF00) >> 13`; `getItemCharTypeNumber` = `(& 0x00001FFF) >> 8`.
- `iff::findCommomItem(typeid)` switches on the group and returns the `Base*` from the right map.

### The 57 tables and their record types

Loaded by `iff::load()` in this order (see `iff.cpp` ~580-660). The list below is every `m_*` loaded:

- Items with `Base`: `Character`, `Part`, `AuxPart`, `Club`, `ClubSet`, `Ball`, `Item`, `SetItem`, `Caddie`, `CaddieItem`, `Card`, `Course`, `Furniture`, `HairStyle`, `Mascot`, `Skin`, `Achievement`, `CounterItem`, `QuestStuff`, `QuestItem`.
- Other keyed tables: `Match`, `Enchant`, `Ability`, `Desc` (names/descriptions), `CutinInfomation`, `ArtifactManaInfo`, `ErrorCodeInfo`, `HoleCupDropItem`, `LevelUpPrizeItem`, `PointShop`, `ShopLimitItem`, `SpecialPrizeItem`, `SubscriptionItemTable`, `SetEffectTable`, `TikiPointTable`, `TikiRecipe`, `TikiSpecialTable`, `TimeLimitItem`, `GrandPrixData`, `GrandPrixAIOptionalData` (.sff), `MemorialShopCoinItem` (.sff), `ClubSetWorkShopLevelUpProb`, `ClubSetWorkShopRankUpExp`.
- Vector tables: `CadieMagicBox`, `CadieMagicBoxRandom`, `CharacterMastery`, `ClubSetWorkShopLevelUpLimit`, `GrandPrixConditionEquip`, `GrandPrixRankReward`, `GrandPrixSpecialHole`, `MemorialShopRareItem`, `AddonPart`, `CaddieVoiceTable`, `FurnitureAbility`, `NonVisibleItemTable`, `TwinsItemTable`.
- `ScratchRewardSetting.iff` exists in the ZIP (8 bytes, 0 records) and has a struct, but is not in `load()` (unverified whether it is used elsewhere).

`iff::load()` holds a recursive mutex, calls `reset()` first if already loaded, logs `[iff::load][Log] IFF carregado com sucesso.` ("IFF loaded successfully") and **re-throws** on error so the server aborts startup.

## The IFF Manager tool

### Source layout

```
Server Lib/IFF Manager/
  IFF Manager.sln
  IFF Manager/IFF Manager.cpp      main(): interactive menu (15 options)
  IFF Manager/pch.{h,cpp}, IFF Manager.vcxproj
  IFF Manager/data/                the iff variants (see below)
  UTIL/iff_writer.{hpp,cpp}        class iff_writer : public iff
Server Lib/Linux Builds/Makefile-iff.mk
Server Lib/Linux Builds/IFF Manager/data/pangya_jp.iff   (run dir for Linux build)
```

### `iff_writer`

`class stdA::iff_writer : public iff` adds `saveXxx()` for every table (`saveAll()`, `saveBall()`, `saveCharacter()`, `saveSetItem()`, ... one per table, including `.sff` ones). Because it derives from `iff`, constructing it runs `iff::load()` on `data/pangya_jp.iff` (cwd-relative). Mechanics (`iff_writer.cpp`, macros `BEGIN_SAVE_IFF`, `SAVE_IFF_MAP`, `SAVE_IFF_VECTOR`):

1. `zip_open(PATH_PANGYA_IFF, ZIP_CREATE)`; locate the entry by name.
2. If found: read its header and abort the save (logs error) when the version is not 13 or the size does not match; remember its ZIP external attributes (OS + attr).
3. If not found: log "did not have the file, creating a new one", new header `version=13`, attributes `FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM`.
4. Serialize header + records into a `zip_source_buffer` and `zip_file_add(..., ZIP_FL_OVERWRITE)`, restore external attributes, `zip_close`.

`saveAll()` rewrites a fixed subset of tables (starting with AuxPart, Ball, Caddie, CaddieItem, Card, Character, ClubSet, ...; see `iff_writer.cpp` ~line 125). **It edits `data/pangya_jp.iff` in place - there is no backup.** Copy the file first.

Note: `iff_writer.cpp` includes `../../Projeto IOCP/ZIP/zip.h` unconditionally (the bundled header), even for the Linux build.

### Menu (from `IFF Manager.cpp`; prompts are in Portuguese)

The program prints the menu, reads one integer from stdin, runs that action once and exits. Errors (`exception`) are printed to stdout.

| # | Action (translated) | Input -> output |
|---|---|---|
| 1 | Generate list of base items active in shop | Writes `data.raw`: `IFF::Head` + sorted `IFF::Base` of every saleable (`is_saleable`) Ball, Caddie, CaddieItem, Card, Character, ClubSet, HairStyle, Item, Mascot, Part, SetItem, Skin |
| 2 | Read list and change the iff | Reads `data.raw`; for each typeid copies `flag_shop`, `sale_price`, `price` into the iff and calls `saveAll()`. Warns if typeid not found / count mismatch |
| 3 | Remove all date limits from SetItems in shop | Clears `date` of saleable SetItems with subgroup21 == 1 ("Set Character Shop"), `saveAll()` |
| 4 | Generate price list of timed items in shop | Writes `data_price.raw` (`Head` + `{typeid, u16 price[5]}`) for saleable CaddieItem, timed Item, Mascot, Skin |
| 5 | Read price list and change the iff | Reads `data_price.raw`, writes prices back per group (CAD_ITEM, ITEM, MASCOT, SKIN, ...) |
| 6 | Update seq of CadieMagicBox | Renumbers `seq` 1..N, `saveCadieMagicBox()` |
| 7 | Make table list "Donate Item" | Builds an HTML table (name/image/description) via `Desc`, output through the message pool (test log) |
| 8 | Show all parts with 10 curves or more | Logs `INSERT INTO [pangya].[pangya_papel_shop_item](Nome,[typeid],probabilidade,numero,tipo,active) ...` (MSSQL syntax) for parts with `c[4]>=10`/`slot[4]>=10` |
| 9 | "Lucia Attendance System" | Generates a table/HTML for the attendance reward items (unverified details, ~500 lines of code) |
| 10 | Show all Card personal-shop saleable | Lists cards by sub-type for enabling/disabling PS (output via log) |
| 11 | Show all Tiki shop items with TP >= 15 | Prints items; helper lambda `printAllTikiPtsMoreOrEqual15` |
| 12 | Name of gacha items | Logs `INSERT INTO [pangya].[pangya_gacha_jp_all_item_list]([typeid],[name],[char_type])` for a hard-coded typeid list |
| 13 | Enable all Cutin Fate in shop | Hard-coded Skin typeids `0x39400211..0x39400264`: `is_saleable=1`, `price=5`, `saveSkin()` |
| 14 | Papel Shop Rare Number 28 | Finds Part items by icon name from a hard-coded list of names (output SQL via log) |
| 15 | Generate Grand Prix list, bot scores | Logs `GP[TYPEID, QNTD_HOLE] BOT SCORE [MIN, MED, MAX]` for every `GrandPrixData` |

Options 7-15 write via `_smp::message_pool` with `CL_ONLY_FILE_LOG_TEST`, i.e. into the engine's test log file, not to the console. The generated SQL targets tables `pangya.pangya_papel_shop_item` and `pangya.pangya_gacha_jp_all_item_list` (as written in the tool; check the SQL dump for the real schema).

## Variants of `data/pangya_jp.iff`

Directory `Server Lib/IFF Manager/IFF Manager/data/` contains these ZIPs. All are 57-entry archives with the same entry names. Nothing in the repo documents what the variants mean; the table below states what the folder/file **name** says (translated) and what I measured by comparing record counts to the root `pangya_jp.iff` (the base). Interpretation of the numbers is **unverified**.

| File | Name means | Measured difference vs. base |
|---|---|---|
| `pangya_jp.iff` | default; identical copy is in `Multi Client/.../data` and (size differs 19 bytes, zip metadata) `Linux Builds/IFF Manager/data` | base: 17,543,464 bytes uncompressed |
| `pangya_jp_.iff` | trailing underscore, probably a backup/working copy | only `Ball`, `Item`, `SetItem` bytes differ, same counts |
| `pangya_jp_989.iff` | probably client/patch build 989 (`Multi Client` sends client version `SS.R7.989.00`) | slightly fewer Part (9416 vs 9417), ClubSet, Ball, Item, Mascot, Desc, CadieMagicBox records |
| `pangya_jp_990.iff` | probably build 990 | like 989, plus `GrandPrixData` 750 vs 726 records and `CadieMagicBox` 2132 vs 2126 |
| `973/pangya_jp.iff` | build 973 | fewer Part/Item/SetItem/Desc/Card/CadieMagicBox/MemorialShopRareItem records (older/smaller data set); several other tables byte-different |
| `grand prix event 951/pangya_jp.iff` | "Grand Prix event 951" | smallest file (17.26 MB); `GrandPrixData` has 783 records (vs 726) - the Grand Prix event set; many item tables smaller |
| `All itens ativo no shop/pangya_jp.iff` | "all items active in the shop" | same as base for most tables but Item 612, Part 9399, SetItem 1062... (shop flags changed; byte-different in nearly every table) |
| `Nem Todos ativos no shop/pangya_jp.iff` | "Not all active in the shop" | similar to above, fewer tables differ (Character, Part, ClubSet, Ball, Item, Caddie, CaddieItem, SetItem, Desc, HairStyle, Mascot, Card, CadieMagicBox) |
| `MOD/pangya_jp.iff` | "MOD" - modified | small edits to Part, ClubSet, Ball, Item (632), SetItem (1069), Desc, Skin, Mascot, Card, CadieMagicBox |

The Game Server folder additionally has `data/pangya_jp_962_02.iff` and `data/pangya_jp_994.iff` (in `Server Lib/Game Server/Game Server/data/` and the Linux copy). No source file references them; the loader only ever opens `data/pangya_jp.iff`, so to use a variant you **replace/rename it to `pangya_jp.iff`** (inferred from the code, not documented).

Important: the file the **client** reads (its own `data/pangya_jp.iff`) and the file the servers read should come from the same game version; the records are fixed-size client structures.

## Build & run

### Windows

Open `Server Lib/IFF Manager/IFF Manager.sln` in Visual Studio. The project uses `pch.h`, includes the engine sources from `../../Projeto IOCP/UTIL` and links libzip (`PATH_LIBZIP_LIB` is `../../Projeto IOCP/ZIP/lib/zip-64.lib` on 64-bit). Place/run with the working directory containing `data/pangya_jp.iff`. Uses `STDA_SET_SE_EXCEPTION` (SEH mini-dump handler) on Windows.

### Linux

`Server Lib/Linux Builds/Makefile-iff.mk` (run from `Server Lib/Linux Builds`):

```
make -f Makefile-iff.mk                       # debug (-Og -D _DEBUG -ggdb)
make -f Makefile-iff.mk __build_type=release  # -O3
make -f Makefile-iff.mk clean
```

- Compiler `g++`, flags `-fpermissive -fsigned-char -std=c++20` + `pkg-config --cflags glib-2.0`; link `-pthread -lg -ldl` + `pkg-config --libs libzip glib-2.0` (so `libzip-dev` and `libglib2.0-dev` are required; `-lg` is a leftover that may fail on some distros - unverified).
- Sources: `IFF Manager.cpp`, `iff_writer.cpp`, and from `Projeto IOCP/UTIL`: `exception.cpp`, `message_pool.cpp`, `message.cpp`, `WinPort.cpp`, `reader_ini.cpp`, `util_time.cpp`, `hex_util.cpp`, `iff.cpp`.
- Output: `output-iff-{debug|release}/iff`, then copied to `./IFF Manager/iff` (relative to `Linux Builds`). Run it from `Server Lib/Linux Builds/IFF Manager/` so `data/pangya_jp.iff` resolves.
- The file/dir names contain spaces; the makefile encodes them with `|` placeholders (`../IFF|Manager/...`) and escapes them in rules.
- The copy-to variable is spelled `DIRSAVEPRROGRAM` (sic) consistently in the makefile, so it works.

## Gotchas

- Path is cwd-relative and hardcoded; a server started from the wrong directory fails with "Error ao abrir o arquivo zip" (error `_IFF`, code 1) and aborts.
- Struct layouts are binary-exact (`#pragma pack(1)`); `SYSTEMTIME` on Linux comes from `WinPort.h`.
- `count_element` is a `uint16`, so a table cannot exceed 65535 records; the tool casts sizes to `unsigned short`.
- `IFF Manager` menu prompts and log texts are in Portuguese/English mix; many actions embed hard-coded typeids for one particular shop event and are one-off scripts rather than general features.
- `saveAll()` has no backup and no dry-run.
