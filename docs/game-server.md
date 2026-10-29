# Game Server

Source: `Server Lib/Game Server`. Linux build: `Server Lib/Linux Builds/Makefile-gs.mk`, runtime folder `Server Lib/Linux Builds/Game Server/`.

## 1. Purpose

The Game Server is where the game itself happens. After the Login Server authenticates a player and hands over an auth key, the client connects here. The Game Server then handles everything until logout:

- channels, lobbies, rooms and the in-game golf simulation (shots, holes, scoring) for every game mode;
- inventory (warehouse), equipment, shops (normal shop, gift, Papel Shop, personal shop, Memorial, Tiki), mail, card/box/gacha systems;
- timed events (Grand Zodiac event, Golden Time, Bot GM event, login reward, attendance, daily quests);
- writing all progress to the database (pang, exp, user statistics, items, trophies, achievements).

It is by far the largest component. `GAME/channel.cpp` alone is ~17,000 lines, and `Game Server/game_server.cpp`, `PACKET/packet_func_sv.cpp`, `GAME/room.cpp` and `GAME/item_manager.cpp` are each ~6,000 lines.

## 2. How it fits in the system

```
Client --(Login Server: id/pw, gets auth key + GS list)--> Login Server
Client --TCP [SERVERINFO] PORT (default 20203)--> Game Server
Game Server --TCP client--> Auth Server ([AUTHSERVER], default 127.0.0.1:7777)   (cross-server commands)
Game Server --in-process GameGuard (GGSrvLib26-1, csauth2.cfg)          (default build)
Game Server --TCP client--> GG Auth Server ([GGAUTHSERVER], 127.0.0.1:7788) (only if MY_GG_SRV_LIB==0 on x64)
Game Server --DB ([NORMAL_DB])--> pangya database (shared with every other server)
Game Server --dlopen--> Smart Calculator lib (optional, see docs/smart-calculator.md)
```

- The server registers itself in the DB every second via `pangya.ProcRegServer_New` (`CmdRegisterServer`, `Server Lib/Projeto IOCP/Server/server.cpp` `registerServer()`, 1 s loop, includes `curr_user`). That is how the Login Server learns the Game Server list. There is no direct Login-to-Game socket.
- The Login-to-Game handoff runs entirely through the DB: the auth key goes into table `pangya.authkey_login`. See section 5.
- `server::server()` always creates a `unit_auth_server_connect` (`Server Lib/Projeto IOCP/Server/server.cpp`). The Auth Server relays shutdown, notice, ticker, disconnect, new mail, rate change and reload commands. See section 6.3 and `docs/auth-server.md`.
- Base class: `game_server : public server` (`Game Server/game_server.h`), singleton `sgs::gs`. The shared engine in `Server Lib/Projeto IOCP` provides IOCP/epoll, the thread pool, packet crypt/compress, the DB layer and `session_manager`.

Start order: database, then Auth Server (optional; the GS retries the connection), then Login Server and Game Server. The Smart Calculator is optional. `Server Lib/Linux Builds/run-as-to-rs.sh` starts smart, auth, login, msn and rank, and starts the Game Server only with the `all` argument.

## 3. Startup and runtime flow

`main()` (`Game Server/Game Server.cpp`):
1. If `argc > 2`, it does `chdir(argv[2])`. Note that this is the **second** argument; `argv[1]` is ignored. It also sets the log prefix `"GS"`.
2. Linux: unlimited core dump size (`RLIMIT_CORE`) and process priority raised by 2. Windows: SEH-to-minidump translator.
3. `sgs::gs::getInstance().start()`.

`game_server::game_server()` (`Game Server/game_server.cpp` ~line 126):
1. Base `server(m_player_manager, 2 accept threads, 26 DB instances, 8 job threads)`. `server::config_init()` reads the common `[SERVERINFO]` keys and `[OPTION] ANTIBOTTTL` (`Projeto IOCP/Server/server.cpp` ~line 2197). `server.ini` is resolved as `<cwd>/server.ini` (`ReaderIni::init`, `_INI_PATH`).
2. Starts the `_check_player` monitor thread (every 10 s: GameGuard query/timeout check and item-expiry check, `game_server::check_player`).
3. `config_init()` reads `ICONINDEX`, `FLAG` and `GAMEGUARDAUTH`, then loads **rates from the DB** (`pangya.ProcGetRateConfigInfo` with the server GUID). If no row exists, it uses defaults (all rates 100, all events on, angel event off) and writes them back with `pangya.ProcUpdateRateConfigInfo`.
4. `init_systems()` loads all global singletons: IFF, Card, Comet Refill, Papel Shop, Box, Memorial, Cube Coin, Treasure Hunter, Drop, Attendance Reward, Map, Approach Mission, Grand Zodiac Event, Coin Cube Location, Golden Time, Login Reward and Bot GM Event. It also pre-instantiates the RoomGrandPrix, RoomGrandZodiacEvent and RoomBotGMEvent statics.
5. If `GAMEGUARDAUTH` is on, it creates `GGAuth(max_user)` (default) or `unit_gg_auth_server_connect`. See section 9.
6. Synchronous `CmdDailyQuestInfo`. A failure here throws and the server goes to `FAILURE`.
7. Builds `NUM_CHANNEL` `channel` objects from `[CHANNELn]`.
8. Registers the client packet handlers (`packet_func::funcs`), the server-to-client "do nothing" acks (`funcs_sv`), and the Auth Server command `0x1` (`funcs_as`, `packet_as001`, the GG key-login relay).

Runtime:
- `onAcceptCompleted` sends packet `0x3F` (plain) = `{1, 1, session key}` and initialises the per-session GameGuard context.
- `onHeartBeat` (monitor thread) does the following:
  - channel invite timeouts;
  - re-loads any global system that is not loaded;
  - Grand Zodiac / Bot GM event room creation, Golden Time rounds, login-reward update and daily quest rollover;
  - Papel Shop day rollover, Treasure Hunter point broadcast (`0x131`);
  - GM/cube-rare notice queue (`0x42` / `0x1D3`) and ticker queue (`0xC9`). The notice list uses a 60 s interval and the ticker list a 30 s interval (constructor).
- `onDisconnected`: `channel::leaveChannel`, `pangya.ProcRegisterLogon(uid, 1)` (logout), and removes the Smart Calculator context.
- `checkPacket`: per-session flood limit. The base is `CHK_PCKT_COUNT_LIMIT` = 5 (`Projeto IOCP/SOCKET/session.h`), plus a per-packet allowance: +150 for 0x14 and 0x63, +100 for 0xB9 and 0xEB, +50 for 0x18 and 0x20, +30 for 0x16 and 0x48, +20 for 0x0A, 0xBA, 0x12E, 0x143, 0x146 and 0x147, +10 for 0x03, 0x1C, 0x2D and 0x2F, +5 for 0x12D, and +2 for all others. If a session goes over the limit it is logged as a "DDoS" attempt and disconnected.

### Console commands (`game_server::checkCommand`)
| Command | Effect |
|---|---|
| `exit` | shut down |
| `reload_files` | re-read server.ini (`server::config_init` + `config_init`), reload all systems, broadcast `0xF9` server info |
| `reload_socket_config` | re-read `config/socket_config.ini` |
| `reload_system <all\|iff\|card\|comet_refill\|papel_shop\|box\|memorial_shop\|cube_coin\|treasure_hunter\|drop\|attendance_reward\|map_course\|approach_mission\|grand_zodiac_event\|coin_cube_location\|golden_time\|login_reward\|bot_gm_event\|smart_calc>` | reload a single global system |
| `rate <pang\|exp\|club\|chuva\|treasure\|scratchy\|pprareitem\|ppcookieitem\|memorial> <n>` | change a rate (100 = x1), persisted to the DB (`chuva` = rain) |
| `event <grand_zodiac_event\|angel_event\|grand_prix\|golden_time\|login_reward\|bot_gm_event\|smart_calc> <0\|1>` | toggle an event |
| also: `snapshot`, `broadcast_tst`, `msg_gz`, `scratch`, `smart_calc`, `upt_coin_cube_location`, `golden_time`, `login_reward`, `bot_gm_event`, `chat_discord ...` | misc/test (see source, ~line 4490-4860) |

## 4. Configuration

### 4.1 `Game Server/server.ini`

Linux copy: `Linux Builds/Game Server/server.ini`. It is identical except `IP = 192.168.15.20`.

**[SERVERINFO]**
| Key | Read by | Meaning / values | Shipped value |
|---|---|---|---|
| `VERSION` | `server::config_init` | free text, server version string | `IOCP Version Beta 1.1` |
| `CLIENTVERSION` | `server::config_init`, `LoginManager::loadIni` | Expected client version (`REGION.SEASON.HIGH.LOW`, e.g. `SS.R7.995.00`). A mismatch does **not** reject the login. If `high` differs or `low` is older, the player gets `block_flag.all_game = 1` and cannot play any game (`requestLogin` ~line 1332). | `SS.R7.995.00` |
| `NAME` | base | Server name shown in the server list | `Tittan Boo` |
| `GUID` | base | Server UID. It is the key for the DB rate config, `ProcRegServer_New` and server switching. It must be unique among servers. | `20203` |
| `IP` | base | IP published in the server list (what clients connect to) | `127.0.0.1` |
| `PORT` | base | Listen port | `20203` |
| `MAXUSER` | base | max sessions (also GGAuth capacity) | `2001` |
| `ICONINDEX` | `config_init` | Server list icon (`img_no`) | `2` |
| `PROPERTY` | base | bitfield, table 4.2 | `2048` |
| `FLAG` | `config_init` (64-bit, `readBigInt`) | Feature-block bitfield, table 4.3. OR-ed into every player's `block_flag` at login. | `0` |
| `EXPRATE`, `PANGRATE`, `CLUBMASTERYRATE` | **nobody** | Present in the ini ("100 = x1, 200 = x2") but **never read**. Rates come from the DB (`ProcGetRateConfigInfo`), see 4.5. | `100` |
| `GAMEGUARDAUTH` | `config_init` | `>=1` enables GameGuard CS-auth | `1` |

The comment block at the top of `[SERVERINFO]` documents the account **capability** bits (DB column). It is reproduced in 4.4.

**[OPTION]**
| Key | Meaning | Shipped |
|---|---|---|
| `SAME_ID_LOGIN` | "Can log in with the same ID, this option is only for tests". When `1`, `requestLogin` **skips** the packet-version check, the auth-key "already used" and "match" checks, and the kick of an already-logged-in session with the same UID. `authCmdDisconnectPlayer` also refuses to disconnect. Read by `LoginManager::loadIni` (`GAME/login_manager.cpp` ~line 1274). | `1` (**insecure, see section 11**) |
| `TTL` | ms without any packet before the session is dropped. `0` = disabled (`Projeto IOCP/SOCKET/session_manager.cpp`). | `60000` |
| `ANTIBOTTTL` | ms expected between client keep-alive packets `0xF4`. In release builds a late packet only logs `[Warning][Bot Or Lag]` (`packet0F4`). Default if missing: 1000 (base `server.cpp`). | `10000` |

**[LOG]** `DIR = Log`: log directory (`Projeto IOCP/TYPE/list_fifo_console.h`).

**[CHANNELINFO]** `NUM_CHANNEL`: number of `[CHANNELn]` sections to read (1-based).

**[CHANNELn]** (read in the constructor ~line 261)
| Key | Meaning |
|---|---|
| `NAME` | channel name |
| `MAXUSER` | channel capacity |
| `MAXLEVEL` | max player level (non-GM) |
| `LOWLEVEL` | min level, optional (commented out in the shipped file, default 0) |
| `FLAG` | optional bitfield (`ChannelInfo::uFlag`, `TYPE/pangya_game_st.h` ~line 1934, enforced in `channel::checkEnterChannel`): `512` junior_bellow = Junior A and below; `1024` junior_above = Junior E and above; `2048` only_rookie; `4096` Beginner E to Junior A; `8192` Junior E to Senior A. `512` or `2048` also mark rooms as `channel_rookie` (reduced wind, `GAME/channel.cpp` ~line 982, `course.cpp`). GMs bypass all channel checks. |

Shipped: 4 channels. "Canal (Iniciantes)" (Beginners) has MAXLEVEL 16 and FLAG 512. "Canal (Livre 1..3)" (Free 1..3) have MAXLEVEL 70 and FLAG 0. All have MAXUSER 500.

**[NORMAL_DB]**: shared format (DBENGINE/DBIP/DBNAME/DBUSER/DBPASS/DBPORT). Shipped: mysql, localhost, `pangya`/`pangya`/`pangya`, 3306. The ini comment says the port is not used with ODBC on SQL Server Express.

**[AUTHSERVER]** `IP`, `PORT` (`Projeto IOCP/UNIT/unit_auth_server_connect.cpp` ~line 745). Shipped: 127.0.0.1:7777.

**[GGAUTHSERVER]** `IP`, `PORT` (`UNIT/unit_gg_auth_server_connect.cpp` ~line 445). Shipped: 127.0.0.1:7788. These keys are only used when the GG Auth Server mode is compiled in (section 9); the default build never reads them.

### 4.2 PROPERTY bits (`uProperty`, `Projeto IOCP/TYPE/pangya_st.h` ~line 147)
| Value | Struct field | ini comment / effect |
|---|---|---|
| 16 | `mantle` | "Mantle": hidden server. `requestLogin` rejects players without the `mantle` or `game_master` capability. |
| 64 | `only_rookie` | Only rookies: `requestLogin` rejects level >= 6 (Beginner E+) |
| 128 | `natural` | "Natural" mode |
| 512 | `verde` | green colour ("Club[Guild]" in the ini) |
| 1024 | `azul` | blue colour |
| 2048 | `grand_prix` | ini comment: "natural (new season Grand Prix)". `requestChangeServer` refuses to move a player who is in the Grand Prix lobby (lobby id 176) to a server without this bit. |

### 4.3 FLAG bits (server feature blocks, `uFlag`, `pangya_st.h` ~line 105)
The struct names are authoritative. Where the ini comment differs, it is noted.

| Value | Field | Blocks |
|---|---|---|
| 1 | - | unknown |
| 2 | `all_game` | all games / room creation |
| 4 | `buy_and_gift_shop` | shop buying (ini: "Shop ALL OFF") |
| 8 | `gift_shop` | gift |
| 16 | `papel_shop` | Papel Shop |
| 32 | `personal_shop` | personal shop (ini says "Shop Lounge OFF") |
| 64 / 128 / 256 | `stroke` / `match` / `tourney` | modes |
| 512 | `team_tourney` | team tourney (ini: "Short Game") |
| 1024 / 2048 / 4096 | `guild_battle` / `pang_battle` / `approach` | modes |
| 8192 | `lounge` | lounge |
| 16384 | `scratchy` | Scratchy |
| 65536 | `rank_server` | rank server |
| 131072 | `ticker` | ticker |
| 262144 | `mail_box` | mail box |
| 524288 / 1048576 / 2097152 | `grand_zodiac` / `single_play` / `grand_prix` | marked "I think" in source |
| 16777216 | `guild` | guild |
| 33554432 | `ssc` | Special Shuffle Course |
| 268435456 | `memorial_shop` | Memorial shop |
| 536870912 | `short_game` | short game |
| 1073741824 | `char_mastery` | character mastery |
| 4294967296 | `lolo_copound_card` | Lolo card compose |
| 8589934592 | `cadie_recycle` | caddie recycle (cauldron) |
| 17179869184 | `legacy_tiki_shop` | Legacy Tiki Shop (ini: "Unknown") |

The ini comment suggests `0x28` (8+32) to disable gift and personal shop.

### 4.4 Account capability bits (`uCapability`, `TYPE/pangya_game_st.h` ~line 479; DB `m_cap`)
| Value | Field | Notes |
|---|---|---|
| 1 | `A_I_MODE` | AI mode |
| 4 | `game_master` | GM. Bypasses channel limits and gets GM visibility/whisper flags. |
| 8 | `gm_edit_site` | GM with web-admin access |
| 16 | `block_give_item_gm` | GM cannot use giveitem/goldenbell (the ini comment calls 16 "MC") |
| 128 | `gm_normal` | GM currently playing as a normal player (can switch back with `identity`) |
| 1024 | `mantle` | may enter mantle servers |
| 16384 | `premium_user` | premium |
| 32768 | `title_gm` | shows "GM" title. The ini says it also needs 4 for full GM. |

The ini comment also lists 256 "Block present", 512 "Login to test server" and 14 "Observer". These are **not** in the struct (unverified).

### 4.5 Rates and events (DB, not ini)
`RateConfigInfo` (`pangya_st.h` ~line 204) holds: `scratchy`, `papel_shop_rare_item`, `papel_shop_cookie_item`, `treasure`, `pang`, `exp`, `club_mastery`, `chuva` (rain), `memorial_shop` (100 = x1), and the flags `grand_zodiac_event_time`, `angel_event` (each finished game reduces the quit counter by 1), `grand_prix_event` (a 16-bit mask of GP event types), `golden_time_event`, `login_reward_event`, `bot_gm_event` and `smart_calculator`.

Sources: `pangya.ProcGetRateConfigInfo(server_uid)`, updated by `ProcUpdateRateConfigInfo`. They can be changed at runtime by the console (`rate`/`event`) or by an Auth Server command `0x9` (new rate), which goes to `updateRateAndEvent(tipo, qntd)` with the type ids 0=pang, 1=exp, 2=mastery, 3=rain, 4=treasure, 5=scratchy, 6=PP rare, 7=PP cookie, 8=memorial, 9=GZ event, 10=angel, 11=GP, 12=golden time, 13=login reward, 14=bot GM and 15=smart calc. Changes are broadcast to clients with `0xF9`. When a game starts, the room copies `exp`, `pang`, `club_mastery`, `chuva`, `treasure` and `angel_event` into the game (`GAME/room.cpp` ~line 3696).

### 4.6 Other runtime files (working directory)
| File | Use |
|---|---|
| `config/socket_config.ini` | IP rules / DDoS protection (shared format). Shipped: `enable_ip_rules: 1`, `limit_connection_per_ip: 10`, `order: deny,allow`, `allow: 127.0.0.1`, `ddos_interval: 3000`, `ddos_count: 5`, `ddos_autoreset: 600000`. **Only localhost is allowed by default.** |
| `data/pangya_jp.iff` | The only IFF file the code opens (`PATH_PANGYA_IFF "data/pangya_jp.iff"`, `Projeto IOCP/UTIL/iff.h`). It is a zip of `*.iff` tables (Item, Part, Ball, Caddie, Card, Course, CounterItem, Achievement, QuestItem, ...). Each table's header version must equal `IFF_VERSION 0x0D`, otherwise load throws. |
| `data/pangya_jp_962_02.iff`, `data/pangya_jp_994.iff` | Present in the data folder but not referenced by code. They are presumably alternative client-version packs (rename to use; unverified). |
| `data/CounterItem.txt` | A one-line binary/Shift-JIS file (counter-item names). Not referenced by any source file (grep), so likely a leftover dump (unverified). Counter items are actually loaded from `CounterItem.iff` inside the IFF zip. |
| `csauth2.cfg` | GameGuard protocol version list, read by `InitGameguardAuth` (`GGSrvLib26-1.cpp` ~line 1380, `fscanf "%d"` up to 100 ints). Windows copy contains `70`, Linux copy `701`. A missing file makes GG init fail. |

## 5. Player lifecycle

### 5.1 Login handshake (Login Server to Game Server)
1. Login Server, after a successful login, runs `CmdAuthKeyLogin` (`pangya.ProcGeraAuthKeyLogin`, `Login Server/PACKET/packet_func_ls.cpp` ~line 696). It then sends the key and the Game Server list (taken from the DB) to the client. See `docs/login-server.md`.
2. The client connects to the GS. The GS sends `0x3F {1,1,key}` and all later traffic is encrypted with that key.
3. The client sends **`0x02`**, handled by `game_server::requestLogin` (~line 1110). The packet carries the id, uid, ntKey, auth key, client version, encrypted packet version, MAC address and a second auth key. The checks run in this order:
   - IP/MAC ban list (`pangya.pangya_ip_table` / `pangya_mac_table`);
   - `CmdPlayerInfo` (`ProcGetPlayerInfoGame`). The uid must exist and the id must match;
   - account block state: temporary or forever, block-all-IP (`ProcInsertBlockIP`), block-MAC (`ProcInsertBlockMAC`);
   - packet version must equal `PACKET_VERSION_SERVER 2017082900` (XOR-decoded);
   - `CmdAuthKeyLoginInfo` (`ProcGetAuthKeyLogin`): the key must be `valid` and match case-insensitively;
   - client version, which only sets `all_game` (see 4.1);
   - `CmdMemberInfo` (`ProcGetUserInfo`); GM flag setup;
   - server PROPERTY checks (rookie, mantle);
   - kick any older session with the same UID.

   The last three groups of checks (packet version, auth key, duplicate session) are skipped when `SAME_ID_LOGIN=1`.
4. On success:
   - the server `FLAG` is OR-ed into the player block flags and the session is marked authorised;
   - `ProcRegisterLogon(uid, 0)` and `ProcRegisterLogonServer(uid, GUID)` run;
   - the Papel Shop counters are initialised;
   - `LoginManager::createTask` runs, with a limit of 2000 concurrent tasks.

   The login task asynchronously loads the full player cache from many DB commands: characters, caddies, mascots, warehouse, cards, equip, trophies, achievements, quests, mail, friends, guild, my-room, Dolfini locker, item buffs, Grand Prix clears, Legacy Tiki points, and so on (`GAME/login_manager.cpp`, list of `new Cmd...`). Meanwhile `0x44` (option `0xD3`) is sent immediately.
5. `LoginTask::sendCompleteData` (`GAME/login_task.cpp` ~line 97) validates equipped items and then sends the initial data burst: `0x44` (player info), `0x70`/`0x71`/`0x73` (characters/caddies/warehouse), `0xE1` (mascots), `0x72` (equip), `0x131`, `0x102`, `0xF1`, `0x144`, `0x135`/`0x138`/`0x136`/`0x137` (cards), `0x13F`, `0x181` (buffs), `0x096`, `0x169`/`0x0B4`/`0x158` (trophies/stats), `0x25D` (GP trophies), and the login reward.
6. Any failure sends `0x44 {0xE2, 500020}` and shuts down the receive side of the socket.

Server switch (`0x119`, `requestChangeServer`): the target must be in the cached server list (`ProcGetServerList`). The GS creates a game auth key (`pangya.ProcGeraAuthKeyGame`, `CmdAuthKeyGame`), marks the login key with `UPDATE pangya.authkey_login SET valid = 1` (`CmdUpdateAuthKeyLogin`), and returns `0x1D4` with the key.

### 5.2 Channel, lobby, room
| Step | Client packet | Handler |
|---|---|---|
| server/channel list, rank server | `0x43`, `0x47` | `sendServerListAndChannelListToSession`, `sendRankServer` |
| enter channel | `0x04` | `requestEnterChannel` then `channel::checkEnterChannel` (level/flag/full) |
| enter/exit lobby | `0x81` / `0x82`, `0x83` (other channel + lobby) | `channel::requestEnterLobby` ... |
| Grand Prix lobby / room | `0x176`/`0x177`, `0x179`/`0x17A` | `RoomGrandPrix` |
| make / enter / spy room | `0x08` / `0x09` / `0x3E` | `channel::requestMakeRoom`, `requestEnterRoom`, `requestEnterSpyRoom` |
| room settings, ready, team, kick, AFK | `0x0A`, `0x0D`, `0x10`, `0x26`, `0x32` | `room::...` |
| invite | `0xBA`, `0x29` | invite with timeout checked in `onHeartBeat` |
| start game | `0x0E` | `room` creates the game object (5.3) and broadcasts `0x230`/`0x231` |
| exit room | `0x0F` | `requestExitRoom` |

### 5.3 Playing a game
`room` chooses the game class from `RoomInfo::TIPO` (`TYPE/pangya_game_st.h` ~line 2360; `GAME/room.cpp` ~line 3711):

| TIPO (value) | Class | Base |
|---|---|---|
| STROKE (0) | `Versus` | `VersusBase : Game` |
| MATCH (1) | `Match` (teams) | `VersusBase` |
| PANG_BATTLE (7) | `PangBattle` (source comment: "not done yet") | `VersusBase` |
| TOURNEY (4) | `Tourney` | `TourneyBase : Game` |
| GUILD_BATTLE (6) | `GuildBattle` (uses `GuildRoomManager`, `Dupla` pairs) | `TourneyBase` |
| APPROCH (10) | `Approach` | `TourneyBase` |
| PRACTICE (19) / default | `Practice` | `TourneyBase` |
| SPECIAL_SHUFFLE_COURSE (18) | `SpecialShuffleCourse` | `TourneyBase` |
| GRAND_ZODIAC_PRACTICE (14) | `ChipInPractice` | `GrandZodiacBase : TourneyBase` |
| GRAND_ZODIAC_INT (11) / ADV (13) | `GrandZodiac` | `GrandZodiacBase` |
| GRAND_PRIX (20) | `GrandPrix` (via `RoomGrandPrix`) | `TourneyBase` |
| LOUNGE (2) | no game; lounge room state (`0xEB`, `0x63`) | - |

In-game packet loop (all routed via `channel` to `room` to `Game`):
- load: `0x48` load %, `0x34` char intro;
- hole: `0x1A` init hole, `0x11` finish load hole, `0x31` hole data;
- shot: `0x12` init shot, `0x1B` sync shot (the server re-requests it with `0x8A` from lagging players), `0x1C` finish shot, `0x12F` shot end data;
- aim, power and club: `0x13`/`0x14`/`0x15`/`0x16`;
- items: `0x17` and `0x65`;
- wearable rings, wings, paws, gloves and earcuffs: `0x15C`, `0x15D`, `0x138`, `0x180`, `0x171`, `0x196`..`0x199`;
- assist: `0x184`/`0x185`;
- pause: `0x30`; team hole: `0x35`; versus continue: `0x36`/`0x37`; leave practice: `0x130`/`0x131`;
- Grand Zodiac: `0x12D`/`0x137`;
- finish: `0x06` (also `0xCB` and `0x12C`).

### 5.4 Saving results
`Game::requestSaveInfo` (`GAME/game.cpp` ~line 1270) builds the per-player `UserInfo` delta. Options: 0 = finished, 1 = quit/DC, 2 = aborted before hole 1, 4 = SSC, 5 = no-quit leave. With angel event on, `quitado = -1`. Then:
- `PlayerInfo::addUserInfo` sends `pangya.ProcUpdateUserInfo` (`CmdUpdateUserInfo`);
- `addPang` / `consomePang` send `UPDATE pangya.user_info SET pang = pang ±n` (`CmdUpdatePang`);
- `player::addExp` sends `UPDATE pangya.user_info SET level=, Xp=` (`CmdUpdateLevelAndExp`), plus level-up prize items;
- also: `ProcUpdateMapStatistics` (course records), item consumption (`CmdUpdateItemQntd`...), drops, trophies (`ProcUpdateTrofelNormal`, GP clear), achievements and quests (`ProcUpdateQuestUser`, `ProcCheckAchievement`), last-5-games (`ProcUpdateLast5PlayerGame`), clubset workshop, caddie/mascot exp.

All writes go through the async `NormalManagerDB` queues. Only login-critical reads use waiter commands.

Logout or disconnect: `channel::leaveChannel` (leaves the room or game, which saves as quit if the player was mid-game) and then `ProcRegisterLogon(uid, 1)`.

## 6. Network protocol and ports

### 6.1 Listen port
`[SERVERINFO] PORT` (default 20203), PangYa client protocol (crypt/compress from `Projeto IOCP`). The first packet `0x3F` is sent raw.

### 6.2 Client packets (registered in `game_server.cpp` ~lines 296-493; handlers in `PACKET/packet_func_sv.cpp`)
| Group | Packet IDs (handler) |
|---|---|
| Login / server | 02 login, 04 channel, 43/47 server+rank list, 5C date-time, 83 other channel+lobby, 119 change server, F4 bot keep-alive, 88 GameGuard answer, 33 client exception msg |
| Chat / social | 03 chat, 2A whisper, 55 whisper state, DE, 54 team chat, 4F chat block, 69 chat macro, 3A report chat, 2F player info, B5, 9C last 5 games |
| GM | 8F common GM cmd (`CCG_VISIBLE=3, WHISPER, CHANNEL, OPEN/CLOSE_WHISPER_LIST=8/9, KICK, DISCONNECT, DESTROY=13, CHANGE_WIND, CHANGE_WEATHER, IDENTITY, GIVE_ITEM=18, GOLDENBELL`), 57 GM notice, 41 identity, 60 destroy room, 66/67 ticker |
| Lobby / room | 81, 82, 08, 09, 0A, 0D, 0E, 0F, 10, 26, 29, 2D, 32, 3E, 63, B4, B7, BA, 176, 177, 179, 17A, EB, 184 |
| In-game | 06, 11-19, 1A-1C, 22, 30, 31, 34-37, 42, 48, 4A, 64, 65, 9D, 12D, 12E, 12F, 130, 131, 137, 138, 141, 15C, 15D, 171, 180, 181, 185, 196-199, CB, 12C |
| Shop / items | 1D buy, 1F gift, 0B/0C/20 equip (channel/room/my room), E6/E7 rental, 4B clubset stats, 164-16D clubset workshop, 187-18C character mastery/cards, 0D8 item buff, 0E5 cutin, 156 auto command, 0EC comet refill, 158 caddie cauldron, 39/6B caddie holiday, 73 mascot msg, 3D cookie, 9E gacha coupon |
| Box / card / gacha | 0B2 open box, 0EF box from mail, 0CA card pack, 155 Lolo card compose, 0BD special card, 098/14B Papel Shop, 186 Big Papel Shop, 17F Memorial, 126-129 Legacy Tiki, 18D Tiki exchange |
| Personal shop | 74-7D (open, edit, view, buy, ...) |
| Mail | 143 open, 144 info, 145 send, 146 take item, 147 delete |
| Dolfini locker | 0CC-0D5 |
| Quests / rewards | 151-154 daily quest, 16E/16F attendance, 0AE tutorial, 0AA/0AB ticket report |
| Misc | 07 (nick check), 0B9 UCC/self-design, 0C9 UCC web key, 0A1/0A2 web link/guild, 0C1 place update, 0D3, 0FB, 0FE, 140, 157, 172 (Workshop event 2013 log) |

`funcs_sv` registers about 200 server-to-client packet IDs as "do nothing" send callbacks (`packet_svFazNada`). Exceptions are `0x4D`, `0x55`/`0x8A` (sync shot re-request) and `0x156`-`0x15E`/`0x257` (`packet_svRequestInfo`).

### 6.3 Auth Server link
Client connection to `[AUTHSERVER]`. Commands received (`Projeto IOCP/UNIT/unit_auth_server_connect.cpp` ~line 63) and the GS action for each:

| ID | Command | GS action |
|---|---|---|
| 0x0 | first key | handshake |
| 0x1 | ask login | handshake |
| 0x2 | shutdown | `authCmdShutdown(sec)` |
| 0x3 | broadcast notice | queued notice |
| 0x4 | broadcast ticker | ticker queue |
| 0x5 | cube rare-win broadcast | notice queue |
| 0x6 | disconnect player | `authCmdDisconnectPlayer(req_server, uid, force)`; respects `SAME_ID_LOGIN` unless forced |
| 0x7 | confirm disconnect | - |
| 0x8 | new mail arrived | notify the player |
| 0x9 | new rate | `updateRateAndEvent` |
| 0xA | reload system | `reloadGlobalSystem` with the same ids 0-18 as the console list |
| 0xB / 0xC | player-online info | request / confirm |
| 0xD / 0xE | relayed command / reply between servers | - |

The GS-specific command `0x1` in `funcs_as` (`packet_as001`) forwards the GG Auth Server "key login" (MD5 of `GUID ^ key`) and is only active in GG Auth Server mode. Details: `docs/auth-server.md`.

## 7. Database usage

The GS uses the `pangya` schema (dumps `bk-squema-*.sql` in the repo root). It has about 200 command classes in `Game Server/PANGYA_DB/cmd_*` plus shared ones from `Projeto IOCP/PANGYA_DB`.

| Area | Procedures / tables (examples, from `m_szConsulta`) |
|---|---|
| Server / auth | `ProcRegServer_New`, `ProcGetServerList`, `ProcGetRateConfigInfo`, `ProcUpdateRateConfigInfo`, `ProcGetAuthKeyLogin`, `ProcGeraAuthKeyGame`, `authkey_login` (UPDATE), `ProcRegisterLogon`, `ProcRegisterLogonServer`, `pangya_ip_table`, `pangya_mac_table`, `ProcInsertBlockIP`, `ProcInsertBlockMAC` |
| Player load | `ProcGetPlayerInfoGame`, `ProcGetUserInfo`, `ProcGetCaddieInfo`, `ProcGetMascotInfo`, `ProcGetWarehouseItem`, `ProcGetCardInfo`, `ProcGetCardEquip`, `USP_CHAR_EQUIP_LOAD_S4`, `ProcGetTrofel*`, `ProcGetAchievement_New`, `ProcGetDailyQuest_New`, `ProcGetFriendInfo`, `ProcGetGuildInfo`, `ProcGetMyRoom_One`, `ProcGetDolfiniLockerInfo`, `ProcGetItemBuff`, `ProcGetGrandPrixClear`, `ProcGetLegacyTikiShopInfo`, `ProcGetMsgOff`, `ProcGetPremiumTicket`, `ProcGetLastPlayerGame`, `ProcGetMapStatistics` |
| Player save | `ProcUpdateUserInfo`, `user_info` (pang, level/Xp), `ProcUpdateMapStatistics`, `ProcUpdateTrofelNormal`, `ProcUpdateQuestUser`, `ProcCheckAchievement`, `ProcUpdateLast5PlayerGame`, `ProcUpdateClubSetWorkshop`, `ProcUpdateCaddieInfo`, `ProcUpdateMascotInfo`, `USP_FLUSH_*`, `USP_CHAR_EQUIP_SAVE_S4`, `ProcUpdatePlayerLocation` |
| Items | `ProcAddItem`, `ProcAddPart`, `ProcAddBall`, `ProcAddCaddie`, `ProcInsertMascot`, `ProcInsertCard`, `ProcInsertClubSet`, `ProcInsertSkin`, `ProcAddFurniture`, `ProcTransferPart`, `ProcExtendRental`, `USP_ADD_ITEM`, `pangya_item_warehouse` |
| Mail | `ProcGetEmailFromMailBox`, `ProcGetAllEmailFromMailBox`, `ProcGetInformationEmail[2]`, `ProcInsertItemNoEmail`, `ProcColocaMsgNoGiftTable`, `pangya_item_mail`, `pangya_gift_table` |
| Global systems | `pangya_new_cards`/`pangya_new_card_pack`, `ProcGetBoxInfo`, `pangya_papel_shop_config/_item/_coupon`, `pangya_new_memorial_level`, `ProcGetMemorialNormalItemInfo`, `pangya_coin_cube_info/_location`, `pangya_course_reward_treasure`, `pangya_treasure_item`, `ProcGetDropCourseConfig`, `pangya_new_course_drop_item`, `pangya_attendance_table_item_reward`, `ProcGetApproachMissions`, `pangya_grand_zodiac_times`, `pangya_golden_time_info/_item/_round`, `pangya_login_reward[_player]`, `pangya_bot_gm_event_time/_reward`, `ProcGetCometRefillInfo`, `pangya_daily_quest` |
| Logs | `ProcInsertCPLog[Item]`, `ProcInsertBoxRareWinLog`, `ProcInsertPapelShopRareWinLog`, `ProcInsertMemorialRareWinLog`, `ProcInsertSpinningCubeSuperRareWinBroadCast`, `ProcInsertPersonalShopLog`, `ProcRegisterTicker`, `ProcInsertCommand` |

The full list: `grep -rho 'pangya\.[A-Za-z0-9_]*' "Server Lib/Game Server/PANGYA_DB" | sort -u`.

## 8. Source layout

| Dir | Contents |
|---|---|
| `Game Server/` | `Game Server.cpp` (main), `game_server.{h,cpp}` (server class, login, chat, GM, Auth/GG callbacks, heartbeat, config), `server.ini`, `config/`, `data/`, `csauth2.cfg`, `Game Server.vcxproj` |
| `GAME/` | channel/lobby/room/game logic and the global systems (tables below) |
| `PACKET/` | `packet_func_sv.{h,cpp}`: `packetXXX` request parsers and `pacoteXXX` response builders, `session_send`/`channel_broadcast`/`room_broadcast` |
| `PANGYA_DB/` | `cmd_*` DB command classes |
| `SESSION/` | `player` (session + `PlayerInfo m_pi` + GameGuard ctx), `player_manager` (lookup, item-expiry and GG checks, OID block) |
| `TYPE/` | structs: `pangya_game_st.h` (RoomInfo, ChannelInfo, capability...), `player_info.*`, per-system `*_type.hpp`, `game_guard_type.hpp` |
| `UTIL/` | 3D physics (`ball3d`, `club3d`, `vector3d`, `matrix3d`, `quad_tree3d`), `lottery`, `map` (course data from IFF), `mgr_achievement`/`sys_achievement`, `mgr_daily_quest`, `broadcast_list`, `extra_power`, `block_exec_one_per_time` |
| `UNIT/` | `unit_gg_auth_server_connect` (GG Auth Server client, compiled out by default) |
| `CSAuth/` | `GGAuth.{hpp,cpp}` (in-process GameGuard wrapper), vendor headers `ggsrv*.h`, vendor libs/dlls (`ggauth*.dll`, `ggsrvlib*.lib`) |
| `DATABASE/` | `pangya_game_db.*` (not in the Linux Makefile CFILES) |

### 8.1 Rooms, channels and game modes
| System | Purpose | Files |
|---|---|---|
| Channel | Channel membership and the main dispatcher for lobby, room, shop, mail and item requests | `GAME/channel.{h,cpp}` |
| Lobby | lobby container | `GAME/lobby.{h,cpp}` |
| Room / RoomManager | Room state, players, teams, start game, game-object creation | `GAME/room.{h,cpp}`, `room_manager.*` |
| Special rooms | server-created event rooms | `room_grand_prix.*`, `room_grand_zodiac_event.*`, `room_bot_gm_event.*` |
| Game base | Common in-game logic: shots, holes, drops, pang calculation, save, achievements | `game.*`, `course.*`, `hole.*`, `team.*` |
| Versus family | Stroke, Match (teams), Pang Battle | `versus_base.*`, `versus.*`, `match.*`, `pang_battle.*` |
| Tourney family | Tourney, Practice, Approach, SSC, Guild Battle, Grand Prix | `tourney_base.*`, `tourney.*`, `practice.*`, `approach.*`, `special_shuffle_course.*`, `guild_battle.*`, `grand_prix.*` |
| Grand Zodiac | GZ Int/Adv, chip-in practice | `grand_zodiac_base.*`, `grand_zodiac.*`, `chip_in_practice.*` |
| Guild battle support | guild teams and player pairs | `guild.*`, `guild_room_manager.*`, `dupla.*`, `dupla_manager.*` |

### 8.2 Items, shops, mail
| System | Purpose | Files | DB (load) |
|---|---|---|---|
| ItemManager | Add/remove/transfer every item type, gifts, personal shop transfers | `item_manager.*`, `item.*` | many `CmdAdd*/CmdDelete*/CmdFind*/CmdUpdate*` |
| PapelShopSystem | Papel Shop / Big Papel Shop gacha, daily limit and coupons | `papel_shop_system.*` | `CmdPapelShopConfig/Item/Coupon` |
| PersonalShop(Manager) | Player sale shops in the lounge | `personal_shop.*`, `personal_shop_manager.*` | `CmdPersonalShopLog` |
| MemorialSystem | Memorial shop gacha | `memorial_system.*` | `CmdMemorialLevelInfo`, `CmdMemorialNormalItemInfo` |
| BoxSystem | Box opening (my room / mail) | `box_system.*` | `CmdBoxInfo` |
| CardSystem | Card packs, card compose | `card_system.*` | `CmdCardPack` |
| CometRefillSystem | Comet (ball) refill | `comet_refill_system.*` | `CmdCometRefillInfo` |
| PremiumSystem | Premium ticket and premium ball handling | `premium_system.*` | - |
| MailBoxManager / PlayerMailBox | Gift/mail box, putting items into mail | `mail_box_manager.*`, `player_mail_box.*` | `CmdPutItemMailBox`, `CmdAddMsgMail` |

### 8.3 Events and rewards
| System | Purpose | Files | DB |
|---|---|---|---|
| TreasureHunterSystem | Per-course treasure points and rewards | `treasure_hunter_system.*` | `CmdTreasureHunterInfo/Item` |
| DropSystem | Course item drops | `drop_system.*` | `CmdDropCourseConfig/Info` |
| CubeCoinSystem / CoinCubeLocationUpdateSystem | Spinning cube / coin placement on holes | `cube_coin_system.*`, `coin_cube_location_update_system.*` | `CmdCoinCubeInfo/LocationInfo` |
| GrandZodiacEvent | Timed GZ event rooms | `grand_zodiac_event.*` | `CmdGrandZodiacEventInfo` |
| GoldenTimeSystem | Golden Time rounds and winner prizes | `golden_time_system.*` | `CmdGoldenTime*` |
| BotGMEvent | Timed bot-GM event rooms | `bot_gm_event.*` | `CmdBotGMEventInfo` |
| LoginRewardSystem | Login reward | `login_reward_system.*` | `CmdLoginReward*` |
| AttendanceRewardSystem | Daily attendance | `attendance_reward_system.*` | `CmdAttendanceRewardItemInfo` |
| ApproachMissionSystem | Approach-mode missions | `approach_mission_system.*` | `CmdApproachMissions` |
| Daily quest / achievements | Server daily quest and per-player achievements | `UTIL/mgr_daily_quest.*`, `UTIL/mgr_achievement.*`, `UTIL/sys_achievement.*` | `CmdDailyQuestInfo`, `CmdAchievementInfo` |
| Login | Async player-cache load at login | `login_manager.*`, `login_task.*` | see 5.1 |

## 9. GameGuard (CSAuth)

Enabled by `GAMEGUARDAUTH=1`. Mode is chosen at compile time by `#define MY_GG_SRV_LIB 1` in `CSAuth/GGAuth.hpp`:

- **Default (`MY_GG_SRV_LIB 1`, any arch):** in-process `GGAuth`, which calls `InitGameguardAuth(NULL, max_user, true, 0x03)` from the repo's own `Server Lib/GGSrvLib26-1` library. Windows links `../../GGSrvLib26-1/{x64/,}{Debug,Release}/GGSrvLib26-1.lib`. Linux links the static `Linux Builds/GGSrvLib26-1/GGSrvLib26-1.a` built by `Makefile-ggsrvlib.mk`. `csauth2.cfg` must be in the working directory.
- **Alternative (`MY_GG_SRV_LIB 0` on x64):** `unit_gg_auth_server_connect` talks to the external GG Auth Server at `[GGAUTHSERVER]`. Login to it uses the Auth Server relay (`packet_as001`). See `docs/gg-auth-server.md`.

Flow:
- `check_player` runs every 10 s. When `m_auth_time` has passed, it calls `GetAuthQuery` and sends `0xD7` with 16 bytes.
- The next check comes 60 s later plus a random 0-60 s (`AUTH_PERIOD_SEC`, `AUTH_PERIOD_VARIATION_SEC` in `TYPE/game_guard_type.hpp`).
- The client answers with `0x88`, which goes to `requestCheckGameGuardAuthAnswer` and `CheckAuthAnswer`.
- A missing reply or a failed check disconnects the player (`player_manager::checkGameGuard`).

## 10. Build and run

### Windows
`Game Server/Game Server.sln` contains the single project `Game Server/Game Server.vcxproj` (Debug/Release, Win32/x64, toolset v141, `stdcpplatest`). It needs the `GGSrvLib26-1` project built first for the matching configuration, plus libzip and the DB client libraries used by `Projeto IOCP` (see the engine docs). Run it from a folder that contains `server.ini`, `config/socket_config.ini`, `data/pangya_jp.iff` and `csauth2.cfg`. Alternatively, pass that folder as the **second** command-line argument.

### Linux (`Server Lib/Linux Builds/Makefile-gs.mk`)
- Run from `Server Lib/Linux Builds` (the CFILES paths are `../Game Server/...` and `../Projeto IOCP/...`; `|` stands for a space): `make -f Makefile-gs.mk` (debug: `-Og -D _DEBUG -D SHOW_DEBUG_PACKET -ggdb`) or `make -f Makefile-gs.mk __build_type=release` (`-O3`). Clean with `make -f Makefile-gs.mk clean`.
- Flags: `g++ -std=c++20 -fpermissive -fsigned-char -fvisibility=hidden` plus glib-2.0 cflags. Link flags: `-Wl,--export-dynamic -pthread -ldl`, libzip, mysqlclient, `-lodbc`, `-lcrypto`, glib-2.0. Prerequisite: `GGSrvLib26-1/GGSrvLib26-1.a` (build `Makefile-ggsrvlib.mk` first).
- Output: `output-gs-{debug|release}/game`, copied to `Linux Builds/Game Server/game`. The `strip` target exists (`$(STRIPPED_OUTPUT)`) but is not part of `all`.
- Runtime folder `Linux Builds/Game Server/` already contains `server.ini` (IP `192.168.15.20`, change it), `config/socket_config.ini`, `csauth2.cfg` (`701`) and `data/` (the 3 iff files + CounterItem.txt). Run `./game` from inside that folder (cwd is used for `server.ini`, `data/`, `csauth2.cfg`, `Log/`).
- `Linux Builds/odbc.ini` is a sample ODBC DSN `[pangya]` for PostgreSQL (127.0.0.1:5432), for the ODBC-based engines.
- `run-as-to-rs.sh` starts the servers in tmux sessions named after the folders, with a hardcoded base dir `/home/acrisio/projects/Linux Builds`. `./run-as-to-rs.sh all` also starts `Game Server` (`./game`), and `./run-as-to-rs.sh kill` sends `exit` to each session.

## 11. Known quirks, gotchas, hardcoded values

- **`SAME_ID_LOGIN = 1` ships enabled.** Any client that knows a uid/id pair can log in without a valid auth key; the source marks this as "a fatal security flaw" (~line 1318). Set it to `0` in production.
- `EXPRATE`/`PANGRATE`/`CLUBMASTERYRATE` in server.ini are dead keys. Use the DB rate config, the console `rate`, or the Auth Server.
- `config/socket_config.ini` does not restrict the Game Server to `127.0.0.1`: With `order: deny,allow` and no `deny:` rule, every IP is accepted. `allow: 127.0.0.1` only exempts localhost from the DDoS and per-IP limits (verified in `Server Lib/Projeto IOCP/SOCKET/socket.cpp`, `connect_check`). To restrict access, add `deny: all` or use `order: mutual-failure`.
- `IP` in `[SERVERINFO]` is what goes into the DB server list, so it must be the client-reachable address.
- `GUID` must be unique. The rate config row and the server-switch targets are keyed by it.
- Client version mismatch does not reject the login. It silently sets `all_game` (cannot play). The packet version is hardcoded `2017082900`.
- Hardcoded limits:
  - 2000 concurrent login tasks;
  - 2 accept threads, 26 DB connections, 8 job threads;
  - notice interval 60 s, ticker interval 30 s;
  - Grand Prix lobby id 176;
  - only-rookie server rejects level >= 6.
- The `main()` working-directory switch uses `argv[2]`, not `argv[1]`.
- The ini comments for FLAG and PROPERTY differ from the struct field names (4.2/4.3). The ini lists some capability bits (14, 256, 512) that the code does not define.
- Only `data/pangya_jp.iff` is loaded. The IFF table version must be `0x0D`.
- `PangBattle` is noted in source as not finished ("uses Versus normal").
- If `CmdDailyQuestInfo` fails at startup the server goes to `FAILURE`. If the IFF or other systems fail, `onHeartBeat` retries loading them.
- `DATABASE/pangya_game_db.*` is not compiled on Linux. Its use on Windows is unverified.
