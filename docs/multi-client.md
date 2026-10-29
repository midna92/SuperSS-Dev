# Multi Client (load-test bot client)

## Purpose

`Server Lib/Multi Client` is a **console load-test / bot client**, not a server. It opens N simultaneous TCP connections, speaks the PangYa client protocol (login server -> game server -> channel -> lobby) with N fake accounts, and then idles in the lobby sending chat messages and heartbeats. Evidence in code: class `multi_client : public client` (`Server Lib/Multi Client/Multi Client/multi_client.{h,cpp}`) built on the shared `Server Lib/Projeto IOCP/Client/client.h`; accounts named `nat0..nat199`; a lobby-chat timer; the cap of 2000 connections in `main()`.

## How it fits in the system

- It talks to a **running Login Server** (hard-coded `127.0.0.1:10303`, which equals `PORT = 10303` in `Server Lib/Login Server/Login Server/server.ini`) and then to the Game Server whose address the Login Server returns in the server list.
- It does not touch the database, `.iff` files or ini-configured server settings. (A copy of `pangya_jp.iff` sits in `Multi Client/Multi Client/data/`, but no code in this project reads it - unverified why it is there.)
- **Windows only**: uses WinSock2, IOCP (`associaDeviceToPort`), `GetSystemInfo`, `SetPriorityClass`, `system("pause")`, DbgHelp mini-dump handler (`STDA_SET_SE_EXCEPTION`), and `IP_ADAPTER_INFO` (iphlpapi) to obtain the MAC address. There is no Linux makefile for it in `Server Lib/Linux Builds/`.

## Startup and runtime flow

`Multi Client/Multi Client/Multi Client.cpp` (file is UTF-16 encoded):

1. Number of connections: `argv[1]` if given, else prompt on stdin ("Numero de conexoes" = "number of connections"). Values `<= 0` or `> 2000` fall back to **1**.
2. `multi_client mc(qnt); mc.start();` then `system("pause")`.

`multi_client` constructor: creates `ClienteManager` with capacity `num_connection * 2`, registers the client-side packet handlers (below), sets `smc::mc` (a global pointer to the instance), state `GOOD`.

`multi_client::start()`: for `i` in `0..N-1` builds a `ClientInfo` and calls `ConnectAndAssoc("127.0.0.1", 10303, ci)`:

| Field | Value (hard-coded) |
|---|---|
| user id `m_id` | `"nat" + (i % 200)` - so at most 200 distinct accounts; with N > 200 the same account logs in repeatedly (the server will answer "already logged" error 4, which the client answers with packet 0x04 opt 0 = log out) |
| password `m_pass` | `"123456"` |
| `m_client_version` | `"SS.R7.989.00"` |
| `m_packet_version` | `2019110100` |

The accounts must exist in the server DB (not created by this tool). Then `client::start()` (engine class) starts monitor/send-message threads and the console command loop (`commandScan`).

`ConnectAndAssoc`: creates an overlapped TCP socket with `TCP_NODELAY`, adds a session to the `ClienteManager`, associates the socket with one of 16 IOCP threads picked randomly, connects, sets `m_ci.m_uid = 1` (login marker) and posts the first receive.

### Protocol sequence implemented (`PACKET/packet_func_client.cpp`)

Packet ids below are the first 2 bytes of the PangYa packet; "recv" = handled in `packet_func::packetXXX`, "send" = built in `pacote...`.

| Step | Packet | What the bot does |
|---|---|---|
| 1 | recv 0x000 (server hello, with crypt key + server uid) | store key, reply **send 0x001**: id, password, byte 2, int64 0, int64 0x7FFFFFFFFFFFFFFF, MAC string of first adapter |
| 2 | recv 0x001 (login result) | `err==0`: reads uid, capability, level, nickname (two layouts keyed on `opt` 0 or 5). `err==4` "already logged": sends 0x004 opt 0 (log out). `0xE2`: logs error code. other: logs error |
| 3 | recv 0x010 | stores key #0 (string) |
| 4 | recv 0x002 (game server list) | reads count + `ServerInfo[]`, stores `m_guid = servers[0].uid`, sleeps `1000 \| (rand() & 3000)` ms, **send 0x003** (uid of the server to pick) |
| 5 | recv 0x003 (key for game server) | stores key #1, opens a **new connection** to `servers[0].ip:port` with a copy of the credentials/keys and disconnects the login session. If the list was empty: marks `m_uid = ~0` (disconnect/retry marker; the retry monitor `checkClienteOnline()` is commented out so no retry actually happens) |
| 6 | recv 0x03F (game-server hello) | reads key, **send 0x002**: id, uid, 0, 0x6696, key #0, client version, packet version (passed through `de_encrypt_packet_ver`), MAC, key #1 |
| 7 | recv 0x044 | status: 0xD3 "logged in", 0xD2 loading percentage (`value/36*100`), 1 error code |
| 8 | recv 0x04D (channel list) | reads `ChannelInfo[]`, picks index 0, **send 0x004** opt 1 + channel id |
| 9 | recv 0x04E | `0|1` success: records channel, **send 0x081** (enter lobby). Other: tries next channel (index+1) |
| 10 | recv 0x0F5 (in lobby) | marks `m_lobby=1`, sends packet **0x09** (room 0, no password), starts a periodic timer (15000 + rand%5000 ms) calling `packet_func::sendMessage` (lobby chat text `"[Canal ID: n] [Timer->Ola Lobby]"` = "Hello Lobby") |
| - | recv 0x1A9 | `byte==1`: reads TTL ms, starts periodic **heartbeat** timer sending **0x0F4** |
| - | recv 0x1AD (web key) | builds the web link (`link_gacha`, `link_guild`, `link_entrance`, `link_weblink[0..2]` + `?ID=<id>&KID=<webkey>`) and logs it; replies 0xA1 |
| - | recv 0x0D7 (GameGuard check) | bot disconnects immediately ("leaves before it kicks") |
| - | recv 0x006 / 0x009 / many others | chat macro stored; MSN server list read; the rest are registered to `packet_svFazNada` ("does nothing") |

Server-to-server style handlers `funcs_sv` (0x001, 0x002-0x004, 0x03D, 0x081, 0x09E, 0x0A1, 0x0A2, 0x0C1, 0x0F4, 0x0FB) are registered but only 0x001 has logic (`packet_sv001`).

Console commands (`multi_client::commandScan`, typed on stdin): `exit`; `timer start|stop|restart|pause` (a periodic 30000/10000/1000 ms timer that sends a lobby message for the first channel session).

## Configuration

`Multi Client/Multi Client/server.ini`:

```
[OPTION]
TTL = 0      ; "Time To Live": ms before dropping a player that does not answer, or 0 to disable
```

`Multi Client/Multi Client/config/socket_config.ini` is the standard engine socket config (IP rules `enable_ip_rules: 1`, `limit_connection_per_ip: 10`, `order: deny,allow`, `allow: 127.0.0.1`, DDoS `ddos_interval: 3000`, `ddos_count: 5`, `ddos_autoreset: 600000`). The README-level shared description of this file applies; for a pure client it is mostly irrelevant (`accept_completed()` is an empty override).

Hard-coded instead of configurable: host `127.0.0.1`, port `10303`, account naming, password, version strings, max 2000 connections.

## Source layout

```
Server Lib/Multi Client/
  Multi Client.sln
  Multi Client/            Multi Client.cpp (main), multi_client.{h,cpp}, stdafx.*, targetver.h, *.vcxproj, server.ini, config/, data/
  PACKET/packet_func_client.{h,cpp}   client-side packet handlers (packetXXX) and builders (pacoteXXX)
  SESSION/player.{hpp,cpp}, cliente_manager.{hpp,cpp}   per-connection session, session manager
  TYPE/client_info.{h,cpp}, pangya_client_st.h   ClientInfo (ids, keys, server/channel lists, timers, web links)
```

Key engine classes reused from `Server Lib/Projeto IOCP`: `client` (`Client/client.h`), `session_manager`, `threadpl_client`, `timer`/`timer_manager`, `message_pool`, `packet`.

## Build & run

- Windows: open `Server Lib/Multi Client/Multi Client.sln` in Visual Studio (links `ws2_32.lib`, `DbgHelp.lib`). Start the Login and Game servers first (and have DB accounts `nat0`.. with password `123456`), then run `Multi Client.exe [count]` from a directory with `server.ini` and `config/`.
- Linux: not supported by any makefile in this repo.

## Gotchas

- Not a general emulator of a full game client: it only gets to the lobby, tries to enter room 0, chats and sends heartbeats.
- Sleeps (`Sleep(1000 | rand&3000)`) inside a packet handler block an IOCP worker thread.
- The same typo-ish naming (`ClienteManager`, `cliente_manager`) is Portuguese for "client manager".
- `ci.m_uid = ~0` is used as a "needs reconnect" flag but the monitoring loop that would use it is commented out.
