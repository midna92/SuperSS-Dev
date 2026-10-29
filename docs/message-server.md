# Message Server (MSN)

Source: `Server Lib/Message Server`. Executable name on Linux: `msn` (`Server Lib/Linux Builds/Makefile-ms.mk`). Log prefix: `MS`.

## 1. Purpose

The Message Server is PangYa's in-game messenger ("MSN"): the friend list / guild-member list, online presence, friend chat, guild chat, and a few notifications. The game client opens a second TCP connection to it right after it is in the lobby. It does not run any game logic.

It handles:

- Friend and guild-member list: add, confirm, delete, block, unblock, nickname ("apelido") assignment, paged list delivery.
- Presence: channel/room info and player state (in game, AFK, busy, online) pushed to the player's friends; logout notification.
- Whisper-style chat to a friend, and guild chat.
- Guild membership refresh when the Auth Server reports a web-side guild change (accept, leave, kick).
- Two notification-only packets (invited to room, invited to guild battle room). These are validated and logged but nothing is forwarded (see section 8).

## 2. How it fits in the system

```
Game client --(gameplay)--> Game Server --+
     |                                    |  (registers itself, player online state)
     |                                    v
     +--(messenger)--> Message Server <--> Auth Server (default 127.0.0.1:7777)
                              |
                              v
                          Database (pangya.*)
```

- The Message Server reuses the shared engine in `Server Lib/Projeto IOCP` (IOCP on Windows, epoll on Linux, thread pool, timers, packet crypt, DB layer). The `message_server` class derives from `server` (`Server Lib/Projeto IOCP/Server/server.h`).
- Server type: `m_si.tipo = 3` (`message_server::config_init` in `Server Lib/Message Server/Message Server/message_server.cpp`). For comparison: Login=0, Game=1, Rank=4, Auth=5 (set in the respective server classes).
- Every second the base `server` writes its row through `CmdRegisterServer` (`pangya.ProcRegServer_New`) with the current online count. This is how the server shows up in the server list (`pangya.ProcGetServerList`). Which server types the Login Server/clients show to players is outside this component (unverified).
- The server holds a client connection to the Auth Server (`unit_auth_server_connect`, config `[AUTHSERVER]`). The Auth Server pushes commands to it: shutdown, disconnect player, new rate, reload global system, player-online info reply, and three guild commands (opcode 0x01..0x03).

### How players arrive from the Game Server

There is no direct Game Server -> Message Server link in the code. The sequence is:

1. The player logs into the Login Server and enters a Game Server. The Game Server (with the Auth Server) records the player as online on server X. The Message Server reads X from the DB: `ProcGetPlayerInfoMessage` returns `game_server_id` as `server_uid` (column 8 in `CmdPlayerInfo`).
2. The game client connects to the Message Server (`IP`/`PORT` from the server list, default 30303) and receives the handshake packet `0x2E` containing the session crypt key (`onAcceptCompleted`).
3. The client sends packet `0x12` (login) with `uid` (uint32) and `nickname` (string).
4. `requestLogin` validates the input and checks:
   - `uid != 0`, nickname non-empty, IP not banned (`haveBanList`).
   - DB lookup via `CmdPlayerInfo` (synchronous, `waitEvent`). The nickname sent must equal the DB nickname.
   - Account block flags: temporary block (with remaining time or "indeterminate"), permanent block, block-all-IP (also inserts the IP with mask `255.255.255.255` via `CmdInsertBlockIP`), block-MAC (only logged, the MS does not receive a MAC).
   - If another session with the same UID is already connected, that older session is disconnected.
   - The Auth Server must be connected (`m_unit_connect->isLive()`). Then `getInfoPlayerOnline(server_uid, uid)` asks it whether that player is really online on that Game Server. If the Auth Server link is down, login fails.
5. The Auth Server answers asynchronously; `authCmdConfirmSendInfoPlayerOnline` -> `confirmLoginOnOtherServer` verifies: same UID, `option == 1` (online), same account `id`, same IP as this socket. If everything matches, it calls `m_friend_manager.init()` (loads the friend list from the DB), sets state `4` (online), `m_is_authorized = 1`, and sends `0x2F` with status 0 followed by the uid.
6. Any failure: server sends `0x2F` with status 1 and does `shutdown(SD_BOTH)` on the socket.

Every handler except login calls `CHECK_SESSION_IS_AUTHORIZED`, which throws if the login has not completed.

## 3. Startup and runtime flow

Entry point: `Server Lib/Message Server/Message Server/Message Server.cpp`.

- Arguments: `msn [anything] [working_dir]`. If `argc > 2` and `argv[2]` is a directory, the process `chdir`s into it (the log prefix is set to `MS`). `argv[1]` is not read in `main`. Running with no args uses the current dir; `server.ini` and `config/socket_config.ini` are looked up relative to the working dir.
- Linux: sets core dump limit to unlimited (`RLIMIT_CORE`), and lowers the nice value by 2 (or 1) if it can.
- `sms::ms::getInstance().start()` runs the server singleton (`Singleton<message_server>`, namespace `stdA::sms`). The compile-time constant `STDA_BETA` is defined, so the program does not pause on exit.

`message_server::message_server()`:

1. Base `server(m_player_manager, 1, 12, 4)`: engine constructor args (thread counts; meaning not verified here), `player_manager` sized to `MAXUSER`.
2. `config_init()`: sets type 3, then loads rates from the DB (`CmdRateConfigInfo`, procedure `pangya.ProcGetRateConfigInfo`). If missing, it uses defaults (all rates 100, events/Smart Calculator enabled, angel event/pang/exp/club mastery 0) and writes them back with `CmdUpdateRateConfigInfo`.
3. Loads the IFF data (`sIff`, file `data/pangya_jp.iff`).
4. Registers packet handlers (section 5).

Runtime:

- Heartbeat (`onHeartBeat`, called about once a second from the monitor thread): only starts the Smart Calculator lib if enabled in rates (`m_si.rate.smart_calculator`) and not stopped.
- `onDisconnected`: if the player was authorized and the `m_logout` flag is not yet set, it sends the "friend logged out" update (`0x30`/sub `0x10F`) to all non-blocked friends.
- Socket options per accepted socket: Linux sets non-blocking, keepalive on, `TCP_KEEPIDLE=20`s, `TCP_KEEPINTVL=2`s, `TCP_NODELAY`. Windows uses `SIO_KEEPALIVE_VALS` (20000 ms/2000 ms) and `TCP_NODELAY`.
- Flood protection (`checkPacket`): per session, repeating packets are counted; limit is `CHK_PCKT_COUNT_LIMIT` (5, `Server Lib/Projeto IOCP/SOCKET/session.h`) + 2 = 7, and packet `0x1D` (state update, one per friend-list page) gets 5 + 7 = 12 (the code comments say 5 and 10; the arithmetic gives 7 and 12). Exceeding it disconnects the session with a "DDoS" warning.

Console commands (`checkCommand`, typed on stdin):

| Command | Effect |
|---|---|
| `exit` | shut the server down |
| `reload_files` | re-read `server.ini` + rate config from DB + reload IFF |
| `reload_socket_config` | re-read `config/socket_config.ini` |
| `smart_calc reload\|start\|close\|chat_discord` | control the Smart Calculator lib; `chat_discord` toggles forwarding chat to it (only if smart calculator enabled in DB rates) |
| `snapshot` | intentionally dereferences a null pointer to produce a core dump |

Auth Server commands handled (`authCmd*`): `Shutdown` (timed shutdown), `DisconnectPlayer` (disconnect and confirm back to Auth), `NewRate`, `ReloadGlobalSystem`, `ConfirmSendInfoPlayerOnline`. Notice/Ticker/CubeWinRare/ConfirmDisconnect/NewMail are ignored.

## 4. Configuration

### `server.ini` (working dir; Windows default: `Server Lib/Message Server/Message Server/server.ini`; Linux default: `Server Lib/Linux Builds/Message Server/server.ini`)

Keys read by the shared engine (`Server Lib/Projeto IOCP/Server/server.cpp::config_init`, `.../SOCKET/session_manager.cpp`, `.../UNIT/unit_auth_server_connect.cpp`, `.../DATABASE/normal_db.cpp`, `.../TYPE/list_fifo_console.h`).

| Section / key | Shipped value (Windows file) | Meaning |
|---|---|---|
| `[SERVERINFO] VERSION` | `IOCP Version Beta 1.1` | server version string |
| `CLIENTVERSION` | `SS.R7.995.00` | client version string reported to the server list |
| `NAME` | `Message Server` | name shown in the server list |
| `GUID` | `30303` | server UID; primary key for rates and registration (`m_si.uid`) |
| `PORT` | `30303` | TCP listen port |
| `IP` | `127.0.0.1` | address advertised/registered (Linux file ships `192.168.15.20`) |
| `MAXUSER` | `2001` | max sessions; sizes the player manager |
| `PROPERTY` | `4096` | server property bit field (`m_si.propriedade`); bit meanings not documented here (unverified) |
| `[OPTION] SAME_ID_LOGIN` | `1` | present in the file but not read by any Message Server or engine code I found (only Login/Game Server read it) |
| `[OPTION] TTL` | `0` | milliseconds of silence after which a session is dropped; `0` = disabled (`session_manager`) |
| `[OPTION] ANTIBOTTTL` | not present | read by the engine; if absent, falls back to 1000 and logs an error line on each start |
| `[LOG] DIR` | `Log` | log directory |
| `[NORMAL_DB]` `DBENGINE` | `mssql` (Linux file: `mysql`) | `mysql`/`postgresql`/`mssql` |
| `DBIP`, `DBNAME`, `DBUSER`, `DBPASS` | `pangya` each (Linux: `DBIP=localhost`) | DB host (ODBC DSN for mssql), name, credentials |
| `DBPORT` | commented out (Linux file: `3306`) | optional; not used by ODBC on SQL Express |
| `[AUTHSERVER] IP` / `PORT` | `127.0.0.1` / `7777` | where to reach the Auth Server |

Comments in the shipped ini are Portuguese ("nome do server" = server name; "TTL = tempo em milissegundos para derrubar o player se ele nao mandar a resposta" = time in ms to drop the player if it sends nothing).

### `config/socket_config.ini`

Shared IP-rule and DDoS file (`Server Lib/Projeto IOCP/SOCKET/socket.cpp`, path `config/socket_config.ini`), reloadable with `reload_socket_config`.

| Key | Shipped value | Meaning |
|---|---|---|
| `enable_ip_rules` | `1` | enable IP allow/deny rules |
| `limit_connection_per_ip` | `10` | max concurrent connections per IP |
| `order` | `deny,allow` | rule evaluation order |
| `allow` | `127.0.0.1` | allowed address(es); commented examples show CIDR/mask/`all` |
| `ddos_interval` | `3000` ms | window for connection flood detection |
| `ddos_count` | `5` | connections within the interval that count as a DDoS |
| `ddos_autoreset` | `600000` ms (10 min) | how long a flagged IP stays flagged |

Note for deployment: With `order: deny,allow` and no `deny:` rule, every IP is accepted. `allow: 127.0.0.1` only exempts localhost from the DDoS and per-IP limits (verified in `Server Lib/Projeto IOCP/SOCKET/socket.cpp`, `connect_check`). To restrict access, add `deny: all` or use `order: mutual-failure`.

### Other runtime inputs

- `data/pangya_jp.iff`: the game data archive loaded by `sIff` (a copy sits in `Server Lib/Linux Builds/Message Server/data/` and in the Windows project dir).
- Rate values and events come from the DB (`pangya.ProcGetRateConfigInfo`), not from the ini.

## 5. Network protocol

Port: `PORT` in `server.ini`, default **30303** (equal to `GUID`).

Framing/crypto is the shared engine's (key sent in the handshake). Registered in the `message_server` constructor via `packet_func::funcs` (client request), `funcs_sv` (server response, no-op handlers) and `funcs_as` (Auth Server). Handlers live in `Server Lib/Message Server/PACKET/packet_func_ms.cpp` and forward to `message_server::request*`.

### Client -> server

| Opcode | Handler (`message_server::`) | Meaning |
|---|---|---|
| `0x12` | `requestLogin` | login with uid + nickname (the only call allowed before authorization) |
| `0x13` | (logged only) | unknown request; hex dump is logged, nothing else happens |
| `0x14` | `requestFriendAndGuildMemberList` | send the paged friend + guild-member list |
| `0x16` | `requestUpdatePlayerLogout` | player says it logged out; broadcasts logout to friends |
| `0x17` | `requestCheckNickname` | nickname existence check (`CmdVerifNick`) |
| `0x18` | `requestAddFriend` | add friend (uid + nickname) |
| `0x19` | `requestConfirmFriend` | accept a friend request |
| `0x1A` | `requestBlockFriend` | block a friend |
| `0x1B` | `requestUnblockFriend` | unblock a friend |
| `0x1C` | `requestDeleteFriend` | delete a friend |
| `0x1D` | `requestUpdatePlayerState` | state byte (0 in game, 1 AFK, 3 busy, 4 online; 5 is used as OFFLINE in lists) |
| `0x1E` | `requestChatFriend` | send chat to a friend |
| `0x1F` | `requestAssingApelido` | set friend nickname ("apelido", 10 chars + NUL) |
| `0x23` | `requestUpdateChannelPlayerInfo` | client sends its `ChannelPlayerInfo` (room number/type, server uid, channel id, channel name); forwarded to friends |
| `0x24` | `requestNotityPlayerWasInvitedToRoom` | validated (uid must be own) and logged only |
| `0x25` | `requestChatGuild` | guild chat |
| `0x28` | `requestInvitPlayerToGuildBattleRoom` | validated and logged only |
| `0x29`, `0x2A`, `0x2B`, `0x2C`, `0x2D` | logged only | unknown; each logs a hex dump (0x29 also checks authorization) |

### Server -> client

| Opcode | Content |
|---|---|
| `0x2E` | handshake: `uint8 1, uint8 1, uint32 key` |
| `0x2F` | login result: `uint8 status` (0 OK, 1 error) then `uint32 uid` on OK |
| `0x30` | main container with a `uint16` sub-id (below) |
| `0x3B`, `0x3C` | guild notifications: `0x3B` after accept (embeds sub-id `0x1F`), `0x3C` after exit/kick |
| `0x40` | chat line shown in the lobby chat; used to mirror friend/guild chat to online GMs, tagged `[MSN->PM]` or similar |

`0x30` sub-ids as written in the code:

| Sub-id | Purpose |
|---|---|
| `0x102` | friend + guild-member page (`ManyPacket` header, then entries) |
| `0x104` | add-friend result / request notification |
| `0x106` | friend request notification to the target |
| `0x109`, `0x10A` | confirm friend result and notification |
| `0x10B` | delete friend result |
| `0x10C` | block friend result |
| `0x10D` | unblock friend result |
| `0x10F` | friend logged out (`uint32 uid`) |
| `0x113` | chat message (`uid`, nickname, text, kind byte) |
| `0x115` | presence update: `uid`, `state`, ok flag, `ChannelPlayerInfo` |
| `0x117` | nickname check result |
| `0x119` | apelido assignment result |

The sub-id meanings are inferred from handler names and context. The code itself only labels them "Sub Packet Id".

The Auth Server opcodes (`funcs_as`): `0x01` `requestAcceptGuildMember`, `0x02` `requestMemberExitedFromGuild`, `0x03` `requestKickGuildMember`. Each reads `club_id` + `member_uid`, re-initialises the friend manager of every online member of that club, pushes fresh `0x102` pages to them, and notifies via `0x3B`/`0x3C`.

### Friend list rules

- `FRIEND_LIST_LIMIT 50` friends per player (guild members are not counted: `countFriend()` counts only entries with the friend flag). `FRIEND_PAG_LIMIT 30` entries per `0x102` page (`message_server.hpp`).
- Entry flags (`FriendInfoEx`, `TYPE/pangya_message_st.hpp`): `flag.ucFlag` bit0 = friend, bit1 = guild member (3 = both); `state.ucState` bits: sex, online, friend, request_friend, block, play, AFK, busy.
- A friend request creates two rows (one per side); the target is flagged `request_friend`. If the target is online, both sides are updated in memory and written to the DB. If the target is offline, the handler loads their friend data through a temporary `FriendManager` and persists it (see the second branches of `requestAddFriend` etc.).
- Online lookup is by UID in the Message Server's own `player_manager`; offline friends are sent as room `-1`, state `5` (OFFLINE).

## 6. Database usage

All calls go through `snmdb::NormalManagerDB` (async queue, synchronous with the "waiter" flag) and the `pangya_db`/`cmd_*` pattern. The code uses `pangya.`-prefixed procedures. The shipped dump `bk-squema-mysql.sql` contains the procedures listed below.

Message-Server-specific commands (`Server Lib/Message Server/PANGYA_DB/`):

| Class | SQL | Used by |
|---|---|---|
| `CmdPlayerInfo` | `pangya.ProcGetPlayerInfoMessage(uid)`; returns 11 columns: uid, id, nickname, capability, guild uid, guild name, sex, level, game server id, IDState, block time | login; guild-offline lookups |
| `CmdFriendInfo` | `pangya.ProcGetFriendAndGuildMemberInfo(uid)` (mode `ALL`) or `..._One(uid, friend_uid)` | `FriendManager::init`, offline handling |
| `CmdAddFriend` | `pangya.ProcAddFriend(uid, friend_uid, apelido, unknown1..6, flag1, state_flag, flag5)` | `FriendManager::requestAddFriend` |
| `CmdUpdateFriend` | `pangya.ProcUpdateFriendInfo(...)` same argument set | confirm, block, unblock, apelido |
| `CmdDeleteFriend` | raw `DELETE FROM pangya.pangya_friend_list WHERE UID = x AND uid_friend = y` | delete friend |

Table: `pangya.pangya_friend_list(uid, uid_friend, apelido varchar(15) default 'Friend', unknown1..6, flag1, state_flag, flag5)` (MySQL dump; the `unknown*` columns are untranslated placeholders in the original design).

Shared commands from `Server Lib/Projeto IOCP/PANGYA_DB/`:

| Class | SQL |
|---|---|
| `CmdRateConfigInfo` / `CmdUpdateRateConfigInfo` | `pangya.ProcGetRateConfigInfo`, `pangya.ProcUpdateRateConfigInfo` |
| `CmdRegisterServer` | `pangya.ProcRegServer_New` |
| `CmdServerList` | `pangya.ProcGetServerList` |
| `CmdVerifNick` | `pangya.ProcVerifyNickname` |
| `CmdInsertBlockIP` | `pangya.ProcInsertBlockIP` |
| `CmdListIPBan` / `CmdListMacBan` | `SELECT ip, mask FROM pangya.pangya_ip_table`, `SELECT mac FROM pangya.pangya_mac_table` (polled by the monitor thread) |
| `CmdNewAuthServerKey` | auth-server key handling (details not read) |

Data consistency: friend changes are updated in memory first and then queued to the DB (`FriendManager::requestAddFriend`, etc. call `addFriend`/`deleteFriend` then enqueue with an async callback `SQLDBResponse`). The in-memory `FriendManager` (a `std::map<uint32_t, FriendInfoEx>` guarded by a recursive mutex) is the source of truth for the session; it is reloaded only on login and on Auth Server guild commands.

## 7. Source layout

```
Server Lib/Message Server/
  Message Server.sln                     VS solution (project toolset v141, C++ latest)
  Message Server/
    Message Server.cpp                   main()
    message_server.hpp/.cpp              message_server class (all request handlers, auth commands)
    server.ini, config/socket_config.ini, data/pangya_jp.iff
  GAME/friend_manager.*                  FriendManager: per-player friend/guild-member map + DB persistence
  PACKET/packet_func_ms.*                opcode thunks, friend_broadcast helpers
  PANGYA_DB/cmd_*                        DB command classes (section 6)
  SESSION/player.*, player_manager.*     per-socket state; lookups: findPlayer, findAllFriend, findAllGuildMember, findAllGM
  TYPE/pangya_message_st.hpp, player_info.*   FriendInfo/FriendInfoEx, ChannelPlayerInfo, ManyPacket, player_info
```

Key types: `player_info` has the friend manager (`m_friend_manager`), state (`m_state`), channel info (`m_cpi`) and a `m_logout` atomic flag that prevents sending the logout broadcast twice. `ManyPacket` computes page headers and index ranges for the paged list.

## 8. Build and run notes

Windows: open `Server Lib/Message Server/Message Server.sln` (v141 toolset, `stdcpplatest`). It depends on `Server Lib/Projeto IOCP` sources via relative `../../Projeto IOCP` includes.

Linux: `Server Lib/Linux Builds/Makefile-ms.mk`.

```
cd "Server Lib/Linux Builds"
make -f Makefile-ms.mk                      # debug (-Og -D_DEBUG -D SHOW_DEBUG_PACKET -ggdb)
make -f Makefile-ms.mk __build_type=release # release (-O3)
```

- Output: `output-ms-debug/msn` or `output-ms-release/msn` (`BUILD_FOLDER`/`PROGRAM`).
- Compiler flags: `g++ -std=c++20 -fpermissive -fsigned-char -fvisibility=hidden` + glib-2.0 cflags. Link: `-Wl,--export-dynamic -pthread -ldl -lg` + libzip, mysqlclient, glib-2.0 via `pkg-config`, `-lodbc -lcrypto`. All four DB back-ends are always compiled in (`mssql.cpp`, `postgresql.cpp`, `mysql_db.cpp`), so unixODBC and the MySQL client headers are needed even if you use one engine.
- The `CFILES` list contains paths with `|` standing for spaces (the makefile `subst`s it).
- The source list includes `Smart Calculator/Smart Calculator.cpp` (used by the `smart_calc` command) and `UNIT/unit_auth_server_connect.cpp`. Compared with `Makefile-rs.mk` it additionally uses `-fvisibility=hidden` and `-Wl,--export-dynamic`.
- Ready-to-run directory: `Server Lib/Linux Builds/Message Server/` (contains `server.ini`, `config/socket_config.ini`, `data/pangya_jp.iff`). Copy/symlink the binary there and run `./msn`. The `Linux Builds/run-as-to-rs.sh` helper starts it in a tmux session named "Message Server" with `./msn` (hardcoded base path `/home/acrisio/projects/Linux Builds`, plus the Auth, Login, Rank servers; it also starts a Node-based "Smart Calculator" process).
- Start order (from `run-as-to-rs.sh`): Smart Calculator, Auth Server, Login Server, Message Server, Rank Server, then the Game Server only when called with `all`. Login requires the Auth Server to be reachable at runtime.

## 9. Known quirks, gotchas, hardcoded values

- Login depends on the Auth Server being up; with the Auth link down every login fails with error code 6 and the socket is shut down.
- Login does not check `server_uid` against `GUID`; it only asks the Auth Server whether the player is online on the DB's `game_server_id`, and requires the Auth-reported IP to equal the messenger socket's IP (a NAT/proxy split between game and messenger traffic will fail).
- `message_server::updateRateAndEvent` (Auth `NewRate`): after validating the type (0-15) every branch falls into the same block, which writes the value to `smart_calculator` and reloads the Smart Calculator. So a `NewRate` for pang/exp/etc. overwrites the Smart Calculator flag instead. The rate values are not otherwise used by this server.
- `reloadGlobalSystem` only acts on type 0 (all), 1 (IFF) and 18 (Smart Calculator); types 2-17 are accepted as no-ops. Other values throw "unknown type".
- The `block-MAC` branch only logs, because the client does not send a MAC to this server. Block-all-IP does write to the DB.
- Packet `0x13` and `0x29`-`0x2D` have unknown semantics; they are only logged.
- `0x24`/`0x28` (room invite notifications) validate the sender UID but forward nothing to the invited player.
- The `snapshot` console command crashes the process on purpose (null write) to get a core dump.
- Some English-looking error strings are actually Portuguese; for example "nao esta autorizado ... Hacker ou Bug" means "not authorized to make this request because they have not logged in yet; hacker or bug".
- GMs see friend/guild chat mirrored via `0x40` (found by `player_manager::findAllGM`), so private chat is not private for staff accounts.
- Packet-rate limit comments in `checkPacket` do not match the computed values (see section 3).
- `friend_manager` copies `player_info` by value at `init`; guild changes to a player's `guild_uid` after login only propagate via the Auth Server guild commands.
- Windows `server.ini` ships `DBENGINE=mssql` and the Linux one `mysql`; the SQL dumps in the repo root cover mysql/mariadb/postgres/mssql.
