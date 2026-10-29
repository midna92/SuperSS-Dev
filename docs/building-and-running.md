# Building and running

This guide covers building the C++ servers on Linux (the primary documented path) and Windows, preparing the runtime
directories and starting the stack. Component-specific details are in the other documents in this folder.
Nothing here was built or run while writing it: the commands are derived from the Makefiles, `.vcxproj` files and
`server.ini` files in the repo.

## 1. Linux

### 1.1 Prerequisites

From the link lines of `Server Lib/Linux Builds/Makefile-*.mk`:

| Need | Why |
|---|---|
| `g++` with C++20 support, `make`, `pkg-config` | all builds use `-std=c++20 -fpermissive -fsigned-char` |
| glib-2.0 dev package (`libglib2.0-dev`) | the Linux INI reader is GLib based |
| libzip dev package (`libzip-dev`) | reading `pangya_jp.iff` (a ZIP) |
| MySQL/MariaDB client dev package (`libmysqlclient-dev` or `libmariadb-dev`) | `pkg-config --libs mysqlclient`; linked even if you use another DB |
| unixODBC dev package (`unixodbc-dev`) | `-lodbc`; used for PostgreSQL and MSSQL |
| OpenSSL dev package (`libssl-dev`) | `-lcrypto` |
| An ODBC driver (only for PostgreSQL / MSSQL), e.g. `odbc-postgresql` | runtime |
| `tmux`, `node` | only for `run-as-to-rs.sh` and the Smart Calculator |

Package names are for Debian/Ubuntu and are my mapping of the libraries to packages, not taken from the repo.

### 1.2 Build

Run every makefile **from `Server Lib/Linux Builds`**: sources are referenced as `../<Dir>/...`.

```bash
cd "Server Lib/Linux Builds"

# 1) GameGuard static library, required by the Game Server
make -f Makefile-ggsrvlib.mk __build_type=release

# 2) Servers
make -f Makefile-as.mk   __build_type=release     # -> ./Auth Server/auth
make -f Makefile-ls.mk   __build_type=release     # -> ./Login Server/login
make -f Makefile-ms.mk   __build_type=release     # -> ./Message Server/msn
make -f Makefile-rs.mk   __build_type=release     # -> ./Rank Server/rank
make -f Makefile-gs.mk   __build_type=release     # -> ./Game Server/game

# 3) Optional
make -f Makefile-ggauth70.mk __build_type=release # ggauth701.so, copied to ./Game Server/
make -f Makefile-smart.mk    __build_type=release # SMARTCALCULATORLIB.so, copied to ./Game Server/ and ./Message Server/
make -f Makefile-iff.mk      __build_type=release # -> ./IFF Manager/iff
```

Without `__build_type=release` the default is a debug build (`-Og -D _DEBUG -D SHOW_DEBUG_PACKET -ggdb`) which prints
packet dumps. `make -f <file> clean` removes `*.o`/`*.d` of that makefile's build folder. Object files go to
`output-<abbr>-<debug|release>/`.

Known build issues (see [iocp-core-library.md](iocp-core-library.md)): `Makefile-test.mk` and `testcl.cpp` do not build
as-is. The `ggauth70` and `smart` makefiles copy the `.so` straight into the `Game Server` (and, for `smart`, `Message Server`)
folders, so those folders must exist (they do in the repo). The `*-stripped` rule of `Makefile-ggauth70.mk` references
`$(MESSAGESERVERDIR)`, which that makefile never defines.

### 1.3 Runtime directory layout

Each server reads `server.ini`, `config/socket_config.ini` and `data/pangya_jp.iff` from its **current working
directory**. The repository ships one such folder per server under `Server Lib/Linux Builds/`:

```
Linux Builds/
  Auth Server/     server.ini  config/socket_config.ini            (binary: auth)
  Login Server/    server.ini  config/  data/pangya_jp.iff         (binary: login)
  Message Server/  server.ini  config/  data/pangya_jp.iff         (binary: msn)   + SMARTCALCULATORLIB.so
  Rank Server/     server.ini  config/  data/pangya_jp.iff         (binary: rank)
  Game Server/     server.ini  config/  data/pangya_jp.iff  data/CounterItem.txt  csauth2.cfg  (binary: game) + SMARTCALCULATORLIB.so, ggauth701.so
```

The binaries only honour a different working directory as `argv[2]` (not `argv[1]`).

### 1.4 Configure

1. **Database**: create it and import a dump first, see [database.md](database.md). The Linux `server.ini` files default to
   `DBENGINE=mysql`, `DBIP=localhost`, `DBNAME=pangya`, `DBUSER=pangya`, `DBPASS=pangya`, `DBPORT=3306`.
   For `postgresql`/`mssql`, `DBIP` is an **ODBC DSN name** (e.g. `pangya`, see `Linux Builds/odbc.ini`) and `DBPORT` is ignored.
2. **Server identity**: every server needs a unique `GUID` and the right `IP`/`PORT`. The Game Server's Linux sample has
   `IP = 192.168.15.20`; change it to the address clients use.
3. **Auth link**: `[AUTHSERVER] IP/PORT` in each non-Auth server must point to the Auth Server (sample `127.0.0.1:7777`).
4. **INI syntax on Linux**: comments start with `#`, values are unquoted. Windows INI files use `;` and quotes; do not copy
   them over.
5. **IP rules**: see the warning in [architecture.md](architecture.md) about `socket_config.ini`.
6. **Game Server specifics** (`FLAG`, `PROPERTY`, channels, GameGuard): [game-server.md](game-server.md).

### 1.5 Run

```bash
cd "Server Lib/Linux Builds/Auth Server"    && ./auth
cd "../Login Server"                        && ./login
cd "../Message Server"                      && ./msn
cd "../Rank Server"                         && ./rank
cd "../Game Server"                         && ./game
```

Each is a foreground console process (commands such as `reload_socket_config` can be typed into it). Use `tmux`/`screen`
or a service manager to keep them running. `run-as-to-rs.sh` does this with tmux but contains hardcoded paths
(`/home/acrisio/projects/...`); edit `dir=` and the Node path before using it, and note that it starts the Game Server only
when called with `all`, and stops everything with `kill`.

Core dumps and logs: the servers write logs to the `[LOG] DIR` (sample: `Log`) and raise the core-dump limit.

## 2. Windows

Each server has its own solution, for example `Server Lib/Game Server/Game Server.sln`. The projects reference the shared
`Server Lib/Projeto IOCP` sources directly (there is no project for the engine itself). All 11 projects use platform
toolset **v141 (Visual Studio 2017)**; open them in VS 2017 or retarget the toolset in a newer Visual Studio. Types:
servers and tools are console applications, `GGSrvLib26-1` is a static library, `ggauth70-1` and `Smart Calculator lib` are DLLs.

Third-party headers/libraries (MySQL client, libzip, OpenSSL, nProtect `ggsrvlib*.lib`) are not fetched by the repo; the
`.vcxproj` files do not list explicit library paths, so check each project's settings and your VS user property sheets.
Use the Windows `server.ini` files in each `Server Lib/<Server>/<Server>/` folder (note `;` comments).

## 3. Optional services

- **Smart Calculator** (Node services): see [smart-calculator.md](smart-calculator.md). Start `node smart.js` before the C++ servers.
- **Web site**: see [web-and-apache.md](web-and-apache.md).
- **GG Auth Server**: Windows only, see [gg-auth-server.md](gg-auth-server.md).

## 4. First-run checklist

1. DB created, dump imported, DB user can reach it from the server host.
2. `pangya_rank_config` has a row with a sensible refresh interval (the dump has the table but no data, see [rank-server.md](rank-server.md)).
3. Unique `GUID`s; `IP`/`PORT` set; `[AUTHSERVER]` points to the Auth Server.
4. `data/pangya_jp.iff` present in every server's working directory (table version must match, see [iff-manager.md](iff-manager.md)).
5. `CLIENTVERSION` is only informational for the Login Server (not enforced); the Game Server's GameGuard check needs `csauth2.cfg` and `ggauth701.so` (Linux) next to the binary, or set `GAMEGUARDAUTH = 0` for testing.
6. `SAME_ID_LOGIN = 0` and tighter `socket_config.ini` before exposing anything to a network.
