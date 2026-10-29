# Auth Server

Source: `Server Lib/Auth Server` (executable name `auth` on Linux, solution `Server Lib/Auth Server/Auth Server.sln` on Windows).

Facts below are taken from the code. Anything not verified is marked "unverified". Portuguese strings are translated where cited.

## 1. Purpose

The Auth Server is the internal message hub between the other PangYa servers (Login, Game, Message, Rank, GG Auth). It never talks to game clients. It does three things:

1. Accepts long-lived connections from the other servers and authenticates them with a one-time key stored in the database (`pangya.pangya_auth_key`).
2. Relays server-to-server requests: "disconnect player X on server Y", "give me info on player X from server Y", and generic "send command / reply to server (UID or type)".
3. Polls the database once per second for administrative commands (notice, ticker, shutdown, new rate, reload, kick from website, new mail) written by a web panel or other tools into `pangya.pangya_command`, and pushes them to the target servers. It also triggers the daily guild ranking update.

## 2. How it fits in the system

- Class: `stdA::auth_server` (`Server Lib/Auth Server/Auth Server/auth_server.hpp`), derived from `unit` (`Server Lib/Projeto IOCP/UNIT/unit.hpp`). `unit` is a lighter variant of the shared `server` class and has no client-facing protocol logic. Singleton accessor: `sas::as::getInstance()`.
- Other servers embed `unit_auth_server_connect` (`Server Lib/Projeto IOCP/UNIT/unit_auth_server_connect.cpp`, based on `unit_connect`), which is the client side of this protocol. `server::server()` creates it and `server::start()` starts it. The Login Server confirms this: `Server Lib/Projeto IOCP/Server/server.cpp` lines ~80 and ~1975.
- The Auth Server is optional at runtime for the Login Server: `login_server::requestDownPlayerOnGameServer` checks `m_unit_connect->isLive()` and falls back to a direct DB update (see `docs/login-server.md`).
- Start order: start the database first, then the Auth Server, then the others. The other servers retry the connection every 5 seconds (`hEventContinueMonitor->wait(5000)` in `unit_connect.cpp` ~line 969/973), so strict ordering is not required. The helper script `Server Lib/Linux Builds/run-as-to-rs.sh` starts Auth, Login, Message, Rank, and optionally Game in separate tmux sessions, in that order.

Server types (`m_si.tipo`, set in each server's `config_init`):

| tipo | Server | Source |
|---|---|---|
| 0 | Login Server | `Server Lib/Login Server/Login Server/login_server.cpp` |
| 1 | Game Server | `Server Lib/Game Server/Game Server/game_server.cpp` |
| 3 | Message Server | `Server Lib/Message Server/Message Server/message_server.cpp` |
| 4 | Rank Server | `Server Lib/Rank Server/Rank Server/rank_server.cpp` |
| 5 | Auth Server | `auth_server.cpp` `config_init()` |
| 6 | GG Auth Server | `Server Lib/GG Auth Server/GG Auth Server/gg_auth_server.cpp` |

(Type 2 is not assigned anywhere I found.) `CmdServerList::TYPE_SERVER` in `Server Lib/Projeto IOCP/PANGYA_DB/cmd_server_list.hpp` uses a different numbering (GAME=0, MSN=1, LOGIN=2, RANK=3, AUTH=4) for the `ProcGetServerList` argument. Do not mix the two.

## 3. Startup and runtime flow

`main()` in `Server Lib/Auth Server/Auth Server/Auth Server.cpp`:

1. If `argc > 2` and `argv[2]` is a non-empty directory, `chdir` into it, set the log prefix to `AS`, and reload the log files. `argv[1]` is not used. The working directory matters because `server.ini` and `config/socket_config.ini` are read relative to it.
2. Windows: installs a structured-exception handler that writes a mini dump, then sets normal priority class. Linux: sets the core-dump limit to unlimited and lowers the nice value by 2 (or 1).
3. Calls `sas::as::getInstance().start()`.

`auth_server::auth_server()`:

- Base `unit(m_player_manager, 4 /*DB instances*/, 4 /*job threads*/)` reads `server.ini`, creates the DB manager pool of 4 connections, and starts three threads: monitor, disconnect-session, and accept.
- `m_player_manager(*this, m_si.max_user)` pre-allocates `MAXUSER` `player` sessions.
- `config_init()` sets `m_si.tipo = 5`.
- Registers packet handlers 0x01 to 0x07 (incoming from servers) and no-op server-side handlers 0x00 to 0x0E (`packet_svFazNada`).

Per-second monitor loop (`unit::monitor` in `Server Lib/Projeto IOCP/UNIT/unit.cpp`, ~lines 218 to 320):

1. Rolls the log files at midnight, and restarts dead threads.
2. `checkSessionLive()` drops sessions that exceeded `TTL`.
3. Registers itself in the DB: `CmdRegisterServer(m_si)` calls `pangya.ProcRegServer_New(uid, name, ip, port, tipo, max_user, curr_user, rate.pang, version, version_client, property, angelic_wings_num, ...)`. This is how every server (including the Auth Server) appears in the server list. It also acts as a heartbeat.
4. `cmdUpdateServerList()` fetches the Game server list (`CmdServerList::GAME` / `pangya.ProcGetServerList`) and calls `updateServerList`.
5. Calls `auth_server::onHeartBeat()`, which does the DB polling described in section 6.
6. Sleeps 1 second.

Console commands (`auth_server::checkCommand`, typed on stdin):

| Command | Effect |
|---|---|
| `exit` | Leave the console loop and shut down. |
| `reload_files` | Re-reads `server.ini` (`unit::config_init` plus `config_init`). |
| `reload_socket_config` | Re-reads `config/socket_config.ini` on the accept socket. |
| `shutdown [seconds]` | Sends packet 0x02 (time) to all connected servers, then shuts itself down after `seconds`. With 0 or no argument it waits 5 seconds. |
| `snapshot` | Deliberately writes through a null pointer to produce a crash dump. On Linux this kills the process. |

## 4. Network protocol and ports

- Listens on `[SERVERINFO] PORT` (sample: 7777). Default sample IP is `127.0.0.1`.
- Packet framing, crypt and compression come from the shared engine (`Server Lib/Projeto IOCP/PACKET`, `CRYPT`, `COMPRESS`). Outgoing packets are built with `makeFull(session->m_key)`.

### Handshake

1. On accept (`auth_server::onAcceptCompleted`), the Auth Server sends a raw, uncompressed, unencrypted packet `0x00`: `int32 key` (the session crypt key) and `int32 auth_server_uid` (`GUID`).
2. The connecting server (`unit_auth_server_connect::requestFirstPacketKey`) stores the key, then calls `pangya.ProcGetNewAuthServerKey(server_uid)`. That procedure generates a random 16-character upper-case hex key and inserts or updates the row in `pangya.pangya_auth_key` with `valid = 1`.
3. It replies with packet `0x01`: `uint32 tipo`, `uint32 uid`, `string name`, `string key`.
4. `auth_server::requestAuthenticPlayer` loads the key row (`CmdAuthServerKey`: `SELECT server_uid, key, VALID FROM pangya.pangya_auth_key WHERE server_uid = <uid>`) and compares it with the received key. It also requires `valid == 1`.
5. On success it marks the key used (`CmdUpdateAuthServerKey` calls `pangya.ProcUpdateAuthServerKey`, with `valid = 0`) and sets `m_is_authorized = 1`. It then replies with packet `0x01`: `uint32 oid`.
6. On any failure the session is disconnected.

Consequence: trust is anchored in the shared database. Any process that can call `ProcGetNewAuthServerKey` for a UID can connect as that server. Restrict Auth Server access with `socket_config.ini` (see section 5).

Every other request calls `CHECK_SESSION_IS_AUTHORIZED` first and throws if the handshake has not completed.

### Packets from servers to the Auth Server

Registered in `auth_server::auth_server()`, handlers in `Server Lib/Auth Server/PACKET/packet_func_as.cpp`:

| ID | Handler | Payload | Behaviour |
|---|---|---|---|
| 0x01 | `requestAuthenticPlayer` | `uint32 tipo, uint32 uid, string id, string key` | Handshake above. |
| 0x02 | `requestDisconnectPlayer` | `uint32 player_uid, uint32 server_uid` | Forwards 0x06 to server `server_uid` (`player_uid`, requester uid, force=0). If the server is not connected, replies 0x07 `player_uid` to the requester so it can proceed. |
| 0x03 | `requestConfirmDisconnectPlayer` | `uint32 player_uid, uint32 server_uid` | If `server_uid` is not the Auth Server, forwards 0x07 `player_uid` to that server. |
| 0x04 | `requestInfoPlayer` | `uint32 server_uid, uint32 player_uid` | Forwards 0x0B (requester uid, `player_uid`) to `server_uid`. If not found, answers 0x0C with `int32 -1` as the error marker. |
| 0x05 | `requestConfirmSendInfoPlayer` | `uint32 req_server_uid, int32 option, uint32 uid [, string id, string ip if option==1]` | Forwards as 0x0C to the requesting server. |
| 0x06 | `requestSendCommandToOtherServer` | `CommandOtherServerHeader { uint32 send_server_uid_or_type; uint16 command_id }` plus opaque command buffer | Looks up a server by UID. If none matches it treats the number as a server type and sends to all servers of that type except the sender. Forwards as 0x0D (`uint32 from_uid, uint16 command_id, buffer`). Errors if no target. |
| 0x07 | `requestSendReplyToOtherServer` | same header and buffer | Forwards as 0x0E to one server (UID only, no type fan-out). |

### Packets from the Auth Server to servers

Handlers on the receiving side are in `Server Lib/Projeto IOCP/UNIT/unit_auth_server_connect.cpp`:

| ID | Meaning | Payload |
|---|---|---|
| 0x00 | Hello (raw) | `int32 key, int32 server_uid` |
| 0x01 | Authenticated | `int32 oid` |
| 0x02 | Shutdown | `uint32 seconds` (the receiver reads it as int32) |
| 0x03 | Broadcast notice | `string message` |
| 0x04 | Broadcast ticker | `string nickname, string message` |
| 0x05 | Cube win rare notice | `uint32 option, string message` |
| 0x06 | Disconnect player | `uint32 player_uid, uint32 requesting_server_uid, uint8 force` |
| 0x07 | Disconnect confirmed | `uint32 player_uid` |
| 0x08 | New mail in mailbox | `uint32 player_uid, uint32 msg_id` |
| 0x09 | New rate | `uint32 rate_type, uint32 amount` |
| 0x0A | Reload system | `uint32 type` |
| 0x0B | Request player info | `uint32 requesting_server_uid, uint32 player_uid` |
| 0x0C | Player info reply | `uint32 server_uid, int32 option, uint32 uid [, string id, string ip]`. `option` is -1 when the Auth Server could not reach the target. |
| 0x0D | Command from another server | `uint32 from_uid, uint16 command_id, buffer` |
| 0x0E | Reply from another server | `uint32 from_uid, uint16 command_id, buffer` |

The Login Server ignores 0x03, 0x04, 0x05, 0x08, 0x09 and 0x0A (`login_server::authCmd*`), and implements shutdown, disconnect player and confirm disconnect.

## 5. Configuration

### `server.ini` (read by `unit::config_init`, `session_manager::config_init`, `normal_db.cpp`)

Samples: Windows `Server Lib/Auth Server/Auth Server/server.ini` (uses `mssql`, comment style `#`) and Linux `Server Lib/Linux Builds/Auth Server/server.ini` (uses `mysql`, `localhost`, port 3306).

| Section / key | Meaning | Sample | Notes |
|---|---|---|---|
| `[SERVERINFO] VERSION` | Server build string, registered in DB. | `IOCP Version Beta 1.1` | Stored in a 40-byte buffer. |
| `CLIENTVERSION` | Client version string registered in DB. | `SS.R7.995.00` | The Auth Server does not use it otherwise. |
| `NAME` | Server name, registered in DB. | `Auth Server` | |
| `GUID` | Unique server UID. Used as `m_si.uid`, sent in the hello packet, key of the DB auth key. | `80808` | Must be unique across all servers. |
| `PORT` | Listen port. | `7777` | Other servers' `[AUTHSERVER] PORT` must match. |
| `IP` | IP registered in the DB (what other components see). | `127.0.0.1` | The listening socket binding is in the engine (unverified whether it binds to this IP or to any). |
| `MAXUSER` | Number of pre-allocated sessions, so maximum connected servers. | `20` | |
| `PROPERTY` | Server property flags registered in the DB. | `0` | |
| `[OPTION] TTL` | Milliseconds of silence before a session is dropped. `0` disables it. | `0` | Read by `session_manager::config_init`. |
| `[LOG] DIR` | Log directory. | `Log` | Read by the message pool (unverified in detail). |
| `[NORMAL_DB] DBENGINE` | `mysql`, `postgresql` or `mssql`. | | |
| `DBIP`, `DBNAME`, `DBUSER`, `DBPASS` | DB credentials. | `localhost`/`pangya`/`pangya`/`pangya` | |
| `DBPORT` | DB port. | `3306` | Optional. The Windows sample comments it out because ODBC with SQL Express does not use it. |

There is no `[AUTHSERVER]` section: the Auth Server is the target, not a client.

### `config/socket_config.ini`

Parsed by `socket.cpp` (`Server Lib/Projeto IOCP/SOCKET/socket.cpp`, ~lines 700 to 720, defaults ~849). The Linux sample for this server:

| Key | Value | Meaning |
|---|---|---|
| `enable_ip_rules` | `1` | Turn IP allow/deny on. |
| `limit_connection_per_ip` | `10` | Max simultaneous connections per IP. |
| `order` | `deny,allow` | Rule evaluation order. |
| `allow` | `127.0.0.1` | Whitelist entry. Exempts localhost from DDoS and per-IP limits but does NOT block other IPs under `order: deny,allow` (see `iocp-core-library.md`). Other examples are commented out: `192.168.0.0/16`, `10.0.0.0/255.0.0.0`, `all`. |
| `ddos_interval` | `3000` | Window in ms. |
| `ddos_count` | `5` | Connections within the window that count as a DDoS. |

If the other servers run on different hosts, add their IPs here, or the connection is denied ("IP Negado"). Comment lines use `//`.

## 6. Database polling and command dispatch

`auth_server::onHeartBeat` runs every second:

1. `CmdCommandInfo` calls `pangya.ProcGetCommands()` and returns rows with `valid = 1` and (`reserveDate IS NULL` or `now() >= reserveDate`), ordered by `regDate`. The procedure is defined in `bk-squema-mysql.sql`. Table `pangya.pangya_command` has columns `idx, command_id, arg1..arg5, target, regDate, reserveDate, flag, valid`.
2. `translateCmd()` iterates the rows. It additionally skips rows whose `reserveDate` is in the future.
3. Each command is delivered to `target`. `FIND_TARGET_AND_SEND` first tries `target` as a server UID (`findSessionByUID`). If that fails it treats it as a server type and broadcasts to every connected server of that type. If nothing matches it logs an error and the command stays in the table.

`COMMAND_ID` enum (`Server Lib/Auth Server/TYPE/pangya_auth_st.h`, values are the DB `command_id`):

| id | Name | Reads | Sends | Marks row invalid? |
|---|---|---|---|---|
| 0 | `BROADCAST_NOTICE` | `pangya.ProcGetNotice(idx)` (`CmdNoticeInfo`) | 0x03 `message` | No call in Auth Server (unverified whether the procedure does it) |
| 1 | `BROADCAST_TICKER` | `pangya.ProcGetTicker(idx)` (`CmdTickerInfo`: nick, message) | 0x04. Excludes the server in `arg2` (the originating server UID). | No (same note) |
| 2 | `BROADCAST_CUBE_WIN` | `ProcGetNotice(idx)` | 0x05 with `arg2` as option | No (same note) |
| 3 | `SHUTDOWN` | `pangya.ProcGetShutdownServer(idx)` returns seconds | 0x02. If `target` equals the Auth Server's UID or type (5), sends to all connected servers and shuts itself down. | No (same note) |
| 4 | `NEW_ITEM_NOTICE` | `arg1`=player UID, `arg2`=msg id | 0x08 | Yes, `ProcUpdateCommand` |
| 5 | `NEW_RATE` | `arg1`=rate type, `arg2`=amount | 0x09 | Yes |
| 6 | `ADM_KICK_FROM_WEBSITE` | `arg1`=player UID | 0x06 with force=1 and requester = Auth Server UID | Yes |
| 7 | `RELOAD_SYSTEM` | `arg1`=type | 0x0A | Yes |

Unknown ids are only logged. The "No" rows are an observation about the C++ side only. Check the stored procedures in the SQL dumps if you need to know whether they flip `valid`, otherwise the same notice could be re-sent every second.

Guild ranking:

- On the first heartbeat, `CmdGuildRankingUpdateTime` calls `pangya.ProcGetGuildRankingUpdateTime()` to initialize `m_guild_ranking_time`.
- When the local date is newer (year, month or day field smaller than today's), it queues `CmdUpdateGuildRanking` (`pangya.USP_UPDATE_GUILD_RANKING`) on DB queue 3 and sets `m_guild_ranking_time` to now. So the ranking updates once per day while the Auth Server is up.

### DB objects used (all in schema `pangya`)

| Class | Statement | File |
|---|---|---|
| `CmdRegisterServer` | `pangya.ProcRegServer_New` | `Server Lib/Projeto IOCP/PANGYA_DB/cmd_register_server.cpp` |
| `CmdServerList` | `pangya.ProcGetServerList` | `.../cmd_server_list.cpp` |
| `CmdAuthServerKey` | `SELECT ... FROM pangya.pangya_auth_key` | `.../cmd_auth_server_key.hpp` |
| `CmdUpdateAuthServerKey` | `pangya.ProcUpdateAuthServerKey` | `.../cmd_update_auth_server_key.hpp` |
| `CmdNewAuthServerKey` (used by the clients) | `pangya.ProcGetNewAuthServerKey` | `.../cmd_new_auth_server_key.hpp` |
| `CmdCommandInfo` | `pangya.ProcGetCommands` | `Server Lib/Auth Server/PANGYA_DB/cmd_command_info.hpp` |
| `CmdUpdateCommand` | `pangya.ProcUpdateCommand` | `.../cmd_update_command.hpp` |
| `CmdNoticeInfo` | `pangya.ProcGetNotice` | `.../cmd_notice_info.hpp` |
| `CmdTickerInfo` | `pangya.ProcGetTicker` | `.../cmd_ticker_info.hpp` |
| `CmdShutdownInfo` | `pangya.ProcGetShutdownServer` | `.../cmd_shutdown_info.hpp` |
| `CmdGuildRankingUpdateTime` | `pangya.ProcGetGuildRankingUpdateTime` | `.../cmd_guild_ranking_update_time.hpp` |
| `CmdUpdateGuildRanking` | `pangya.USP_UPDATE_GUILD_RANKING` | `.../cmd_update_guild_ranking.hpp` |

DB queue ids passed to `NormalManagerDB::add(idx, ...)` are 0 (waiting reads), 1 (command updates), 2 (key update), 3 (guild ranking). Only the 4 connections created in the constructor serve them.

## 7. Source layout

```
Server Lib/Auth Server/
  Auth Server.sln
  Auth Server/   Auth Server.cpp (main), auth_server.{hpp,cpp} (hub class), server.ini, config/socket_config.ini, pch.*
  PACKET/        packet_func_as.{h,cpp}: handlers 0x01-0x07 and send helpers
  PANGYA_DB/     cmd_command_info, cmd_update_command, cmd_notice_info, cmd_ticker_info,
                 cmd_shutdown_info, cmd_guild_ranking_update_time, cmd_update_guild_ranking
  SESSION/       player.{hpp,cpp}, player_manager.{hpp,cpp}: session type, find by uid / oid / server type
  TYPE/          pangya_auth_st.h (CommandInfo, COMMAND_ID, TickerInfo, player_info), player_info.*
```

Shared code: `Server Lib/Projeto IOCP/UNIT/unit.*`, `SOCKET/`, `DATABASE/`, `PANGYA_DB/`, `THREAD POOL/`, `TIMER/`.

## 8. Build and run

Windows: open `Server Lib/Auth Server/Auth Server.sln` in Visual Studio (project `Auth Server/Auth Server.vcxproj`, uses `pch.h`; links `ws2_32.lib` and `DbgHelp.lib`).

Linux (`Server Lib/Linux Builds/Makefile-as.mk`):

- Run from `Server Lib/Linux Builds/`, since all source paths are relative (`../Auth Server/...`, `../Projeto IOCP/...`). Spaces in directory names are encoded with `|` in the file list.
- `make -f Makefile-as.mk` builds a Debug binary by default (`-Og -D _DEBUG -D SHOW_DEBUG_PACKET -ggdb`, which prints packet dumps). `make -f Makefile-as.mk __build_type=release` builds release (`-O3`).
- Compiler flags: `g++ -std=c++20 -fpermissive -fsigned-char`, glib-2.0 include flags from `pkg-config`.
- Link libraries: `-pthread -lg -ldl`, libzip, mysqlclient, `-lodbc`, `-lcrypto`, glib-2.0 (all via `pkg-config` where noted). Install the development packages for those, and `epoll` support comes from `Projeto IOCP/IOCP/epoll.cpp`.
- Objects go to `output-as-debug` or `output-as-release`. The link step copies the binary to `./Auth Server/auth`, which is the runtime directory.
- Runtime directory `Server Lib/Linux Builds/Auth Server/` holds `server.ini` and `config/socket_config.ini`. Run `cd "Auth Server" && ./auth` (no arguments needed). To start from another directory, the code only honours `argv[2]` (not `argv[1]`), so pass a placeholder first, for example `./auth x "/path/to/dir"`.
- `make -f Makefile-as.mk clean` removes `*.o` and `*.d` from the output folder.
- ODBC settings for the SQL Server driver live in `Server Lib/Linux Builds/odbc.ini` (relevance only for `mssql`).

## 9. Known quirks and gotchas

- The Windows and Linux sample `server.ini` files differ: Windows defaults to `mssql` with host `pangya`; Linux defaults to `mysql` on `localhost:3306`.
- Auth key handshake is single-use per connection: the key row is set `valid = 0` after use, and every new connection of the other server generates a fresh key. Therefore all servers must use the same database as the Auth Server. A missing or invalid (`valid = 0`) `pangya_auth_key` row causes authentication to fail and the session is dropped.
- `requestAuthenticPlayer` does `memcpy(_session.m_pi.id, readString().c_str(), sizeof(id))` with `sizeof(id)` = 22. For server names shorter than 22 bytes this reads past the end of the temporary string (undefined behaviour). The "id" is the server `NAME` sent in packet 0x01, and it is copied to `nickname` for log lines only.
- `translateCmd` skips a command silently when the target server is down, and for the notice/ticker/cube/shutdown types it does not mark the row done. Whether the stored procedure consumes the row is unverified.
- `auth_server::onDisconnected` only logs. No cleanup of pending commands.
- `TTL` is 0 (disabled) for the Auth Server on purpose, since other servers keep the connection open and may be idle. A DDoS-style limiter per packet id exists in `checkPacket` (`CHK_PCKT_COUNT_LIMIT` = 5 identical packet ids in `CHK_PCKT_INTERVAL_LIMIT` = 1000 ms, defined in `Server Lib/Projeto IOCP/SOCKET/session.h`). A server spamming the same packet id (for example bursts of 0x06) would be disconnected. Whether the interval check is active is unverified: the time-based part is commented out in `auth_server::checkPacket` and it relies on `m_check_packet.checkPacketId` / `incrementCount`.
- Log messages mix English and Portuguese ("nao encontrou o Server" = "did not find the Server").
