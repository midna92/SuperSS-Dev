# Web side: `www/` (PHP) and `apache-conf/` (Apache 2.4)

## 1. Purpose

The PangYa client talks to more than the C++ game servers. It also opens an embedded browser (an Internet-Explorer-based control; several pages check for `MSIE 7.0` / `Trident` user agents) and does plain HTTP requests for patching and bug reports. The `www/` tree is the PHP implementation of those endpoints, plus a player-facing website. `apache-conf/` is the Apache configuration that exposes it.

Verified by reading the code. Nothing in `Server Lib/**` hard-codes any web URL, so the URLs the client actually calls are baked into the client binary or its patch data. They are not visible in this repo. Everything below about "how the client uses it" is inferred from the PHP side (request parameters, response format) and is marked as such.

## 2. How it fits in the system

```
 PangYa client ──TCP──> Login/Game/Message/Rank servers (C++)
      │
      ├─ HTTP :7273  (patcher + reports + UCC upload)  ─┐
      ├─ HTTPS :447  (in-game web pages, site, GM tools)├─> Apache + PHP (www/pangya) ──> DB (same `pangya` DB as the C++ servers)
      └─ HTTP :80    (nProtect/GameGuard, stadium, donation callbacks: separate hostnames)
```

Shared state with the C++ servers is the database only:

* Account and login data: tables `account`, `user_info` (read in `www/pangya/config/base_classe.inc`).
* **Web key hand-off** (verified): the game server handles packet `0x0FB`, runs `CmdGeraWebKey` (`Server Lib/Game Server/PANGYA_DB/cmd_gera_web_key.cpp`) which calls the stored procedure `ProcGeraWeblinkKey`, and sends the key back in packet `0x1AD` (`packet_func::pacote1AD`, `Server Lib/Game Server/PACKET/packet_func_sv.cpp`). The procedure (see `bk-squema-mysql.sql`) writes a 6-hex-char key into `pangya_weblink_key` (`uid`, `key`, `valid`). The web pages `guild/login.php`, `gachaJP/login.php` and `ext/lucia_attendance/login.php` receive `?ID=<account id>&KID=<key>` and call `CheckWeblink_Key(id, key)`. That procedure returns the `uid` and sets `valid = 0`, so **the key is single-use**. The client therefore never sends a password to those pages.
* Admin commands: `base_c::adm_view()` calls procedures such as `ProcRegisterShutdownServer`, `ProcRegisterReloadSystemServer`, `ProcRegisterNoticeBroadcast`, `ProcRegisterNewRateServer`, `ProcRegisterKickPlayerCommand`. These insert rows into the command table (`pangya_command`) that the Auth Server polls (`Server Lib/Auth Server/PANGYA_DB/cmd_command_info.cpp`, see `docs/auth-server.md`). The web panel is therefore a remote control for the C++ servers.
* The home page's server-status box reads `pangya_server_list` (`UpdateTime` newer than 12 seconds counts as online).

## 3. Layout of `www/`

| Path | What it is |
|---|---|
| `www/pangya/` | Main site and in-game web endpoints (HTTPS vhost docroot, also HTTP vhost docroot). |
| `www/manu/` | Static maintenance page ("Sorry we are in maintenance"). |
| `www/notify-donation/` | PagSeguro/PayPal payment notification receivers (own vhost). |
| `www/nprotect/` | nProtect GameGuard update files and "LogServer3" log endpoints (own vhost). |
| `www/stadium/` | JSON API for the Stadium calculator/macro tool (own vhost, own DB). |

### 3.1 `www/pangya/` in detail

| Path | Role |
|---|---|
| `pangya.php` | Front controller. Builds `base_c` (`config/base_classe.inc`, ~6000 lines) and renders the whole site. Routing by `?link=N`: 1 home, 2 client download (needs login), 3 register, 4 bug report, 5 FAQ, 7 finish registration, 8 donate, 9 password/ID recovery, 10 my donations. Other query flags: `?adm`, `?guild`, `?notice_manager`, `?bug_report_view`, `?pincode_view`. |
| `change_password.php`, `change_nickname.php`, `rescue_password.php`, `changeip.php`, `adm_edit_player.php` | 5-line wrappers that instantiate `base_c` and call `change_password_view()`, `change_nickname_view()`, `rescue_password_view()`, `changeip_view()`, `adm_edit_player()`. |
| `check_nickname.php` | Takes POST `nickname`, converts to SJIS and reports its byte length (`OK` if <= 16, else `TOO LONG`). Mirrors the DB nickname limit. |
| `notice.php`, `notice_view.php` | Notice/update/event list and detail popup (`notice_view.php?id=N`). Data source is `config/notice.dat` (PHP-serialized array) managed by `config/notice_manager.inc`, not the database. Types: all/notice/event/update (`NT_*`). |
| `item_list.php` | Sorted item catalogue rendered from `config/list_item_itens.json` (fields `id`, `_typeid`, `type`, `name`, `icon`, `description`; Japanese item names, English descriptions). `item_list.html` is a static copy. `pangya.dirs` allows `item_list.php` and denies `item_list.html`. |
| `recharge.php` | Redirects to `http://127.0.0.1:8080/pangya.php` (placeholder; hard-coded). |
| `notice2..10.html` | Static notice HTML. |
| `EntryPoint/` | "Entry Point" donation UI used inside the client: `etp.php` (info), `donation_register.php` (register a PagSeguro/PayPal transaction code, 36 chars / 17 or 19 chars), `epin_exchange.php` (exchange a 36-char E-Pin for Cookie Points), `captcha.php` (image captcha). Config in `EntryPoint/config/config.inc` (`NAME_SITE`, `DONATION_TO_CP = 20`, i.e. 20 CP per 1 BRL) and `EntryPoint/config/auth.inc` (payment API credentials, placeholders). |
| `gm_tools/` | GM web tools: `golden_time.php`, `login_reward.php`, `bot_gm_event.php`, `stream_monitor.php`, `block_word_register.php` (e-mail domain/IP block lists). Gate: session login plus `cap & 4` and `cap & 8` both set (`gm_tools/source/gm_tools_base.inc`). They call `ProcInsert/Update/Delete{GoldenTime{Event,Round,Item},LoginRewardEvent,BotGMEvent{Time,Reward}}`. |
| `guild/` | Club (guild) web. `login.php` (weblink login, then `ProcGetPlayerGuildInfo`), `rss.php`, `guild_new/` (create guild, agreement, FAQ, list, ranking, BBS) and `guild_home/` (club home, notices, members, private BBS, edit). Many `ProcGet*/Write*/Delete*Guild*` procedures. |
| `gachaJP/` | Japanese-style gacha ("Papel"/ticket) web game: `login.php` (weblink), `gacha_whats.php`, `gacha_lottery.php`, `lottery.php`, `gacha_result.php`, `list_item.php`, `current/` (current gacha item pages), Flash `.swf` assets, `items.json`. Uses `ProcGetPlayerGachaSystemInfo`, `ProcGetGachaJPConfig`, `ProcRegisterGachaJPPlayerWin`, `ProcUpdateGachaJPTicketPlayer`, `ProcSendGachaJPPlayerItemToMail`. |
| `ext/` | `entrance.php` only accepts the client's IE user agent, otherwise redirects to the home page; it then redirects to `ext/lucia_attendance/login.php`, passing the query string through. `lucia_attendance/` is a daily attendance reward (`ProcGetPlayerLuciaAttendanceInfo`, `ProcRegisterLuciaAttendanceReward`, `ProcFinishLuciaAttendanceReward`, anti-cheat `ProcUpdateTryHackingPlayerLuciaAttendance`, `ProcBlockPlayerLuciaAttendance`). |
| `event/` | `event_13122016.php`: "Grand Zodiac Event" page (`ProcRegisterGrandZodiacEventWebPlayerWin`). |
| `SelfDesign/UploadFiles.php` | UCC (self-design) image upload; see 4.3. |
| `report/`, `post/` | Client crash/bug report receivers; see 4.4. |
| `S4_Patch/` | Client patch distribution; see 4.2. |
| `Translation/` | Static `Read*.html` pages (text shown by the client). |
| `tools/` | Static helper pages and `.rar` downloads (ruler, ball ruler, angle viewer; Portuguese names). |
| `PHPMailer/` | Bundled old PHPMailer (class.phpmailer.php, class.smtp.php, class.pop3.php, translations, tests, examples). Third-party. |
| `img/`, `_Files/`, `report_files/` | Images; uploaded/generated files (`_Files/SelfDesign`, `_Files/guild`); stored client reports. |
| `config/` | PHP include files, data files, DB config. Blocked from the web (see 7). |

## 4. How the client uses these endpoints

Only what the PHP code makes visible. Treat the "client does X" statements as inferences.

### 4.1 Login-adjacent pages and in-game browser
* `pangya.php` accepts POST `id`, `key`, `level` (plus `g-recaptcha-response3` if reCAPTCHA is on). If `level == -1` the key is treated as a plaintext password typed in a browser and is hashed with `strtoupper(md5(...))`; otherwise it is used as-is, which implies the client sends the already-MD5'd password. The values are checked against `account.password` and `user_info` with a prepared statement and stored in `$_SESSION['player_log']`. Validation regexes: id `^[a-zA-Z0-9]{3,16}$`, key `^[0-9A-z@#$%&*!_?=+'\-]{6,16}$`. Passwords in the DB are uppercase hex MD5 (see also `ProcMakeUserBetaWithMD5`).
* Change nickname: POST `change_nickname` + `uid` -> `ProcChangeNickname`. Return codes: 1 ok, -2 not enough CP, -3 less than one month since last change, -4 request already pending. On success it logs the cost with `ProcInsertCPLog(uid, 3, -1, 60, 0)` (type 3 = nickname change, 60 CP).
* Change password: POST `pwd_old`, `pwd_new`, `pwd_new_confirm`, `uid` -> `ProcChangePassword`; sends a notification e-mail via `smtp_base.inc`.
* Rescue password/ID: `rescue_password.php` plus `?link=9`, using `ProcRescuePassword`, `ProcRescueID`, `ProcMakeEmailKey`, `ProcCheckRescuePasswordAndIDKey`; sends e-mail. Registration (`?link=3`, `?link=7`) uses `ProcMakeUserBetaWithMD5`, a block list of e-mail domains (`config/word_ban_register.dat`) and IPs (`config/ip_ban_register.dat`).
* Weblink pages (guild, gacha, Lucia attendance, Grand Zodiac event): `GET ?ID=&KID=` with `strlen(ID) <= 25`, `strlen(KID) <= 8` (matches the `varchar(25)`/`varchar(8)` procedure parameters). The login page stores `$_SESSION['player']` and redirects to the feature page. Note in `gachaJP/login.php`: the success redirect is deliberately relative (a comment says an absolute http link resets the session).
* `Design::checkIE()` in the EntryPoint pages confirms the intended browser is the client's embedded IE.

### 4.2 Patching: `S4_Patch/`
* `S4_Patch/updatelist` (binary, encrypted/compressed; the `updatelist_*` variants are other client builds, e.g. `_JP_973`) is the list the patcher downloads. Patch zips live in `S4_Patch/update/*.zip`.
* `S4_Patch/extracontents.xml` and `extracontents/default/pangya_default.xml` describe themed main-menu button icons (`.tga`), with absolute URLs `http://superss.ga:7273/S4_Patch/extracontents/default/`. These URLs are hard-coded to the original author's host; change them for your own domain.
* Served on the **plain-HTTP** vhost (port 7273): only `updatelist`, `extracontents/*.{png,gif,jpg,tga,xml}` and `update/*.zip`.

### 4.3 UCC / SelfDesign upload
`SelfDesign/UploadFiles.php` takes multipart POST `selfDesignUID`, `selfDesignItemID`, `selfDesignKey`, `selfDesignFolder`, file `selfDesignFileName`. The key is validated by `ProcCheckSecurityKey(uccuid, item_id, key)`; the UCC index is derived from the filename suffix (text between the last `_` and the last `.`) and checked in `TU_UCC`. The file is saved to `_Files/SelfDesign/<folder>/<name>`. The response is a bare number, apparently read by the client: `0` ok, `1` bad key, `3` missing folder, `12` wrong UCCIDX, `13` DB error. The matching game-server request is `game_server::requestUCCWebKey` (`Server Lib/Game Server/GAME/game_server.cpp`). `_Files/` serves only `png/jpg/jpeg` (`_files.dirs`).

### 4.4 Reports
All four endpoints answer `0<br />Success` and write through `config/save_log.inc` into `report_files/` (file names get a timestamp; files with extensions `php*, html, htm, asp, aspx, vbs, js, css` are rejected or renamed to `.txt`).

| Endpoint | POST fields |
|---|---|
| `report/BugReport.php` | `id`, `email`, `content` + files |
| `report/StackLogDmp.php` | `id`, `version`, `packetversion`, `BugsPointLine`, `ModuleName` + dump file |
| `report/UpdateFailLog.php` | `FailCode`, `FailText`, `FailReason`, `Ret1`, `Ret2`, `UpdateVer`, `PatchNum`, `PrePatchNum` |
| `post/BugReport.php` | `code`, `userid`, `packet_ver`, `client_ver`, `comment`; also dumps raw GET/FILES/POST into `report_files/*_log_<time>.txt` |
| `post/CrashReport.php` | `uid`, `nation` + dump |
| `post/CrimeReport.php` | `id`, `type` + screenshot/log |

### 4.5 Notices and item list
The `notice.php` page is an HTML list that opens `notice_view.php?id=N` in a 600x400 popup. GMs edit notices through `?notice_manager` on the main site (stored in `config/notice.dat`). `item_list.php` is a read-only catalogue from JSON. Neither touches the DB.

### 4.6 nProtect GameGuard (`www/nprotect/`)
* `GameGuard/RealServer/*.npz|*.cfg`: GameGuard update package files served statically (`.cfg` and `.npz` only).
* `LogServer3/{bl,bn,el,hl,mr,se,service}.php`: log/report endpoints of the nProtect protocol. They log `$_GET/$_POST/$_FILES` into `LogServer3/log/` (the directory is not in the repo; create it and make it writable) and answer `Complete`/`Success`. `service.php` decrypts `gd` with `config/encrypt_source.inc` and computes a CRC-based answer; `hl.php` decrypts a `|`-separated record. Apache rewrites `/LogServer3/<name>` and `/LogServer3/<name>.do` to `<name>.php`. The separate C++ GG Auth Server (`Server Lib/GG Auth Server`) is a different component.

### 4.7 Stadium API (`www/stadium/`)
`calculadora.php` and `macro.php` are JSON APIs (POST `token`, `type` = `create|update|delete|load|list|copy`, plus `uid`, `permission`, `name`, `raw` base64) for saving calculator/macro definitions. They use their **own** MySQL database `stadium` (`stadium/config/config.inc`: keys `IP_ADDR`, `DB_NAME`, `User`, `pwd`, `PORT` and a shared secret `$GLOBALS['TOKEN']`), tables/procedures `calculators`, `create_calculator`, `update_calculator`, `delete_calculator`, `copy_calculator` (and macro equivalents). The schema for that DB is not in the repo (unverified: `Server Lib/Node/Stadium-Calculator` and `Smart Calculator App` are probably the consumers; see `docs/smart-calculator.md`). Note: `stadium/config/db_manager_singleton.inc` is called as `getInstanceDB()` / `getInstanceParams()` with no arguments, unlike the `pangya/` version.

### 4.8 Donations (`www/notify-donation/` and `config/donation/`)
`resposta_pag.php` (PagSeguro notification), `resposta_pay.php` (PayPal notification) and `update_pag.php` (poll PagSeguro for transaction updates) call the provider API (cURL, `config/cacert.pem`) and register donations in the DB (`ProcInsertDonationNew`, `ProcUpdateDonationNew*`, `ProcRegisterDonationEpin`). Rate: 20 CP per 1 BRL (`DONATION_TO_CP`). The vhost also whitelists `update_pay.php`, but that file does not exist in the repo.

## 5. PHP configuration files and DB settings

### 5.1 `www/pangya/config/config.inc`
Builds `$GLOBALS['DatabaseConfigInstance']` (class `DatabaseConfig`, `config/database_config.inc`) by calling `addConfig(type, server_ip, db_name, user, pass)` once per engine, then selects the engine:

| Call | Meaning |
|---|---|
| `addConfig(_MYSQL_, host, db, user, pass)` | MySQL via `mysqli` (`config/MysqlManager.inc`). Host `127.0.0.1`, port defaults to 3306 (no port argument is passed). DB name, user and password are all simple defaults (`pangya`). |
| `addConfig(_MSSQL_, dsn, db, user, pass)` | MS SQL Server via **ODBC**. The first argument is an **ODBC DSN name** (`pangyaPHP`), passed to `odbc_connect()` (`config/database.inc`, `mssql_odbc_manager.inc`). |
| `addConfig(_PSQL_, dsn, db, user, pass)` | PostgreSQL via ODBC, same pattern (`postgresql_odbc_manager.inc`); a commented alternative DSN `pangyaPHP_pgpi` is present. |
| `$GLOBALS['DatabaseCurrentUsed'] = DatabaseConfig::_MYSQL_` | The engine actually used. Change it to `_MSSQL_` or `_PSQL_` to switch. |

Array keys stored per engine: `IP_ADDR`, `DB_NAME`, `User`, `pwd`. This is different from the C++ `[NORMAL_DB]` keys in `server.ini`; the two must be kept in sync by hand. Every page contains per-engine SQL branches (MSSQL `exec db.Proc ?`, PostgreSQL `select ... from db.Proc(?)` with `_RET_`-style output columns, MySQL `call Proc(?)`), so all three engines are meant to work. MySQL is the default.

`notify-donation/config/config.inc` has the same structure but a different MySQL host string (`"127.0.0.1:3308"`; `mysqli` does not parse `host:port`, so this probably needs a port argument. Unverified) plus `DEBUG_NOTIFY`, `PAG_AUTH_EMAIL/TOKEN`, `PAY_AUTH_EMAIL/TOKEN`, `DONATION_TO_CP`.

### 5.2 Other PHP config keys
| File | Keys / content |
|---|---|
| `config/recaptcha.inc` | `reCAPTCHA::$enable` (default `false`), `$pubKey_V2/$privKey_V2/$pubKey_V3/$privKey_V3` (placeholders "put your key here"). With `enable=false`, all captcha checks pass. Score threshold for login/changenick is 0.3. |
| `config/smtp_base.inc` | `GUSER`, `GPWD` (Gmail account and password, placeholders `#`), PHPMailer over `smtp.gmail.com:587` TLS. The sender address is the placeholder text `#Seu email do remetente#` ("your sender e-mail") in `base_classe.inc` and `first_cadastro_exec.inc`; replace it. There is a monthly SMTP-limit message in the registration flow. |
| `EntryPoint/config/config.inc` | `NAME_SITE`, `DEBUG_ETP`, `DONATION_TO_CP`, platform constants, `BASE_ETP_URL`, `BASE_ETP_DIRECTORY`. |
| `EntryPoint/config/auth.inc` | `PAG_AUTH_EMAIL`, `PAG_AUTH_TOKEN`, `PAY_AUTH_EMAIL`, `PAY_AUTH_TOKEN` (placeholders). **Syntax issue**: the last `define(...)` has no terminating `;`. |
| `ext/source/config.inc`, `gachaJP/source/config.inc`, `gm_tools/source/config.inc`, `event/source/config.inc`, `guild/source/config.inc` | `NAME_SITE` (hard-coded to the original author's domain; set your own), debug level constant `DEBUG_ERROR` (`DT_ONLY_FILE_ERROR`), and `BASE_*_URL` / `BASE_*_DIRECTORY` built from `$_SERVER['REQUEST_SCHEME']`, `NAME_SITE`, `$_SERVER['SERVER_PORT']`, `$_SERVER['DOCUMENT_ROOT']`. gacha adds `PLAY_MODO` (1 or 10 pulls) and `RARITY_TYPE`. |
| `config/notice.dat`, `ip_ban_register.dat`, `word_ban_register.dat` | PHP `serialize()` data (notices; blocked registration IPs; blocked e-mail domains). Must be writable by the web user. |
| `config/list_item_itens.json`, `*/source/items.json`, `ext/lucia_attendance/source/cards.json` | Item data for item list / gacha / attendance. |
| `config/contas.txt` | A serialized account record (id, nickname, MD5 password hash, e-mail, IP). See 8. |

Charset handling: nicknames are stored in SJIS and converted with `mb_convert_encoding($nick, 'UTF-8', 'SJIS')` throughout (62 occurrences). `base_classe.inc` tries a list of encodings (`CP874`, `TIS-620`, `CP932`, `SJIS`, `CP936`, `EUC-KR`, `CP949`, `CP950`, `CP860`) when converting.

## 6. PHP requirements (as visible in code)
* No explicit version check anywhere. Syntax needs at least PHP 7.1 (`public const` in classes). Old code style (PHP 5 era PHPMailer, `(Object)[]` casts, `each`-free) suggests PHP 7.x; PHP 8 is untested (unverified). Notably `strpos`-based helpers in `base.inc` and non-strict array-to-string patterns may warn on 8.x.
* Extensions seen in use: `mysqli` (MySQL), `odbc` (+ system ODBC driver and DSN for MSSQL/PostgreSQL), `mbstring` (`mb_convert_encoding`), `iconv`, `json`, `curl` (donation APIs), `SimpleXML`/`DOM` (`simplexml_load_string`, `DOMDocument`, PagSeguro XML), `gd` (captcha and guild image checks: `EntryPoint/source/captcha.inc`, `guild/source/check_*.inc`), `intl` (`IntlChar`, used in a mostly commented-out snippet), `session`, and file uploads (`file_uploads=On`).
* `session_start()` is called by the site and the weblink pages; the session save path must be writable.
* The code expects `$_SERVER['DOCUMENT_ROOT']` and relative includes to work as shipped, i.e. docroot = `www/pangya/`.

## 7. Apache configuration (`apache-conf/`)

### 7.1 Files
| File | Role |
|---|---|
| `apache2.conf` | Debian-style main config (target `/etc/apache2`). Notable lines: `ServerName ss3.sytes.net`, `Define SRVROOT /etc/apache2`, `Define INSTALL_DIR /media/acrisio/hd-extern` (**author's external disk; change it**), `DocumentRoot "${INSTALL_DIR}/www"` with `Require local`, `Include ports.conf`, `IncludeOptional mods-enabled/*`, `conf-enabled/*.conf`, `sites-enabled/*.conf`, `AccessFileName .htaccess`, `<FilesMatch "^\.ht"> Require all denied`, `KeepAlive On`, `Timeout 300`. `ports.conf` is not in the repo. |
| `sites-available/site-principal-ssl.conf` | Main HTTPS vhost `*:447`, docroot `www/pangya`. |
| `sites-available/static-files.conf` | Plain-HTTP vhost `*:7273`, same docroot. |
| `sites-available/manutenance.conf` | `*:80` for `ss3.sytes.net`, docroot `www/manu/` (maintenance page). |
| `sites-available/nprotect.conf` | `*:80`, `ss5.ddns.net`, docroot `www/nprotect/`. |
| `sites-available/notify-donation.conf` | `*:80`, `ss7.ddns.me`, docroot `www/notify-donation/`. |
| `sites-available/api-stadium.conf` | `*:80`, `api.stadium.com`, docroot `www/stadium/`. |
| `sites-available/000-other-vhosts-deny.conf` | Catch-all default vhost on `*:80` and `*:7273` that denies everything and returns `Unknown error`; docroot `${INSTALL_DIR}/deny/`. |
| `extra/httpd-vhosts.conf` | An older, monolithic variant of the same vhosts (Windows-style `extra/` include). Contains the 7273 and 80 vhosts plus the nprotect/stadium vhosts commented out. Superseded by `sites-available/*`; do not enable both. |
| `ban/blacklist` | A 1-line file (`127.0.0.2 -`). Not referenced by any config in the repo. The `IP -` form looks like a `RewriteMap txt:` or similar deny map (unverified). |
| `pangya/*.dirs` | Per-directory access rules for the HTTPS vhost (see 7.3). |
| `pangya/*.dirs2` | Same idea for the HTTP vhost (see 7.3). |

### 7.2 How vhosts are structured
* **Variables via `Define`.** Each vhost defines its own variables at the top: HTTPS vhost `DOCROOT`, `VHNAME` (`ss3.sytes.net`), `VHPORT` (`447`); HTTP vhost `DOCROOT_HTTP`, `VHNAME_HTTP`, `VHPORT_HTTP` (`7273`); others `DOCROOT_STADIUM`, `DOCROOT_NOTFY_DONA`, `DOCROOT_NPROTECT`. The included `.dirs` files use these (`${DOCROOT}`, `${VHNAME}`, ...), so they only work when included from inside the vhost.
* **Default deny / allow-list.** Every directory starts with `<FilesMatch ".*"> require all denied </FilesMatch>` and then whitelists individual files (`<Files "x.php"> require all granted`) or extensions. The docroot itself is `Require local`. Support directories (`config/`, `source/`, `util/`, `log/`, `PHPMailer/`) are `require all denied`.
* **Error handling.** Unknown paths, 403 and 400 are redirected to `https://${VHNAME}:${VHPORT}/pangya.php` (the home page) on the main vhost, or answered with the string `Unknown error` on the side vhosts. This hides which paths exist.
* **Directory index.** `DirectoryIndex disabled` then `DirectoryIndex pangya.php`, and `AliasMatch ^/$` maps `/` to `pangya.php`.
* **Logs.** Each vhost writes to `${INSTALL_DIR}/logs/*.log` (`ssl_error.log`, `ssl_access.log`, `ssl_request.log`, `pangya_error.log`, `manu_*`, `not_dona_*`, `nprotect_*`, `stadium_*`, `other_vhosts_deny_*`); `TraceEnable off`, `ServerSignature off` everywhere.
* **Port split.** The HTTP vhost on 7273 redirects any non-HTTPS request for the docroot to `https://ss3.sytes.net:447` but includes only `*http.dirs2`, which re-open the few directories the client needs over plain HTTP (see below). Everything else must go over HTTPS on 447.
* **Includes.** The HTTPS vhost does `Include "${SRVROOT}/conf/pangya/pangya.dirs"` inside `<Directory>` and `Include "${SRVROOT}/conf/pangya/*.dirs"` at vhost level (so `pangya.dirs` is included twice). The HTTP vhost does `Include "${SRVROOT}/conf/pangya/*http.dirs2"`. So `apache-conf/pangya/` must be installed as `/etc/apache2/conf/pangya/`.

### 7.3 `.dirs` / `.dirs2` files
`*.dirs` (HTTPS) is the allow-list for each folder; `*_http.dirs2` (HTTP 7273) exist only for six areas.

| File(s) | Directory | Allowed |
|---|---|---|
| `pangya.dirs` | docroot (file level) | everything denied, then granted: `favicon.ico`, `pangya.php`, `notice.php`, `notice_view.php`, `item_list.php`, `change_password.php`, `change_nickname.php`, `check_nickname.php`, `rescue_password.php`, `changeip.php`, `adm_edit_player.php`, `recharge.php`, plus names of files that are not in the repo (`disponibiliza.php`, `event_to_post.php`, `time.php`, `test_smtp.php`, `teste_*.php`, `from_post_array_test.php`); `update_nickname_emoticon.php` is `require local`; `*.html|htm` granted but `item_list.html` denied. |
| `config.dirs` | `config/` | deny all |
| `phpmailer.dirs` | `PHPMailer/` | deny all |
| `img.dirs` | `img/` | `png gif jpg jpeg` |
| `_files.dirs`, `_files_http.dirs2` | `_Files/` | `png jpg jpeg`; `report_files.dirs` locks `report_files/` |
| `entrypoint*.dirs` | `EntryPoint/` | `etp.php donation_register.php epin_exchange.php captcha.php` + static assets; `source/`, `config/`, `font/`, `util/` denied |
| `gm_tools*.dirs` | `gm_tools/` | the 5 tool pages + static assets; `source/` denied |
| `guild*.dirs`, `guild_new*.dirs`, `guild_home*.dirs` | `guild/`, `guild/guild_new/`, `guild/guild_home/` | `login.php rss.php`, listed `Club_*.php` and `club_*.php` pages, static assets; `source/` denied |
| `gachajp*.dirs` | `gachaJP/`, `gachaJP/current/`, `gachaJP/iX2PnGylD42/` | gacha PHP pages, `gacha_p2.swf`, media; the obfuscated folder allows only two named `.swf` files |
| `ext*.dirs`, `lucia_attendance*.dirs` | `ext/`, `ext/lucia_attendance/` | `entrance.php`; `index.php reward.php login.php reward_notice.php` |
| `event*.dirs` | `event/` | `event_13122016.php` + assets |
| `post.dirs`, `post_http.dirs2` | `post/` | `BugReport.php CrashReport.php CrimeReport.php` |
| `report.dirs`, `report_http.dirs2` | `report/` | `BugReport.php StackLogDmp.php UpdateFailLog.php` |
| `selfdesign*.dirs`, `selfdesign_http.dirs2` | `SelfDesign/` | `UploadFiles.php` |
| `s4_patch*.dirs`, `s4_patch_http.dirs2`, `s4_patch_update*.dirs2`, `s4_patch_extracontents*.dirs2` | `S4_Patch/` (+`update/`, `extracontents/`) | `updatelist`; `*.zip`; `png gif jpg tga xml` |
| `translation.dirs`, `translation_http.dirs2` | `Translation/` | `Read.html` |
| `tools.dirs` | `tools/` | static + `.rar` |

Files named in `.dirs` that do not exist in the repo are harmless (the rule just never matches); do not assume those pages exist. Two of the `.dirs` contain apparent typos: `_files.dirs` has the malformed variable `${VHNAME:${VHPORT}` (missing `}`) and redirects to `http://`; `site-principal-ssl.conf` has `RewriteRule %{HTTP_HOST} ^127.0.0.1$` (a RewriteRule where a RewriteCond was intended). Both are likely to fail the config test or have no effect (unverified; run `apache2ctl configtest`).

### 7.4 SSL
Only `site-principal-ssl.conf` is SSL: `SSLEngine on`, `SSLCertificateFile ${SRVROOT}/conf/ssl/server.cert`, `SSLCertificateKeyFile ${SRVROOT}/conf/ssl/private.key`, listening on `*:447` (the stock `Listen 447 https` line is commented in the file; you must add it to `ports.conf`). The rest of the file is the stock Apache `httpd-ssl.conf` comment text (cipher/protocol directives are all commented, so module defaults apply), plus `BrowserMatch "MSIE [2-5]" nokeepalive ssl-unclean-shutdown downgrade-1.0 force-response-1.0` and an `ssl_request.log`. No certificate is provided in the repo. Because the client's embedded IE and old TLS stack may only support old protocols, expect to relax `SSLProtocol` for the client to connect (unverified; depends on the client build).

### 7.5 Ban / extra dirs
* `apache-conf/ban/` holds only the unreferenced `blacklist` (see 7.1). Application-level bans are different: `www/pangya/config/ip_ban_register.dat` and `word_ban_register.dat` (registration), and the `ProcInsertBlockIP/MAC` procedures used by the admin panel.
* `apache-conf/extra/` holds the legacy monolithic vhost file.

## 8. Security issues found (short list)

1. **Default and hard-coded DB credentials** in `www/pangya/config/config.inc`, `www/notify-donation/config/config.inc`, `www/stadium/config/config.inc` (simple defaults, same string for user/password/DB name). Change them and move them out of the docroot.
2. **Secrets as source code**: payment tokens (`EntryPoint/config/auth.inc`, `notify-donation/config/config.inc`), Gmail credentials (`smtp_base.inc`), reCAPTCHA keys, Stadium `TOKEN`. Currently placeholders; real values must not be committed. Stadium token is compared with non-constant-time `!=` and echoed back in the error message (`'Invalid Token: '.$_POST['token']`).
3. **Account data committed**: `www/pangya/config/contas.txt` contains a serialized account record with an MD5 hash and e-mail. Remove it.
4. **Weak password storage**: unsalted MD5 (uppercase hex) both in DB and in the PHP session (`$_SESSION['player_log']['key']` holds the hash), and the client-side flow sends the MD5 itself.
5. **SQL injection**: no injectable concatenations found. Values go through prepared statements (`execPreparedStmt` with typed params). The only interpolated pieces are the configured DB name. Not a full audit of the ~6000-line `base_classe.inc` (unverified).
6. **XSS / unsafe output**: notice bodies are echoed as raw HTML (`notice_view.php`, by design for GMs); some admin pages echo request values directly; `check_nickname.php` uses `htmlspecialchars` correctly. `unserialize()` is used on `notice.dat`, `ip_ban_register.dat` and `contas.txt`; these are server-side files, but keep them unwritable from the web.
7. **Unauthenticated file writes**: the report endpoints and the nProtect `LogServer3` endpoints accept anonymous uploads and write to `report_files/` and `LogServer3/log/` with no size limit or rate limit; `post/BugReport.php` also dumps raw GET/POST/FILES for every request. Risk is disk exhaustion. `UploadFiles.php` writes to `_Files/SelfDesign/<selfDesignFolder>/`: the folder comes from POST **unsanitised** (only the filename goes through `checkFileName`), so path traversal is possible after a valid key. Upload extensions are filtered by `checkType` (denylist, not allowlist).
8. **Weblink keys** are only 6 hex characters (`left(md5(rand()),6)`) with no rate limit on `?ID=&KID=`; single use mitigates, but brute-forcing an account's active key is feasible.
9. **Admin gate** is `capability & 4` and `& 8` from the session; the IP check is commented out (`//&& ($this->ip_addr == '127.0.0.1' ...`). The login endpoint is protected only by the optional reCAPTCHA V3 (disabled by default).
10. **Apache**: `AllowOverride None` and `.ht*` denial are set, but `DOCUMENT_ROOT` use is inconsistent (`DOCUMENT_ROOT."config/"` in `notice_manager.inc` versus `DOCUMENT_ROOT.'/config/'` elsewhere). If your Apache reports the docroot without a trailing slash, notices will look in the wrong path (unverified). The 7273 vhost serves patch data unencrypted by design.

## 9. Step-by-step: set up the web side on Apache on Linux

Derived strictly from the configs. Paths marked "adapt" are the author's values. Debian/Ubuntu layout is assumed because `apache2.conf` uses `/etc/apache2`, `mods-enabled`, `sites-enabled`, `${APACHE_RUN_USER}`.

1. **Install** Apache 2.4 and PHP with the extensions in section 6 (for MySQL: `mysqli`, `mbstring`, `iconv`, `json`, `curl`, `xml`/`simplexml`/`dom`, `gd`, `intl`; plus `odbc` only for MSSQL/PostgreSQL). Use Apache's `mod_php` or equivalent handler (the repo has no `AddHandler`/FPM config; this must come from your PHP install, unverified).
2. **Enable modules** needed by the configs: `rewrite`, `ssl`, `alias`, `authz_core`, `setenvif`, `log_config`, `php`.
3. **Choose the install dir**: copy `www/` to e.g. `/srv/pangya/www`, create `/srv/pangya/logs` and `/srv/pangya/deny` (empty docroot for the deny vhost). Edit `Define INSTALL_DIR` in `apache-conf/apache2.conf` (adapt from `/media/acrisio/hd-extern`). If you keep your distro's `apache2.conf`, instead put `Define INSTALL_DIR ...` and `Define SRVROOT /etc/apache2` in a `conf-available` snippet loaded before the sites, because the vhost files use both variables.
4. **Install the include files**: copy `apache-conf/pangya/*` to `/etc/apache2/conf/pangya/` (the vhosts include `${SRVROOT}/conf/pangya/*.dirs` and `*http.dirs2`).
5. **Ports**: in `ports.conf` add `Listen 80`, `Listen 447` (the SSL vhost) and `Listen 7273` (patch/report vhost).
6. **Certificate**: put a certificate and key at `/etc/apache2/conf/ssl/server.cert` and `/etc/apache2/conf/ssl/private.key` (names from `site-principal-ssl.conf`) or edit those two directives.
7. **Hostnames**: replace `ss3.sytes.net` (main site, `VHNAME`, `VHNAME_HTTP`, `ServerName`, the `RewriteCond`), `ss5.ddns.net` (nProtect), `ss7.ddns.me` (donations), `api.stadium.com` (stadium) with your own names, and set `NAME_SITE` in all `*/source/config.inc` and `EntryPoint/config/config.inc` (currently `superss.ga`). Also fix the hard-coded `http://superss.ga:7273/...` URLs in `www/pangya/S4_Patch/extracontents.xml` and `extracontents/default/*.xml`. The `ServerName` of the maintenance vhost is the same as the main site's; enable only one of `manutenance.conf` and the main vhosts for that name on the same port (they use different ports: 80 vs 447/7273, so they coexist).
8. **Enable sites**: copy `sites-available/*.conf` to `/etc/apache2/sites-available/` and `a2ensite` the ones you need: `site-principal-ssl`, `static-files`, `000-other-vhosts-deny` at minimum; `nprotect`, `notify-donation`, `api-stadium`, `manutenance` optionally. Do not use `extra/httpd-vhosts.conf` in addition.
9. **Database**: load one of `bk-squema-{mysql,mariadb,postgres,mssql}.sql` (the stored procedures `CheckWeblink_Key`, `ProcGeraWeblinkKey`, `ProcChangeNickname`, ... must exist), create a DB user, and edit `www/pangya/config/config.inc` (and `notify-donation/config/config.inc`, `stadium/config/config.inc`). Pick the engine in `$GLOBALS['DatabaseCurrentUsed']`. For MSSQL/PostgreSQL configure an ODBC DSN named `pangyaPHP` (or edit the name).
10. **Writable dirs for the web user** (`APACHE_RUN_USER`, typically `www-data`): `www/pangya/config/` files `notice.dat`, `ip_ban_register.dat`, `word_ban_register.dat`; `www/pangya/report_files/`; `www/pangya/_Files/SelfDesign/`; `www/nprotect/LogServer3/log/` (create it); PHP session directory. Log directories for the notify-donation (`log/`) are referenced but not present; create them if you enable that vhost.
11. **Fill placeholders**: `config/recaptcha.inc` (leave `enable=false` to disable), `config/smtp_base.inc` (`GUSER`, `GPWD`), sender address placeholders in `base_classe.inc` / `first_cadastro_exec.inc`, and the payment auth files if you use donations.
12. **Linux case-sensitivity fix**: `config/smtp_base.inc` does `require_once($_SERVER['DOCUMENT_ROOT']."/phpmailer/class.phpmailer.php")` but the folder is `PHPMailer/`. On Linux, fix the path (or add a symlink `phpmailer -> PHPMailer`) before e-mail features are used.
13. **Test the config**: `apache2ctl configtest` (see the possible typos in 7.3), then `systemctl reload apache2`.
14. **Verify**: `https://<host>:447/` should render the site (`pangya.php`); `http://<host>:7273/S4_Patch/updatelist` should download; `http://<host>:7273/config/` should be denied; `http://<host>:7273/pangya.php` should redirect to the HTTPS port.
15. **Point the client at it**: the URLs the client uses are inside the client (not in this repo; unverified). The paths above (`/S4_Patch/updatelist`, `/report/*`, `/post/*`, `/SelfDesign/UploadFiles.php`, `/guild/login.php`, `/gachaJP/login.php`, `/ext/entrance.php`, `/EntryPoint/etp.php`, `/pangya.php`) are what the web side serves.

## 10. Known quirks and gotchas
* Everything is configured for the original author's hosts (`ss3.sytes.net`, `ss5.ddns.net`, `ss7.ddns.me`, `superss.ga`, `api.stadium.com`) and disk (`/media/acrisio/hd-extern`).
* Port 447 and 7273 are non-standard; the redirect target in `static-files.conf` is hard-coded to `:447`.
* `www/pangya/.htaccess` is almost entirely commented out; the Apache `AllowOverride None` means `.htaccess` files are ignored anyway (the `.dirs` files carry the rules).
* Several `*/source/db_manager_singleton_deleted.inc` files are leftovers of a refactor (they have been replaced by `config/db_manager_singleton.inc`, which pages include through `$_SERVER['DOCUMENT_ROOT'].'/config/...'`).
* Many UI strings are Portuguese (menus: HOME, CADASTRO, DOWNLOAD CLIENT, FAQ, DOAR = donate); level names are English. The rank/level table in `base_classe.inc` maps level 0..70+ to names (Rookie F ... Infinit Legend I).
* The in-game pages target an old IE engine; modern browsers are only used for the plain site.
