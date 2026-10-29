# Database guide (`pangya` schema)

All C++ servers (Login, Game, Message, Rank, Auth) and the PHP web site share one database called `pangya`. This document covers the four SQL dumps in the repo root, the engines the C++ code supports, how to import each dump, how the code calls the database, what the tables hold, and what data you must insert yourself.

Unless a path says otherwise, it is relative to the repo root. Counts come from `grep` on the dumps; "unverified" means I did not confirm it by reading code or running it.

---

## 1. The four dumps

| File | Engine / version (from header) | Dump date | Encoding | Tables | Routines | Notes |
|---|---|---|---|---|---|---|
| `bk-squema-mysql.sql` | mysqldump 8.0.25, server **MySQL 8.0.28** (Ubuntu 20.04) | 2022-03-20 (`-- Dump completed`) | UTF-8 | 179 | 411 procedures + 11 functions | **Newest.** Use it as the reference schema. |
| `bk-squema-mariadb.sql` | same header as the MySQL file | same | UTF-8 | 179 | 411 + 11 | Same bytes as the MySQL file except every `utf8mb4_0900_ai_ci` collation is replaced by `utf8mb4_unicode_ci` (3522 diff lines, all collation-only). MariaDB has no `utf8mb4_0900_ai_ci`. |
| `bk-squema-postgres.sql` | pg_dump 13.3, server **PostgreSQL 13.4** | 2021-10-19 | UTF-8 | 151 | 427 `plpgsql` functions (425 distinct names, so some are overloaded) | Has no `CREATE DATABASE`. Creates schema `pangya`. Uses identity columns (needs PG 10 or newer). |
| `bk-squema-mssql.sql` | SSMS "Generate Scripts", `COMPATIBILITY_LEVEL = 140` (**SQL Server 2017**) | 2021-10-31 | **UTF-16LE**, CRLF | 179 | 462 procedures + 17 functions (469 in schema `[pangya]`, 9 in `[dbo]`, 2 in `[m2ss]`) | Was converted from MySQL with SSMA. You can tell from the `MS_SSMA_SOURCE` extended properties and the `[m2ss]` helper schema (`substring_index`, `time_to_sec`). |

What all four have in common:

- **They contain schema only, no data.** None of them has a single top-level `INSERT` or `COPY` row. Every lookup and config table starts empty. See §6.
- Every object is in a schema or database named `pangya`, and the C++ code always writes `pangya.<name>`.
- The PostgreSQL dump is missing 28 of the MySQL tables: `achievements`, `boxitemrarewin`, `char_equip`, `counter_items`, `memorial_shop_coin|item|rate`, `memorialshopitemrarewin`, `pangya_box_item`, `pangya_box_table_item`, `pangya_character_information_antiga`, `pangya_character_part_padrao`, `pangya_gacha`, `pangya_gacha_coin|items|user_key|user_won`, `pangya_item_typelist`, `papelshopitemrarewin`, `quest_items`, `quest_stuffs`, `scratchy_item|rare_win|rate`, `temp_counter_typeid_init`, `temp_tmp`, `temp_typeid`, `type_list`. They are legacy tables (see §7.15), and nothing the C++ code calls needs them.
- **Coverage check.** I collected every `"pangya.<name>"` string in `Server Lib/**/*.{cpp,hpp,h}` (198 names) and compared them with each dump. All three engines define every routine and table the code uses, except four: `ProcCheckAttendanceReward`, `ProcGeraAuthKey`, `ProcGetAuthKeys` and `ProcGetEmailFromMailBox`. MSSQL does have the last one. These four only show up in legacy or commented-out code: `Game Server/DATABASE/pangya_game_db.cpp` and `Login Server/DATABASE/pangya_login_db.cpp`, which are not in `Makefile-gs.mk`/`Makefile-ls.mk`, plus the unused `CmdKeysOfLogin` and the commented-out `CmdMailBoxInfo` calls.

MySQL dump specifics:

- All 179 tables are `ENGINE=MyISAM DEFAULT CHARSET=latin1`. MyISAM means no transactions and no foreign keys. Columns that hold user text (`account.NICK`, `contas_beta.*`, `pangya_notice_list.message`, nicknames, emails and so on) override the table default with `utf8mb4`.
- The database is created with `DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin`.
- 421 routines have `DEFINER=root@localhost`. One, `GetAchievementPoint`, has `DEFINER=pangya_extrn@192.168.15.13`, which is the author's LAN account. It is called only from `ProcGetLevelMemorial`, which no C++ code uses.
- Every routine is created with `sql_mode='ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION'`.
- There are no views, triggers or events.
- Some column names need quoting: `user_info` has `O.B`, `Long-putt`, `Chip-in` and `16bit_naosei`.

---

## 2. Engines supported by the code and what the Linux builds use

The DB layer is in `Server Lib/Projeto IOCP/DATABASE/`:

| Class | File | Driver | How it connects | How it calls a routine (`ExecProc`) | String literal (`makeText`) | Identifier quoting (`makeEscapeKeyword`) |
|---|---|---|---|---|---|---|
| `mysql_db` | `mysql_db.cpp` | libmysqlclient | `mysql_real_connect(DBIP, DBUSER, DBPASS, DBNAME, DBPORT, CLIENT_MULTI_STATEMENTS)`, then `mysql_set_character_set("utf8mb4")` | `CALL name(params)` | `'...'`, escapes backslashes only | `` `x` `` |
| `mssql` | `mssql.cpp` | ODBC (`SQLDriverConnect`) | connection string `DSN=<DBIP>;DATABASE=<DBNAME>;UID=<DBUSER>;PWD=<DBPASS>` | `exec name params` (no parentheses) | `N'...'`, no escaping | `[x]` |
| `postgresql` | `postgresql.cpp` | ODBC (`SQLDriverConnect`) | same DSN string as mssql | `select * from name(params)` | `'...'::text`, no escaping | `"x"` |

`NormalDB::runExecQuery` (`normal_db.cpp`) chooses the class by comparing `[NORMAL_DB] DBENGINE` case-insensitively: `mysql`, `mssql` or `postgresql`. **An unknown or missing value falls back to `mssql`.**

Important consequences:

- **With `mssql` and `postgresql`, `DBIP` is an ODBC DSN name, not a host.** The host and port come from the DSN. `DBPORT` is read (`NormalDB::loadIni`, default 0 = engine default) but only `mysql_db` uses it.
- Raw SQL can mark identifiers with `-//name//-`. The macro is `DB_MAKE_ESCAPE_KEYWORD_A` in `database.h`. `database::parseEscapeKeyword` rewrites the marker into the engine's quoting, for example `pangya_auth_key.key` → `` `key` `` / `[key]` / `"key"`. This rewrite only runs in `consulta()` (SELECT) and on procedure names, not in `_insert/_update/_delete` (`PANGYA_DB/pangya_db.cpp`).

**Shipped configs**

| File | DBENGINE | DBIP | DBNAME/USER/PASS | DBPORT |
|---|---|---|---|---|
| `Server Lib/Linux Builds/{Auth,Game,Login,Message,Rank} Server/server.ini` | `mysql` | `localhost` | `pangya`/`pangya`/`pangya` | `3306` |
| `Server Lib/Game Server/Game Server/server.ini` (Windows) | `mysql` | `localhost` | `pangya`×3 | `3306` |
| `Server Lib/{Auth,Login,Message,Rank} Server/.../server.ini` (Windows) | `mssql` | `pangya` (a DSN) | `pangya`×3 | commented out (`#DBPORT = 1433`). The comment, in Portuguese, says the port is not used with ODBC on SQL Express. |
| `Server Lib/GG Auth Server/GG Auth Server/server.ini` | no `[NORMAL_DB]` section | | | |

So **the Linux builds use MySQL** (or MariaDB through the same client library) at `localhost:3306`. That makes the MySQL or MariaDB dump the one to import for Linux. The Windows ini files mix engines (Game Server on mysql, the rest on mssql). Pick one engine and make all ini files match.

`Server Lib/Linux Builds/odbc.ini` defines an unixODBC DSN called `[pangya]`: `Driver = PostgreSQL`, `Database = pangya`, `Servername = 127.0.0.1`, `Port = 5432`. It is only used if you switch a Linux server to `DBENGINE = postgresql` with `DBIP = pangya`. Install it as `/etc/odbc.ini` or `~/.odbc.ini`; that location is standard unixODBC behaviour, not something in the repo. The Linux makefiles (`Makefile-{as,gs,ls,ms,rs}.mk`) compile all three backends and link `pkg-config --libs mysqlclient` plus `-lodbc`. A commented-out line mentions `/usr/lib/libmsodbcsql-17.so` (Microsoft's ODBC driver for Linux), so MSSQL from Linux should work through ODBC (unverified).

The PHP site has its own credentials in `www/pangya/config/config.inc`: MySQL `127.0.0.1`/`pangya`/`pangya`/`pangya`, MSSQL and PostgreSQL via DSN `pangyaPHP` with user `pangyaPHP`, and `DatabaseCurrentUsed = _MYSQL_`. See `docs/web-and-apache.md`.

---

## 3. Creating the database and importing a dump

Pick a strong password instead of the shipped `pangya` (see §9). If you do, update `DBPASS` in every `server.ini` and in `www/pangya/config/config.inc`.

### 3.1 MySQL 8.0 (`bk-squema-mysql.sql`)

The dump runs `CREATE DATABASE IF NOT EXISTS pangya ... utf8mb4 COLLATE utf8mb4_bin` and `USE pangya` itself.

```sh
# 1. Import as root. The routines use DEFINER=`root`@`localhost`; a non-root user
#    would need SET_USER_ID/SUPER. With binary logging on (the MySQL 8 default),
#    creating functions also needs SUPER or log_bin_trust_function_creators=1.
mysql -u root -p < bk-squema-mysql.sql

# 2. Application user. EXECUTE is required because almost everything goes through CALL.
mysql -u root -p -e "
  CREATE USER 'pangya'@'localhost' IDENTIFIED BY 'change-me';
  GRANT SELECT, INSERT, UPDATE, DELETE, EXECUTE, CREATE TEMPORARY TABLES ON pangya.* TO 'pangya'@'localhost';"
```

- `CREATE TEMPORARY TABLES` is there because `GeraRankAll` and `USP_UPDATE_GUILD_RANKING` use temporary tables. I inferred this from the routine bodies (`__tabRank`, `tmp`) and did not test it. `GRANT ALL ON pangya.*` also works.
- `GetAchievementPoint` has `DEFINER=pangya_extrn@192.168.15.13`. Calling it will fail unless that account exists. Either change the DEFINER or recreate the function under `root@localhost`. Nothing in the C++ code reaches it.
- The servers connect with `CLIENT_MULTI_STATEMENTS` and set the connection charset to `utf8mb4`.

### 3.2 MariaDB (`bk-squema-mariadb.sql`)

Same steps as 3.1, using the MariaDB file. The only change in that file is the collation (`utf8mb4_unicode_ci`). Things to watch for:

- Line 1 still has `/*!80016 DEFAULT ENCRYPTION='N' */`. MariaDB runs versioned comments whose number is lower than its own version, so it may reject this clause. If the import fails on line 1, delete that comment (unverified).
- The Linux makefiles link against `mysqlclient` through pkg-config. Whether MariaDB Connector/C's `libmariadb`/`mysqlclient.pc` is a drop-in replacement depends on your distribution (unverified).

### 3.3 PostgreSQL 13 (`bk-squema-postgres.sql`)

The dump expects the database and three roles to exist already: owner `postgres` (577 `OWNER TO postgres`), `pangya` (147 GRANTs, owner of `pangya_rank_antes`/`pangya_rank_atual`) and `"pangyaPHP"` (72 GRANTs, SELECT/INSERT/UPDATE only).

```sh
sudo -u postgres psql -c "CREATE ROLE pangya LOGIN PASSWORD 'change-me';"
sudo -u postgres psql -c "CREATE ROLE \"pangyaPHP\" LOGIN PASSWORD 'change-me-too';"
sudo -u postgres psql -c "CREATE DATABASE pangya ENCODING 'UTF8';"
sudo -u postgres psql -d pangya -f bk-squema-postgres.sql
```

Then switch the servers over:

1. Install the PostgreSQL ODBC driver (psqlODBC) and unixODBC, and put `Server Lib/Linux Builds/odbc.ini` in place (DSN `pangya`).
2. In every `server.ini`, set `DBENGINE = postgresql`, `DBIP = pangya` (the DSN name), `DBNAME = pangya`, and the user and password.

### 3.4 SQL Server 2017+ (`bk-squema-mssql.sql`)

The script is UTF-16LE. It starts with `USE [master]` and creates database `[pangya]` with hard-coded file paths (`C:\Program Files\Microsoft SQL Server\MSSQL14.SQLEXPRESS\MSSQL\DATA\pangya.mdf` / `pangya_log.ldf`). It then runs `CREATE USER [pangyaPHP] FOR LOGIN [pangyaPHP]` and `CREATE USER [pangya] FOR LOGIN [pangya]`, but it **does not create the logins**.

1. Edit the two `FILENAME = N'...'` paths near the top of the file if your instance is not `MSSQL14.SQLEXPRESS`. Or delete the `ON PRIMARY ... LOG ON ...` clause to use the default locations.
2. Create the logins:
   `CREATE LOGIN pangya WITH PASSWORD='change-me'; CREATE LOGIN pangyaPHP WITH PASSWORD='change-me-too';`
3. Run the script: `sqlcmd -S .\SQLEXPRESS -E -i bk-squema-mssql.sql`. sqlcmd reads UTF-16 files with a BOM.
4. The script puts the `pangya` user in `db_datareader` and `db_datawriter` only. **Those roles do not include EXECUTE.** Add it with `USE pangya; GRANT EXECUTE ON SCHEMA::pangya TO pangya;`. I worked this out from the script; there is no `GRANT EXECUTE` in it.
5. Create an ODBC System DSN named `pangya` that points at the instance and database (Windows: ODBC Data Source Administrator; Linux: `odbc.ini` with the MS ODBC driver). Then set `DBENGINE = mssql`, `DBIP = pangya`.

---

## 4. How the C++ code talks to the database

### 4.1 Layers

```
server code (login_server.cpp, game_server.cpp, ...)
   │  new CmdXxx(args, waitable)                             PANGYA_DB/cmd_*.{hpp,cpp}
   ▼
snmdb::NormalManagerDB (singleton)                           DATABASE/normal_manager_db.{hpp,cpp}
   │  add(msg_id, pangya_db*, callback, arg) → picks one NormalDB
   ▼
NormalDB × N  (one DB connection + 2 threads each)           DATABASE/normal_db.{hpp,cpp}
   │  runExecQuery thread: msg->execQuery(db) → pangya_db::exec(db)
   │  runResponse  thread: callback(msg_id, pangya_db&, arg)  (async mode only)
   ▼
database (abstract) → mysql_db | mssql | postgresql          DATABASE/database.h, mysql_db.cpp, mssql.cpp, postgresql.cpp
   │  ExecQuery(sql) / ExecProc(name, params) → response → result_set(s) → ctx_res rows (char** data, cols)
```

- **NormalManagerDB** (`normal_manager_db.cpp`) creates `N` `NormalDB` instances. The default is `NUM_DB_THREAD = 26`. Each server passes its own count to the `server`/`unit` constructor: Game 26, Message 12, Login 8, Rank 4, Auth 4, GG Auth 2 (`game_server.cpp:126`, `message_server.cpp:56`, `login_server.cpp:81`, `rank_server.cpp:48`, `auth_server.cpp:54`, `gg_auth_server.cpp:28`). **Each instance holds its own connection, so plan for about 56 connections if every server runs on one database.** `add()` chooses the instance as `std::time(nullptr) % m_dbs.size()`, which means every request made in the same second goes to the same connection. A revive thread (`checkIsDeadAndRevive`) restarts dead instances.
- **NormalDB** (`normal_db.cpp`) reads `[NORMAL_DB]` from `server.ini` in the working directory (`loadIni`). It creates the engine object and connects, then loops over its queue (`list_async<msg_t*>`). If the connection is lost (error source `NORMAL_DB`, code 5), the message is put back in the queue and the exception is rethrown.
- **pangya_db** (`Server Lib/Projeto IOCP/PANGYA_DB/pangya_db.{h,cpp}`) is the base of every `Cmd*` class. Subclasses implement:
  - `prepareConsulta(database&)`: builds the call with `procedure(_db, "pangya.ProcX", params)` or runs SQL with `consulta/_insert/_update/_delete`;
  - `lineResult(ctx_res*, result_index)`: called once per row of each result set;
  - `_getName()`.
  `exec()` calls `prepareConsulta`, walks every result set and row, and stores any exception in `m_exception`. It never throws.
- **Two calling modes:**
  - *Waitable (synchronous)*: `CmdX cmd(..., true); NormalManagerDB::add(0, &cmd, nullptr, nullptr); cmd.waitEvent(); if (cmd.getException().getCodeError() != 0) ...`. The DB thread signals an event when done. The caller owns the object.
  - *Async*: `add(msg_id, new CmdX(..., false), &Server::SQLDBResponse, this)`. The response thread calls the server's static `SQLDBResponse(msg_id, pangya_db&, arg)`, which switches on `msg_id`. The `msg_t` destructor deletes the command.
- Helpers: `checkColumnNumber(expected, got)` throws if the row width is wrong. `checkResponse(r, msg)` throws if no result set came back and no rows were affected. `IFNULL(atoi, data[i])` turns SQL NULL into 0.
- `DATABASE/exec_query.*` and `DATABASE/pangya_db.{h,cpp}` (`pangya_base_db`) are an older pooled-query path. Their only remaining use, `postAndWaitResponseQuery`, is commented out in the current helpers, so commands now call `database::ExecQuery/ExecProc` directly.

There are 250 `cmd_*.cpp` classes: 27 in `Projeto IOCP/PANGYA_DB` (shared), 197 in Game Server, 9 in Login, 7 in Auth, 5 in Message, 5 in Rank. 194 of the 422 MySQL routines are named in C++ code. About 116 are named in the PHP site (`www/`, which calls `CALL ProcX(...)`). Roughly 115 are not referenced by either and are legacy or admin-only.

### 4.2 Worked example 1: login password check (Login Server)

`Server Lib/Login Server/Login Server/login_server.cpp`, `requestLogin` (around lines 355-430):

1. The password from the client is hashed: `md5::processData(...)`, then `pass_md5 = md5::getHash()`.
2. `CmdVerifyID cmd_verifyId(id, true)` (`Projeto IOCP/PANGYA_DB/cmd_verify_id.*`, `m_szConsulta = "pangya.ProcVerifyID"`). On MySQL this becomes `CALL pangya.ProcVerifyID('<id>')`:
   ```sql
   SELECT uid, id FROM pangya.account WHERE CAST(id as BINARY(25)) = CAST(_ID as BINARY(25));
   ```
   (case-sensitive match on `account.ID`). `lineResult` stores the UID.
3. `CmdVerifyPass cmd_verifyPass(uid, pass_md5, true)` (`cmd_verify_pass.cpp`) builds `procedure(_db, "pangya.ProcVerifyPass", to_string(uid) + ", " + _db.makeText(pass))`, so on MySQL: `CALL pangya.ProcVerifyPass(123, 'abcd...')`:
   ```sql
   SELECT uid FROM pangya.account WHERE UID = _IDUSER AND `PASSWORD` = UPPER(_PASS);
   ```
   `lineResult` checks `checkColumnNumber(1, cols)` and that the returned uid equals the requested one, and sets `m_lastVerify`.
4. If the ID does not exist and `[OPTION] CREATEUSER` is on (with `ACCESSFLAG` off), `CmdCreateUser` (`Login Server/PANGYA_DB/cmd_create_user.cpp`) calls `pangya.ProcNewUser(id, plaintext_password, ip, server_uid)`. That procedure stores `upper(md5(__pass))` in `account` and inserts the default row into 17 per-player tables (§6.3).
5. Next come `CmdPlayerInfo` (`pangya.ProcGetPlayerInfoLogin`, reads `account`), `CmdLogonCheck` (`SELECT LOGON, game_server_id FROM pangya.account WHERE UID = `) and `CmdFirstLoginCheck`.

Tables touched: `account` (columns `ID`, `UID`, `PASSWORD` = upper-case hex MD5, `Logon`, `FIRST_LOGIN`, `FIRST_SET`, `IDState`, `BlockTime`, `capability`, `game_server_id`).

### 4.3 Worked example 2: server registration and server list (every server)

`Server Lib/Projeto IOCP/Server/server.cpp` `registerServer` thread (around line 1228): **once per second** it sets `m_si.curr_user` and queues `new CmdRegisterServer(m_si)` asynchronously. The `unit` base class used by Auth and GG Auth does the same (`UNIT/unit.cpp:268`).

- `CmdRegisterServer::prepareConsulta` (`Projeto IOCP/PANGYA_DB/cmd_register_server.cpp`) calls `pangya.ProcRegServer_New` with 21 arguments: `uid, makeText(nome), makeText(ip), port, tipo, max_user, curr_user, rate.pang, makeText(version), makeText(version_client), property, angelic_wings_num, event_flag, rate.exp, img_no, rate.scratchy, rate.club_mastery, rate.treasure, rate.papel_shop_rare_item, rate.papel_shop_cookie_item, rate.chuva`.
- `ProcRegServer_New` (MySQL line ~25153): `SELECT UID INTO RegType FROM pangya_server_list WHERE UID = IDUSER`. If a row exists it `UPDATE`s everything and sets `State = 1, UpdateTime = now()`. Otherwise it `INSERT`s, with 0 for the legacy columns (`PCBangUser`, `ServiceControl`, `AppRate`, `EventMap`, `EventDropRate`, `HanbitUser`, `ParanUser`, `AuthState`). **So `pangya_server_list` fills itself. You do not seed it.**
- `CmdServerList(type)` (`cmd_server_list.cpp`) calls `pangya.ProcGetServerList(OPT)`. `OPT` is the `CmdServerList::TYPE_SERVER` enum: GAME=0, MSN=1, LOGIN=2, RANK=3, AUTH=4. The procedure maps it to the stored `Type` and returns only **live** servers:

  | OPT | Selects `Type` = | Liveness window | Stored by (`m_si.tipo`) |
  |---|---|---|---|
  | 0 Game | 1 | `UpdateTime > now() - 8 s` and `State = 1` | `game_server.cpp:4957` |
  | 1 Message | 3 | 8 s | `message_server.cpp:3203` |
  | 2 Login | 0 | 11 s | `login_server.cpp:1198` |
  | 3 Rank | 4 | 11 s | `rank_server.cpp:783` |
  | 4 Auth | 5 | 11 s | `auth_server.cpp:1125` |
  | n/a | (6 = GG Auth, `gg_auth_server.cpp:489`, never listed) | | |

  `lineResult` expects 13 columns: `Name, UID, IP, Port, MaxUser, CurrUser, property, AngelicWingsNum, EventFlag, EventMap, ImgNo, AppRate, ScratchRate`.

- Who reads the list: Login Server (`packet_func_ls.cpp:683` GAME, `:694` MSN, for the client's server list), Game Server (`game_server.cpp:968` RANK, `packet_func_sv.cpp:1979` MSN), and `server::cmdUpdateServerList()` (GAME).
- **Consequences:** clocks matter. `UpdateTime` comes from the DB server's `now()` and is compared with the DB's `now()`, so the DB clock is what counts. A server whose heartbeat stalls for more than 8-11 s disappears from the list. The `[SERVERINFO] GUID` in each `server.ini` is the primary key here, and also the key of `pangya_config` and `pangya_auth_key`. Shipped Linux GUIDs: Login 10103, Game 20203, Message 30303, Rank 4774, Auth 80808.

### 4.4 Worked example 3: Auth Server command queue → notice broadcast

The Auth Server polls `pangya_command` (an admin or web "command queue") and forwards the commands to other servers (`Server Lib/Auth Server/Auth Server/auth_server.cpp`, `translateCmd`, around line 763).

1. `CmdCommandInfo` (`Auth Server/PANGYA_DB/cmd_command_info.*`) calls `pangya.ProcGetCommands()`:
   ```sql
   SELECT idx, command_id, arg1, arg2, arg3, arg4, arg5, target, flag, valid,
          IFNULL(UNIX_TIMESTAMP(reserveDate), 0) as reserveDate
   FROM pangya.pangya_command
   WHERE valid = 1 AND (reserveDate IS NULL OR now() >= reserveDate) ORDER BY regDate;
   ```
2. For `command_id = 0` (`COMMAND_ID::BROADCAST_NOTICE` in `Auth Server/TYPE/pangya_auth_st.h`; the others are 1 TICKER, 2 CUBE_WIN, 3 SHUTDOWN, 4 NEW_ITEM_NOTICE, 5 NEW_RATE, 6 ADM_KICK_FROM_WEBSITE, 7 RELOAD_SYSTEM), `CmdNoticeInfo cmd_ni(el.idx, true)` (`cmd_notice_info.cpp`) calls `pangya.ProcGetNotice(idx)`:
   - `__NOTICE_ID = pangya_command.arg1` for that `idx`;
   - `UPDATE pangya_notice_list SET replayCount = replayCount - 1`;
   - when `replayCount <= 0`: `UPDATE pangya_command SET valid = 0`; otherwise `reserveDate = now() + refreshTime minutes`;
   - `SELECT message FROM pangya_notice_list WHERE notice_id = __NOTICE_ID`.
   `lineResult` copies the single column into `m_message`, which the Auth Server sends to the target servers.
3. Tickers (`ProcGetTicker` → `pangya_ticker_list`) and shutdowns (`ProcGetShutdownServer` → `pangya_shutdown_list`) work the same way. `CmdUpdateCommand` (`ProcUpdateCommand`) changes the queue row.

To broadcast a notice by hand:

```sql
INSERT INTO pangya.pangya_notice_list(message, replayCount, refreshTime) VALUES('Server restart at 20:00', 3, 10);
INSERT INTO pangya.pangya_command(command_id, arg1, target) VALUES(0, LAST_INSERT_ID(), 0);
```

What `target` means (one server UID or a type, with 0 apparently meaning all) is decided in `translateCmd`; see `docs/auth-server.md`.

---

## 5. Stored-procedure families

These are the MySQL names; PostgreSQL and MSSQL use the same names. By prefix: `ProcGet*` 139, `ProcUpdate*` 49, `ProcInsert*` 35, `ProcRegister*` 17, `ProcDelete*` 15, `ProcAdd*` 13, `ProcGuild*` 11, `ProcFind*` 11, `ProcCheck*` 8, `ProcWrite*` 5, `ProcVerify*` 4, `ProcMake*` 4, `ProcGera*` 4, other `Proc*` 45, `USP_*` 37, plus a few others.

| Family | Examples | Purpose |
|---|---|---|
| Account and login | `ProcVerifyID`, `ProcVerifyPass`, `ProcVerifyIP`, `ProcVerifyNickname`, `ProcNewUser`, `ProcNewUserWithMD5`, `ProcMakeUserBeta(WithMD5)`, `ProcFirstSet`, `ProcRegisterLogin`, `ProcRegisterLogon`, `ProcRegisterLogonServer`, `ProcSaveNickname`, `ProcChangePassword`, `ProcChangeNickname`, `ProcRescueID`, `ProcRescuePassword`, `ProcCheckRescuePasswordAndIDKey` | Account creation, login checks, logon state, first-character setup, password and ID rescue (web) |
| Auth keys | `ProcGeraAuthKeyLogin`, `ProcGetAuthKeyLogin`, `ProcGeraAuthKeyGame`, `ProcGetAuthKeyGame`, `ProcVerificaAuthKeys`, `ProcGetNewAuthServerKey`, `ProcUpdateAuthServerKey`, `ProcGeraWeblinkKey`, `CheckWeblink_Key`, `ProcGeraSecurityKey`, `ProcCheckSecurityKey` | One-time keys for the Login→Game hand-off (`authkey_login`/`authkey_game`), server-to-Auth keys (`pangya_auth_key`), web-link keys |
| Server and admin | `ProcRegServer_New`, `ProcGetServerList`, `ProcGetRateConfigInfo`, `ProcUpdateRateConfigInfo`, `ProcGetCommands`, `ProcUpdateCommand`, `ProcInsertCommand`, `ProcGetNotice`, `ProcGetTicker`, `ProcGetShutdownServer`, `ProcRegister{NoticeBroadcast,Ticker,ShutdownServer,NewRateServer,ReloadSystemServer,KickPlayerCommand}`, `ProcGetIPTable`, `ProcGetMACTable`, `ProcInsertBlockIP/MAC`, `ProcDeleteBlockIP/MACFromTable` | Server heartbeat and list, per-server rates and event switches, command queue, bans |
| Player load (`ProcGet*Info`) | `ProcGetPlayerInfoLogin/Game/Message/Rank`, `ProcGetUserInfo`, `ProcGetCharacterInfo(_One)`, `ProcGetCaddieInfo(_One)`, `ProcGetMascotInfo(_One)`, `ProcGetCardInfo(_One)`, `ProcGetCardEquip`, `ProcGetWareHouseItem(_One)`, `ProcGetTrofel*`, `ProcGetMapStatistics*`, `ProcGetRoom`, `ProcGetMacrosUser`, `ProcGetTikiPoints`, `GetTutorial`, `USP_CHAR_EQUIP_LOAD_S4(_ONE)` | Loading the full player state on login (Game Server `LoginManager`) |
| Inventory changes | `ProcAddItem`, `USP_ADD_ITEM`, `ProcAddCharacter`, `ProcAddCaddie`, `ProcInsertMascot`, `ProcInsertCard`, `ProcInsertClubSet`, `ProcAddBall`, `ProcAddPart`, `ProcInsertSkin`, `ProcAddFurniture`, `ProcTransferPart`, `ProcExtendRental`, `ProcFind*` (ownership checks), `ProcUpdate{Caddie,Mascot,ClubSet,Card,Character}*`, `ProcEquipCard`, `ProcRemoveEquipedCard` | Buying, gifting, equipping, upgrading |
| Flush / save (`USP_FLUSH_*`, `USP_*_ATT`, `USP_SAVE_*`) | `USP_FLUSH_CHARACTER`, `USP_FLUSH_CADDIE`, `USP_FLUSH_CLUB`, `USP_FLUSH_EQUIP_*`, `USP_FLUSH_GAMEPOINT`, `USP_FLUSH_MOEDAS`, `USP_FLUSH_STAT_*`, `USP_EXP_PLAYER_ATT`, `USP_EXP_CADDIE_ATT`, `USP_EXP_MASCOT_ATT`, `USP_SAVE_MACROS`, `USP_SAVE_ROOM`, `USP_CHAR_EQUIP_SAVE_S4`, `ProcUpdateUserInfo` | Writing equipment, stats, EXP and currencies back. The `_S4` / `td_*` names come from the original "Season 4" schema. |
| Mail / gift box | `ProcColocaMsgNoGiftTable` ("put message in gift table"), `ProcInsertItemNoEmail`, `ProcGetAllEmailFromMailBox`, `ProcGetEmailNaoLidaFromMailBox(One)` ("unread"), `ProcGetInformationEmail(2)`, `ProcMoveItemFromMailToArmario` ("to locker"), `ProcExcluiMsgMailBox` ("delete"), `ProcAddMsgOff`/`ProcGetMsgOff` (offline messages) | In-game mail (`pangya_gift_table` + `pangya_item_mail`) |
| Guild | `ProcMakeGuild`, `ProcGetGuildInfo*`, `ProcGetGuildMemberList`, `ProcGuild{AceitaPedido,CancelaPedido,KickMember,MoveMaster,ChangeAuthorityMember,Closure,ReOpen,Block,Unblock,AcceptMark,RejectMark}`, `ProcPedeParaEntraGuild`/`ProcRequestJoinGuild`, `ProcSaiGuild` ("leave"), `ProcWriteGuild{BBS,BBSReply,Notice,PrivateBBS,PrivateBBSReply}`, `ProcDeleteGuild*`, `ProcUpdateGuild*`, `ProcRegisterGuildMatch`, `USP_UPDATE_GUILD_RANKING`, `ProcGetGuildRankingUpdateTime` | Guild lifecycle, boards, matches, ranking. Many are called only from the web site. |
| Quests / achievements | `ProcGetNewAchievement`, `ProcInsertNewAchievement`, `ProcCheckAchievement`, `ProcInsertNewQuest`, `ProcFinishQuestUser`, `ProcUpdateQuestUser`, `ProcGetDailyQuest_New`, `ProcCheckDailyQuest`, `ProcCheckPlayerDailyQuest`, `ProcUpdateDailyQuest(User)`, `ProcRemoveOldAndAddNewDailyQuestPlayer`, `ProcAddCounterItem`, `ProcGetNewCounterItem`, `USP_CreateAchievementUser` (legacy) | Achievements, quests, counters, daily quests |
| Shops and gacha | `ProcGetBoxInfo`, `ProcInsertBoxRareWinLog`, `ProcGetMemorialNormalItemInfo`, `ProcInsertMemorialRareWinLog`, `ProcInsertPapelShopRareWinLog`, `ProcUpdatePapelShopInfo/Config`, `ProcGetBlackPapelItem/Rate`, `ProcGetGachaJPConfig`, `ProcRegisterGachaJPPlayerWin`, `ProcSendGachaJPPlayerItemToMail`, `ProcGetCardPack`, `ProcGetCardRate`, `ProcGetCometRefillInfo`, `ProcInsertCPLog(Item)`, `ProcInsertPersonalShopLog`, `ProcGetLegacyTikiShopInfo` | Random-reward systems and their logs |
| Events and rewards | `ProcGetAttendanceReward`, `ProcUpdateAttendanceReward`, `ProcGetGoldenTimeInfo`, `ProcInsert/Update/DeleteGoldenTime{Event,Item,Round}`, `procInsert/UpdateLoginRewardPlayer`, `ProcInsert/Update/DeleteLoginRewardEvent`, `ProcInsert/Update/DeleteBotGMEvent{Time,Reward}`, `ProcGetPlayerLuciaAttendanceInfo`, `ProcRegisterLuciaAttendanceReward`, `ProcDo1stAnniversary`, `ProcGetApproachMissions`, `ProcGetCourseReward`, `ProcGetDropCourseConfig`, `ProcGrandPrixClearPlayer`, `ProcRegisterGrandZodiacEventWebPlayerWin` | Timed events (`Insert/Update/Delete*` are used by the admin web pages) |
| Ranking | `GeraRankAll` ("generate all rankings"), `GeraRankAll_Ant` (old), `ProcGetRankRegistryInfo`, `ProcGetRankRegistryCharacterInfo`, `ProcCheckRankRefreshTime` | Rank Server, see `docs/rank-server.md` |
| Donations / web shop | `ProcRegisterDonation(Epin)`, `ProcInsertDonationNew`, `ProcUpdateDonationNew(ByGM)`, `ProcGetAllDonation*`, `ProcExchangeCookiePointByEpin`, `ProcRegisterGMGiftWeb`, `ProcGetInfoPlayerToWeb` | PHP site (`www/`) |
| Functions | `GetEXPByLevel`, `GetLevelClubSet`, `GetRankClubSet`, `GetLevelMemorial`, `GetAchievementPoint`, `GetCardExpRate`, `GetCardPangRate`, `GetItensExpRate`, `GetItensPangRate`, `GroupID`, `SPLIT_STR` | Helpers used inside procedures |

Many names and columns are Portuguese. A short glossary: *nome* = name, *senha* = password, *tipo* = type, *qntd/quantidade* = quantity, *probabilidade* = probability, *ativo/active* = enabled, *antes/atual* = previous/current, *pontos* = points, *moedas* = coins, *armário* = locker, *lida/não lida* = read/unread, *gera* = generate, *inicio/fim* = start/end, *temporada* = season, *dados* = data, *padrão* = default.

---

## 6. Data you must seed

The dumps hold no data. The servers still **start** with empty tables. What happens with each table, as far as the code shows:

### 6.1 Self-populating (do not seed)

- `pangya_server_list`: inserted or updated by `ProcRegServer_New` every second (§4.3).
- `pangya_config`: `ProcGetRateConfigInfo(GUID)` returns `-1 AS ERROR` when no row exists. Game Server (`game_server.cpp:4981`) and Message Server (`message_server.cpp:3206`) then use defaults: 100 % rates, events on, angel event off. They write those defaults back with `CmdUpdateRateConfigInfo`, whose `ProcUpdateRateConfigInfo` INSERTs the missing row. Edit the row afterwards to change rates.
- `pangya_auth_key`: `ProcGetNewAuthServerKey(GUID)` inserts or updates a random 16-character key.
- Per-player rows: created by `ProcNewUser`/`ProcNewUserWithMD5` (see 6.3).

### 6.2 Needed for full functionality (seed from your own data)

| Table(s) | Read by | If empty |
|---|---|---|
| `pangya_rank_config` (1 row: `refresh_time_H`, `reg_date`) | Rank Server `CmdRankConfigInfo` | Interval stays 0 and `GeraRankAll` keeps reporting failure. See `docs/rank-server.md` §quirks. **Seed 1 row.** |
| `pangya_new_card_pack` + `pangya_new_cards` | `CardSystem` (`cmd_card_pack.hpp`) | Card System not loaded |
| `pangya_papel_shop_config` + `pangya_papel_shop_item` (+ `pangya_papel_shop_coupon`) | `PapelShopSystem` | Papel Shop not loaded (`isLoad` needs both item and config) |
| `pangya_new_box` + `pangya_new_box_item` | `BoxSystem` (`ProcGetBoxInfo`) | Box System not loaded |
| `pangya_new_memorial_level`, `pangya_new_memorial_normal_item`, `pangya_new_memorial_lucky_set` | `MemorialSystem` | Memorial shop has no coins |
| `pangya_comet_refill` | `CometRefillSystem` | not loaded |
| `pangya_coin_cube_info`, `pangya_coin_cube_location` | `CubeCoinSystem`, `CoinCubeLocationUpdateSystem` | not loaded |
| `pangya_course_reward_treasure` + `pangya_treasure_item` | `TreasureHunterSystem::initialize` | throws "nao tem item do Treasure Hunter" ("no Treasure Hunter items"); not loaded |
| `pangya_new_course_drop` + `pangya_new_course_drop_item` | `DropSystem` | not loaded |
| `pangya_attendance_table_item_reward` | `AttendanceRewardSystem` | not loaded |
| `pangya_approach_missions` | `ApproachMissionSystem::initialize` | throws; not loaded |
| `pangya_grand_zodiac_times` | `GrandZodiacEvent` | no GZ event times |
| `pangya_golden_time_info/_item/_round`, `pangya_login_reward`, `pangya_bot_gm_event_time/_reward` | `GoldenTimeSystem`, `LoginRewardSystem`, `BotGMEvent` | event simply inactive |
| `pangya_daily_quest` (1 row of 3 achievement typeids) | `ProcGetDailyQuest_New`/`ProcCheckDailyQuest` | `ProcCheckDailyQuest` returns `DATE = 1`. How the server then fills the row is unverified. |
| `pangya_ip_table`, `pangya_mac_table` | `server.cpp:1839/1851` (`CmdListIPBan`, `CmdListMacBan`) | no bans (fine) |
| `pangya_gacha_jp_*` | Gacha JP (`ProcGetGachaJPConfig`) | gacha empty |

Failure is not fatal. `game_server::init_systems()` and `onHeartBeat()` (`game_server.cpp:5060`, `:4300`) call `load()` for every system whose `isLoad()` is false, **on every heartbeat**. An empty table therefore gives a disabled feature and repeated log errors, not a crash. Every typeid in these tables must match the client's IFF files: the servers take item definitions from IFF (`sIff`), not from the DB. The IFF-style catalog tables in the dump (`pangya_item_typelist`, `type_list`, `pangya_parts_list`, `pangya_character_part_padrao`) are **not referenced** by any routine, any C++ code or the PHP site, so you do not need to fill them.

### 6.3 Creating accounts

Use the procedures. Do not insert into `account` directly: `ProcNewUser(id, plain_pass, ip, server_uid)` also creates the required rows in `user_info`, `pangya_player_location`, `pangya_user_equip`, `pangya_last_players_user`, `trofel_stat`, `tutorial`, `pangya_user_macro`, `pangya_attendance_reward`, `pangya_scratchy_prob_sec`, `black_papel_prob_sec`, `pangya_tiki_points`, `pangya_myroom`, `pangya_dolfini_locker`, `pangya_assistente`, `pangya_grand_zodiac_pontos`, `pangya_daily_quest_player`, `pangya_papel_shop_info` (100/100), and two `td_room_data` furniture rows.

```sql
CALL pangya.ProcNewUser('player1', 'secret', '127.0.0.1', 0);          -- hashes for you
CALL pangya.ProcNewUserWithMD5('player2', 'UPPERHEXMD5...', 0, '127.0.0.1', 0); -- pre-hashed, with sex
```

Alternatively, set `[OPTION] CREATEUSER = 1` in the Login Server ini to auto-create unknown IDs on first login (`login_server.cpp:760`). The web registration goes through `ProcMakeUserBetaWithMD5` (`www/pangya/config/seg_cadastro_exec.inc`). The first character is created on the first login by `ProcFirstSet`/`ProcAddCharacter`. GM rights are bits in `account.capability`; the bit values are not documented here (unverified).

---

## 7. Tables by function

The column lists below are abbreviated: they show key and important columns. **Code** means the C++ code queries the table directly (58 tables do). The other tables are reached only through routines or the PHP site, or not at all (marked *unused*: no routine, C++ or PHP reference).

### 7.1 Accounts, auth and session

| Table | Purpose | Key columns |
|---|---|---|
| `account` | Login account (Code) | `UID` PK auto, `ID` unique, `PASSWORD` varchar(33) = upper-case MD5, `NICK`, `IDState`, `BlockTime`, `BlockRegDate`, `Logon`, `LogonCount`, `FIRST_LOGIN`, `FIRST_SET`, `Guild_UID`, `Sex`, `doTutorial`, `capability`, `game_server_id`, `ServerID`, `UserIp`, `change_nick` |
| `contas_beta` | Web registration data ("beta accounts") | `uid`, `nome`, `sobre_nome` (surname), `email`, `key_uniq`, `finish_reg`, `ip_register` |
| `authkey_login` / `authkey_game` | One-time keys for the Login→Game hand-off | `UID`, `AuthKey`, `ServerID`, `valid` |
| `pangya_auth_key` | Per-server key for the Auth Server link (Code) | `Server_UID`, `key`, `valid` |
| `pangya_weblink_key` | Keys for web links opened from the client | `uid`, `key`, `valid` |
| `pangya_player_ip` | Last IP per player and beta block | `uid`, `ip`, `block_beta`, `change_count` |
| `pangya_ip_table` / `pangya_mac_table` | IP (with mask) and MAC ban lists (Code) | `ip`, `mask` / `mac` |
| `pangya_player_location` | Where the player is now | `UID`, `channel`, `lobby`, `room`, `place` |
| `pangya_change_pwd_log`, `pangya_change_nickname_log`, `pangya_rescue_pwd_log` | Web account-change logs | `uid`, dates, `key_uniq` |

### 7.2 Player profile and statistics

| Table | Purpose | Key columns |
|---|---|---|
| `user_info` | Main stats and currencies, 68 columns (Code) | `UID`, `level`, `Xp`, `Pang`, `Cookie`, shot stats (`Tacadas`, `Putt`, `Holein`, `HIO`, `Albatross`, ...), `BestScore0..4`, `MaxPang0..4`, team/ladder stats, medals |
| `trofel_stat` | Trophy counts by grade (`AMA_6_G` ...) | `UID` + 39 counters |
| `pangya_record` | Per-course records (`tipo` = mode) | `UID`, `tipo`, `course`, `best_score`, `best_pang`, `holein`, `assist` |
| `pangya_trofel_especial`, `pangya_trofel_grandprix` | Special and Grand Prix trophies | `item_id`, `UID`, `typeid`, `qntd` |
| `pangya_grandprix_clear` | Grand Prix clear flags (Code) | `uid`, `typeid`, `flag` |
| `pangya_last_players_user` | Last 5 players played with | `UID`, `SEX_n`, `ID_n`, `NICK_n`, `UID_n` |
| `tutorial` | Tutorial progress | `UID`, `Rookie`, `Beginner`, `Advancer` |
| `pangya_assistente` | Assist-mode flag | `UID`, `Assist` |
| `pangya_user_macro` | 10 chat macros | `UID`, `Macro1..10` |
| `pangya_tiki_points` | Tiki points | `UID`, `Tiki_Points` |
| `pangya_grand_zodiac_pontos` | Grand Zodiac points (Code) | `uid`, `pontos` |

### 7.3 Characters, items, equipment, room

| Table | Purpose | Key columns |
|---|---|---|
| `pangya_character_information` | Owned characters (Code) | `item_id` PK, `UID`, `typeid`, `parts_1..24`, `auxparts_1..5`, `CutIn_1..4`, `PCL0..4` (stats), `Mastery`, `default_hair` |
| `pangya_user_equip` | Current loadout (Code) | `UID`, `character_id`, `caddie_id`, `club_id`, `ball_type`, `item_slot_1..10`, `Skin_1..6`, `mascot_id`, `poster_1/2` |
| `td_char_equip_s4` | Parts equipped per character (S4 format) | `UID`, `CHAR_ITEMID`, `ITEMID`, `EQUIP_NUM`, `EQUIP_TYPE`, `USE_YN` |
| `pangya_item_warehouse` | General inventory: clubs, balls, consumables, parts (Code) | `item_id` PK, `UID`, `typeid`, `valid`, `C0..C4` (counters or stats), `EndDate`, `Gift_flag`, `ClubSet_WorkShop_*`, `Mastery_Pts`, `Level`, `Up` |
| `pangya_clubset_enchant` | Club enchant cost and stats | `uid`, `item_id`, `pang`, `c0..c4` |
| `pangya_last_up_clubset`, `pangya_transforme_clubset_temp` | Club upgrade/transform staging | `UID`, `item_id`/`TAQUEIRA_ID` (taqueira = club set), `STATE` |
| `pangya_item_buff` | Timed buffs (EXP/pang boosts) | `uid`, `typeid`, `end_date`, `tipo`, `percent` |
| `tu_ucc` | Self-designed items (UCC) | `UID`, `TYPEID`, `UCCIDX`, `UCC_NAME`, `SKEY`, `STATUS` |
| `pangya_myroom`, `td_room_data` | My Room lock/password and furniture positions | `uid`, `senha`, `public_lock` / `UID`, `TYPEID`, `POS_X/Y/Z/R` |
| `pangya_dolfini_locker`, `pangya_dolfini_locker_item` | Dolfini locker (password, stored pang, items) (Code) | `uid`, `senha` varchar(5), `pang`, `locker` / `uid`, `item_id` |

### 7.4 Caddies, mascots, cards

| Table | Purpose | Key columns |
|---|---|---|
| `pangya_caddie_information` | Owned caddies (Code) | `item_id`, `UID`, `typeid`, `parts_typeid`, `cLevel`, `Exp`, `EndDate`, `RentFlag`, `CheckEnd` |
| `pangya_mascot_info` | Owned mascots | `item_id`, `UID`, `typeid`, `mLevel`, `mExp`, `EndDate`, `Message` |
| `pangya_card` | Owned cards (Code) | `card_itemid`, `UID`, `card_typeid`, `QNTD`, `Slot`, `Efeito` (effect), `USE_YN` |
| `pangya_card_equip` | Cards slotted on a character/caddie | `UID`, `parts_id`, `card_typeid`, `Slot`, `END_DT` |
| `pangya_new_card_pack`, `pangya_new_cards` | **Seed**: card packs (rates N/R/SR/SC) and their cards (Code) | `typeid`, `tipo`, `rate_*` / `typeid`, `probabilidade`, `pack` |
| `pangya_card_rate`, `pangya_cards` | Older card tables, used only by routines | |

### 7.5 Guild

`pangya_guild` (`GUILD_UID`, `GUILD_NAME`, `GUILD_LEADER`, `GUILD_SUB_MASTER`, `GUILD_STATE`, `GUILD_PANG`, `GUILD_POINT`, win/lose/draw, mark and intro images, notice), `pangya_guild_member` (`GUILD_UID`, `MEMBER_UID`, `MEMBER_FLAG` = rank, `GUILD_POINT`), `pangya_guild_atividade_player` (activity), `pangya_guild_update_activity` (pending changes, Code), `pangya_guild_bbs` + `pangya_guild_bbs_res`, `pangya_guild_private_bbs` + `pangya_guild_private_bbs_res`, `pangya_guild_notice` (boards and replies: `SEQ`, `OWNER_UID`, `TITLE`, `TEXT`, `VIEWS`), `pangya_guild_match` (guild vs guild results), `pangya_guild_ranking` (`GUILD_UID`, `RANK`, `LAST_RANK`; rebuilt by `USP_UPDATE_GUILD_RANKING` from the Auth Server), `pangya_guild_intro_img_log`, `pangya_guild_mark_log` (*unused*).

### 7.6 Mail, messages, friends

| Table | Purpose | Key columns |
|---|---|---|
| `pangya_gift_table` | Mail headers (Code) | `Msg_ID`, `UID`, `fromid`, `message`, `giftdate`, `Lida_YN` (read), `valid` |
| `pangya_item_mail` | Attachments per mail (Code) | `Msg_ID`, `item_typeid`, `Quantidade_item`, `Quantidade_Dia` (days), `Pang`, `Cookie`, `valid` |
| `pangya_msg_user` | Offline chat messages (`ProcAddMsgOff`, which charges 10 pang) | `uid`, `uid_from`, `msg`, `valid` |
| `pangya_friend_list` | Friend list (Message Server, Code) | `uid`, `uid_friend`, `apelido` (nickname), `state_flag` |

### 7.7 Shops, Papel Shop, gacha, boxes, memorial

| Table | Purpose |
|---|---|
| `pangya_papel_shop_config` / `_item` / `_coupon` | **Seed**: Papel Shop prices and limits, item pool (`typeid`, `probabilidade`, `numero`, `tipo` = ball colour or rarity, `active`), accepted coupons (Code) |
| `pangya_papel_shop_info` | Per-player daily counter (`current_cnt`, `remain_cnt`, `limit_cnt`) |
| `pangya_papel_shop_rare_win_log` | Rare-win log |
| `black_papel_item`, `black_papel_rate`, `black_papel_prob_sec`, `black_papel_qntd_prob`* | "Big/Black Papel" pool, rates and per-player pity counter |
| `pangya_new_box`, `pangya_new_box_item`, `pangya_new_box_rare_win_log` | **Seed**: mystery boxes and their contents (`raridade` = rarity, `duplicar` = duplicate) |
| `pangya_new_memorial_level`, `_normal_item`, `_lucky_set`, `_rare_win_log` | **Seed**: Memorial Shop (coin gacha) |
| `pangya_memorial_shop_coin_item`, `pangya_memorial_shop_rate` | Older memorial tables used by routines |
| `pangya_gacha_jp_all_item_list`, `pangya_gacha_jp_item_list`, `pangya_gacha_jp_rate`, `pangya_gacha_jp_player_win` | Gacha JP pool and winners (`send_mail`) |
| `pangya_comet_refill` | **Seed**: comet-refill min/max per typeid |
| `pangya_coupon_desconto` | Discount coupons (`VerificaCouponDesconto`) |
| `pangya_scratchy_prob_sec` | Per-player Scratchy pity counter |
| `pangya_personal_shop_log` | Player-to-player shop sales |
| `pangya_cookie_point_log`, `pangya_cookie_point_item_log` | Cookie-point (cash) purchase log |
| `pangya_counter_item` | Counter items (achievement counters, Code). Listed again in 7.10. |

### 7.8 Courses: drops, treasure, cube coins

`pangya_new_course_drop` (global drop rates), `pangya_new_course_drop_item` (per-course drop pool by hole count: `probabilidade_3H/6H/9H/18H`, Code), `pangya_course_reward_treasure` (`COURSE`, `PANGREWARD`), `pangya_treasure_item` (Treasure Hunter pool, Code), `pangya_coin_cube_info` (`course_id`, `active`, Code), `pangya_coin_cube_location` (`course`, `hole`, `tipo`, `x/y/z`, Code), `pangya_cube_coin_location` and `pangya_course_cube_coin_temporada` (older or seasonal cube-coin data used by routines, e.g. `ProcVerificaCourseCubeCoin`). All of these need seed data (§6.2).

### 7.9 Events and rewards

`pangya_attendance_reward` (per-player daily attendance state, `counter`, `last_login`), `pangya_attendance_table_item_reward` (**seed**, reward pool, Code), `pangya_lucia_attendance` + `_reward_log` (Lucia attendance, anti-cheat `try_hacking_count`, `block_type`), `pangya_login_reward` (**seed**, events: `days_to_gift`, `item_typeid`, `end_date`, Code) + `pangya_login_reward_player`, `pangya_golden_time_info` / `pangya_golden_time_item` / `pangya_golden_time_round` (Golden Time windows, rewards, round times, Code), `pangya_bot_gm_event_time` / `pangya_bot_gm_event_reward` (Bot GM event schedule per channel, Code), `pangya_grand_zodiac_times` (Grand Zodiac event windows, Code), `pangya_approach_missions` (**seed**, Approach mode missions), `pangya_ticket_report` + `pangya_ticket_report_dados` (ticket/tournament result reports), `pangya_1st_anniversary` + `pangya_1st_anniversary_player_win_cp`, `pangya_gz_event_2016121600_rare_win` (one-off event logs), `pangya_grandprix_event_config` (*unused*).

### 7.10 Quests and achievements

`pangya_achievement` (`ID_ACHIEVEMENT`, `UID`, `TypeID`, `status`, Code), `pangya_quest` (`id`, `achievement_id`, `uid`, `typeid`, `counter_item_id`, Code), `pangya_counter_item` (`Count_ID`, `UID`, `TypeID`, `Count_Num_Item`, Code), `pangya_quest_clear`, `pangya_daily_quest` (global today's 3 quests + `Reg_Date`, Code), `pangya_daily_quest_player` (`last_quest_accept`, `today_quest`). Legacy achievement tables used only by old routines: `achievement_quest`, `achievement_tipo`, `count_item`, `achievements`, `counter_items`, `quest_items`, `quest_stuffs`.

### 7.11 Ranking

`pangya_rank_atual` (current ranking: `position`, `UID`, `tipo_rank`, `tipo_rank_seq`, `valor`), `pangya_rank_antes` (previous ranking), `pangya_rank_atual_character` (82 columns: equipped character snapshot for the rank viewer), `pangya_rank_config` (**seed 1 row**: `refresh_time_H`, `reg_date`). All are written by `GeraRankAll`.

### 7.12 Server configuration and admin

`pangya_server_list` (heartbeat and server list, §4.3), `pangya_config` (per-server rates and event switches keyed by GUID: `PangRate`, `ExpRate`, `ClubMasteryRate`, `ScratchyPorPointRate`, `PapelShopRareItemRate`, `PapelShopCookieItemRate`, `TreasureRate`, `ChuvaRate` (rain), `MemorialShopRate`, `GrandZodiacEventTime`, `AngelEvent`, `GrandPrixEvent`, `GoldenTimeEvent`, `LoginRewardEvent`, `BotGMEvent`, `SmartCalculator`), `pangya_command` (command queue, §4.4), `pangya_notice_list`, `pangya_ticker_list`, `pangya_shutdown_list`, `pangya_gm_gift_web_log`.

### 7.13 Donations (web site)

`pangya_donation_new` (PagSeguro-style payment records: `code`, `status`, `gross_amount`, `net_amount`, `epin_id`), `pangya_donation_epin`, `pangya_donation_log`, `pangya_donation_item_log`.

### 7.14 Logs (summary)

Rare-win and audit logs: `pangya_new_box_rare_win_log`, `pangya_new_memorial_rare_win_log`, `pangya_papel_shop_rare_win_log`, `pangya_gacha_jp_player_win`, `pangya_personal_shop_log`, `pangya_cookie_point_log`, `pangya_cookie_point_item_log`, `pangya_lucia_attendance_reward_log`, `pangya_gm_gift_web_log`, `pangya_change_*_log`, `pangya_rescue_pwd_log`, `pangya_guild_intro_img_log`. None of them is ever pruned by the code (as far as I found), so plan your own retention.

### 7.15 Legacy, temporary, unused

These 34 tables have **no reference** in any routine, any C++ file or the PHP site: `black_papel_qntd_prob`, `boxitemrarewin`, `char_equip`, `memorial_shop_coin`, `memorial_shop_item`, `memorial_shop_rate`, `pangya_box_item`, `pangya_box_table_item`, `pangya_cardpack`, `pangya_character_information_antiga` ("old"), `pangya_character_part_padrao` ("default parts"), `pangya_course_drop`, `pangya_course_drop_item`, `pangya_gacha`, `pangya_gacha_coin`, `pangya_gacha_items`, `pangya_gacha_user_won`, `pangya_grandprix_event_config`, `pangya_guild_mark_log`, `pangya_item_typelist`, `pangya_memorial_shop_level`, `pangya_memorial_shop_luckyset`, `pangya_memorial_shop_normal_item`, `pangya_memorial_shop_rare_item`, `pangya_parts_list`, `pangya_reward_ssc`, `papelshopitemrarewin`, `scratchy_item`, `scratchy_rare_win`, `scratchy_rate`, `temp_counter_typeid_init`, `temp_tmp`, `temp_typeid`, `type_list`. `pangya_gacha_user_key` is referenced only by old routines. Most of these are also absent from the PostgreSQL dump.

---

## 8. Known quirks

- `NormalManagerDB::add` balances load by `time(nullptr) % N`, so a burst of queries in one second all land on one connection.
- An unknown `DBENGINE` silently becomes `mssql`. For ODBC engines, `DBIP` must be a DSN name.
- `ProcVerifyID` compares `CAST(... AS BINARY(25))`, so login IDs are case-sensitive on MySQL. The PG and MSSQL versions may behave differently (unverified).
- The heartbeat liveness windows (8 s for Game/Message, 11 s for others) depend on the DB server clock (§4.3).
- The Windows and Linux `server.ini` files ship different engines (§2).
- MySQL tables are MyISAM, so multi-statement procedures are not atomic. A crash in the middle of `ProcNewUser` can leave half-created accounts.

---

## 9. Security issues

- **Default credentials everywhere.** Every `server.ini` uses `DBUSER = pangya`, `DBPASS = pangya`. The PHP config uses `pangya`/`pangya` (MySQL) and `pangyaPHP`/`pangya` (MSSQL/PG) in `www/pangya/config/config.inc`. Change them all.
- **Weak password storage.** `account.PASSWORD` is **unsalted MD5** (upper-case hex, `ProcNewUser`: `upper(md5(__pass))`; Login Server: `md5::getHash()`). The MD5 is effectively the password, because the server compares hash to hash. `pangya_dolfini_locker.senha` (varchar 5) and `pangya_myroom.senha` (varchar 15) are stored in **plaintext**.
- **Credentials in logs.** `login_server.cpp` logs `"Senha: " + pass_md5` (the password hash) to the log file in release builds (`CL_ONLY_FILE_LOG`) and to the console in debug builds. `CmdCreateUser` puts the plaintext password into its exception message. `mssql::connect` logs `PWD=` in `_DEBUG` builds.
- **SQL injection risk.** All queries are built by string concatenation. `mysql_db::makeText` escapes only backslashes, not quotes. `mssql::makeText` and `postgresql::makeText` escape nothing. The MySQL connection enables `CLIENT_MULTI_STATEMENTS`, so stacked queries are possible. Login IDs are filtered by a regex that rejects `'` (`login_server.cpp:355`). The plaintext password passed to `ProcNewUser` when `CREATEUSER=1`, and other free-text fields (nicknames, guild board text, macros, mail), depend on validation elsewhere that I did not verify.
- **Over-privileged routines.** The MySQL routines run as `DEFINER=root@localhost` (SQL SECURITY DEFINER by default). Give the application user only `SELECT/INSERT/UPDATE/DELETE/EXECUTE` on `pangya.*`, and do not let it connect as root.
- **Leftover environment details** in the dumps: LAN host `192.168.15.20` (dump header), definer `pangya_extrn@192.168.15.13`, and the MSSQL file paths. They do no harm, but remove them before publishing your own dumps.
