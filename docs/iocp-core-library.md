# IOCP core library (`Server Lib/Projeto IOCP`)

## Purpose

`Server Lib/Projeto IOCP` (Portuguese for "IOCP project") is the shared engine that every C++ server in this repository compiles in from source. It isn't built as a separate library: each server's Visual Studio project and Linux makefile lists the `.cpp` files it needs. The engine provides:

- networking: an IOCP backend on Windows and an epoll port on Linux, sockets, sessions, and IP rules with DDoS protection;
- the thread pool, timers and job queue;
- packet framing, encryption (XOR with a key table) and LZO compression;
- packet dispatch tables;
- the database layer (MySQL native, MSSQL and PostgreSQL through ODBC) and the shared `cmd_*` query classes;
- logging, the INI reader, and a Windows-API shim for Linux;
- the server base classes: `server` for client-facing servers and `unit` for the Auth Server and GG Auth Server listeners;
- the Auth Server client connector (`unit_auth_server_connect`);
- the hook that loads the Smart Calculator shared library.

Everything lives in `namespace stdA`.

## How it fits in the system

| Base class | File | Used by |
|---|---|---|
| `stdA::server` (derives `threadpl_server` → `threadpool`, and `IUnitAuthServer`) | `Server Lib/Projeto IOCP/Server/server.cpp` | Login, Game, Message, Rank servers (`login_server.cpp:81`, `game_server.cpp:126`, `message_server.cpp:56`, `rank_server.cpp:48`) |
| `stdA::unit` (derives `threadpl_unit` → `threadpool`) | `Server Lib/Projeto IOCP/UNIT/unit.cpp` | Auth Server (`auth_server.cpp:54`), GG Auth Server (`gg_auth_server.cpp:28`) |
| `stdA::client` (derives `threadpl_client`) | `Server Lib/Projeto IOCP/Client/client.cpp` | Multi Client. The header only has Windows signatures (`DWORD WINAPI`), so this class is Windows-only. |
| `unit_auth_server_connect` (derives `unit_connect_base`) | `Server Lib/Projeto IOCP/UNIT/unit_auth_server_connect.cpp` | Created inside every `server`. It keeps the TCP link to the Auth Server. |

The constructor arguments are `server(session_manager&, accept_thread_num, db_instance_num, job_thread_num)`:

| Server | Accept threads | DB instances | Job threads |
|---|---|---|---|
| Login | 2 | 8 | 4 |
| Game | 2 | 26 | 8 |
| Message | 1 | 12 | 4 |
| Rank | 1 | 4 | 4 |

For `unit(session_manager&, db_instance_num, job_thread_num)`: the Auth Server uses 4 and 4, the GG Auth Server uses 2 and 2.

## Startup and runtime flow (base class `server`)

The process entry point is each server's `main`, for example `Server Lib/Login Server/Login Server/Login Server.cpp`:

1. If `argc > 2`, it calls `chdir(argv[2])`. Only the **second** argument is used, and it becomes the working directory. It then sets the log prefix (for example `"LS"`) and calls `message_pool::reload_log_files()`, which reads `[LOG] DIR`.
2. On Linux it sets `RLIMIT_CORE` to unlimited and lowers the nice value by 2 (or 1). On Windows it installs an SE-exception minidump handler (`TYPE/set_se_exception_with_mini_dump.h`).
3. It calls `sls::ls::getInstance().start()`. The server object is a singleton, so construction happens on first access.

The constructors run in this order:

1. `threadpool` constructor (`THREAD POOL/threadpool.cpp`). `server` passes `threadpl_server(16, 16, job_thread_num)`, so this creates:
   - one console/log thread;
   - `job_thread_num` job threads;
   - 16 each of `worker_io`, `worker_io_send`, `worker_io_recv`, `worker_logical` and `worker_send`.
2. `server::server` (`server.cpp:58`):
   1. Sets `ssv::sv = this` (global pointer to the server).
   2. Creates `new unit_auth_server_connect(*this, m_si)`, which reads `[AUTHSERVER]`.
   3. Calls `config_init()`, which reads `[SERVERINFO]` and `[OPTION] ANTIBOTTTL` (see below).
   4. Creates four manual-reset events: ShutdownServer, Shutdown, MoreAccept and AcceptConnection.
   5. Spawns `accept_thread_num` × `TT_ACCEPTEX_IO` threads (`_worker_io_accept`) and one `TT_ACCEPTEX` thread (`acceptEx()`). The `acceptEx` thread:
      - creates the listener with `SO_REUSEADDR` (`SOCKET/socketserver.cpp`);
      - wraps it in `new socket(listener, 1)`, where option 1 means server mode: it reads `config/socket_config.ini` and starts the DDoS timer thread;
      - binds `0.0.0.0:PORT`; the `[SERVERINFO] IP` value isn't used for binding;
      - waits on `EventAcceptConnection` **before** calling `listen()`.
   6. Calls `NormalManagerDB::create(db_instance_num)`. Each `NormalDB` instance gets an exec thread and a response thread, which read `[NORMAL_DB]` and connect.
   7. Starts the monitor thread, the register-server thread and the disconnect-session thread, then sets `m_state = GOOD`.
   8. Any `stdA::exception` thrown in this constructor is logged and sets `m_state = FAILURE`.
3. The derived constructor (for example `login_server`) registers its packet handlers in `packet_func_base::funcs` and `funcs_sv`, and constructs its `player_manager`. That is a `session_manager`, which reads `[OPTION] TTL`.

`server::start()` (`server.cpp:1967`):

1. If `m_state == FAILURE`, it logs "Server initialized with failure" and goes straight to `waitAllThreadFinish`.
2. Otherwise it calls, in order:
   1. `m_unit_connect->start()`, which begins trying to reach the Auth Server;
   2. `onStart()` (virtual);
   3. `setAcceptConnection()`, which signals the event, so `listen(10000)` runs and the log shows "Escutando na porta: N" ("listening on port N");
   4. `commandScan()`, the blocking console loop.
3. When `commandScan()` returns, it calls `waitAllThreadFinish(INFINITE)`.

`commandScan()` reads keystrokes with `_kbhit/_getch` (Linux port in `UTIL/ConioPort.h`). It accepts letters, digits, `_` and space, plus backspace and Enter. On Enter it passes the line to the virtual `checkCommand(std::stringstream&)`. If that returns `true` (each server implements `exit`), the loop ends. `server::shutdown()` sets `EventShutdownServer`, which also ends the loop. Server commands such as `reload_socket_config` are implemented by the derived servers (for example `auth_server.cpp:1037`). The console needs an interactive TTY, which is why the Linux launcher uses tmux.

Background threads owned by `server`:

| Thread | Period | Work |
|---|---|---|
| `monitor()` (`server.cpp:316`) | 1 s | Rotates log files at midnight (`checkUpdateDayLog`). Restarts any dead thread in `m_threads` and the console thread. Calls `session_manager::checkSessionLive()`. Runs `CmdServerList(GAME)` asynchronously (fills `m_server_list`). Runs `CmdListIPBan` and `CmdListMacBan` **synchronously** (fills the ban lists). Calls `onHeartBeat()`. |
| `registerServer()` | 1 s | Sets `m_si.curr_user` to the number of online sessions, then runs `CmdRegisterServer` (`pangya.ProcRegServer_New`). |
| `disconnect_session()` | 1 s wait | Pops sessions from `session_manager`'s delete queue and calls `DisconnectSession`, which calls `onDisconnected()` and then `deleteSession()`. |
| `socket::pThread_Timer` | 1 s | Prunes dead live-connection entries. Every `ddos_autoreset` ms it also clears expired DDoS history. |
| `NormalManagerDB::Revive` | 1 s | Restarts dead DB exec and response threads, which reconnects to the database. |

`checkSessionLive()` (`SOCKET/session_manager.cpp:351`) queues a session for disconnect when:

- the session is not created;
- the socket is dead (`socket::getConnectTime < 0`);
- the session is still not authorized after `STDA_TIME_LIMIT_NON_AUTHORIZED` = 10000 ms (`SOCKET/session.h:33`);
- `TTL > 0` and nothing has been received for `TTL` ms. In `_DEBUG` builds this case is only logged; the session is not kicked.

Shutdown: `waitAllThreadFinish` first releases every waiter on `pangya_db`. It then posts a "null" completion to each IOCP or queue type, decrements the atomic run flags, and joins all threads.

## Session and thread model

- **Accept, Windows.** `AcceptEx` keeps up to `MAXUSER` pending accept sockets. Completions arrive on `m_iocp_io_accept`, and `accept_completed()` checks IP rules, `SO_UPDATE_ACCEPT_CONTEXT`, keep-alive (20 s idle, 2 s interval) and `TCP_NODELAY`.
- **Accept, Linux.** The listener is non-blocking and registered with epoll. Each readiness event makes an accept worker call `::accept()` once inside `accept_completed()`. Sockets get `O_NONBLOCK`, `SO_KEEPALIVE`, `TCP_KEEPIDLE=20`, `TCP_KEEPINTVL=2` and `TCP_NODELAY`.
- Each accepted socket becomes a session through `session_manager::addSession(sock, addr, key)`. The key is `std::rand() % 16`. It is **both** the crypt key and the index of the epoll/IOCP port (`m_iocp_io[key]`) the session is attached to. On Linux the registration flags are `EPOLLIN|EPOLLOUT|EPOLLET|EPOLLHUP|EPOLLRDHUP`. Then the virtual `onAcceptCompleted(session*)` runs, and the derived server sends its raw hello packet (see below).
- **Pipeline:** `worker_io[i]` (epoll/IOCP) → `worker_io_recv[i]` / `worker_io_send[i]` → `translate_packet()` splits the byte stream into frames and decrypts them → the frame is posted to `m_iocp_logical` → one of the 16 `worker_logical` threads → `dispach_packet_same_thread()`, which runs the handler. Outgoing data goes through `session::requestSendBuffer` → `STDA_OT_SEND_REQUEST` → `worker_send` → `send_new`. On Linux the send, recv and logical stages are `list_fifo_asyc<stThreadpoolMessage>` queues, not IOCP ports (`THREAD POOL/threadpool.h:310-320`).
- Each session has a 16 KiB `Buffer` per direction (`MAX_BUFFER_SIZE 0x4000`, `UTIL/buffer.h:9`).
- **Lifetime** is reference-counted. `session::usa()` takes a reference and `session::devolve()` releases one. When a disconnect is requested while the session is in use, the quit flag is set and the last `devolve()` deletes the session (`stUseCtx`, `SOCKET/session.h`).
- **Timers.** `TIMER/timer.cpp` uses `CreateTimerQueueTimer` on Windows and the `QueueTimer` emulation on Linux (`TIMER/queue_timer.cpp`). Timer types are `NORMAL`, `PERIODIC` and `PERIODIC_INFINITE`. `threadpool::postTimer()` creates timers; the base class uses one for the timed shutdown (`end_time_shutdown` → `shutdown_time()`).
- **Jobs.** `job_pool` holds job objects that the `TT_JOB` threads run (`threadpool::postJob`).

## Network protocol: framing, encryption, compression

Implemented in `PACKET/packet.cpp`, `CRYPT/crypt.cpp`, `COMPRESS/compress.cpp` (minilzo, `lzo1x_1_compress` / `lzo1x_decompress`) and `THREAD POOL/threadpool.h` (the `LOOP_TRANSLATE_*` macros). All integers are little-endian. The structs are `#pragma pack(1)`.

**Plain packet:** `[packet_id u16][payload]`.

**Client to server** (`packet::unMake`, used by `threadpl_server` on receive). The 4-byte header is `packet_head_client`:

```
low_key:u8 | size:u16 | seq:u8 | encrypted[size]
```

After decryption, byte 0 is a check byte and the rest is the plain packet. The server doesn't check the `seq` byte.

**Server to client** (`packet::makeFull`, used by every `session_send`). The 3-byte header is `packet_head`:

```
low_key:u8 | size:u16 | encrypted[size]
```

where `encrypted[size]` decrypts to:

```
check:u8 | len:4 bytes | LZO1X-1(plain)
```

- `size` = compressed length + 5.
- The 4-byte length is built in `conversionByte` with `CB_BASE_256`. For plain length `n`, `v = ((n/255)<<8) | (n%255)` is written big-endian.
- `unMakeFull` applies the reverse transform when it reads such a frame.

**Raw frames** (`packet::makeRaw` / `unMakeRaw`):

```
low_key=0 | size=n+1 | 0x00 | plain[n]
```

These are unencrypted. The derived servers use one as the first packet after accept. For example, `login_server::onAcceptCompleted` (`login_server.cpp:1035`) sends raw packet `0x0000` containing `int32 key` and `int32 server_uid`, so the client learns the session key. `unMakeFull` also detects raw frames (`low_key==0` and the first byte `0`) for packet ids `0x00` (only when `size==0x0B`), `0x2E`, `0x3F` and `0x1388`.

**Crypt scheme as implemented** (`crypt::init_key`, `encrypt`, `decrypt`):

- Index: `idx = (session_key << 8) | low_key`, with `session_key` 0..15, so the index runs 0..4095.
- XOR byte: `K = _Keys[idx]`. Check byte: `C = _Keys[4096 + idx]`. The table is `CRYPT/key_dictionary.h`. Its comments say it holds 4×4096 blocks for different seasons; only blocks 0 and 1 are used.
- `m_key[0]` is a `uint32_t` holding the single byte `K`. Because of that, only byte 0 is XORed with `K`; bytes 1..3 are XORed with 0 and stay plaintext.
- For every `i >= 4`: `cipher[i] = plain[i] ^ plain[i-4]`, a 4-byte chain over the **plaintext**.
- The first plain byte must equal `C`. `decrypt` throws `CRYPT` error 3 when it doesn't, and the receive path then shuts the socket down.
- The sender picks `low_key` at random for every packet (`std::rand() & 255`).
- This is obfuscation, not real cryptography.

**Framing errors.** A crypt or compress error, or a packet error (codes 1 and 15), on the client path calls `::shutdown(sock, SD_BOTH)` and stops parsing (`threadpl_server.cpp`).

**Server-to-server links** (`unit` ↔ `unit_auth_server_connect`) use `makeFull` / `unMakeFull` (the 3-byte header) in **both** directions (`THREAD POOL/threadpl_unit.hpp`, `UNIT/unit_connect.cpp:182`).

### Packet dispatch (`PACKET/packet_func.*`, `UTIL/func_arr.*`)

- `packet_func_base` has three static tables of type `func_arr`, each a flat array of 10000 entries (`MAX_CALL_FUNC_ARR`):
  - `funcs`: handlers for packets received from the client;
  - `funcs_sv`: "server return" handlers;
  - `funcs_as`: Auth Server handlers.
- A handler has the signature `int (*)(void* param, void* ParamDispatch*)`. Handlers are registered with `funcs.addPacketCall(id, fn, this)`.
- `dispach_packet_same_thread` (`server.cpp:1561`):
  1. Looks up `funcs[id]`. An unknown id, or an id of 10000 or more, logs a hex dump and **disconnects** the session.
  2. Calls `session.usa()` and sets `m_tick = clock()`. Only known packets refresh the TTL.
  3. Calls the virtual `checkPacket(session, packet)`. If that returns `false`, the handler is skipped. A per-session repeat tracker is available in `ctx_check_packet`: `CHK_PCKT_INTERVAL_LIMIT` 1000 ms, `CHK_PCKT_COUNT_LIMIT` 5, last 3 ids remembered.
  4. Runs the handler, then calls `devolve()`.
- `funcs_sv` is a **post-send hook**. When a send completes (`STDA_OT_SEND_COMPLETED`), `threadpl_server::translate_packet` re-parses the sent bytes with `unMakeFull` and calls `dispach_packet_sv_same_thread`, which dispatches by id to `funcs_sv`. Servers register no-op handlers for this (for example `packet_svFazNada`, "does nothing"). A missing `funcs_sv` entry is only logged.

### Auth Server link (`UNIT/unit_auth_server_connect.cpp`)

- Connects to `[AUTHSERVER] IP:PORT`. The IP may be a hostname; it is resolved with `getaddrinfo`.
- The connector's monitor runs every 5 s. In `onHeartBeat()` it reconnects if the link is down.
- The 15 incoming ids (`0x0`–`0xE`) are all listed in the table below. The connector's outgoing ids are `0x1` (identify: type, uid, name, key from `CmdNewAuthServerKey`) through `0x7`.

| Id | Handler |
|---|---|
| 0x0 | `FirstPacketKey` |
| 0x1 | `AskLogin` |
| 0x2 | `ShutdownServer` |
| 0x3 | `BroadcastNotice` |
| 0x4 | `BroadcastTicker` |
| 0x5 | `BroadcastCubeWinRare` |
| 0x6 | `DisconnectPlayer` |
| 0x7 | `ConfirmDisconnectPlayer` |
| 0x8 | `NewMailArrivedMailBox` |
| 0x9 | `NewRate` |
| 0xA | `ReloadSystem` |
| 0xB | `InfoPlayerOnline` |
| 0xC | `ConfirmSendInfoPlayerOnline` |
| 0xD | `SendCommandToOtherServer` |
| 0xE | `SendReplyToOtherServer` |

Each incoming id maps to a pure-virtual `authCmd*` method on `server`. The Auth Server side of this protocol is documented in `docs/auth-server.md`.

## Configuration

### `server.ini`: parsing rules (`UTIL/reader_ini.cpp`)

The path is always `getcwd() + "/server.ini"` (`"\\server.ini"` on Windows; `_INI_PATH` in `Server/server.h:30`). This means **the working directory decides which ini file is used**. Use `argv[2]` or tmux `-c` to set it.

The file is re-opened and re-parsed on every `readString` / `readInt` call.

| | Windows | Linux |
|---|---|---|
| Backend | `GetPrivateProfileStringA` / `GetPrivateProfileIntA` | GLib `GKeyFile` (`g_key_file_load_from_file`, `g_key_file_get_string` / `_integer` / `_int64`) |
| Comments | `;` | Only `#` lines and blank lines. A `;` line is not valid `GKeyFile` syntax, so the whole file fails to load. |
| Quotes | `"..."` are stripped | Quotes are kept literally, so `NAME = "Login Server"` gives the name **with** quotes. |
| Case | Section and key names are case-insensitive | Section and key names are case-sensitive |
| Missing key | `readString` throws `READER_INI` error 6; `readInt` throws error 6 (sentinel value -3232) | Throws `READER_INI` error 6. A file that fails to parse gives error 5. |

This is why the repository has two sets of ini files:

- the Windows ones (`Server Lib/<Server>/<Server>/server.ini`) use `;` comments and quotes;
- the Linux ones (`Server Lib/Linux Builds/<Server>/server.ini`) use `#` comments and no quotes.

Compare `Login Server/Login Server/server.ini` with `Linux Builds/Login Server/server.ini`.

String values are copied with `memcpy(dst, str.c_str(), sizeof(dst))`. That reads past short strings and leaves no terminator if a value fills the field. Keep values below the field sizes in `TYPE/pangya_st.h:181`:

- `NAME` < 40
- `IP` < 18
- `VERSION` and `CLIENTVERSION` < 40

### Sections the engine itself reads

| Section / key | Read by | Required? | Meaning |
|---|---|---|---|
| `[SERVERINFO] NAME` | `server::config_init`, `unit::config_init` | yes (failure → `m_state=FAILURE`, server exits) | Display name. Stored in the DB through `ProcRegServer_New`. |
| `[SERVERINFO] VERSION` | same | yes | Server version string (sample value `IOCP Version Beta 1.1`). |
| `[SERVERINFO] CLIENTVERSION` | same | yes | Expected client version (sample value `SS.R7.995.00`). |
| `[SERVERINFO] IP` | same | yes | Public IP published to the server list. It isn't used for `bind`, which always uses `INADDR_ANY`. |
| `[SERVERINFO] GUID` | same | yes | Server UID (`m_si.uid`). Must be unique across servers. |
| `[SERVERINFO] PORT` | same | yes | TCP listen port. Linux samples: Login 10303, Game 20203, Message 30303, Rank 4774, Auth 7777. |
| `[SERVERINFO] MAXUSER` | same | yes | Windows `AcceptEx` backlog size and the published capacity. Sessions are pre-allocated by the derived `player_manager(*this, m_si.max_user)`. |
| `[SERVERINFO] PROPERTY` | same | yes | `uProperty` bitfield (`pangya_st.h:147`): bit4 mantle (GM-only), bit6 only_rookie, bit7 natural, bit9 green, bit10 blue, bit11 grand_prix. What these bits mean for non-Game servers is unverified. |
| `[OPTION] ANTIBOTTTL` | `server::config_init` | no | Anti-bot TTL in ms, used by the Game Server (`packet_func_sv.cpp:2969`). The constructor default is 10000, but when the key is missing `config_init` sets **1000**. |
| `[OPTION] TTL` | `session_manager::config_init` | **yes** (throws) | Idle kick in ms. `0` disables it. Not enforced in `_DEBUG` builds. |
| `[LOG] DIR` | `list_fifo_console::init_log_files` (`TYPE/list_fifo_console.h:198`) | no | Log directory. Created if missing. Falls back to `Log`. |
| `[NORMAL_DB] DBENGINE` | `NormalDB::loadIni` (`DATABASE/normal_db.cpp:494`) | no | `mysql`, `mssql` or `postgresql` (case-insensitive). A missing or unknown value means **mssql**. |
| `[NORMAL_DB] DBIP` | same | yes | For `mysql`: host. For `mssql` and `postgresql`: the **ODBC DSN name** (`DSN=<DBIP>;DATABASE=..;UID=..;PWD=..`, `mssql.cpp:165`, `postgresql.cpp:167`). |
| `[NORMAL_DB] DBNAME`, `DBUSER`, `DBPASS` | same | yes | Database name and credentials. |
| `[NORMAL_DB] DBPORT` | same | no (0 = default) | Used only by MySQL (`mysql_real_connect`). The ODBC connection string ignores it. |
| `[AUTHSERVER] IP`, `PORT` | `unit_auth_server_connect::config_init` | no (on failure the connector is marked FAILURE and never connects; the server still runs) | Auth Server address. Samples use `127.0.0.1:7777`. |

The other sections (`[CHANNELINFO]`, `[GGAUTHSERVER]`, the other `[OPTION]` keys such as `SAME_ID_LOGIN`, `CREATEUSER` and `ACCESSFLAG`) are read by the individual servers; see their docs. `GG Auth Server/GG Auth Server/server.ini` is empty in the repository.

### `config/socket_config.ini`: IP rules and DDoS protection (`SOCKET/socket.cpp`)

- The path is relative: `config/socket_config.ini`, resolved from the working directory.
- The file is read only by the listening socket (`socket(..., 1)`), when the `acceptEx` thread starts.
- If the file is missing, `config_read` throws (`_SOCKET` error 13), the accept thread logs the error, and the server **never listens**.
- The derived servers' console command `reload_socket_config` re-reads the file. It clears the allow and deny lists first; other values keep their old value unless the file sets them again.

Syntax:

- Every line has the form `key: value` (`sscanf("%[^:]: %[^\r\n]")`).
- Comments must start at column 0 with `//`.
- Keys are case-insensitive. A key must start at column 0, because leading spaces become part of the key.
- The value runs to the end of the line, including trailing spaces.

| Key | Default (constructor) | Meaning |
|---|---|---|
| `enable_ip_rules` | 1 | `0` skips `connect_check` entirely. That turns off allow/deny rules, DDoS protection **and** the per-IP limit. |
| `limit_connection_per_ip` | 3 (sample: 10) | Maximum simultaneous live connections from one IP. Further connections are rejected and logged as "connection limit per IP". |
| `order` | `deny,allow` | `deny,allow`, `allow,deny` or `mutual-failure`. Compared exactly, so `allow, deny` with a space (as in the sample comment) is silently ignored. |
| `allow` | — (repeatable) | Adds an allow entry: `all`, `a.b.c.d`, `a.b.c.d/nn` or `a.b.c.d/m.m.m.m`. An invalid entry is dropped. |
| `deny` | — (repeatable) | Adds a deny entry, same formats as `allow`. |
| `ddos_interval` | 3000 | Window in ms. A new connection from the same IP within this time of the previous one increments a counter; a later connection resets the counter to 0. |
| `ddos_count` | 10 (sample: 5) | When the counter (post-increment) reaches `>= ddos_count`, the IP is flagged as DDoS. Every later connection is rejected and logged "DDoS Atack From IP". |
| `ddos_autoreset` | 600000 | How long a DDoS flag lasts, in ms. History is purged on this period by the timer thread: entries without a flag are purged after `3 × ddos_interval`, flagged entries after `ddos_autoreset`. |

Decision logic (`socket::connect_check`): each order produces a verdict of 0 (reject), 1 (accept, subject to the DDoS and per-IP checks) or 2 (accept unconditionally).

| `order` | IP matches deny | IP matches allow | Neither |
|---|---|---|---|
| `deny,allow` | 0 (deny wins over allow) | 2 | 1 |
| `allow,deny` | 0, unless it also matches allow | 2 (allow wins over deny) | 1 |
| `mutual-failure` | 0 | 2 if not also denied, else 0 | 0 |

- A verdict of 2 still passes when the IP is flagged as DDoS or over the per-IP limit.
- The sample `order: deny,allow` with `allow: 127.0.0.1` does **not** restrict access to localhost. It only exempts localhost from the DDoS and per-IP limits. To whitelist, use `mutual-failure` or add `deny: all`.
- The server-side MAC and IP ban lists (`pangya.pangya_ip_table`, `pangya.pangya_mac_table`) are separate from this file. They are refreshed every second and checked by the derived servers through `server::haveBanList()`:
  - type `IP_BLOCK_NORMAL`: blocked when `(ip & mask) == (ban_ip & mask)`;
  - type `IP_BLOCK_RANGE`: blocked when `ip` is between `ip` and `mask`;
  - an empty MAC is always blocked.

## Database usage

### Layering

- `database` (abstract, `DATABASE/database.h`) has three implementations:
  - `mysql_db`: libmysqlclient. Uses `CLIENT_MULTI_STATEMENTS` and `utf8mb4`. Procedures are called as `CALL name(params)`.
  - `mssql`: ODBC. Procedures are called as `exec name params`. Text is quoted as `N'..'`.
  - `postgresql`: ODBC. Procedures are called as `select * from name(params)`. Text is quoted as `'..'::text`.
- Keyword escaping: write `DB_MAKE_ESCAPE_KEYWORD_A("key")`, which produces `-//key-//`. `parseEscapeKeyword` rewrites that to `` `key` `` (MySQL), `[key]` (MSSQL) or `"key"` (PostgreSQL).
- `NormalDB` (`DATABASE/normal_db.cpp`) is one connection plus two threads: an exec loop over `m_unExec` and a response loop that runs the callbacks.
- `NormalManagerDB` (singleton `snmdb::NormalManagerDB`) owns N `NormalDB` instances. `add(id, pangya_db*, callback, arg)` picks an instance with `time(nullptr) % N`, so every query in the same second goes to the same connection.
- `pangya_db` (`PANGYA_DB/pangya_db.h`) is the base of every `Cmd*` class:
  - `prepareConsulta(database&)` builds the SQL or procedure call;
  - `lineResult(row, result_set_index)` parses each row;
  - errors are stored in `m_exception`, not thrown;
  - commands created with `_waitable=true` let the caller block on `waitEvent()`.
  - Typical use: `add(0, &cmd, nullptr, nullptr); cmd.waitEvent();` for a synchronous call, or pass a callback such as `server::SQLDBResponse` for an asynchronous one.

### Shared commands (`PANGYA_DB/`)

| Class | Procedure or SQL | Class | Procedure or SQL |
|---|---|---|---|
| CmdRegisterServer | `pangya.ProcRegServer_New` | CmdServerList | `pangya.ProcGetServerList` |
| CmdListIPBan | `SELECT ip, mask FROM pangya.pangya_ip_table` | CmdListMacBan | `SELECT mac FROM pangya.pangya_mac_table` |
| CmdNewAuthServerKey | `pangya.ProcGetNewAuthServerKey` | CmdAuthServerKey | `SELECT server_uid, key, VALID FROM pangya.pangya_auth_key WHERE server_uid = ` |
| CmdUpdateAuthServerKey | `pangya.ProcUpdateAuthServerKey` | CmdAuthKeyLogin / Info | `pangya.ProcGeraAuthKeyLogin` / `pangya.ProcGetAuthKeyLogin` |
| CmdAuthKeyGame / Info | `pangya.ProcGeraAuthKeyGame` / `pangya.ProcGetAuthKeyGame` | CmdUpdateAuthKeyLogin | `UPDATE pangya.authkey_login SET valid = .. WHERE UID = ..` |
| CmdKeysOfLogin | `pangya.ProcGetAuthKeys` | CmdLogonCheck | `SELECT LOGON, game_server_id FROM pangya.account WHERE UID = ` |
| CmdRegisterLogon | `pangya.ProcRegisterLogon` | CmdVerifyID / Pass / Nick | `pangya.ProcVerifyID` / `ProcVerifyPass` / `ProcVerifyNickname` |
| CmdSaveNick | `pangya.ProcSaveNickname` | CmdAddCharacter | `pangya.ProcAddCharacter` |
| CmdUpdateCharacterEquiped | `pangya.USP_FLUSH_CHARACTER` | CmdChatMacroUser | `pangya.ProcGetMacrosUser` |
| CmdInsertBlockIP / MAC | `pangya.ProcInsertBlockIP` / `ProcInsertBlockMAC` | CmdRateConfigInfo / Update | `pangya.ProcGetRateConfigInfo` / `ProcUpdateRateConfigInfo` |

`cmd_add_item_base` is a helper base class with no SQL of its own. The base `server` uses `RegisterServer`, `ServerList`, `ListIPBan` and `ListMacBan` every second, and `NewAuthServerKey` through the Auth Server link. All of these live in the `pangya` schema; see the root dumps `bk-squema-*.sql`.

## Source layout

| Directory | Contents |
|---|---|
| `IOCP/` | `iocp.cpp/.h`: Windows completion-port wrapper. `epoll.cpp/.hpp`: Linux `iocp` class built on `epoll_create1`, with a pipe that `postStatus()` uses to wake waiters. |
| `SOCKET/` | `socket` (IP rules and DDoS, connect, bind), `socketserver` (listener creation, `SO_REUSEADDR`), `session` (per-connection state, buffers, reference counting), `session_manager` (pool, TTL, delete queue), `session_pool`, `wsa` (WSAStartup, Windows). |
| `THREAD POOL/` | `threadpool` (core pipeline), `threadpl_server` / `threadpl_unit` / `threadpl_client` (`translate_packet` variants), `thread` (portable thread with type `TT_*` and priority), `job` / `job_pool`. |
| `TIMER/` | `timer`, `timer_manager`, `queue_timer` (Linux emulation of the Win32 timer queue). |
| `PACKET/` | `packet` (plain and "maked" buffers, framing, typed read/write, `readString` = u16 length + bytes), `packet_func` (dispatch tables, `MAKE_*_SPLIT_PACKET` helpers for splitting large lists). |
| `CRYPT/` | `crypt.cpp`, `key_dictionary.h` (`_Keys` table). |
| `COMPRESS/` | `compress.cpp` wrapping bundled minilzo (`minilzo.c`, `lzoconf.h`, `lzodefs.h`). |
| `ZIP/` | libzip headers and Windows import libraries `lib/zip.lib` and `zip-64.lib`. Used by `UTIL/iff.cpp` to read `data/pangya_jp.iff`, a zip of the game's `.iff` tables (`UTIL/iff.h:28`). On Linux the system libzip is linked. |
| `DATABASE/` | `database`, `mysql_db`, `mssql`, `postgresql`, `normal_db`, `normal_manager_db`, `response` / `result_set`, `exec_query`, and a legacy `pangya_db`. |
| `PANGYA_DB/` | `pangya_db` base and the shared `cmd_*` classes (table above). |
| `UTIL/` | Notable files listed below. |
| `TYPE/` | `stdAType.h` (`myOver`, `ParamDispatch`, `call_func`, `ctx_db`), `pangya_st.h` (`ServerInfo`, `ServerInfoEx`, `RateConfigInfo`, `IPBan`, player and account structs), `data_iff.h` (IFF record layouts), `list_fifo.h` / `list_async.h` (blocking queues), `singleton.h`, `stda_error.h` (error source enums, `STDA_MAKE_ERROR(source, code, syserr)`), the Smart Calculator types, `threadpool_linux_type.h`. |
| `UNIT/` | `unit` (listener base for the Auth Server and GG Auth Server), `unit_connect` (outgoing-connection base with its own io, logical, send and monitor threads), `unit_auth_server_connect`, `unit_auth_interface.hpp` (`IUnitAuthServer`). |
| `Server/` | `server.cpp/.h` (described above). |
| `Client/` | `client` base class (Windows only), used by Multi Client. |
| `Smart Calculator/` | `SmartCalculator` (singleton `sSmartCalculator`). See below. |

Notable `UTIL/` files:

- **`WinPort.h/.cpp`: the Windows-on-Linux shim.**
  - Defines `DWORD`, `BOOL`, `LARGE_INTEGER`, `SYSTEMTIME`, `FILETIME`, `WSABUF`, `SD_BOTH`, `INVALID_SOCKET`, `closesocket`, `ZeroMemory`, the time-zone and system-time functions, `DIFF_TICK` over `timespec`, and the recursive-mutex macros.
  - On Linux, `SOCKET` is a **struct** `{fd, connect_time}`, not an int. The connect time stands in for Windows `SO_CONNECT_TIME`.
- `ConioPort.h`: `_kbhit`, `_getch` and `_putch` for Linux.
- `event.cpp`: manual-reset `Event` with `waitMultipleEvent`.
- `reader_ini`: the INI reader described above.
- `message` / `message_pool`: logging. `message_pool` is a singleton queue drained by the console thread. Log types `CL_*` (`UTIL/message.h:12`) route each message to the console and/or to `log`, `log_time`, `log_io_data` or `log_test` (debug builds), as `<DIR>/log <ddmmyyyyhhmmss> <prefix>.log`.
- `buffer`: `Buffer`.
- `exception`: `stdA::exception`.
- `md5`: WinCrypt on Windows, OpenSSL EVP on Linux.
- Also: `hex_util`, `iff`, `iphelp`, `random_gen`, `string_util`, `util_time`, `func_arr`.

### Smart Calculator hook

- `Smart Calculator/Smart Calculator.cpp` loads `SMARTCALCULATORLIB.so` from the working directory (`dlopen(getcwd()+"/SMARTCALCULATORLIB.so")`) or `SMARTCALCULATORLIB.dll` (`LoadLibraryA`). Windows debug builds use `../../Smart Calculator lib/[x64/]Release/`.
- It resolves `initSmartCalculatorLib`, `sendCommandServer`, `makePlayerContext`, `getPlayerContext` and `removeAllPlayerContext`.
- It exports `scLog` and `responseCallBack`, which the library calls back.
- `server::sendMessageToDiscordChatHistory` forwards `nickname - msg` when `m_si.rate.smart_calculator` and `m_chat_discord` are both set. The default for `m_chat_discord` is on.
- The other hooks (`sendSmartCalculatorReplyToPlayer`, `sendNoticeGMFromDiscordCmd`) are no-ops in the base class; the Game and Message servers override them. See `docs/smart-calculator.md`.

## Build and run notes

### Windows

Each server has its own `.sln` / `.vcxproj` (for example `Server Lib/Login Server/Login Server.sln`) that references the `Projeto IOCP` sources directly. `Login Server.vcxproj` uses toolset v141, a `stdcpp17` / `stdcpplatest` language standard, and the defines `_CONSOLE` and `_DEBUG`/`NDEBUG`. There is no project for `Projeto IOCP` itself.

### Linux: `Server Lib/Linux Builds/Makefile-*.mk`

Run every makefile **from `Server Lib/Linux Builds`**, because the sources are referenced as `../<Dir>/...`:

```
make -f Makefile-ls.mk                         # debug (default)
make -f Makefile-ls.mk __build_type=release    # release
make -f Makefile-ls.mk clean                   # deletes *.o / *.d in the build folder only
make -f Makefile-ls.mk output-ls-release/login-stripped __build_type=release   # objcopy --strip-debug + copy
```

| Makefile | Output (`PROGRAM`) | Copied to | Notes |
|---|---|---|---|
| `Makefile-as.mk` | `auth` | `./Auth Server/` | |
| `Makefile-ls.mk` | `login` | `./Login Server/` | |
| `Makefile-ms.mk` | `msn` | `./Message Server/` | `-fvisibility=hidden`, `-Wl,--export-dynamic` |
| `Makefile-rs.mk` | `rank` | `./Rank Server/` | |
| `Makefile-gs.mk` | `game` | `./Game Server/` | `-fvisibility=hidden`, `-Wl,--export-dynamic`, `LIBSTATIC := GGSrvLib26-1/GGSrvLib26-1.a` |
| `Makefile-ggsrvlib.mk` | `GGSrvLib26-1.a` (`ar -crvs`) | `./GGSrvLib26-1/` | Build this **before** `gs`. |
| `Makefile-ggauth70.mk` | `ggauth701.so` (`-fPIC -shared`, `-D GGAUTH701_EXPORTS`) | `./ggauth70-1/` | Copies straight into `./Game Server/` (`GAMESERVERDIR`); the `DIRSAVEPRROGRAM` directory is not used by the copy rule. |
| `Makefile-smart.mk` | `SMARTCALCULATORLIB.so` (`-fPIC -shared`, `-D SMARTCALCULATORLIB_EXPORTS`) | `./Game Server/` and `./Message Server/` | |
| `Makefile-iff.mk` | `iff` | `./IFF Manager/` | Links only libzip and glib. |
| `Makefile-test.mk` | see below | | |

How the makefiles work:

- **Build type.** `__build_type=release` switches `CURRENT_CXXFLAGS` from `-Og -D _DEBUG -D SHOW_DEBUG_PACKET -ggdb` to `-O3`, and switches the object folder from `output-<abbr>-debug` to `output-<abbr>-release`. The ggauth70, ggsrvlib and iff makefiles leave out `SHOW_DEBUG_PACKET`. A build prints `****** Build Type=... ******`.
- **Common flags:** `-fpermissive -fsigned-char -std=c++20` plus `pkg-config --cflags glib-2.0` (ggsrvlib, ggauth70 and smart don't use glib).
- **Link line** for the servers (as, ls, ms, rs, gs):

  ```
  -pthread -lg -ldl `pkg-config --libs libzip` `pkg-config --libs mysqlclient` -lodbc -lcrypto `pkg-config --libs glib-2.0`
  ```

  This requires: `g++` with C++20, `pkg-config`, the glib-2.0, libzip and mysqlclient (MySQL/MariaDB client) development packages, unixODBC (`-lodbc`) and OpenSSL `libcrypto`. At run time you also need an ODBC driver for mssql or postgresql; a comment in the makefile mentions `/usr/lib/libmsodbcsql-17.so`.
- **The `|` space-escape trick.** Make cannot handle spaces in word lists, so `CFILES` writes paths with `|` in place of spaces (`../Projeto|IOCP/THREAD|POOL/thread.cpp`).
  - The `target` macro maps each source to `$(BUILD_FOLDER)/<notdir>.o`, with `subst |,\ `.
  - The `obj` macro generates one rule per file (`$(eval $(call SOURCES,...))`).
  - `scape_WS` escapes spaces for the compiler command line.
  - `OUT_OBJECTS` re-joins `$^` with backslashes for the link command.
  - Objects are flattened with `notdir`, so two sources with the same file name in one makefile would collide.
- **Dependencies.** `-MMD -MP` produces `.o.d` files, which are pulled in with `-include`.
- **`LIBSTATIC`** is a prerequisite of the program rule. It ends up in `$^` and is therefore linked; a changed `.a` triggers a relink. It is set only in `gs`; in the others it is commented out as `testlib.a`. There is no rule to build it from `Makefile-gs.mk`.
- The link rule copies the binary to `DIRSAVEPRROGRAM`.
- `.PHONY:` is declared with no targets, so `all` isn't phony.
- `.gitignore` excludes `*.o`, `*.d`, `*.log`, `/Log`, `test.cpp`, `test` and core files.

### Run directory layout (Linux)

Each `Linux Builds/<Server>/` directory contains:

- `server.ini` (Linux syntax);
- `config/socket_config.ini`;
- for the servers that use IFF data: `data/pangya_jp.iff`. The Game Server directory also has `csauth2.cfg` and other `.iff` variants.

Start each binary with its directory as the working directory. Put `SMARTCALCULATORLIB.so` next to `game` and `msn`.

### `odbc.ini`

`Linux Builds/odbc.ini` defines the DSN `[pangya]`:

```
Driver = PostgreSQL
Database = pangya
Servername = 127.0.0.1
Port = 5432
ReadOnly = No
...
```

The ODBC engines connect with `DSN=<DBIP>`. So for `DBENGINE = postgresql` (or `mssql`) set `DBIP = pangya`, install this DSN into the unixODBC configuration (`/etc/odbc.ini` or `~/.odbc.ini`), and register a driver named `PostgreSQL` in `odbcinst.ini`. The installation locations are standard unixODBC behaviour and aren't stated in this repository. The shipped Linux `server.ini` files use `DBENGINE = mysql`, `DBIP = localhost`, `DBPORT = 3306`.

### `run-as-to-rs.sh` (tmux launcher)

`run-as-to-rs.sh` starts detached tmux sessions:

1. `Smart-Calculator`, running `node smart.js` in `/home/acrisio/projects/Node/Smart-Calculator/` (the source is in `Server Lib/Node/Smart-Calculator`);
2. `Auth Server` (`./auth`);
3. `Login Server` (`./login`);
4. `Message Server` (`./msn`);
5. `Rank Server` (`./rank`);
6. with the argument `all`, also `Game Server` (`./game`).

Each session uses `-c "${dir}/<Server>/"`.

- **The paths are hardcoded:** `dir=/home/acrisio/projects/Linux\ Builds` and the Node path above. Edit both before use.
- There is no delay between starts, and the GG Auth Server isn't launched.
- `./run-as-to-rs.sh kill` types `exit` + Enter into each server session (a clean console shutdown) and kills the Smart-Calculator session.

### Test programs (`Linux Builds/`)

Prebuilt x86-64 ELF binaries are committed next to the sources.

| File | What it tests |
|---|---|
| `testcl.cpp` | A standalone copy of the `commandScan` keyboard/line-editing loop. It includes `"../ConioPort.h"`, which doesn't exist at that path (the header is at `Projeto IOCP/UTIL/ConioPort.h`), so it won't compile as-is. |
| `testefs.cpp` | `std::filesystem::status` / `exists` on a missing directory (`"Loge"`), the same logic as the log-directory check. |
| `testlc.cpp` | `wstring_convert` / `codecvt<char16_t>` conversion with a hex dump through `hex_util`, related to the `u16string` conversion that the ODBC backends do on Linux. |
| `testmd5.cpp` | Reads a string and prints `md5::getHash()` (OpenSSL). |

`Makefile-test.mk` uses `CC := g++-10`:

- `tmd5` builds `testmd5` from `testmd5.cpp` plus md5, exception, message_pool, message, hex_util, util_time and WinPort, with `-lcrypto`.
- `teste` needs `test.cpp`, which is gitignored and absent.
- The default `all: todo` target is stale: it references `IOCP Linux/main.cpp` and `IOCP/iocp.cpp`, which don't exist, and links `lglib-2.0` without the `-`.
- `clean` and `clean-all` remove `*.o` (and the program).

There is no build target for `testcl`, `testefs` or `testlc`.

## Known quirks, gotchas, hardcoded values

- **`std::clock()` measures wall time on Windows but process CPU time on Linux.** The TTL check, the 10 s unauthorized-login limit, the Game Server's anti-bot TTL and the DDoS history purge (`socket::pThread_Timer`) all compare against `clock()`. On Linux, where the process is mostly idle, these limits fire much later in real time.
- **The per-IP connection limit doesn't work on Linux.** In the Linux `accept_completed` (`server.cpp:1439-1518`), `connect_check` runs before `accept.connect_time` is set. The live-connection entry therefore stores a zero `connect_time`, `getConnectTime()` returns -1 for it, and that entry is never counted. The allow/deny rules and DDoS detection still work.
- `epoll_event.events = EPOLLIN || EPOLLET` for the listener (`server.cpp:1042`) is a logical OR, so it evaluates to `1`, which is `EPOLLIN`, level-triggered.
- **A DB error stops the monitor thread for good.** If `CmdListIPBan` or `CmdListMacBan` returns an error in `monitor()`, the exception is rethrown out of the loop (it isn't a THREAD-type error). `m_thread_monitor` isn't in `m_threads`, so nothing restarts it, and log rotation, TTL checks, server-list refresh and `onHeartBeat` all stop. If the DB is merely unreachable, the synchronous `waitEvent()` blocks the monitor instead.
- The engine sends one stored-procedure call and three queries per second per server: register server, server list, IP bans, MAC bans.
- Hardcoded values:
  - `listen` backlog 10000;
  - keep-alive 20 s / 2 s;
  - 16 IO ports, with the session key reused as the port index;
  - session buffer 16 KiB;
  - dispatch table limit 10000 ids;
  - unauthorized timeout 10 s;
  - `NUM_DB_THREAD` default 26;
  - the IFF path `data/pangya_jp.iff`;
  - the socket config path `config/socket_config.ini`.
- `ReaderIni` on Linux never frees the `GKeyFile` on the success path (a small leak on every read) and re-parses the whole file on every key.
- `ANTIBOTTTL` defaults are inconsistent: 10000 in the constructor, 1000 when the key is missing.
- `MAXUSER` is also the number of outstanding `AcceptEx` calls on Windows.
- `[SERVERINFO] IP` is advertised to clients but never bound to.
- Linux ini files must not contain `;` comments or quoted values (see the parsing table).
