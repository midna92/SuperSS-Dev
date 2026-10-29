# Architecture overview

SuperSS-Dev is a server emulator for the online golf game PangYa. It consists of several independent
processes that all share **one database** and one C++ engine (`Server Lib/Projeto IOCP`). This page shows how the
pieces fit together. Details for each component are in the per-component documents linked below.

## Component map

```
                          +---------------------------+
                          |   SQL database "pangya"   |   MySQL / MariaDB / PostgreSQL / MSSQL
                          +--+------+------+------+---+   (shared by everything below)
                             |      |      |      |
        +--------------------+      |      |      +----------------------+
        |                           |      |                             |
 +------+-------+   +---------------+--+ +-+----------+  +---------------+--+   +-----------------+
 | Auth Server  |<--| Login Server     | | Game Server|  | Message Server   |   | Rank Server     |
 | 7777         |   | 10303            | | 20203      |  | 30303            |   | 4774            |
 | internal hub |<--+------------------+ +-----+------+  +------------------+   +-----------------+
 +------+-------+<-------------------------+   |   (Message and Rank Server also connect to Auth)
        |                                      |
        | polls pangya_command (1x/s)          | GameGuard check in-process (GGSrvLib26-1)
        v                                      | or optional GG Auth Server (7788, Windows only)
   admin commands: notice, ticker,             |
   shutdown, rate, kick, mail                  | SMARTCALCULATORLIB.so (Game + Message Server)
                                               v
                                    node smart.js (127.0.0.1:12345) --HTTPS--> Stadium-Calculator (:8000)

 Game client  --->  Login Server (login)  --->  Game Server (play)  ;  Message Server (friends/chat)  ;  Rank Server (rankings)
 Web browser  --->  Apache + PHP (www/, port 447 HTTPS / 7273 HTTP)  ---> same database
```

| Component | Port (sample config) | Role | Doc |
|---|---|---|---|
| Auth Server | 7777 | Internal hub: servers register and authenticate here; relays admin commands | [auth-server.md](auth-server.md) |
| Login Server | 10303 | Account login, account creation, server list, auth-key hand-off | [login-server.md](login-server.md) |
| Game Server | 20203 | Channels, lobbies, rooms, all game modes, shops, items, events | [game-server.md](game-server.md) |
| Message Server | 30303 | Friend list / messenger | [message-server.md](message-server.md) |
| Rank Server | 4774 | Serves rankings computed by the database | [rank-server.md](rank-server.md) |
| GG Auth Server | 7788 | Optional, Windows only: nProtect GameGuard auth service | [gg-auth-server.md](gg-auth-server.md) |
| IFF Manager | - | Offline editor for the game's `.iff` data tables | [iff-manager.md](iff-manager.md) |
| Multi Client | - | Windows load-test bot | [multi-client.md](multi-client.md) |
| Smart Calculator | 12345 / 8000 | Shot calculator (C++ lib + Node services) | [smart-calculator.md](smart-calculator.md) |
| Web (PHP + Apache) | 447 / 7273 / 80 | Website, GM panel, guild/gacha pages, patcher | [web-and-apache.md](web-and-apache.md) |
| Core engine | - | Shared IOCP/epoll networking, DB layer, crypto | [iocp-core-library.md](iocp-core-library.md) |
| Tools | - | Format dumpers, pak tool, font tool | [tools.md](tools.md) |
| Database | - | Schema, stored procedures, import | [database.md](database.md) |

## How the processes cooperate

1. **The database is the integration bus.** There is no direct socket between Login Server and Game Server.
   The Game Server registers itself in the DB every second (`ProcRegServer_New`), the Login Server reads the
   server list from it, and the one-time login key travels through the DB (`ProcGeraAuthKeyLogin` /
   `ProcGetAuthKeyLogin`). All servers must therefore use the same database.
2. **The Auth Server authenticates the servers to each other.** On start, each server asks the DB for a one-time key
   (`ProcGetNewAuthServerKey`), connects to the Auth Server (`[AUTHSERVER]` in `server.ini`) and presents it.
   Connections are retried every 5 seconds, so the start order is not strict, but the Message Server and Rank Server
   cannot accept logins without the Auth link.
3. **Admin commands go through the DB.** The Auth Server polls `pangya.pangya_command` once a second and relays
   notice, ticker, shutdown, rate, reload, kick and mail commands to the target servers. The PHP admin panel
   (`www/pangya/pangya.php?adm`) writes into that table.
4. **The client talks to several servers directly.** After login, the game client connects to the Game Server and, on
   its own, to the Message Server and Rank Server. Those two ask the Auth Server whether the player is online.
5. **The web side shares the DB, not the C++ config.** The PHP site has its own DB configuration
   (`www/pangya/config/config.inc`); keep it in sync with the `[NORMAL_DB]` sections of the C++ servers by hand.
6. **Game data comes from `data/pangya_jp.iff`**, a ZIP of 57 tables. Every server loads it relative to its working
   directory, so each server must be started from the folder that contains `server.ini`, `config/` and `data/`.

## Recommended start order

1. Database
2. `node smart.js` (only if you use the Smart Calculator)
3. Auth Server
4. Login Server, Message Server, Rank Server
5. Game Server (retries the Auth link by itself)

`Server Lib/Linux Builds/run-as-to-rs.sh` automates this with tmux (Game Server only with the argument `all`), but it
has hardcoded `/home/acrisio/...` paths and must be edited before use.

## Shared engine in one paragraph

All C++ servers derive from the `server` base class in `Server Lib/Projeto IOCP/Server/server.cpp`. It reads
`<cwd>/server.ini`, opens the log, connects to the database (`[NORMAL_DB]`), loads `config/socket_config.ini`
(IP rules and DDoS protection) and starts the accept socket, the worker thread pool, timers and a monitor thread.
Windows uses I/O completion ports, Linux uses an epoll port plus a `WinPort.cpp` shim that emulates the Windows API.
Packets are framed, obfuscated and optionally LZO-compressed, then dispatched through per-server packet tables
(`PACKET/packet_func_*.cpp`). Database access goes through `PANGYA_DB/cmd_*` classes, each wrapping one query or stored
procedure. See [iocp-core-library.md](iocp-core-library.md).

## Things to know before you deploy

- The shipped `config/socket_config.ini` does **not** restrict access to localhost (`order: deny,allow` with only an
  `allow:` line accepts everyone). Add `deny: all` or use `order: mutual-failure` to whitelist.
- Game Server `SAME_ID_LOGIN = 1` lets anyone who knows an account's uid/id log in without a valid auth key.
  Set it to 0 outside of tests.
- The Login Server logs the MD5 hash of each password; treat its logs as sensitive. Passwords are stored as unsalted MD5.
- Rates (`EXPRATE`, `PANGRATE`, `CLUBMASTERYRATE`) in `server.ini` are not read; they come from the database.
- On Linux, idle timeouts and the per-IP connection limit behave differently from Windows (see the quirks section of
  [iocp-core-library.md](iocp-core-library.md)).
