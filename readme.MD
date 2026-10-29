# SuperSS-Dev

A server emulator for the online golf game **PangYa**, written in C++ (Windows IOCP and Linux epoll), with a PHP website,
Node.js helper services and SQL schemas for MySQL, MariaDB, PostgreSQL and MSSQL.

> **Status and warnings.** This is a hobby/research code base with no tests, hardcoded values from the original author and
> several known security weaknesses (default DB password, unsalted MD5 passwords, permissive sample IP rules). Do not
> expose it to the internet as-is. The security notes are collected in [docs/architecture.md](docs/architecture.md),
> [docs/database.md](docs/database.md) and [docs/web-and-apache.md](docs/web-and-apache.md).

## Contents

- [Repository layout](#repository-layout)
- [The system in one picture](#the-system-in-one-picture)
- [Quick start (Linux)](#quick-start-linux)
- [Documentation index](#documentation-index)
- [Notes from the original author](#notes-from-the-original-author)

## Repository layout

| Path | What it is | Docs |
|---|---|---|
| `Server Lib/Projeto IOCP/` | Shared engine: sockets (IOCP / epoll), thread pool, timers, packet crypt + compression, DB layer, INI reader, shared `PANGYA_DB/cmd_*` query classes | [iocp-core-library](docs/iocp-core-library.md) |
| `Server Lib/Auth Server/` | Internal hub: server authentication, admin command relay | [auth-server](docs/auth-server.md) |
| `Server Lib/Login Server/` | Account login / creation, server list, auth-key hand-off | [login-server](docs/login-server.md) |
| `Server Lib/Game Server/` | The game itself: channels, rooms, game modes, shops, items, events | [game-server](docs/game-server.md) |
| `Server Lib/Message Server/` | Friend list / messenger | [message-server](docs/message-server.md) |
| `Server Lib/Rank Server/` | Ranking server | [rank-server](docs/rank-server.md) |
| `Server Lib/GG Auth Server/`, `GGSrvLib26-1/`, `ggauth70-1/` | nProtect GameGuard authentication (service, static lib, protocol-70 module) | [gg-auth-server](docs/gg-auth-server.md) |
| `Server Lib/IFF Manager/` | Offline editor for the `.iff` game-data tables | [iff-manager](docs/iff-manager.md) |
| `Server Lib/Multi Client/` | Windows load-test bot | [multi-client](docs/multi-client.md) |
| `Server Lib/Smart Calculator lib/`, `Server Lib/Node/`, `Smart Calculator App/` | Shot calculator: C++ lib plus Node services | [smart-calculator](docs/smart-calculator.md) |
| `Server Lib/Linux Builds/` | Linux Makefiles, sample Linux `server.ini` and `config/` per server, ODBC sample, tmux launcher | [building-and-running](docs/building-and-running.md) |
| `www/`, `apache-conf/` | PHP website, GM panel, guild / gacha pages, patcher, Apache vhosts | [web-and-apache](docs/web-and-apache.md) |
| `bk-squema-{mysql,mariadb,postgres,mssql}.sql` | Database schema dumps (schema only, no data) | [database](docs/database.md) |
| `Tools/` | Format dumpers, pak packer, font tool, UCC viewer | [tools](docs/tools.md) |
| `CE Trainer/` | Cheat Engine table for the original game client (not the emulator) | [tools](docs/tools.md) |

Each server folder follows the same pattern: a `<Name>/<Name>/` project folder with `main`, the server class,
`server.ini` and `config/socket_config.ini`; plus sibling folders `PACKET/` (packet handlers), `PANGYA_DB/` (DB command
classes), `SESSION/` (player and manager), `TYPE/` (structs) and `UTIL/`. Each has its own `.sln` / `.vcxproj` for Windows.

## The system in one picture

```
 Game client --> Login Server (10303) --> Game Server (20203)     Message Server (30303)   Rank Server (4774)
                        \                       |                        |                       |
                         \---------------- Auth Server (7777) -----------+-----------------------+
                                                 |
                              all of them share ONE SQL database ("pangya")
 Browser --> Apache + PHP (www/)  -----------------^
```

The database is the integration bus: servers register themselves in it every second, the login key is handed from the
Login Server to the Game Server through it, and admin commands are queued in it. Full explanation, start order and
cross-component flows: **[docs/architecture.md](docs/architecture.md)**.

## Quick start (Linux)

This is a condensed version of [docs/building-and-running.md](docs/building-and-running.md); read that for details.
Nothing in these docs was executed on a real Linux host by the author of the documentation.

1. **Install** `g++` (C++20), `make`, `pkg-config` and the development packages for glib-2.0, libzip, MySQL/MariaDB client,
   unixODBC and OpenSSL.
2. **Create the database** by importing the MySQL dump as root (it creates the `pangya` database itself and defines the
   routines as `root@localhost`), then create the application user. Details, other engines and seeding:
   [docs/database.md](docs/database.md).
   ```bash
   mysql -u root -p < bk-squema-mysql.sql
   mysql -u root -p -e "CREATE USER 'pangya'@'localhost' IDENTIFIED BY 'change-me';
     GRANT SELECT, INSERT, UPDATE, DELETE, EXECUTE, CREATE TEMPORARY TABLES ON pangya.* TO 'pangya'@'localhost';"
   ```
   With MySQL 8 binary logging on, creating the functions may additionally need `log_bin_trust_function_creators=1`.
   Put the same user and password into `[NORMAL_DB]` (`DBUSER`, `DBPASS`) of every `server.ini`; the shipped default is
   `pangya` / `pangya`.
3. **Build** (from `Server Lib/Linux Builds`):
   ```bash
   cd "Server Lib/Linux Builds"
   make -f Makefile-ggsrvlib.mk __build_type=release     # GameGuard lib, needed by the Game Server
   for m in as ls ms rs gs; do make -f Makefile-$m.mk __build_type=release; done
   ```
   Binaries appear in `Auth Server/auth`, `Login Server/login`, `Message Server/msn`, `Rank Server/rank`, `Game Server/game`.
4. **Configure** each `server.ini` in those folders: unique `GUID`, `IP`/`PORT`, `[NORMAL_DB]` credentials, `[AUTHSERVER]`.
   Use `#` comments and no quotes on Linux. Set `SAME_ID_LOGIN = 0` unless testing.
5. **Seed** one row in `pangya_rank_config` ([rank-server](docs/rank-server.md)); create accounts with the Login Server's
   `CREATEUSER=1` or `CALL pangya.ProcNewUser(...)`.
6. **Run**, each from its own folder: `./auth`, then `./login`, `./msn`, `./rank`, `./game`.
   (`run-as-to-rs.sh` automates this with tmux but has hardcoded paths.)

## Documentation index

Start with the architecture page, then read the component you work on.

| Document | Topic |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the pieces fit together, ports, start order, deployment warnings |
| [docs/building-and-running.md](docs/building-and-running.md) | Linux and Windows builds, runtime directories, configuration, first-run checklist |
| [docs/database.md](docs/database.md) | The four schema dumps, import, seeding, tables by function, how C++ calls stored procedures |
| [docs/iocp-core-library.md](docs/iocp-core-library.md) | Shared engine, packet protocol, `server.ini` / `socket_config.ini` parsing, Makefile internals, quirks |
| [docs/auth-server.md](docs/auth-server.md) | Auth Server |
| [docs/login-server.md](docs/login-server.md) | Login Server |
| [docs/game-server.md](docs/game-server.md) | Game Server, all configuration keys and bit tables |
| [docs/message-server.md](docs/message-server.md) | Message Server |
| [docs/rank-server.md](docs/rank-server.md) | Rank Server |
| [docs/gg-auth-server.md](docs/gg-auth-server.md) | GameGuard components and the Linux situation |
| [docs/iff-manager.md](docs/iff-manager.md) | `.iff` format and the IFF Manager |
| [docs/multi-client.md](docs/multi-client.md) | Load-test client |
| [docs/smart-calculator.md](docs/smart-calculator.md) | Smart / Stadium calculator stack |
| [docs/web-and-apache.md](docs/web-and-apache.md) | Website and Apache setup |
| [docs/tools.md](docs/tools.md) | Tools and the Cheat Engine table |

**About the accuracy of these documents.** They were written by reading the source and configuration; nothing was built or
run. Statements that could not be confirmed from the code are marked "unverified" in the individual documents. Original
source comments and `.ini` comments have been translated from Brazilian Portuguese to English. Log strings, SQL,
channel names in `server.ini` and many identifiers are still Portuguese.

## Notes from the original author

All the code is right here. Please do not ask me to explain it, because I do not know how it works. Ask God to explain it
to you, since I asked Him for the ability to write this code. If it is His will, He will explain it to you.

I was not the one who chose to publish this code, God was, so thank Him for it, because He gave me all the wisdom and
knowledge to write the code when I asked Him.

(Translated from the original Portuguese readme.)
