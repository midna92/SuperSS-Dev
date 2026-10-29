# Login Server

Source: `Server Lib/Login Server` (executable `login` on Linux, solution `Server Lib/Login Server/Login Server.sln` on Windows).

Facts are taken from the code. Anything not verified is marked "unverified". Portuguese text is translated where cited.

## 1. Purpose

The Login Server is the first server a PangYa client talks to. It authenticates the account, handles first-time account setup (nickname, starting character), then hands the client the Game Server list and the keys needed to move on to a Game Server (or Message Server). It also gates access (GM-only mode, IP/MAC bans, one-session-per-account).

## 2. How it fits in the system

- Class `stdA::login_server` (`Server Lib/Login Server/Login Server/login_server.h`), derived from the shared `server` class (`Server Lib/Projeto IOCP/Server/server.h`). Singleton accessor: `sls::ls::getInstance()`. Server type `m_si.tipo = 0`.
- Clients connect here with a game-client protocol (encrypted/compressed packets from the shared engine in `Server Lib/Projeto IOCP`).
- `server::server()` creates a `unit_auth_server_connect`, which connects to the Auth Server (`[AUTHSERVER] IP/PORT`) and retries every 5 seconds if the Auth Server is down. See `docs/auth-server.md`. The Login Server works without the Auth Server, but "drop the same account on the Game Server" then falls back to a direct DB update.
- Every second the shared monitor registers the Login Server in the DB (`pangya.ProcRegServer_New`) and refreshes the Game Server list (`pangya.ProcGetServerList`). The Game Server list that clients receive comes from the database, not from a direct connection to the Game Servers.
- The Login Server loads the item database `data/pangya_jp.iff` (`PATH_PANGYA_IFF` in `Server Lib/Projeto IOCP/UTIL/iff.h`) in its constructor (`sIff::getInstance().load()`). It uses it to validate the starting character `typeid`. `reload_files` reloads it.

## 3. Startup and runtime flow

`main()` in `Server Lib/Login Server/Login Server/Login Server.cpp` is identical in structure to the Auth Server's:

1. If `argc > 2` and `argv[2]` is a directory, `chdir` into it, set the log prefix to `LS`, reload logs. `argv[1]` is ignored.
2. Windows: SEH handler plus mini dump. Linux: unlimited core dump size, nice value lowered by 2 (or 1).
3. `sls::ls::getInstance().start()`.

`login_server::login_server()`:

- `server(m_player_manager, 2 /*DB instances*/, 8, 4 /*thread counts: unverified order*/)`.
- Loads the IFF, calls `config_init()`, registers client packet handlers and sets `m_state = INITIALIZED`. If anything throws, `m_state = FAILURE` and the server shuts down at start.
- `config_init()` sets tipo 0 and reads `[OPTION] ACCESSFLAG`, `CREATEUSER`, `SAME_ID_LOGIN`.

On start: `server::start()` starts the Auth Server connector, then the accept thread and the console loop.

Console commands (`login_server::checkCommand`):

| Command | Effect |
|---|---|
| `exit` | Quit. |
| `reload_files` | Re-reads `server.ini`, option flags and the IFF. |
| `reload_socket_config` | Re-reads `config/socket_config.ini`. |
| `open gm` | Sets `m_access_flag = 1`. Only GMs and IPs registered for the account may log in. |
| `open all user` | Sets `m_access_flag = 0`. Everybody can log in. |
| `create_user on` / `create_user off` | Toggles automatic account creation. |
| `snapshot` | Writes through a null pointer to force a crash dump. Kills the process on Linux. |

Remote shutdown: the Auth Server can send packet 0x02, which triggers `login_server::authCmdShutdown` and a shutdown timer.

## 4. Login flow, step by step

### Step 0: connect

1. The client opens a TCP connection to `[SERVERINFO] PORT`. The accept layer (`socket.cpp`) applies `config/socket_config.ini` rules (IP allow/deny, per-IP limit, DDoS detection). Denied clients are rejected before any packet.
2. `login_server::onAcceptCompleted` sends a raw (not compressed, not encrypted) packet `0x00`: `int32 session_key`, `int32 server_uid` (`GUID`). The client uses the key for all further packets.
3. Sessions idle longer than `[OPTION] TTL` ms (sample: 60000) are dropped by `session_manager::checkSessionLive`.
4. Each incoming packet passes `login_server::checkPacket`. If the same packet id is repeated at or above `CHK_PCKT_COUNT_LIMIT` (5, `Server Lib/Projeto IOCP/SOCKET/session.h`) the session is disconnected as a DDoS attempt.

### Client version check against CLIENTVERSION

I did not find any check of the client's version against `CLIENTVERSION` in the Login Server. `CLIENTVERSION` is only copied into `m_si.version_client` (`unit.cpp`/`server.cpp` `config_init`), registered in the DB by `CmdRegisterServer`, and shown in the server list. The only consumer of the string for comparison is the Game Server (`Server Lib/Game Server/GAME/login_manager.cpp` ~line 1280 reads `CLIENTVERSION`). The login packet handler (`login_server::requestLogin`) reads only id, password, options and MAC. Treat the Login Server as accepting any client version.

### Step 1: packet 0x01, credentials (`login_server::requestLogin`)

Payload: `string id`, `string password` (plain text from client), `uint8 opt_count`, `opt_count x uint64` (purpose unknown), `string mac_address`.

1. ID validation: length must be at least 2 and it must not contain any of `^ $ & , ? ` ´ ~ | " @ # ¨ ' % * ! \`. Otherwise reply 0x01 option 2 ("invalid ID") and disconnect.
2. Password is MD5-hashed (`md5::processData` / `getHash`) in the server. Both ID and the hash (and MAC) are written to the log file. In release builds this goes to the file log only, in debug builds also to the console.
3. Ban check `server::haveBanList(ip, mac)`: refuses an empty MAC (assumed to be a modified client), MACs in `pangya.pangya_mac_table`, and IPs in `pangya.pangya_ip_table` (exact/mask or range). Banned: reply `0x01 {uint8 16, int32 500012}` ("banned by region") and disconnect. The lists are loaded by the server class from `CmdListIpBan` and `CmdListMacBan`.
4. `ProcVerifyID(id)` returns the account UID. If found (UID > 0), continue to step 2.
5. If the ID does not exist: if `ACCESSFLAG == 0` and `CREATEUSER == 1`, the account is created (step 1b). Otherwise reply `pacote001` with option 6 (wrong ID or password) and disconnect. The message deliberately does not tell the user whether the ID exists.

### Step 1b: account creation (`CREATEUSER`)

Executed inside `requestLogin` when the ID is unknown and creation is allowed:

1. `CmdCreateUser(id, password, ip, server_uid)` calls `pangya.ProcNewUser(id, pass, ip, server_uid)`. Note that it passes the plain `password`, and the stored procedure computes `upper(md5(pass))` itself (see `bk-squema-mysql.sql`). The procedure inserts the `account` row (nick = id) and a row into many per-player tables (`user_info`, `pangya_player_location`, `pangya_user_equip`, `tutorial`, `pangya_user_macro`, `pangya_myroom`, and others), and returns the new UID.
2. `ProcGetPlayerInfoLogin(uid)` loads the player.
3. The server sends the "first login" sequence (nickname creation, step 3). The session is marked authorized.

### Step 2: checks for an existing account

After `ProcVerifyPass(uid, md5)` succeeds (`CmdVerifyPass::getLastVerify`), the server loads `ProcGetPlayerInfoLogin(uid)` into `m_pi` and in parallel queries:

- `CmdLogonCheck`: `SELECT LOGON, game_server_id FROM pangya.account WHERE UID = <uid>`. Whether the player is currently logged on to a Game Server.
- `CmdFirstLoginCheck`: `SELECT FIRST_LOGIN FROM pangya.account WHERE uid = <uid>`. Whether the nickname was already created.
- `CmdFirstSetCheck`: `SELECT FIRST_SET FROM pangya.account WHERE uid = <uid>`. Whether a starting character was already chosen.

A wrong password gives `pacote001` option 6 and a disconnect, and the log line includes the MD5 hash used.

Then the following checks run in this order. The first that matches decides the reply.

| Order | Condition | Reply to client | Then |
|---|---|---|---|
| 1 | `SAME_ID_LOGIN == 0` and another connected session on this server has the same UID (`HasLoggedWithOuterSocket`) | `0x01 {0xE2, int32 5100107}` | Disconnect (`shutdown(SD_RECEIVE)`) |
| 2 | `m_pi.m_state != 0`, meaning a login was already started on this session | `0x01 {0xE2, int32 500010}` | `m_state` incremented. At 3 or more, disconnect (attack) |
| 3 | `ACCESSFLAG == 1`, player is not a GM (`m_cap & 4`), and `ProcVerifyIP(uid, ip)` says the IP is not registered for this account | `0x01 {0xE2, int32 500015}` ("restricted access") | Disconnect |
| 4 | Account has block flags (`block_flag.m_id_state`), see below | various | Disconnect |
| 5 | `FIRST_LOGIN` not done | first login sequence (step 3) | |
| 6 | `FIRST_SET` not done | first set sequence (step 4) | |
| 7 | Already logged on a Game Server (`LOGON` set) | `0x01 {uint8 4}`, `m_server_uid = game_server_id` stored | Client then sends 0x04 (step 6) |
| 8 | GM (`m_cap & 4`) | success login (step 5) | Log "GM logged in" |
| 9 | Otherwise | success login (step 5) | |

When `ACCESSFLAG` is 0, the IP permission check (row 3) is skipped but the same ordering of rows 5 to 9 applies (the code has two copies of these branches).

Block flags (row 4):

| Flag | Reply | Side effect |
|---|---|---|
| `L_BLOCK_TEMPORARY` with `block_time == -1` or `> 0` | `0x01 {uint8 7, int32 hours}` (1 if under an hour or indefinite) | none |
| `L_BLOCK_FOREVER` | `0x01 {uint8 0x0c}` | none |
| `L_BLOCK_ALL_IP` | `0x01 {uint8 16, int32 500012}` | Inserts the client IP with mask `255.255.255.255` via `ProcInsertBlockIP` |
| `L_BLOCK_MAC_ADDRESS` | `0x01 {uint8 16, int32 500012}` | Inserts the MAC via `ProcInsertBlockMAC` |

Any exception in `requestLogin` replies `0x01 {0xE2, int32 500050}` (system error), or option 2 for an invalid ID, then disconnects.

### Step 3: first login, nickname creation

Triggered by the `FIRST_LOGIN(...)` macro: sets `m_state = 2`, sends packet `0x0F` (tutorial/new-account message with the account id, from `pacote00F`) and `0x01` with option `0xD8` (`pacote001`, first login). The session is `m_is_authorized = 1` from here on, so the follow-up requests are allowed.

The client then:

1. Packet `0x07` (check nickname): `wstring nick`. `packet_func::packet007` validates it and replies `0x0E {int32 result [, string nick]}`. Results:

   | Code | Meaning | Condition in code |
   |---|---|---|
   | 0 `SUCCESS` | Nick available | Replies with the nick string |
   | 1 | Unknown error | Exception |
   | 2 `NICK_IN_USE` | Taken | `ProcVerifyNickname` finds it |
   | 3 `INCORRECT_NICK` | Invalid | Shorter than 4 characters, or contains `^ $ & , ? ` ´ ~ | " @ # ¨ ' % * ! \` |
   | 5 `HAVE_BAD_WORD` | Disallowed words | Matches `.*GM.*` or `.*ADM.*` (case-insensitive), unless the player is a GM |
   | 6 | DB error | A `PANGYA_DB` exception |
   | 7 | (empty error) | Nick contains a space |
   | 9 `SAME_NICK_USED` | Equals the account ID | Case-insensitive compare |

   Code 12 (`CODE_ERROR_INFO`) is used elsewhere to send an error number that the client looks up in the IFF (message ids such as 500051).
2. Packet `0x06` (save nickname): `string nick` (converted with `MbToWc`). The server runs `ProcSaveNickname(uid, nick)`, then `UPDATE pangya.account SET FIRST_LOGIN = 1 WHERE UID = <uid>` (`CmdAddFirstLogin`). It re-checks `FIRST_SET`. If not done, it sends `0x01` option `0xD9` (step 4). Otherwise it proceeds to success login (step 5). On error it sends `0x0E` with code 1.

### Step 4: first set, starting character

Triggered by the `FIRST_SET(...)` macro (`m_state = 3`, packet `0x0F` and `0x01` option `0xD9`). The client sends packet `0x08`: `int32 character_typeid`, `uint8 default_hair`, `uint8 default_shirts`. `packet_func::packet008`:

1. Requires authorized session. Validates `sIff::findCharacter(typeid)` is non-null, `default_hair <= 9`, `default_shirts == 0`.
2. Builds a `CharacterInfo` with default parts (`initComboDef`), and runs `ProcAddCharacter(uid, ...)` and `ProcFirstSet(uid)` in parallel, then `USP_FLUSH_CHARACTER(uid, character_id)` to set it as the equipped character.
3. Replies `0x11 {uint16 0}` (ok) and then goes to success login.
4. On error replies `0x11 {uint16 1}` and `0x0E {12, error 500051}`.

### Step 5: success login and server list (`packet_func::succes_login`)

Called by the `SUCCESS_LOGIN` macro (sets `m_state = 1` and logs "player logged in"). Option 0 is the full login. In `succes_login`, it runs these queries (each waited on):

1. `ProcGetServerList(GAME)`, then `ProcGetServerList(MSN)` (Message Servers).
2. `ProcGeraAuthKeyLogin(uid)`: generates the "auth key login" for the player and returns it.
3. If option 0: `ProcGetMacrosUser(uid)` (chat macros).
4. `ProcRegisterLogin(uid, ip, login_server_uid)`: records the login.

The client then receives, in this order:

| Packet | Content | Only when option 0 |
|---|---|---|
| `0x10` | `string auth_key_login` | no |
| `0x01` | option 0: id, uid, capability (`m_cap`), level, constants, timestamp, a fixed string `"302540"` (unknown purpose, observed as ignored by the client according to the source comments), nickname | yes |
| `0x02` | `uint8 count` followed by raw `ServerInfo` structs: Game Server list | no |
| `0x09` | same format: Message Server list | no |
| `0x06` | `chat_macro_user` block | yes |

Query errors for the server list, Message Server list, macros and auth key are tolerated (the packets are still sent with empty data). Other errors propagate.

### Step 6: choose a Game Server, key handed over (packet 0x03)

The client sends `0x03 {uint32 server_uid}`. `packet_func::packet003` (requires an authorized session):

1. `ProcRegisterLogonServer(uid, server_uid)` records which server the player is going to.
2. `ProcGeraAuthKeyGame(uid, server_uid)` generates the game auth key.
3. Replies `0x03 {int32 0, string auth_key_game}`.

The client presents that key to the Game Server, which verifies it against the same database. On a DB error the reply is still sent, with an empty key.

### Packet 0x04: kick the existing session on a Game Server

When step 2 row 7 applies, the client sends `0x04`. `login_server::requestDownPlayerOnGameServer`:

- If the Auth Server link is live: `m_unit_connect->sendDisconnectPlayer(game_server_uid, player_uid)` (Auth Server packet 0x02). The Game Server drops the player and confirms via 0x03 to the Auth Server, which sends 0x07 to the Login Server. `authCmdConfirmDisconnectPlayer` then runs `succes_login` for the waiting session.
- Otherwise: `ProcRegisterLogon(uid, 1)` directly marks the account as logged off in the database, then `succes_login`.
- On error the client gets `0x0E {12, error}` (500053 or the Login Server error code).

### Packet 0x0B: re-login after leaving a Game Server

`login_server::requestReLogin`: `string id, uint32 server_uid, string auth_key_login`. Checks, in order: ID exists, player info matches the ID, the key equals `ProcGetAuthKeyLogin(uid)`, IP not banned (MAC is not checked because this packet carries none), `ACCESSFLAG` and `ProcVerifyIP`, and the block flags (reported only as exceptions). On success it calls `succes_login(..., option 1)`, which sends only `0x10`, `0x02` and `0x09`. On any failure it sends `0x0E {12, error 500052}` and disconnects. Note that `server_uid` is read and logged but not otherwise used.

## 5. Network protocol summary

Listen port: `[SERVERINFO] PORT` (sample 10303, `Server Lib/Linux Builds/Login Server/server.ini`). Clients are the PangYa game client (region setting: `pangya_jp.iff`, so a JP client, per the file name).

Client to Login Server (`login_server::login_server()` registration):

| ID | Handler | Purpose |
|---|---|---|
| 0x01 | `packet001` then `requestLogin` | Credentials |
| 0x03 | `packet003` | Select Game Server, get game auth key |
| 0x04 | `packet004` then `requestDownPlayerOnGameServer` | Drop the previous session on a Game Server |
| 0x06 | `packet006` | Save nickname |
| 0x07 | `packet007` | Check nickname |
| 0x08 | `packet008` | Choose first character |
| 0x0B | `packet00B` then `requestReLogin` | Relogin with auth key login |

Login Server to client (builders in `Server Lib/Login Server/PACKET/packet_func_ls.cpp`): `0x01` login result or error code, `0x02` Game Server list, `0x03` game auth key, `0x06` chat macros, `0x09` Message Server list, `0x0E` nickname result and error info, `0x0F` tutorial/new-account, `0x10` auth key login, `0x11` first-character result. Handlers for these ids on the "server packet" table are no-ops except `0x03` and `0x06`, which are used for timing logs (`packet_sv006` logs elapsed time to the time log).

Auth Server link: packet table `funcs_as` only has a placeholder `0x01` (`packet_as001`, logs a test line). Real handling of Auth Server messages is done through `server`'s `authCmd*` virtuals: the Login Server implements shutdown, disconnect player and confirm disconnect, and ignores notice, ticker, cube, mail, rate and reload.

## 6. Configuration

### `server.ini`

Samples: Windows `Server Lib/Login Server/Login Server/server.ini` (quoted values, `;` comments, `mssql`) and Linux `Server Lib/Linux Builds/Login Server/server.ini` (unquoted, `#` comments, `mysql`).

| Section / key | Meaning | Sample | Notes |
|---|---|---|---|
| `[SERVERINFO] VERSION` | Build string registered in DB | `IOCP Version Beta 1.1` | |
| `CLIENTVERSION` | Client version string registered in the DB and shown in the server list | `SS.R7.995.00` | Not enforced by the Login Server (see section 4). |
| `NAME` | Server name | `Login Server` | |
| `GUID` | Unique server UID, sent in the hello packet and used as login server id in `ProcRegisterLogin`/`ProcNewUser` | `10103` | Must be unique among servers. |
| `PORT` | Client listen port | `10303` | |
| `IP` | IP registered in the DB. Sample Linux file uses `192.168.15.20`, Windows `127.0.0.1`. | | Change to the address clients can reach. |
| `MAXUSER` | Number of pre-allocated sessions, so maximum concurrent connections | `2001` | |
| `PROPERTY` | Server property bit flags registered in the DB | `2048` | Meaning of the bits: unverified. |
| `[OPTION] SAME_ID_LOGIN` | 1 allows the same account to hold several sockets on this server. Optional (a missing key leaves it off). | `1` | Comment in code: "for development". |
| `CREATEUSER` | 1 auto-creates accounts for unknown IDs. Required (missing key is an error and the server fails to initialize). | `1` | Runtime toggle: `create_user on|off`. |
| `ACCESSFLAG` | 1: only GMs and registered IPs can log in. 0: everyone. Required. | `0` | Runtime toggle: `open gm`, `open all user`. Account creation only works when 0. |
| `TTL` | Idle time in ms before a session is dropped. 0 disables it. | `60000` | |
| `[LOG] DIR` | Log directory | `Log` | |
| `[NORMAL_DB]` | `DBENGINE` (`mysql`, `postgresql`, `mssql`), `DBIP`, `DBNAME`, `DBUSER`, `DBPASS`, `DBPORT` | `mysql`, `localhost`, `pangya`, `pangya`, `pangya`, `3306` | |
| `[AUTHSERVER] IP`, `PORT` | Where to reach the Auth Server | `127.0.0.1`, `7777` | Read by `unit_auth_server_connect::config_init`. |

Required versus optional: `ACCESSFLAG` and `CREATEUSER` are read without a `try`, while `SAME_ID_LOGIN` is wrapped in one. The effect of a missing required key is an exception in `config_init`, which sets the state to FAILURE (inferred from the constructor's catch block, not tested).

### `config/socket_config.ini`

Same parser and keys as for the Auth Server (`socket.cpp`). The Linux sample has `enable_ip_rules: 1`, `limit_connection_per_ip: 10`, `order: deny,allow`, `allow: 127.0.0.1`, `ddos_interval: 3000`, `ddos_count: 5`, `ddos_autoreset: 600000`. With `allow: 127.0.0.1` only, a client on another machine is refused until you add an `allow:` rule (for example `allow: all` or a CIDR), then run `reload_socket_config`. Note the per-IP limit of 10 also matters for clients behind NAT.

## 7. Database usage

Via the shared `Server Lib/Projeto IOCP/PANGYA_DB` classes (all schema `pangya`):

| Class | Statement | Used for |
|---|---|---|
| `CmdVerifyID` | `ProcVerifyID(id)` | Account lookup |
| `CmdVerifyPass` | `ProcVerifyPass(uid, md5)` | Password check |
| `CmdLogonCheck` | `SELECT LOGON, game_server_id FROM pangya.account WHERE UID = ...` | Logged on a Game Server? |
| `CmdRegisterLogon` | `ProcRegisterLogon(uid, option)` | Option 1 logs the player off in DB |
| `CmdInsertBlockIP`, `CmdInsertBlockMAC` | `ProcInsertBlockIP(ip, mask)`, `ProcInsertBlockMAC(mac)` | Auto-ban |
| `CmdListIpBan`, `CmdListMacBan` | `SELECT ip, mask FROM pangya.pangya_ip_table`, `SELECT mac FROM pangya.pangya_mac_table` | Ban lists |
| `CmdAuthKeyLoginInfo` | `ProcGetAuthKeyLogin(uid)` | Re-login verification |
| `CmdAuthKeyLogin` | `ProcGeraAuthKeyLogin(uid)` | Generate login key |
| `CmdAuthKeyGame` | `ProcGeraAuthKeyGame(uid, server_uid)` | Generate game key |
| `CmdChatMacroUser` | `ProcGetMacrosUser(uid)` | Chat macros |
| `CmdAddCharacter` | `ProcAddCharacter(...)` | First character |
| `CmdUpdateCharacterEquiped` | `USP_FLUSH_CHARACTER(uid, character_id)` | Equip it |
| `CmdVerifNick`, `CmdSaveNick` | `ProcVerifyNickname`, `ProcSaveNickname` | Nickname |
| `CmdServerList` | `ProcGetServerList(type)` | Server lists |
| `CmdRegisterServer` | `ProcRegServer_New(...)` | Self-registration, every second |

Login-Server specific (`Server Lib/Login Server/PANGYA_DB`):

| Class | Statement |
|---|---|
| `CmdPlayerInfo` | `ProcGetPlayerInfoLogin(uid)` |
| `CmdVerifyIP` | `ProcVerifyIP(uid, ip)` |
| `CmdFirstLoginCheck` | `SELECT FIRST_LOGIN FROM pangya.account WHERE uid = ...` |
| `CmdFirstSetCheck` | `SELECT FIRST_SET FROM pangya.account WHERE uid = ...` |
| `CmdAddFirstLogin` | `UPDATE pangya.account SET FIRST_LOGIN = <flag> WHERE UID = ...` |
| `CmdAddFirstSet` | `ProcFirstSet(uid)` |
| `CmdCreateUser` | `ProcNewUser(id, pass, ip, server_uid)` |
| `CmdRegisterLogonServer` | `ProcRegisterLogonServer(uid, server_uid)` |
| `CmdRegisterPlayerLogin` | `ProcRegisterLogin(uid, ip, server_uid)` |

`Server Lib/Login Server/DATABASE/pangya_login_db.{h,cpp}` is a legacy static-method DB helper (`VerifyID`, `getAuthKey`, `getMsn`, ...). It is not in the Linux Makefile, so it is probably unused (unverified).

## 8. Source layout

```
Server Lib/Login Server/
  Login Server.sln
  Login Server/   Login Server.cpp (main), login_server.{h,cpp}, server.ini, config/socket_config.ini, data/pangya_jp.iff, stdafx.*
  PACKET/         packet_func_ls.{h,cpp}: client handlers, packet builders (pacote00X), succes_login
  PANGYA_DB/      cmd_player_info, cmd_verify_ip, cmd_first_login_check, cmd_first_set_check,
                  cmd_add_first_login, cmd_add_first_set, cmd_create_user,
                  cmd_register_logon_server, cmd_register_player_login
  SESSION/        player.*, player_manager.*
  TYPE/           pangya_login_st.h, player_info.*
  DATABASE/       pangya_login_db.* (legacy)
```

## 9. Build and run

Windows: open `Server Lib/Login Server/Login Server.sln` in Visual Studio (links `ws2_32.lib`, `DbgHelp.lib`). Make sure `data/pangya_jp.iff` is in the working directory (`data/` relative path).

Linux (`Server Lib/Linux Builds/Makefile-ls.mk`): identical build settings to `Makefile-as.mk` (see `docs/auth-server.md` section 8). Differences:

- `PROGRAM := login`, output folder `output-ls-debug` or `output-ls-release`, binary copied to `./Login Server/login`.
- It additionally compiles `Server/server.cpp`, `THREAD POOL/threadpl_server.cpp`, `UNIT/unit_auth_server_connect.cpp`, `UNIT/unit_connect.cpp`, and `Smart Calculator/Smart Calculator.cpp` from the shared engine, plus the Login Server `login_server.cpp`, `packet_func_ls.cpp`, `SESSION/*`, `TYPE/player_info.cpp` and the nine `PANGYA_DB/cmd_*.cpp` files above. `pangya_login_db.cpp` is not compiled.
- Run from `Server Lib/Linux Builds/`: `make -f Makefile-ls.mk` (debug, includes `SHOW_DEBUG_PACKET`) or `make -f Makefile-ls.mk __build_type=release`.
- Runtime dir `Server Lib/Linux Builds/Login Server/` contains `server.ini`, `config/socket_config.ini` and `data/pangya_jp.iff`. Start with `cd "Login Server" && ./login`. Since `argv[2]` is the only supported working-directory argument, use `./login x "/path"` to start elsewhere.
- Prerequisites at runtime: the database with the schema from `bk-squema-*.sql`, and ideally the Auth Server running (it is retried every 5 seconds if absent).

## 10. Known quirks and gotchas

- Passwords: the client sends plain text. The Login Server MD5-hashes it for verification and logs both the ID and the hash (`"Senha: "` = "Password:" lines in the file log, plus wrong-password messages). Treat the log files as sensitive.
- For account creation, `ProcNewUser` receives the plain password and hashes it in SQL (`upper(md5(...))`), while verification sends `md5` from C++. Whether `ProcVerifyPass` normalizes case is unverified, so keep the same hash casing if you create accounts by other means. The SQL dump also contains `ProcNewUserWithMD5`.
- `CLIENTVERSION` is not enforced by the Login Server.
- Many failures are signalled by numeric error ids interpreted by the client's IFF strings (500010, 500012, 500015, 500050 to 500053, 5100107). The meaning is inferred from code comments only.
- A banned or empty MAC address blocks login on the 0x01 path: an empty MAC is treated as a modified client.
- The code duplicates the check chain (rows 5 to 9) for the `ACCESSFLAG` case and the open case, so any change must be applied to both branches in `requestLogin`.
- `m_pi.m_state` doubles as a "login already in progress" flag and as a marker for first-login (2) and first-set (3) state. Repeated 0x01 packets in those states count as an attack after three tries.
- `login_server::onHeartBeat` and `onStart` do nothing. IP and MAC ban lists are refreshed by the shared `server` monitor, not here.
- `server_uid` in relogin (0x0B) is unused. The MAC ban flag cannot be enforced there.
