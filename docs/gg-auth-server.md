# GG Auth Server, GGSrvLib26-1, ggauth70-1 (nProtect GameGuard server-side auth)

All paths are relative to the repo root. Source comments are translated to English; log strings are still Portuguese (translated where cited).

## 1. Purpose

### What nProtect GameGuard auth is here
The PangYa client ships with nProtect GameGuard (an anti-cheat). Besides the client-side module, GameGuard has a "CS auth" (client/server authentication) feature. The game server periodically sends the client a 16-byte opaque challenge (the *query*). The GameGuard module in the client computes a 16-byte *answer*. The server verifies that answer with the nProtect server library.

- Both blobs are `GG_AUTH_DATA` = 4 x `UINT32` (`dwIndex`, `dwValue1..3`), defined in `Server Lib/GG Auth Server/CSAuth/ggsrv26.h` and `Server Lib/GGSrvLib26-1/GGSrvLib26-1/GGSrvLib26-1.h`.
- Verification needs a server library plus a "protocol module" (`ggauthNN.dll`/`.so`). The protocol module implements the query/answer algorithm for one GameGuard protocol number.
- In this repo the real nProtect server library is the closed-source vendor `ggsrvlib26_MT.lib`. `GGSrvLib26-1` is a source-level reimplementation of it (see 3). `ggauth70-1` is a source-level reimplementation of the vendor protocol module `ggauth70.dll` (see 4).
- The check is only an anti-tamper gate. If the answer is wrong, or the client stops answering, the Game Server drops the player.

### What the GG Auth Server does
`Server Lib/GG Auth Server` is a standalone TCP server (server type 6, `m_si.tipo = 6` in `gg_auth_server::config_init`). It was written to hold the GameGuard auth contexts outside the Game Server process.

The reason is architectural. The vendor lib only exists as 32-bit x86 Windows code, while the Game Server can be built 64-bit (the comment in `GG Auth Server/CSAuth/GGAuth.hpp` says "Disable GG for now, because it is x86"). The Game Server (x64 build) would talk to a 32-bit GG Auth Server process, which hosts the 32-bit vendor lib.

**Important current state (verified):** the Game Server no longer uses the GG Auth Server by default. See section 6 and section 11.

## 2. How it fits in the system

```
 PangYa client (GameGuard module)
        |  0xD7 query (16 B)  <---------------- Game Server ----+
        |  0x88 answer (16 B) ----------------> Game Server     |
                                               |                |
   [default build, MY_GG_SRV_LIB==1]           |  [alt build, x64 && MY_GG_SRV_LIB==0]
   in-process: CCSAuth2 -> GGSrvLib26-1        |  TCP (GGAUTHSERVER ip:port) to GG Auth Server
        -> loads ggauth<N>.dll/.so             |  pkts 0x01-0x06 (see 7)
        reads csauth2.cfg from CWD             |  GG Auth Server runs CCSAuth2 -> vendor ggsrvlib26_MT.lib
                                               |  + ggauth70.dll (Win32 only)
 Auth Server (port 7777) <--- relays "key login" command GG AS -> Game Server uid
```

The GG Auth Server also connects (as a client) to the **Auth Server**. It uses `unit_auth_server_connect` with `[AUTHSERVER]` IP/PORT from its `server.ini`. It uses the Auth Server as a message bus: `sendCommandToOtherServerWithAuthServer(p, uid)` sends a command to the Game Server with the given UID. So the start order for the alt (GG AS) mode is: Auth Server, then GG Auth Server, then Game Servers.

## 3. GGSrvLib26-1 (static library, reimplementation of the vendor server lib)

Location: `Server Lib/GGSrvLib26-1/GGSrvLib26-1/`.

| File | Role |
|---|---|
| `GGSrvLib26-1.h` | Public API. It is a copy of the nProtect `ggsrv26.h` API: `InitGameguardAuth`, `CleanupGameguardAuth`, `GGAuthUpdateTimer`, `AddAuthProtocol`, `SetGGVerLimit`, `SetUpdateCondition`, `CheckCSAuth`, `DecryptHackData`, `ModuleInfo`, the `CCSAuth2` class and the C-style `GGAuth*User` functions. Its ERROR_GGAUTH_* codes (1..21, 50, 101, 102, 200) have Korean comments. |
| `GGSrvLib26-1.cpp` (~2900 lines) | Implementation. |
| `gg_srv_lib_type.hpp` | Internal types and constants (see below). |
| `blowfish.cpp/.h` | Blowfish used to scramble the query/answer. |
| `GGSrvLib26-1.vcxproj` | VS project, `ConfigurationType` = StaticLibrary. Platforms Win32 and x64. |

Constants in `gg_srv_lib_type.hpp`:

- `CURRENT_VERSION = 2003010101`, `VERSION_LIMIT_MAX = 2100010101` (GameGuard version limits).
- Config file names: `csauth2.cfg` and `csver.cfg`.
- Protocol module name pattern: `%sggauth%d.dll` on Windows, `%sggauth%d.so` on Linux.
- Blowfish init key: the string constant `gBlowfish_init_key` in that header.
- One global context `gGGAuthCtx` (`_GG_AUTH_CTX`). It holds the protocol list, a 20000-entry unique-value hash table, version-update counters, critical sections (Windows) or recursive pthread mutexes (Linux), and the Blowfish context.

Behaviour (`InitGameguardAuth(path, numActive, useTimer, useLog)`):

1. Opens `<path>csauth2.cfg`. A missing or empty file returns `ERROR_GGAUTH_FAIL_LOAD_CFG` (5).
2. Reads up to 100 whitespace-separated integers. Each is a protocol number. For each one it loads `<path>ggauth<N>.dll|.so`. The first entry is the "head" protocol.
3. `LoadAuthProtocol` uses `LoadLibraryA` (Windows) or `dlopen(getcwd()/name, RTLD_LAZY)` (Linux). It resolves the exports `PrtcGetVersion`, `PrtcGetAuthQuery`, `PrtcCheckAuthAnswer`, `PrtcEncryptQuery`, `PrtcDecryptAnswer`, `PrtcUpdateTimer`, `PrtcLoopAuth`, `PrtcGetInformation`, `PrtcCheckValidAnswer`.
4. Optionally reads `csver.cfg` (one integer, the GameGuard version upper limit). It is ignored if outside `[CURRENT_VERSION, VERSION_LIMIT_MAX]`. No `csver.cfg` exists in the repo (searched).
5. `Blowfish_Init` with the constant key.

Per-player flow:

- `CCSAuth2::Init` assigns the head protocol, a random server key and resets sequence state.
- `GetAuthQuery` asks the protocol module for a query (`PrtcGetAuthQuery`), then `PrtcEncryptQuery`, then `GGEncryptPacket1` (XOR constants plus Blowfish).
- `CheckAuthAnswer` reverses this (`GGDecryptPacket2`, `PrtcDecryptAnswer`), checks CRCs, sequence number, GameGuard version, loop-auth arrays and duplicate-client detection through the unique-value table.
- Return `0` (`ERROR_SUCCESS`) means OK. Other values are the ERROR_GGAUTH_* codes.

Windows static `.lib` outputs are expected by the Game Server's `#pragma comment(lib, ...)` at `Server Lib/GGSrvLib26-1/{Debug,Release,x64/Debug,x64/Release}/GGSrvLib26-1.lib` (`Server Lib/Game Server/CSAuth/GGAuth.cpp`).

## 4. ggauth70-1 (Protocol70, shared library)

Location: `Server Lib/ggauth70-1/`. It is a reimplementation of the vendor protocol module for protocol number 70.

- `ggauth70-1/ggauth70-1.cpp` exports the 9 `Prtc*` functions (`extern "C"`, see `ggauth70-1.h`). Each is a thin wrapper around a static method of `Protocol70` in `Protocol70.cpp/.hpp` (~1240 lines). `getVersion()` returns `70`.
- `Protocol70` implements: `getAuthQuery`, `encryptQuery`/`decryptAnswer` (including `encryptRound8`), `checkAuthAnswer`, `checkValidAnswer`, `loopAuth`, CRC32 helpers, `AlgorithmGenAuthData` and about 19+ `AlgorithmShuffle_N` variants, a `setTimer`/`UpdateTimer` periodic update and `getRandomWord`.
- Windows: the `UpdateTimer` uses a `SetTimer` callback (`m_TimerIdEvent`). Linux: it uses the engine's `timer_manager`/`job_pool`/`thread` from `Projeto IOCP`, which is why the Linux Makefile compiles those engine files in.
- `ggauth70-1.vcxproj` is a DynamicLibrary with `TargetName` = `GGAUTH701`, so it builds `GGAUTH701.dll`. That is protocol number "701" in `csauth2.cfg` terms, not "70". The Linux output is `ggauth701.so` (below).
- `TYPE/ggauthprotocol_type.hpp` holds the shared `GG_AUTH_DATA`/`CCSAuth2` stand-in types for the module.

Note the naming: the vendor DLL is `ggauth70.dll` (protocol "70"). The reimplementation deliberately uses "701" so it can sit beside the vendor one.

## 5. GG Auth Server internals

Entry: `Server Lib/GG Auth Server/GG Auth Server/GG Auth Server.cpp`.

- `main(argc, argv)`: if `argv[2]` is a directory it is `chdir`'d into and the log prefix is set to `GG_AS`. It then installs the SE-exception/minidump handler, sets `NORMAL_PRIORITY_CLASS` and calls `sgg_as::getInstance().start()`.
- It is Windows-only code: `WinSock2.h`, `DbgHelp.h`, `SetPriorityClass`. The project is `Server Lib/GG Auth Server/GG Auth Server.sln`.

`gg_auth_server` (`GG Auth Server/gg_auth_server.cpp/.hpp`) derives from `unit` plus `IUnitAuthServer`:

- Constructor: `unit(m_player_manager, 2 DB instances, 2 job threads)`. It creates `unit_auth_server_connect`, calls `config_init()` (sets type 6), then `new GGAuth(m_si.max_user * 2000)`. It registers client packets 0x01..0x06 (`packet_func_gg_as.cpp`) and no-op server packets 0x00..0x04.
- Console commands (`checkCommand`): `exit`, `reload_files`, `reload_socket_config`, `snapshot` (deliberately null-dereferences to produce a dump).
- `checkPacket` is the DDoS packet-repeat guard (`CHK_PCKT_COUNT_LIMIT`).
- `authCmd*` handlers (shutdown etc.) come from the Auth Server interface. Only shutdown is clearly relevant. The rest is boilerplate shared with the other servers (not examined in detail).
- Sessions: `SESSION/player.cpp` holds a `std::map<uint32, PlayerGameGuard*> m_gg` per connected Game Server session, keyed by the Game Server's client socket id. `PlayerGameGuard` (`TYPE/game_guard_type.hpp`) = `CCSAuth2 m_csa` + `m_auth_time` + `m_auth_reply`. Adding an existing key replaces it.
- `GGAuth` (`CSAuth/GGAuth.cpp/.hpp`): the constructor calls `InitGameguardAuth(NULL, n, true, 0x03)`, so path = CWD, timer enabled, log flags 3. It creates an event and a thread `updateTimerProc` that calls `GGAuthUpdateTimer()` every `m_time_sleep = 5000*60` ms (5 min). `NpLog` and `GGAuthUpdateCallback` write to the message pool.
- `INTPTR_MAX == INT64_MAX` (64-bit build): `GGAuth.cpp` replaces everything with stubs. `InitGameguardAuth` returns 0, and `CCSAuth2::GetAuthQuery`, `CheckAuthAnswer`, etc. just return error `1234`. So a 64-bit GG Auth Server starts but cannot verify anything. A 32-bit build links `../CSAuth/ggsrvlib26_MT.lib` and `ggsrv26.h` (real vendor lib; `MY_CSAuth2` is `FALSE`).

## 6. Data flow of the GAMEGUARDAUTH check in the Game Server

Config: `[SERVERINFO] GAMEGUARDAUTH` (`Server Lib/Game Server/Game Server/server.ini`, line 69, "Game Guard Auth, 1=ON, 0=OFF"). Read in `game_server::config_init`: `m_GameGuardAuth = (readInt >= 1)`. Both shipped `server.ini` files (Windows and Linux) have `GAMEGUARDAUTH = 1`.

Compile-time mode switch: `Server Lib/Game Server/CSAuth/GGAuth.hpp` hardcodes `#define MY_GG_SRV_LIB 1` ("Uses my GG Server Lib"). With that value, on both 32-bit and 64-bit, the Game Server includes `GGSrvLib26-1.h` and uses `CCSAuth2`/`GGAuth` in-process. `unit_gg_auth_server_connect` and `IUnitGGAuthServer` are only active when `INTPTR_MAX == INT64_MAX && MY_GG_SRV_LIB == 0` (`game_server.h`, `game_server.cpp`, `player_manager.cpp`, `packet_func_sv.cpp`). Change the define to 0 and build x64 to get the GG-Auth-Server mode.

### Mode A: in-process (default)
1. Server start: `game_server` constructor does `m_game_guard = new GGAuth(m_si.max_user)` if `m_GameGuardAuth` (`game_server.cpp` ~146). `GGAuth` calls `InitGameguardAuth(NULL, ...)`. That reads `csauth2.cfg` and loads `ggauthNN.dll|.so` from the working directory.
2. Accept (`onAcceptCompleted`): `p->m_gg.m_auth_reply = true; m_auth_time = 0; m_csa.Init();`. Then the Game Server sends its initial packet `0x3F`.
3. Every 10 s the monitor thread (`game_server.cpp` ~5580-5593, `WaitForSingleObject(EventShutdownServer, 10000)`) calls `player_manager::checkPlayersGameGuard()` -> `checkGameGuard(player&)` for each session.
4. `checkGameGuard`: if `m_auth_reply == false`, return true (the caller does `shutdown(sock, SD_RECEIVE)`). Else if `m_auth_time < now`, call `m_csa.GetAuthQuery()`.
   - On success it sets `m_auth_time = now + 60 + random(0..60)` seconds (`AUTH_PERIOD_SEC`, `AUTH_PERIOD_VARIATION_SEC` in `TYPE/game_guard_type.hpp`) and sends packet **0xD7** with the 16-byte `m_AuthQuery`.
   - On error it logs "CSAuth Fail" and disconnects the player.
5. Client replies with packet **0x88** (`packet_func::packet088` -> `game_server::requestCheckGameGuardAuthAnswer`):
   - The session must be authorized (logged in). Otherwise `CHECK_SESSION_IS_AUTHORIZED` throws.
   - If `m_GameGuardAuth` is off, the player is disconnected with a warning ("sent GameGuardAuth Answer but GameGuard Auth is disabled").
   - Otherwise it reads 16 bytes into `m_csa.m_AuthAnswer`, calls `CheckAuthAnswer()`. `ERROR_SUCCESS` sets `m_auth_reply = true`. Anything else logs the Err/Index/Value1-3 and calls `DisconnectSession`.
6. `m_auth_reply` is only ever set to `false` in `SESSION/player.cpp` (`clear`) per my grep of the Game Server tree, never between a query and its answer. So the "player did not answer, disconnect" branch of `checkGameGuard` is effectively dead code. An unanswered query is not punished, only a wrong answer or a `GetAuthQuery` error (unverified beyond that grep).
7. Server packet 0xD7 is registered as `packet_svFazNada` on the server-send table (`game_server.cpp` ~588).

### Mode B: via GG Auth Server (`MY_GG_SRV_LIB == 0`, x64)
Game Server side is `Server Lib/Game Server/UNIT/unit_gg_auth_server_connect.cpp`. It reads `[GGAUTHSERVER] IP`/`PORT` and keeps a reconnecting client connection, polled in `onHeartBeat`.

Login handshake (GG AS <-> Game Server, relayed by the Auth Server):

1. GG AS accepts the TCP connection and sends raw packet `0x00` = {`session key` int32, `server uid` int32} (`gg_auth_server::onAcceptCompleted`).
2. Game Server (`requestFirstPacketKey`) stores the key and sends `0x01` {its own `m_si.uid`}.
3. GG AS (`requestLoginFirstStep`) stores `uid`, generates `auth_key = clock() + rand()` and sends it to the Game Server with that uid through the Auth Server (command packet `0x01`).
4. Game Server gets it as Auth Server packet `packet_as001` (`funcs_as` 0x1) -> `authCmdKeyLoginGGAuthServer`. It computes `md5(to_string(uint32(m_si.uid ^ key_auth)))` and sends packet `0x02` {hash string} to the GG AS (`sendHaskKeyLogin`).
5. GG AS (`requestLoginSecondStep`) recomputes `md5(to_string(uid ^ auth_key))`. If equal, `m_is_authorized = 1` and it replies `0x01` {oid}. The Game Server's `requestAskLogin` logs success when `oid > -1`. The other GG AS requests throw "Hacker or Bug" if the session has not completed this.

Per-player operation, keyed by the player's socket id (`p->m_sock`, `p->m_sock.fd` on Linux):

| Direction | Packet | Content | Handler |
|---|---|---|---|
| GS -> GG AS | 0x03 | socket_id | `requestAddPlayerGameGuardCtx` creates a `PlayerGameGuard` (also done by `CCSAuth2::Init(socket_id)` -> `ggAuthCreateUser`) |
| GS -> GG AS | 0x04 | socket_id | `requestDeletePlayerGameGuardCtx` |
| GS -> GG AS | 0x05 | socket_id | `requestGetAuthQueryPlayer` |
| GG AS -> GS | 0x02 | error u32, socket_id u32, 16 B query | `ggAuthCmdGetQuery` copies the query, schedules the next check and sends 0xD7 to the client. Error `1235` = unknown socket. Any non-zero error disconnects the player. |
| GS -> GG AS | 0x06 | socket_id, 16 B answer | `requestCheckAuthAnswerPlayer` |
| GG AS -> GS | 0x03 | error u32, socket_id u32 | `ggAuthCmdCheckAnswer` (disconnects the player on failure, sets `m_auth_reply = true` on success) |
| GG AS -> GS | 0x04 | error code | error reply for 0x03/0x04 requests |

In this mode the client-facing 0xD7/0x88 packets are identical to mode A. In this mode `CCSAuth2` on the Game Server (`Game Server/CSAuth/GGAuth.cpp`, 64-bit/`MY_GG_SRV_LIB==0` branch) is a proxy: `Init`/`Close`/`GetAuthQuery`/`CheckAuthAnswer` call `ggAuthCreateUser/DeleteUser/GetQuery/CheckAnswer`, which send the packets above.

Wire details: these links use the engine's normal packet framing with per-session key (`makeFull(m_key)`, see `Server Lib/Projeto IOCP`). The GG AS initial packet 0x00 is raw (no crypt, no compress).

## 7. Configuration

### `Server Lib/GG Auth Server/GG Auth Server/server.ini`
The file is in Windows-1252, so non-ASCII characters in comments are garbled.

| Section / key | Value in repo | Meaning |
|---|---|---|
| `[SERVERINFO] VERSION` | "IOCP Version Beta 1.1" | Server version string |
| `CLIENTVERSION` | "SS.R7.995.00" | Client version string (not used for GG auth) |
| `NAME` | "GG Auth Server" | Server name (comment "nome do server") |
| `GUID` | 90909 | Server UID, sent as `m_si.uid` in the 0x00 packet |
| `PORT` | 7788 | TCP listen port. The Game Server `[GGAUTHSERVER] PORT` must match. |
| `IP` | "127.0.0.1" | Listen IP |
| `MAXUSER` | 10 | Max sessions (one per Game Server). It also sizes the GG context pool: `max_user * 2000`. |
| `PROPERTY` | 0 | Server property flags (engine-generic) |
| `[OPTION] TTL` | 0 | Kick a player that does not reply within this many ms, 0 = off (comment: "TTL = Time To Live") |
| `[LOG] DIR` | "Log" | Log directory |
| `[NORMAL_DB]` | `DBIP/DBNAME/DBUSER/DBPASS = pangya` | Engine DB settings. `DBPORT` is commented out. No GG-specific DB use found: the unit is created with 2 DB instances but I found no `cmd_*` call in the GG AS code (verified by reading, not exhaustively by grep). |
| `[AUTHSERVER] IP / PORT` | 127.0.0.1 / 7777 | Auth Server to connect to |

`GG Auth Server/GG Auth Server/config/socket_config.ini`: IP rules (`enable_ip_rules: 1`, `limit_connection_per_ip: 10`, `order: deny,allow`, `allow: 127.0.0.1`) and DDoS protection, the same format as the other servers. With `order: deny,allow` and no `deny:` rule, every IP is accepted. `allow: 127.0.0.1` only exempts localhost from the DDoS and per-IP limits (verified in `Server Lib/Projeto IOCP/SOCKET/socket.cpp`, `connect_check`). To restrict access, add `deny: all` or use `order: mutual-failure`.

### Game Server keys (`Server Lib/Game Server/Game Server/server.ini`, and the copy in `Server Lib/Linux Builds/Game Server/server.ini`)
- `[SERVERINFO] GAMEGUARDAUTH` = 1 or 0 (see section 6).
- `[GGAUTHSERVER] IP = 127.0.0.1`, `PORT = 7788`. Only read in mode B (`unit_gg_auth_server_connect::config_init`).

### `csauth2.cfg`
Plain text, whitespace-separated protocol numbers, at most 100, read with `fscanf("%d")` by `InitGameguardAuth` from the working directory of the process (path `NULL`). The first number is the head protocol used for new players. Copies in the repo:

| Path | Content (bytes) |
|---|---|
| `Server Lib/GG Auth Server/GG Auth Server/csauth2.cfg` and `Server Lib/GG Auth Server/CSAuth/csauth2.cfg` | `70` (2 bytes, no newline) -> `ggauth70.dll` (vendor) |
| `Server Lib/Game Server/Game Server/csauth2.cfg` and `Server Lib/Game Server/CSAuth/csauth2.cfg` | `70` -> `ggauth70.dll` |
| `Server Lib/Linux Builds/Game Server/csauth2.cfg` | `701` (3 bytes) -> `ggauth701.so` (built by `Makefile-ggauth70.mk`) |

There is no per-key documentation in the repo. An empty file makes init fail with error 5. `csver.cfg` is optional (see section 3).

## 8. Vendor `CSAuth` folders

`Server Lib/GG Auth Server/CSAuth/` and `Server Lib/Game Server/CSAuth/` contain the same vendor binaries. The sizes are identical in both (from `ls -l`).

| File | What |
|---|---|
| `ggauth.dll`, `ggauth50.dll`, `ggauth51.dll`, `ggauth52.dll`, `ggauth70.dll` | Vendor protocol modules (Windows DLLs) for protocols 0(?), 50, 51, 52 and 70. Only 70 is selected by `csauth2.cfg`. |
| `ggsrvlib.lib`, `ggsrvlib25.lib`, `ggsrvlib26_MT.lib` | Vendor static libs, 2.x. Only `ggsrvlib26_MT.lib` is referenced (`#pragma comment(lib, "../CSAuth/ggsrvlib26_MT.lib")`, 32-bit builds). |
| `ggsrv.h`, `ggsrv26.h` | Vendor headers. Korean comments. `ggsrv26.h` defines `#define MY_CSAuth2 FALSE`, so the C++ `CCSAuth2` class from the lib is used. |
| `GGAuth.hpp/.cpp` | The repo's own wrapper (see section 5 for the GG AS version, and section 6 for the Game Server version). |
| `csauth2.cfg` | See section 7. |

`GGAuth.cpp` in the Game Server differs from the GG AS one. It supports Linux (`dlopen`, `Event`, `thread`) and has the `MY_GG_SRV_LIB` switch.

## 9. Build and run notes

### Windows
- `Server Lib/GGSrvLib26-1/GGSrvLib26-1.sln`: build the static lib first for each platform you need (Win32/x64, Debug/Release). The Game Server links it by relative path.
- `Server Lib/ggauth70-1/ggauth70-1.sln`: builds `GGAUTH701.dll`. Put it, plus `csauth2.cfg`, in the Game Server's working directory, and put `701` in the cfg (or use the vendor `ggauth70.dll` with `70`).
- `Server Lib/GG Auth Server/GG Auth Server.sln`: only needed for mode B. It has Win32 and x64 configs, but the x64 config has stubbed GG (see section 5), so use Win32.
- Start: `GG Auth Server.exe [anything] <workdir>`. `argv[2]` is the working directory (so `argv[1]` is ignored by this code).

### Linux (`Server Lib/Linux Builds/`)
- `Makefile-ggsrvlib.mk`: g++ `-std=c++20 -fpermissive -fsigned-char`, `ar -crvs`. Builds `GGSrvLib26-1.a` from `GGSrvLib26-1.cpp` + `blowfish.cpp` into `output-ggsrvlib-{debug|release}/` and copies it to `./GGSrvLib26-1/GGSrvLib26-1.a`. That directory exists in the repo containing only a `keepdir` file, so the `.a` has to be built first. Pass `__build_type=release` for `-O3`, default is debug (`-Og -D _DEBUG -ggdb`).
- `Makefile-gs.mk` has `LIBSTATIC := GGSrvLib26-1/GGSrvLib26-1.a`, so the Game Server won't link until the `.a` exists. It compiles `UNIT/unit_gg_auth_server_connect.cpp` and `CSAuth/GGAuth.cpp`, and links with `-ldl`.
- `Makefile-ggauth70.mk`: builds a shared object `ggauth701.so` (`-D GGAUTH701_EXPORTS -fPIC -shared -fvisibility=hidden`). It compiles the three `ggauth70-1` sources plus the engine files needed for timers, and copies the result to `./Game Server/ggauth701.so` (`GAMESERVERDIR`). The stripped target also references an undefined `$(MESSAGESERVERDIR)`, which is a bug in the unused stripped target.
- Run the Game Server from `Linux Builds/Game Server/` (it contains `csauth2.cfg` with `701`, `server.ini`, `config/`, `data/`). `LoadAuthProtocol` uses `getcwd()` + name, so the CWD must contain `csauth2.cfg` and `ggauth701.so`.

## 10. Linux situation

- There is no Linux build of the GG Auth Server: no `Makefile-gg*.mk` for it, and its code uses WinSock2, `DbgHelp`, `CreateThread` etc. directly (no `__linux__` branches in `Server Lib/GG Auth Server`).
- The vendor pieces (`ggsrvlib*.lib`, `ggauth*.dll`) are Windows-only binaries. They cannot be used on Linux, and the 32-bit-only vendor lib cannot be linked into an x64 build either.
- Therefore on Linux the only workable path is mode A with the reimplementations: `GGSrvLib26-1.a` (statically into `game`) and `ggauth701.so` (loaded with `dlopen` from the CWD). Both have `__linux__` branches.
- Consequence: on Linux nothing proprietary from nProtect is needed on the server. The check is only as good as the reimplementation (Protocol70 matches the 70-protocol client the project targets, unverified against a real client by me).
- `GAMEGUARDAUTH = 1` in the Linux `server.ini` plus a missing `csauth2.cfg`/`ggauth701.so` makes `GGAuth` init fail. It logs `Nao conseguiu inicializar GameGuard Auth. Error Code: N`, but the server continues without a working check (the constructor just returns).

## 11. `www/nprotect` and `apache-conf/sites-available/nprotect.conf`

These are the HTTP side of the nProtect client, not the TCP auth above. They are loosely related only by being nProtect.

- `www/nprotect/GameGuard/RealServer/`: the GameGuard client updater files (`update.cfg`, `update_.cfg`, `update_2.cfg`, `update_sem_o_vxd_e_x9p.cfg` = "update without vxd and x9p", and many `*.npz` modules such as `GameMon.npz`, `GameMon64.npz`, `npggNT.npz`, `ggerror.npz`, `PangyaSS.npz`). The client downloads these to update GameGuard.
- `www/nprotect/LogServer3/`: PHP stubs for nProtect's log/report endpoints (`bl.php`, `bn.php`, `el.php`, `hl.php`, `mr.php`, `se.php`, `service.php`) plus `config/*.inc` (logging, crypt, save_log). `bl.php` expects POST fields (`gid`, `type`, `binfo`, `md5`, `hwid`, `mac`, ...) and an optional `erlfile` upload, then logs them to `log/`. `service.php` computes the answer values for a GET with `gd` (encrypted game name), `mtp`, `dm`. These endpoints are not called by any C++ server in the repo (searched `Server Lib`).
- `apache-conf/sites-available/nprotect.conf`: a port 80 VirtualHost (`ServerName ss5.ddns.net`) rooted at `${INSTALL_DIR}/www/nprotect/`. Everything is denied except `*.cfg` and `*.npz` (the updater), and under `LogServer3/` only `*.php`, `*.do`, `bn`, `bl`, `mr`. It rewrites `/LogServer3/<name>` and `/LogServer3/<name>.do` to `<name>.php`, and denies `LogServer3/log/` and `LogServer3/config/`. All error pages return "Unknown error". The hostname is the original author's DDNS, so change `ServerName` for your own setup. The client's nProtect update/log URLs must point to this host (client-side, not verified here).

## 12. Known quirks and gotchas

- The GG Auth Server is effectively an optional/legacy path. `MY_GG_SRV_LIB` is hardcoded to 1 in the Game Server. In that mode `packet_as001` only logs `Teste, ver se esta funcionando.` ("Test, see if it works") and no GG AS connection is made.
- The x64 GG Auth Server is stubbed, returning error 1234 for everything. Only a Win32 build with the vendor lib works for mode B.
- `m_auth_reply` is never cleared after a query, so non-answering clients are not dropped (section 6).
- Blowfish key, XOR masks and the Protocol70 algorithms are hardcoded in the sources.
- Windows `csauth2.cfg` says `70` (vendor DLL), Linux says `701` (own module), and the VS project also outputs `GGAUTH701.dll`. A mismatch gives `ERROR_GGAUTH_FAIL_LOAD_DLL` (2).
- The `GGAuth` wrapper hands `InitGameguardAuth` a `NULL` path, so everything is looked up relative to the CWD. The Windows `GG Auth Server` accepts the workdir as `argv[2]`.
- The `GGAuth.cpp` Game Server wrapper for the x64/`MY_GG_SRV_LIB==1` case links `../../GGSrvLib26-1/x64/{Debug,Release}/GGSrvLib26-1.lib`, so the lib must be built into those exact output dirs.
- `GG Auth Server` `socket_config.ini` does not restrict access to `127.0.0.1`: With `order: deny,allow` and no `deny:` rule, every IP is accepted. `allow: 127.0.0.1` only exempts localhost from the DDoS and per-IP limits (verified in `Server Lib/Projeto IOCP/SOCKET/socket.cpp`, `connect_check`). To restrict access, add `deny: all` or use `order: mutual-failure`.
- Update timer: `GGAuthUpdateTimer()` runs every 5 minutes in both `GGAuth` implementations.
