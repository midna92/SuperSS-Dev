# Rank Server

Source: `Server Lib/Rank Server`. Executable name on Linux: `rank` (`Server Lib/Linux Builds/Makefile-rs.mk`). Log prefix: `RS`.

## 1. Purpose

The Rank Server serves the in-game "Ranking" window (global leaderboards). It keeps an in-memory snapshot of the rankings that the database computed, answers paging/search/player-detail requests from the game client, and periodically asks the database to recompute the rankings.

It does no ranking math itself. All scoring is done by the stored procedure `pangya.GeraRankAll` (section 6); the server only (a) triggers it when the refresh interval has elapsed, (b) loads the result tables into RAM, and (c) formats pages into packets.

## 2. How it fits in the system

```
Game client --(ranking window)--> Rank Server <--> Auth Server (default 127.0.0.1:7777)
                                      |
                                      v
                                  Database: pangya.GeraRankAll, pangya_rank_atual/_antes/_atual_character, pangya_rank_config
```

- Built on the shared engine `Server Lib/Projeto IOCP` (IOCP/epoll, threads, timers, DB layer). `rank_server` derives from `server`.
- Server type: `m_si.tipo = 4` (`rank_server::config_init`, `Server Lib/Rank Server/Rank Server/rank_server.cpp`).
- Like the Message Server it authenticates the player through the Auth Server: the game client connects, sends the account uid/id, and the Rank Server asks the Auth Server whether that player is online on the Game Server recorded in the DB (`getInfoPlayerOnline`). The Rank Server code comments say it was copied from the Message Server project ("I copied the msn project to make this one faster").
- A per-account block flag `rank_server` (`block_flag.m_flag.stBit.rank_server`, "cannot open the rank server") lets an admin deny access.
- Start order dependency: the Auth Server must be reachable for logins (login fails when the Auth link is down), and the database must be up at construction time because the rank registry is loaded in the constructor.

## 3. Startup and runtime flow

Entry point: `Server Lib/Rank Server/Rank Server/Rank Server.cpp`. Same pattern as the Message Server: `rank [anything] [working_dir]`; `argv[2]` if present is `chdir`ed into (also sets log prefix `RS`); `argv[1]` is ignored. Linux: unlimited core dump, nice value lowered by 2 (or 1). Runs `srs::rs::getInstance().start()` (`Singleton<rank_server>`).

Constructor order (`rank_server::rank_server`):

1. Base `server(m_player_manager, 1, 4, 4)` (engine params, meaning not verified here) and member construction: `m_rank_registry_manager` is constructed here and its constructor immediately calls `initialize()`, which loads both result sets from the DB (so the DB must already be reachable). A failure there only sets its internal state to "not loaded"; it is logged but not fatal.
2. `config_init()`: type = 4, then `CmdRankConfigInfo` reads `refresh_time_H` and `reg_date` from `pangya.pangya_rank_config` into `m_refresh_time`. A DB error here throws and marks the server `FAILURE`.
3. Loads `sIff` (`data/pangya_jp.iff`).
4. Registers packet handlers.

Refresh loop (all in `rank_server::onHeartBeat`, executed about once per second by the engine's monitor thread):

1. Preconditions: server state `INITIALIZED`, the atomic guard `m_sync_update_time_refresh == 0`, and `m_refresh_time.isOutDated()`.
2. `isOutDated()` is true when `last_refresh + interval_hours` (as a local time) is earlier than the current local time (`rank_refresh_time.cpp`, uses `STDA_10_MICRO_PER_HOUR`).
3. Sets the guard to 1 and enqueues `CmdUpdateRankRegistry` (procedure `pangya.GeraRankAll`) with callback `SQLDBResponse` (msg id 1).
4. Callback `updateTimeRefresh(ret, date)`:
   - ret 1 (success): store the returned date as `last_refresh`, call `m_rank_registry_manager.load()` (clears and re-reads both result sets), call `makeLog()`, reset the guard to 0.
   - ret 0 (error or an exception): log and reset the guard to 0, so the next heartbeat tries again (about a second later).

Because the guard is reset only after the DB call finishes, there is never more than one `GeraRankAll` in flight.

Console commands (`checkCommand`):

| Command | Effect |
|---|---|
| `exit` | shut down |
| `reload_files` | re-read `server.ini` and the rank config from DB, reload IFF (does not reload the rank data) |
| `reload_socket_config` | re-read `config/socket_config.ini` |
| `make_log` | dump all rank rows to a dated log file (see below) |
| `snapshot` | crash on purpose (null write) to create a core dump |

Auth Server commands: only `Shutdown`, `DisconnectPlayer` and `ConfirmSendInfoPlayerOnline` are handled. Notice, ticker, cube, mail, new-rate and reload-global-system commands are no-ops. The Auth opcode `0x01` is registered but `packet_as001` in `packet_func_rs.cpp` is an empty stub (only a commented example).

## 4. Configuration

### `server.ini`

Windows default `Server Lib/Rank Server/Rank Server/server.ini`; Linux default `Server Lib/Linux Builds/Rank Server/server.ini`. Keys are read by the shared engine (same readers as in `docs/message-server.md`).

| Section / key | Shipped value | Meaning |
|---|---|---|
| `[SERVERINFO] VERSION` | `IOCP Version Beta 1.1` | version string |
| `CLIENTVERSION` | `SS.R7.995.00` | client version |
| `NAME` | `Rank Server` | name in the server list |
| `GUID` | `4774` | server UID |
| `PORT` | `4774` | TCP listen port |
| `IP` | `127.0.0.1` (Linux file: `192.168.15.20`) | advertised address |
| `MAXUSER` | `2001` | max sessions |
| `PROPERTY` | `4096` | server property bitfield (not decoded here, unverified) |
| `[OPTION] SAME_ID_LOGIN` | `1` | present, but not read by Rank Server or engine code found (only Login/Game Server read it) |
| `[OPTION] TTL` | `0` | idle session timeout in ms, 0 = off |
| `[OPTION] ANTIBOTTTL` | absent | engine reads it; falls back to 1000 with an error line in the log |
| `[LOG] DIR` | `Log` | log directory; also used for the rank snapshot log files |
| `[NORMAL_DB]` | `DBENGINE=mssql` etc. | as in the message-server doc. Windows: `mssql`, `DBIP=pangya`, name/user/pass `pangya`, `DBPORT` commented. Linux file: `mysql`, `localhost`, `DBPORT=3306` |
| `[AUTHSERVER] IP` / `PORT` | `127.0.0.1` / `7777` | Auth Server address |

Comments in the file are Portuguese; see the Message Server doc for their translations (server name; TTL = time in ms to drop a silent player).

### `config/socket_config.ini`

Identical content to the Message Server's: `enable_ip_rules: 1`, `limit_connection_per_ip: 10`, `order: deny,allow`, `allow: 127.0.0.1`, `ddos_interval: 3000`, `ddos_count: 5`, `ddos_autoreset: 600000`. Note: with `order: deny,allow` and no `deny:` rule, every IP is accepted. `allow: 127.0.0.1` only exempts localhost from the DDoS and per-IP limits (verified in `Server Lib/Projeto IOCP/SOCKET/socket.cpp`, `connect_check`). To restrict access, add `deny: all` or use `order: mutual-failure`.

### Database-held configuration

Table `pangya.pangya_rank_config(index, refresh_time_H int default 0, reg_date datetime default CURRENT_TIMESTAMP)`:

- `refresh_time_H` = refresh interval **in hours**.
- `reg_date` = time of the last successful regeneration; `GeraRankAll` sets `reg_date = now()` after success.

The shipped dump `bk-squema-mysql.sql` creates this table (with `AUTO_INCREMENT=2`, hinting at one row) but I found no `INSERT` for it, so a row has to exist for the server to work (see quirks).

## 5. Network protocol

Port: `PORT`, default **4774** (equal to `GUID`).

Handshake (`rank_server::onAcceptCompleted`): server sends opcode `0x1388` with `int32 key`, `uint8 5` (commented "Type Rank Server"; note `m_si.tipo` is 4, so this is a protocol constant, not the server-list type) and a `string` current local date. A comment says that sending key -1 makes the client skip encryption.

Client -> server (registered in the constructor; handlers in `Server Lib/Rank Server/PACKET/packet_func_rs.cpp`):

| Opcode | Handler | Meaning |
|---|---|---|
| `0x00` | `requestLogin` | `uint32 uid`, `string id`, then a raw `search_dados_ex` struct (menu, item, `term_s5_type`, `class_type`, page, `active`) |
| `0x01` | `requestPlayerInfo` | `uint32 uid`, `string id`, `uint8 active`; replies with a player's full info (see `0x138A`). No authorization check, with a comment that the info normally arrives with the login/search packet |
| `0x02` | `requestSearchPlayerInRank` | `uint8 option` (0 = by nickname: `string nickname` + `search_dados`; 1 = by position: `uint32 position` + `search_dados`) |
| `0x03`, `0x04`, `0x05` | stubs | hex-dump logged, handler body is a commented `//rs->` call (nothing happens) |

Server -> client:

| Opcode | Content |
|---|---|
| `0x1388` | handshake (above) |
| `0x1389` | first page after login (`sendFirstPage`) |
| `0x138A` | player full info (`sendPlayerFullInfo`): `uint8 status` (0 OK, 1 error), then player/character info and the player's six Overall values in compact form |
| `0x138C` | search result page (`sendPageFoundPlayer`) / error `uint8 1` |
| `0x138B`, `0x138D` | registered as "do nothing after send" like all others; I found no sender for them (unverified) |

Layout of `0x1389` (`sendFirstPage`):

- On failure (`option != 0`): `uint8 option` + 14 zero bytes. The socket is then shut down.
- On success: `uint8 0`, `uint8 rank_menu`, `uint8 rank_menu_item`, `uint8 term_s5_type`, `uint8 class_type`, then the page block from `pageToPacket` (`uint32 page`, `uint32 total pages`, `uint16 count`, then per row: `uint32 uid, uint32 current_pos, uint32 last_pos, int32 value` followed by the player's character block; 10 zero bytes if the page or menu is empty), then the caller's own position: if the client sent `active` it calls `playerPositionToPacket`, else it sends `PPRT_NOT_TOP_RANK`.
- Position type enum (`ePLAYER_POSITION_RANK_TYPE`, `TYPE/pangya_rank_st.hpp`): `0 PPRT_IN_TOP_RANK` (has a record and is in the top), `1 PPRT_NOT_RANK` (no record), `2 PPRT_NOT_TOP_RANK` (has a record, not in the top). The comment in `sendFirstPage` words the same three states slightly differently; the enum names above are the code's own.

Login flow (`requestLogin`, then `confirmLoginOnOtherServer`):

1. Parse; reject uid 0, empty id, banned IP.
2. `CmdPlayerInfo` (`ProcGetPlayerInfoRank`): the DB id must match.
3. Account block flags: temporary, permanent, block-all-IP, block-MAC (log only), and block `rank_server`.
4. A prior session with the same UID gets disconnected.
5. If not yet authorized, ask the Auth Server `getInfoPlayerOnline(server_uid, uid)`. The reply (`authCmdConfirmSendInfoPlayerOnline`) must match uid, `option == 1`, account id and socket IP. On success `m_is_authorized = 1`, then `sendFirstPage(..., 0)`.

All requests except login/player-info require authorization. Searching returns errors via `0x138C` + status byte 1.

Flood limit (`checkPacket`): `CHK_PCKT_COUNT_LIMIT` (5, `Server Lib/Projeto IOCP/SOCKET/session.h`) + 2 = 7 repeated packets, then the session is disconnected with a DDoS warning.

### Rank menus and items (what the client can ask for)

From `TYPE/pangya_rank_st.hpp` (enum values are the wire values):

| Menu (`eRANK_MENU`) | Value | Items |
|---|---|---|
| `RM_OVERALL` | 0 | `eRANK_OVERALL`: 0 total points, 1 total score, 2 trophy points, 3 pang earned, 4 total holes, 5 achievement points |
| `RM_COURSE_RECORDS` | 1 | `eRANK_COURSE_RECORDS` 0..19: Blue Lagoon, Blue Water, Sepia Wind, Wind Hill, Wiz Wiz, West Wiz, Blue Moon, Silvia Cannon, Ice Cannon, White Wiz, Shinning Sand, Pink Wind, Deep Inferno, Ice Spa, Lost Seaway, Eastern Valley, Ice Inferno, Wiz City, Abbot Mine, Mystic Ruins |
| `RM_RECORDS` | 2 | `eRANK_RECORDS`: 0 albatross, 1 hole in one, 3 level, 4 total distance (2 is skipped) |
| `RM_COURSE_RECORDS_NATURAL` | 3 | same 20 courses (natural mode) |
| `RM_COURSE_RECORDS_GRAND_PRIX` | 4 | same 20 courses (Grand Prix) |

These map one-to-one to the DB columns `tipo_rank` (menu) and `tipo_rank_seq` (item). The procedure inserts exactly the combinations: `0,0..5`, `1,0..19`, `2,{0,1,3,4}`, `3,0..19`, `4,0..19` (verified by scanning `GeraRankAll`).

## 6. Database usage and how rankings are computed

### Commands (`Server Lib/Rank Server/PANGYA_DB/`)

| Class | SQL | Notes |
|---|---|---|
| `CmdRankConfigInfo` | `SELECT refresh_time_H, reg_date FROM pangya.pangya_rank_config` | two columns, filled into `rank_refresh_time` |
| `CmdUpdateRankRegistry` | `pangya.GeraRankAll` (no args) | result: 1 column = error/failure code, or 2 columns (`SUCCESS`, `reg_date`) |
| `CmdRankRegistryInfo` | `pangya.ProcGetRankRegistryInfo` | 6 columns: uid, current position, last position, value, `tipo_rank`, `tipo_rank_seq`; reads `pangya_rank_atual` ordered by menu/item, with the previous position taken from `pangya_rank_antes` (0 if none) |
| `CmdRankRegistryCharacterInfo` | `pangya.ProcGetRankRegistryCharacterInfo` | 85 columns: the account id/nickname/level plus the equipped character (parts, card slots, mastery...), from `pangya_rank_atual_character` |
| `CmdPlayerInfo` | `pangya.ProcGetPlayerInfoRank(uid)` | 8 columns: uid, id, nickname, capability, game server id, level, IDState, block time |

Shared commands used from the engine: `CmdInsertBlockIP` (`pangya.ProcInsertBlockIP`), `CmdRegisterServer` (`ProcRegServer_New`), server list, ban lists (same as in the Message Server doc). `CmdRateConfigInfo` is not called by the Rank Server.

### Tables

- `pangya_rank_atual`: current ranking rows `(index, position, UID, tipo_rank, tipo_rank_seq, valor, reg_date)`.
- `pangya_rank_antes`: the previous generation (same columns), used for the "last position" column.
- `pangya_rank_atual_character`: equipped character per ranked player.
- `pangya_rank_config`: refresh interval and last-run time.

### `GeraRankAll` (in `bk-squema-mysql.sql`; other dumps carry their own variants, which I did not compare)

1. Builds the whole ranking in a temp table `__tabRankTmp`, one block per (menu, item). Every block selects the metric, orders it (`ORDER BY ... DESC, UID`), numbers it with `@row_number` and appends it.
2. Only accounts with `FIRST_LOGIN + FIRST_SET = 2` (completed the first login and first setup) are included.
3. Metrics (from the SQL comments, translated): overall score from per-course best scores `(50 - best_score)` weighted by course star rating over modes 0, 51, 52 (normal, natural, Grand Prix), ignoring `best_score = 127`; trophy points from `trofel_stat` (gold 3, silver 2, bronze 1 weights per tier); total pang earned; total holes (holes minus hole-in-ones, divided by 18); achievement points; total overall (sum of overall parts); albatross count; hole-in-one count; level; total distance (the client divides by 10k itself); and per-course best score (`MIN(best_score)`, ties broken by `MAX(best_pang)`) for normal (`tipo = 0`), natural and Grand Prix (`tipo = 52`) records.
4. Safety check: only if the temp table has more than zero rows **and** at least as many rows as the current `pangya_rank_atual` does it commit: `pangya_rank_antes` is replaced by the old `atual`, `atual` is replaced by the temp rows, `pangya_rank_atual_character` is rebuilt from currently equipped characters, `pangya_rank_config.reg_date = now()`, and `SELECT 1 AS SUCCESS, reg_date` is returned. Otherwise it returns `SELECT 0 AS SUCCESS`, leaving the old ranking intact.

A second procedure `GeraRankAll_Ant` (older version) also exists in the dump; the C++ code calls only `GeraRankAll`.

### In-memory registry (`Server Lib/Rank Server/UTIL`)

- `RankRegistry`: one row `{uid, current_position, last_position, value}`. `toPacket` writes full values. `toCompactPacket` writes zeros for both positions when the current position is above 10 000 (`LIMIT_RANK_POSITION_TO_COMPACT_PACKET_SHOW`) and the value otherwise.
- `RankEntry = map<key_menu{menu,item}, map<key_position{position,uid}, RankRegistry>>`: ordered by position, so a page is a contiguous iterator range.
- `RankCharacter`: id, nickname, level, `CharacterInfo`, plus the two legacy "term s5 / class type" bytes that the client still expects. `RankCharacterEntry = map<uid, RankCharacter>`.
- `RankRegistryManager` (guarded by a recursive mutex): `load()` (clear + initialize), `pageToPacket`, `playerPositionToPacket`, `sendPlayerFullInfo`, `searchPlayerByNickname[AndSendPage]`, `searchPlayerByRank[AndSendPage]`, `makeLog`. `LIMIT_REGISTRY_FOR_PAGE = 12` rows per page. Page of a found player = `(position - 1) / 12` style computation (`FoundPlayer = pair<index in page, page>`).
- `isLoad()` is true only when `m_state` and both maps are non-empty. All serving paths throw "not loaded" otherwise, which turns into the failure replies above.
- `makeLog()` opens `<LOG DIR>/log Registros <date>[ <prefix>].log` (opened in append mode) and writes every character and every rank row as text. It is called after each successful refresh and by the `make_log` console command.
- `rank_refresh_time`: `interval_hours`, last-refresh date as both `SYSTEMTIME` and `FILETIME`; `isOutDated()` as above. `toString()` yields `Interval_Hora=<n>, Last_Refresh=<date>` (that "Hora" is Portuguese for hour).

## 7. Source layout

```
Server Lib/Rank Server/
  Rank Server.sln
  Rank Server/
    Rank Server.cpp               main()
    rank_server.hpp/.cpp          rank_server class (login, search, heartbeat refresh, auth commands)
    server.ini, config/socket_config.ini, data/pangya_jp.iff
  PACKET/packet_func_rs.*         opcode thunks (0x00-0x05, server responses, Auth 0x01)
  PANGYA_DB/cmd_*                 DB commands (section 6)
  SESSION/player.*, player_manager.*
  TYPE/pangya_rank_st.hpp         enums (menus/items), search_dados, key_menu/key_position, player_info
  TYPE/player_info.*
  UTIL/rank_registry.*            one rank row
  UTIL/rank_character.*           character display info per ranked player
  UTIL/rank_registry_manager.*    in-memory store, paging, search, log file
  UTIL/rank_refresh_time.*        interval + last-run date logic
```

## 8. Build and run notes

Windows: `Server Lib/Rank Server/Rank Server.sln` (toolset v141, `stdcpplatest`), with `Server Lib/Projeto IOCP` referenced by relative path.

Linux: `Server Lib/Linux Builds/Makefile-rs.mk`.

```
cd "Server Lib/Linux Builds"
make -f Makefile-rs.mk                      # debug (-Og -D_DEBUG -D SHOW_DEBUG_PACKET -ggdb)
make -f Makefile-rs.mk __build_type=release # release (-O3)
```

- Output: `output-rs-debug/rank` or `output-rs-release/rank`.
- Flags: `g++ -std=c++20 -fpermissive -fsigned-char` + glib-2.0 cflags. Link: `-pthread -ldl -lg` + libzip, mysqlclient and glib-2.0 (`pkg-config`), `-lodbc -lcrypto`.
- Differences from `Makefile-ms.mk`: no `-fvisibility=hidden`, no `-Wl,--export-dynamic`, it compiles the `UTIL/rank_*.cpp` files and the five Rank DB commands instead of the friend-related files.
- Run directory: `Server Lib/Linux Builds/Rank Server/` has `server.ini`, `config/socket_config.ini`, `data/pangya_jp.iff`. Put the `rank` binary there and run `./rank` (`Linux Builds/run-as-to-rs.sh` does this in tmux session "Rank Server" after the Auth, Login and Message servers; its base path `/home/acrisio/projects/Linux Builds` is hardcoded).
- The server only needs the DB to have the rank procedures/tables from the dump (`GeraRankAll`, `ProcGet*Rank*`, `pangya_rank_*`).

## 9. Known quirks, gotchas, hardcoded values

- **Seed the config row.** `pangya_rank_config` has no data in the dump. With no row, `CmdRankConfigInfo` returns nothing and the in-memory interval/date stay zero; `isOutDated()` then reports true. In `GeraRankAll` the final `SELECT ... FROM pangya_rank_config` also returns nothing, so the server would see "error" and retry roughly every second. This chain is inferred from the code and SQL, not run. Insert one row with your desired `refresh_time_H`.
- `refresh_time_H = 0` (the column default) means "always outdated": a regeneration starts again as soon as the previous one finishes. Pick a sensible number of hours.
- Time comparison uses the server's local time, and the DB date string is parsed by `_translateDate` (format assumptions unverified).
- Rankings are only as fresh as the last refresh. Players never see a live value.
- On a failed refresh the in-memory ranking is left unchanged. On a failed initial load the server still runs, but every ranking request fails until a refresh succeeds.
- `GeraRankAll` is heavy (many full-table scans and many temp inserts). While it runs, only the guard flag is held; the registry lock is taken only in the later `load()` (inferred from the code, not measured).
- `requestPlayerInfo` skips the authorization check on purpose (comment in code).
- `0x03`-`0x05` are placeholders; `0x138B` and `0x138D` are registered as "nothing to do" server packets but never sent by the code I read.
- Rank `m_si.tipo` is 4 but the handshake byte is 5.
- A 10 000-position cut-off applies to the compact (player-info) packet only. Paged lists show real positions.
- `search_dados` / `search_dados_ex` are read as raw packed structs straight from the packet (`readBuffer`), so a client with a different struct layout breaks parsing; the Linux build wraps the type headers in `#pragma pack(1)`.
- The ini key `SAME_ID_LOGIN` is shipped but unused here.
- Hardcoded: 12 rows per page, 10 000 compact limit, flood limit 7, log subdir default `Log`.
