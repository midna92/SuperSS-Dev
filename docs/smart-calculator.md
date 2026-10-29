# Smart Calculator family

All paths are relative to the repo root. Statements marked "unverified" were not confirmed in code.

## 1. Purpose

In PangYa terms the "calculator" is a player-facing shot-aid: given distance, height difference, wind, wind angle, terrain (ground) percentage, slopes, club, shot type, power-shot and power stats, it computes how much to aim off (the "desvio", deviation in pb/pba units), the power percentage / caliper (power-gauge position), HWI and spin. Players reach it from the in-game chat by sending a whisper (private message) to a pseudo-nickname. There are two flavours (`eTYPE_CALCULATOR_CMD` in `Server Lib/Projeto IOCP/TYPE/smart_calculator_type.hpp`):

| Flavour | Whisper target | Enum | What it does |
|---|---|---|---|
| Smart Calculator | `#SC` | `SMART_CALCULATOR` | Physics simulation of the ball flight (projectile with air drag and Magnus effect, "obtained by reverse engineering PangYa", header of `Server Lib/Node/Smart-Calculator/smart_calculator.js`). Player enters club/shot/power values and shot conditions, then runs `calc`. |
| Stadium Calculator | `#CS` | `CALCULATOR_STADIUM` | User-authored "calculators" (tables of shots/clubs with power per yard, stored in a DB by a PHP API and evaluated by a Node REST service) plus user "macros" (math expressions via vendored mathjs). |

Extras: a Discord bridge (in-game chat history goes to a Discord channel; Discord admins can broadcast notices into servers) and a standalone Discord bot and web page that run the same Smart Calculator physics.

## 2. How it fits in the system

```
 PangYa client --(whisper to "#SC"/"#CS")--> Game Server (C++)
                                               | game_server::requestPrivateMessage
                                               | -> checkSmartCalculatorCmd (permission) -> Game::execSmartCalculatorCmd
                                               v
                          SmartCalculator singleton (Server Lib/Projeto IOCP/Smart Calculator)
                          dlopen / LoadLibrary  SMARTCALCULATORLIB.so/.dll  (Server Lib/Smart Calculator lib)
                                               |  raw TCP 127.0.0.1:12345, binary packets
                                               v
                          Node "Smart-Calculator" (smart.js)  -- HTTPS POST JSON --> Node "Stadium-Calculator" (index.js, :8000)
                                               |                                                  | HTTP POST form
                                               v                                                  v
                                   Discord bot (discord.js, optional)                  PHP API www/stadium/*.php -> MySQL "stadium"
```

Components:

| Dir | Role |
|---|---|
| `Server Lib/Projeto IOCP/Smart Calculator/` | Server-side hook: `stdA::SmartCalculator` singleton (`sSmartCalculator`) inside every server binary built from the shared engine. Parses the command, loads the lib, dispatches. |
| `Server Lib/Smart Calculator lib/` | The loadable library `SMARTCALCULATORLIB`. Holds per-player calculator state (`PlayerManager`), validates arguments, builds binary packets, owns the TCP socket (`Pool`) to the Node process. |
| `Server Lib/Node/Smart-Calculator/` | Node TCP server on loopback. Authenticates the C++ server, computes Smart Calculator results locally, proxies macro/Stadium requests over HTTPS, hosts the Discord chat bridge. |
| `Server Lib/Node/Stadium-Calculator/` | Node HTTP REST API (default :8000): `/calculadora`, `/calculadora_manager`, `/macro`. Pulls calculator/macro definitions from the PHP API and evaluates them. |
| `www/stadium/`, `apache-conf/sites-available/api-stadium.conf` | PHP storage API (`calculadora.php`, `macro.php`) behind an Apache vhost. |
| `Server Lib/Node/Smart-Calculator-Bot/` | Stand-alone Discord bot (`!calc` etc.), same physics, no connection to the game servers. |
| `Smart Calculator App/` | Static HTML+JS page (`index.htm`, `smart_calculator.js`) running the same physics in a browser. |
| `Server Lib/Linux Builds/Makefile-smart.mk` | Linux build of the library. |
| `Server Lib/Linux Builds/run-as-to-rs.sh` | tmux launcher that starts `node smart.js` before the C++ servers. |

## 3. Startup and runtime flow

### 3.1 Start order
`Server Lib/Linux Builds/run-as-to-rs.sh` creates tmux sessions in this order: `Smart-Calculator` (`node smart.js`, working dir `/home/acrisio/projects/Node/Smart-Calculator/`, hardcoded), Auth Server (`./auth`), Login (`./login`), Message (`./msn`), Rank (`./rank`), and Game (`./game`) only when the argument is `all`. `run-as-to-rs.sh kill` exits the C++ sessions and kills the `Smart-Calculator` tmux session. Directory `dir=/home/acrisio/projects/Linux Builds` is also hardcoded. The Node process should be up first because the library connects on load; it reconnects lazily (see 5.2).

### 3.2 Loading the library
- `stdA::SmartCalculator` is a `Singleton`; its constructor registers the command table (`initCommand`) and calls `initDllResource()`.
- Library path macro `PATH_SMART_CALCULATOR_LIB_DLL` (`Smart Calculator.cpp`): on Linux `SMARTCALCULATORLIB.so`, loaded with `dlopen(getcwd() + "/" + name, RTLD_LAZY)`, i.e. it must sit in the server's working directory. Windows debug builds use a relative path `../../Smart Calculator lib/{x64/}Release/SMARTCALCULATORLIB.dll`, otherwise `SMARTCALCULATORLIB.dll` (exact `#if` conditions at lines 27-40 of that file).
- Symbols resolved (all five must resolve): `initSmartCalculatorLib`, `sendCommandServer`, `makePlayerContext`, `getPlayerContext`, `removeAllPlayerContext`. Then `initSmartCalculatorLib(server_uid)` is called with `ssv::sv->getUID()` (0 if no server).
- The host exports `scLog` and `responseCallBack` (`extern "C"`), which the lib uses to log and to deliver replies.
- Gate: `m_si.rate.smart_calculator` (column `SmartCalculator` of `pangya.pangya_config`, read by `Projeto IOCP/PANGYA_DB/cmd_rate_config_info.cpp`, written by `cmd_update_rate_config_info.cpp`; default 1 in `message_server.cpp` when the DB row is missing). The Message Server calls `load()` at start if enabled and again in its heartbeat if the lib is not loaded and not "stopped".
- The Makefile copies the built `.so` into `./Game Server` and `./Message Server` (relative to `Linux Builds`), i.e. these two servers are the consumers. Game Server uses it for player commands; both use it for the Discord chat history (`server::sendMessageToDiscordChatHistory`).

### 3.3 Operator commands (Message Server console)
`message_server::checkCommand` (`Message Server/Message Server/message_server.cpp`, ~line 3046): `smart_calc reload|start` (load), `smart_calc close` (close and set stopped so the heartbeat does not restart it), `smart_calc chat_discord` (toggle `m_chat_discord`). Errors out with "Smart Calculator not active" when the rate flag is 0. The Message Server also has an event command to set the flag (`m_si.rate.smart_calculator = _qntd`, ~line 3356).

### 3.4 Player command path
1. Client whispers `#SC <cmd> <args>` or `#CS <cmd> <args>`. `game_server::requestPrivateMessage` intercepts these nicknames when `m_si.rate.smart_calculator` is set and `checkSmartCalculatorCmd` returns true; otherwise the whisper is handled as a normal PM (and fails because that player does not exist).
2. Permission (`game_server::checkSmartCalculatorCmd`): allowed for GM (`game_master` or `gm_normal` capability), otherwise needs `premium_user` and: for `#SC` a premium-2 ticket (`isPremium2`), for `#CS` a premium ticket (`isPremiumTicket`) on `pt._typeid`. Player must be in a channel.
3. `Game::execSmartCalculatorCmd` (`Game Server/GAME/game.cpp` ~2672): creates the player context (`makePlayerCtx`); for `#SC` it first fills the game shot values (`getGameShotValueToSmartCalculator`: wind, club power etc. taken from the live game) before `checkCommand`.
4. `SmartCalculator::checkCommand` splits on spaces, lower-cases the first token, looks up `(command, type)` in `m_commands`; unknown commands raise an error that is only logged.
5. The handler forwards the joined arguments to the lib function; the lib validates, updates its per-player state and, for ping/calc/macro/expr/list/open, sends a binary packet to Node.
6. Replies come back via `responseCallBack` -> `SmartCalculator::sendReply` -> `responseCallBackPlayer` -> `server::sendSmartCalculatorReplyToPlayer(uid, "#SC" or "#CS", text)`; `game_server` splits the text into lines and sends them as whispers from `#SC`/`#CS`.

## 4. Command reference (whisper commands)

From the keys registered in `SmartCalculator::initCommand` (`Smart Calculator.cpp`). Which keys exist for which flavour is verified for the pairs read in the code; the full per-flavour matrix was not exhaustively checked (unverified for individual short aliases).

| Command | Meaning (from handler names) | Flavour |
|---|---|---|
| `ping`, `info`, `myinfo`, `list`, `calc`, `expr`, `macro`(1), `lastr`, `resolution`, `dfav`, `auto_fit`, `mycella` | Latency test, help, show own stored inputs, list options (clubs/shots/ps, or calculators/macros), compute, evaluate expression, run macro, last result, resolution, favourite deviation, auto-fit, "mycella" degree | both (registered for both enums) |
| `club`, `shot`, `ps`, `power`, `ring`, `mascot`, `card`, `card_ps` | Select club / shot / power shot / power stat and add aux-part (ring), mascot, card and power-shot-card power bonuses | Smart (`#SC`) |
| `d`, `h`, `w`, `a`, `g`, `s`, `c`, `b`, `make_slope`, `aim` | distance, height, wind, wind angle (degree), ground %, spin, curve, slope break, build slope break, aim degree | Smart; Stadium has its own handlers for the equivalents (`cmdStadiumCalculator{Shot,Distance,Height,Wind,Degree,Ground,SlopeBreak,GreenSlope}`) |
| `open` | open a named calculator, which returns its shot/club combo table | Stadium (`#CS`) |

(1) `macro` handler is `cmdSmartAndStadiumCalculatorMacro`; the literal key was not shown in the extracted list, unverified.

The standalone Discord bot (`Server Lib/Node/Smart-Calculator-Bot/index.js`) implements the Smart subset: `!ping !info !myinfo !close !list(club|shot|ps) !club !shot !ps !power !ring !mascot !card !card_ps !d !h !w !a !g !s !c !b !make_slope !calc`, plus admin-only `!destroy`. Per-user session state is purged by `manager_user.js` (`max_time_clear` 300000 ms = 5 min default; `index.js` constructs it with 900000 ms, so effectively 15 min despite the help text saying 5).

Domain enums (`Server Lib/Node/Smart-Calculator-Bot/user.js` / `smart_calculator.js`): clubs `_1W,_2W,_3W,_2I..._9I,PW,SW,PT1,PT2`; power shots `NO_POWER_SHOT, ONE_POWER_SHOT, TWO_POWER_SHOT, ITEM_15_POWER_SHOT`; shots `DUNK, TOMAHAWK, SPIKE, COBRA`. `smart_calculator.js` `calc()` returns `{desvio, percent, caliper, hwi, spin}` or `{error:true, message}`; deviation is reported in `pb`/`pba`/`pba+`/`pba40`/`pba100` units (constants `YARDS_TO_PB*`).

## 5. Protocol

### 5.1 C++ lib <-> Node (`Smart-Calculator`), raw TCP
- Endpoint: lib connects to `127.0.0.1:12345` (hardcoded in `Server Lib/Smart Calculator lib/SOCKET/pool.cpp`, `Pool::connect`). Node listens on `server.listen(12345, '127.0.0.1')` (`Server Lib/Node/Smart-Calculator/smart.js`) and closes any connection whose remote address is not `127.0.0.1`. So the Node service must run on the same host as the game/message servers.
- Framing (both directions, `Node/.../packet.js` and `Packet::makePacket`): `uint16 LE length` | `uint8 type` | payload (`length` bytes). The parsers (`dispatch.js`, `Pool::translate`) use the condition `length < bytes_buffered` (strictly less), so a packet is only consumed once at least one more byte than the frame is buffered. Strings are NUL-terminated; ints/floats little-endian. Unauthenticated sockets are closed by Node after 3 s idle (`socket.setTimeout(3000)`).
- Packet types (`ePACKET`, identical in `Smart Calculator lib/TYPE/common.hpp` and `Node/Smart-Calculator/types.js`):

| Id | Name | Direction | Payload / handler (`translates.js`) |
|---|---|---|---|
| 0 | LOGIN | lib -> Node | `AUTH_KEY\0` + `uint32 server_uid`; Node compares with `auth.js`, stores uid, replies HAND_SHAKE "OK" |
| 1 | HAND_SHAKE | both | text; lib only logs it |
| 2 | COMMAND_SERVER | lib -> Node | NUL string: chat line `"<nick> - <msg>"`; Node decodes Shift_JIS and posts it to the Discord history channel |
| 3 | REPLY_SERVER | Node -> lib | `uint32 uid` + string, e.g. `chat_discord ON/OFF` or `notice <text>` (Shift_JIS); lib calls `responseCallBackServer` which toggles `setChatDiscord` or calls `sendNoticeGMFromDiscordCmd` |
| 4 | REPLY_SMART | Node -> lib | `uint32 player_uid` + `uint8 request type` + result; routed to the player's Smart context `translatePacket` |
| 5 | REPLY_STADIUM | Node -> lib | same, routed to Stadium context |
| 6 | COMMAND_SMART_PING | lib -> Node | `uint32 id`, `uint32 unix_seconds`; reply "Pong! ... latency Nms" |
| 7 / 12 | LIST_MACRO (Smart/Stadium) | lib -> Node | `uint32 id`; Node POSTs `/macro {type:'listex'}` |
| 8 | COMMAND_SMART_CALC | lib -> Node | `id`, power(u8), auxpart/mascot/card/ps_card power (i8 x4), club/shot/power_shot index (u8 x3), 7 floats (distance, height, wind, degree, ground, spin, curve), slope flag u8 then 1 float or 3 floats. Reply: status u8 (0 ok: 5 floats desvio, percent, caliper, hwi, spin; 1 error string; 2 plain string) |
| 9 / 16 | MACRO | lib -> Node | `id`, 13 floats (distance, height, wind, degree, ground, slope_break, green_slope, power, desvio, percent, caliper, hwi, spin), macro text `name(args)`; Node asks `/macro type:'params'` then `type:'exec'` |
| 10 / 17 | EXPRESSION | lib -> Node | `id`, expression string; `/macro type:'expr'` |
| 11 | COMMAND_STADIUM_PING | lib -> Node | as ping |
| 13 | STADIUM_LIST_CALC | lib -> Node | `/calculadora type:'list'` |
| 14 | STADIUM_OPEN_CALC | lib -> Node | `id`, name; `/calculadora type:'open'`; reply: status, opened id (u32, constant 1), name, shot count (u16), per shot `{u32 id, name, u16 clubs, per club {u32 index, float power, name}}` |
| 15 | STADIUM_CALC | lib -> Node | `id`, name, shot u8, 8 floats (distancia, altura, vento, angulo, terreno, slope_bola, slope_green, power); `/calculadora type:'calcule'` |

Auth key: hardcoded `AUTH_KEY = "7436c482-9268-473f-bb8b-7aea263babc0"` in the lib (`SOCKET/pool.cpp`); the Node side `auth.js` ships with the placeholder `'Sua Auth Key, UUID'` (= "Your Auth Key, UUID"). They must be made equal, otherwise Node logs "Failed Key Auth" and closes the socket. Since the lib is open source, change both.

### 5.2 Connection management in the lib
`Pool::initialize` connects and spawns a reader thread (`translate`). `sendPacket` reconnects (and re-sends LOGIN) when the socket is down; on send error it closes the socket. `initSmartCalculatorLib` returns 1 internally when the connection fails but logs only. The library's destructor (`dllmain.cpp`, Linux `__attribute__((destructor))`) closes the pool.

### 5.3 Node `Smart-Calculator` -> Stadium-Calculator, HTTPS JSON
`postRequestCalculadoraStadium` in `Node/Smart-Calculator/util.js` uses the `https` module, `POST https://HOSTNAME:PORT<path>` with `Content-Type: application/json`, paths `/macro` and `/calculadora`, and a `token` field set to `TOKEN` from `config.js`. The reply is only processed on HTTP 200.

### 5.4 Stadium-Calculator REST API (`Node/Stadium-Calculator/index.js`)
- Plain `http` server on `process.env.PORT || 8000`, all interfaces (no host argument). CORS: `OPTIONS` answers `Access-Control-Allow-Origin: *`.
- Only `POST` with a JSON body is accepted; `token` must equal `TOKEN` from `common/config.js` (otherwise HTTP 404 "Not found"). Routes: `/macro` (`MacroMathJS/translateMacro.js`), `/calculadora` (`calculadora/translateCalculadora.js`, request types include `list`, `open`, `calcule` as used by the client), `/calculadora_manager` (`calculadora manager/translateCalculadoraManager.js`: create/edit/delete calculators and their shot/club blocks, used by a management front end; the exact request types were not enumerated here, unverified). Any other path returns `{"error":"Invalid request"}`.
- `/macro` request types used by Smart-Calculator: `listex`, `params`, `exec`, `expr` (more in `translateMacro.js`, e.g. create/update/delete, unverified).
- Error handling: errors return HTTP 200 with `{"error": ...}`; token failure returns 404.
- Macro engine: `MacroMathJS/api_macro.js` uses a vendored mathjs (`math.js`, `math(7.1.0).js`, no npm dependency). Macro inputs exposed to expressions: `distancia, altura, vento, angulo, slope_bola, slope_green, terreno, power` plus results `desvio, percent, caliper, hwi, spin` (from `translates.js` `responseMacro`). Macro call syntax is `name(arg1, arg2)` validated by `getMacroName`/`getMacroParameters` regexes in `Node/Smart-Calculator/util.js`.
- Caching: `common/CacheManager.js` / `CacheObject.js` keep calculators and macros in memory; on a miss they fetch via the PHP API (5.5). Files are named with an owner UID obfuscated by `cipherUID` (digits mapped to letters of the table `"FODASEMINU"` in `util/util.js`).
- A hostname allow-list (`HOSTNAME_ALLOW`, DNS re-resolve every 5 minutes) and an IP filter on `connection` exist in `index.js` but are commented out / inert; the only protection is the token.

### 5.5 Stadium-Calculator -> PHP storage API
`Node/Stadium-Calculator/util/util.js` does `POST http://HOSTNAME:80/stadium/<calculadora.php|macro.php>` form-urlencoded (constants `HOSTNAME = 'Seu HOSTNAME|IP'` placeholder, `PORT = 80`, `PATH = '/stadium'`), with `token` and `type` in `create|update|delete|load|list|copy` (`www/stadium/calculadora.php`, `macro.php`). Permissions: `0` public view and copy, `1` public view, `2` private (see `eTYPE_PERMISSION` in `common/CacheObject.js` and the list queries in the PHP files).

## 6. Configuration

There is no `.ini` for this family; settings are JS constants/files that ship with placeholders (Portuguese "Seu ..." = "Your ...").

| File | Key | Meaning / default |
|---|---|---|
| `Server Lib/Node/Smart-Calculator/config.js` | `TOKEN` (`'Seu Token, UUID'`) | Shared secret sent to Stadium-Calculator; must equal its `TOKEN` |
| same | `HOSTNAME` (`'hostname\|ip'`), `PORT` (443) | Where Smart-Calculator reaches Stadium-Calculator over HTTPS. Stadium-Calculator itself speaks plain HTTP on 8000, so a TLS-terminating reverse proxy is needed in between, or edit `util.js` (`require('https')`) (unverified deployment). |
| `Server Lib/Node/Smart-Calculator/auth.js` | `AuthKey` (`'Sua Auth Key, UUID'`) | Must equal `AUTH_KEY` in `Smart Calculator lib/SOCKET/pool.cpp` |
| `Server Lib/Node/Smart-Calculator/Discord/config.json` | `BOT_TOKEN`, `CHANNEL_CHAT_HISTORY_ID` | Discord bot token and channel id for chat history |
| `Server Lib/Node/Smart-Calculator/smart.js` | listen `127.0.0.1:12345`; `BOT.actived = false` | Port hardcoded. The Discord bot is **disabled in code** (comment: "disabled because another one is testing"); `BOT.start()` then logs "Bot is desactived" and returns false. Set `BOT.actived = true` to enable. |
| `Server Lib/Node/Stadium-Calculator/common/config.js` and `common/token.js` | `TOKEN` (`'Seu Token'`) | Two separate files both exporting `TOKEN`; keep both identical. `config.js` is used by `index.js`, `token.js` by cache/macro code. |
| `Server Lib/Node/Stadium-Calculator/util/util.js` | `HOSTNAME`, `PORT`=80, `PATH`='/stadium' | Location of the PHP API |
| env `PORT` | default 8000 | Stadium-Calculator listen port |
| `Server Lib/Node/Smart-Calculator-Bot/config.json` | `BOT_TOKEN` | Standalone bot token |
| `www/stadium/config/config.inc` | `IP_ADDR` localhost, `DB_NAME` stadium, `User`/`pwd` stadium/stadium, `PORT` 3307, `$GLOBALS['TOKEN']` placeholder | PHP DB connection and token (must equal the Node tokens). `www/stadium/config/.htaccess` denies web access. |
| DB `pangya.pangya_config.SmartCalculator` | smallint, default 0 in schema, 1 if row missing | Enables the feature per server (see 3.2) |

## 7. Node install and run

Dependencies (`package.json`):
| Service | Entry | Deps | Node |
|---|---|---|---|
| `Smart-Calculator` | `npm start` = `node smart.js` | `discord.js ^12.3.1`, `iconv-lite ^0.6.3` | static class fields are used (`static sessions`), needs Node >= 12 (unverified exact) |
| `Smart-Calculator-Bot` | `npm start` = `node index.js` | `discord.js ^12.5.3` | |
| `Stadium-Calculator` | `npm start` = `node index.js` | none (mathjs vendored) | |

Steps:
1. `cd "Server Lib/Node/Smart-Calculator" && npm install`; edit `config.js`, `auth.js`, `Discord/config.json`.
2. Stadium side: set tokens in `Node/Stadium-Calculator/common/config.js` and `common/token.js`, edit `util/util.js` HOSTNAME, `npm start` (port 8000).
3. PHP side: serve `www/stadium` with `apache-conf/sites-available/api-stadium.conf` (below), create MySQL DB `stadium` with tables `calculators`, `macros` and the procedures `create_calculator`, `update_calculator`, `delete_calculator`, `copy_calculator` (and macro equivalents). **These tables/procedures are not in the root `bk-squema-*.sql` dumps** (only `calculadora.php` references them) so you must create them yourself; the schema is unverified.
4. Build the library (section 8), copy `SMARTCALCULATORLIB.so` to the Game and Message server working directories, enable `SmartCalculator` in `pangya_config`.
5. Start `node smart.js` before the servers (see `run-as-to-rs.sh`).

Apache vhost `apache-conf/sites-available/api-stadium.conf`: `ServerName api.stadium.com` on `*:80`, DocumentRoot `${INSTALL_DIR}/www/stadium/`; denies every file except `calculadora.php` and `macro.php`; denies `config/`; logs `stadium_{error,access}.log`; all error pages return "Unknown error". The vhost root is `www/stadium`, whereas the Node client requests `/stadium/calculadora.php`; you need either a matching alias/rewrite or to set `PATH` to `''` (mismatch observed, not tested).

## 8. Build

- Windows: `Server Lib/Smart Calculator lib/Smart Calculator Lib.sln` (project `Smart Calculator Lib/Smart Calculator Lib.vcxproj`, `SMARTCALCULATORLIB_EXPORTS`). The hook code in `Server Lib/Projeto IOCP/Smart Calculator/` is compiled into each server project.
- Linux: `Server Lib/Linux Builds/Makefile-smart.mk` (`make -f Makefile-smart.mk`; `__build_type=release` for `-O3`, default debug `-Og -D_DEBUG -D SHOW_DEBUG_PACKET`). Flags `-std=c++20 -fPIC -fvisibility=hidden -fpermissive -fsigned-char -D SMARTCALCULATORLIB_EXPORTS`, link `-shared -ldl -pthread`. Output `output-smart-{debug|release}/SMARTCALCULATORLIB.so`, then copied to `./Game Server/` and `./Message Server/` (relative to the make directory). Sources: the lib dirs (`SOCKET`, `PLAYER`, `TYPE`, `UTIL`, `Smart Calculator Lib`) plus engine files from `Projeto IOCP` (`hex_util`, `exception`, `thread`, `message`, `message_pool`, `util_time`, `WinPort`). `-fvisibility=hidden` means only `*_API`-marked exports are visible. Paths with spaces are encoded as `|` in the CFILES list.

## 9. Source layout

- `Server Lib/Smart Calculator lib/`: `Smart Calculator Lib/Smart Calculator Lib.cpp` (exports `makePlayerContext`, `getPlayerContext`, `removeAllPlayerContext`, `initSmartCalculatorLib`, `sendCommandServer`), `SOCKET/pool.cpp` (`Pool`: connect, send, reader thread, packet switch), `SOCKET/socket.cpp`, `PLAYER/player_manager.cpp` (`PlayerManager`, keyed by `(uid, type)`), `TYPE/smart_calculator_player.cpp` (~1500 lines: Smart state and commands), `TYPE/stadium_calculator_player.cpp` (~1575 lines), `TYPE/common.hpp` (`ePACKET`, `Packet<>`), `UTIL/logger.cpp`.
- `Server Lib/Projeto IOCP/Smart Calculator/Smart Calculator.{hpp,cpp}`, shared types in `Server Lib/Projeto IOCP/TYPE/smart_calculator_type.hpp` and `smart_calculator_player_base.hpp`.
- Node trees as listed in section 2. Node-side physics: classes `Vector3D`, `Ball`, `ClubInfo`, `Club`, `Wind`, `QuadTree` (course/gravity sim) in `Node/Smart-Calculator/smart_calculator.js`; this file is duplicated in `Smart-Calculator-Bot/` and `Smart Calculator App/` (copies, they may drift).

## 10. Database usage

- C++ side: only the `pangya.pangya_config.SmartCalculator` flag through the generic rate-config command classes above. The lib and Node processes do not touch the game DB.
- PHP side (MySQL via `www/stadium/config/MysqlManager.inc`): tables `calculators`, `macros` (columns `index, uid, owner_uid, permission, name, raw`), procedures `create_calculator`, `update_calculator`, `delete_calculator`, `copy_calculator` (calculators); macro.php uses similarly named operations (not enumerated).

## 11. Known quirks and gotchas

- Loopback only and single host: Node accepts only `127.0.0.1`; lib hardcodes `127.0.0.1:12345`. Game Server and Message Server each open their own connection to the same Node process (identified by server UID from LOGIN).
- Hardcoded AUTH key in the lib vs placeholder in Node; hardcoded paths `/home/acrisio/projects/...` in `run-as-to-rs.sh`.
- Placeholders must be replaced everywhere: tokens in 4 places (`Smart-Calculator/config.js`, `Stadium-Calculator/common/config.js`, `common/token.js`, PHP `config.inc`), `HOSTNAME` in two places.
- Transport mismatch: Smart-Calculator talks HTTPS (port 443 default) but Stadium-Calculator is HTTP on 8000; Stadium-Calculator talks form-encoded HTTP to PHP, whereas Smart-Calculator sends JSON. The PHP vhost is HTTP :80 only.
- Frame parser uses `length < buffered` so the last exact-length frame can stay unprocessed until more data arrives (both sides).
- `Discord` bridge is off by default (`BOT.actived = false`). Discord commands (admin only, prefix `!`): `!chat_discord [uid] ON|OFF` and `!notice [uid,] text` send REPLY_SERVER to all connected servers or the one with matching UID; notice text is encoded as Shift_JIS.
- Chat text is exchanged in Shift_JIS (`iconv-lite`) for notices and chat history.
- `Smart Calculator.cpp` log strings are Portuguese, e.g. "nao conseguiu carregar a DLL" (could not load the DLL), "tentou executar comando, mas ele esta vazio" (tried to run a command but it is empty).
- Constructor of `SmartCalculator` sets `m_load = true` even if `initDllResource` threw (the exception is caught and logged), so `isLoad()` can report true without a library (unverified runtime impact).
- Smart-Calculator and the Stadium path require that the player has the premium ticket (see 3.4); GMs bypass.
