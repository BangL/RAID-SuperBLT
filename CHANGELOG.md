# SuperBLT DLL Changelog

This lists the changes between different versions of the SuperBLT DLL,
the changes for the basemod are listed in their own changelog.
Contributors other than maintainers are listed in parenthesis after specific changes.

## Version 1.2.0.0

- replaced subhook with SafetyHook (and Zydis), original functions are now called through a trampoline instead of removing and re-installing the hook
    - fixes infinite recursion in `blt.forcepcalls` when the `debug` library is unavailable
    - the hooks are never destroyed at process exit, because destroying them hangs the game
- reworked the loader DLLs (taken from the PAYDAY 2 SuperBLT): the original networking DLL is loaded on the first call instead of inside `DllMain`, and the proxied functions jump straight to it. Before, all of them went through one shared variable, so two threads calling different networking functions at the same time could end up in the wrong one. Both DLLs now come from the same code and export all functions of both (with a GNU as version of the assembly for MinGW)
- removed the Lua debugger support and the injection loader, which were disabled anyway
- HTTP requests now verify TLS certificates (revocation checks are skipped)
- HTTP response headers are lower-cased and looked up case-insensitively from Lua (Hoppip)
- HTTP manager no longer keeps a thread object for every request, never blocks process exit on running requests, and progress events can no longer touch a finished request
- ZIP extraction (DLL and updater share one reader now) works from the central directory, so archives with data descriptors are supported; it rejects unsafe paths (zip slip), checks sizes, CRCs and inflate results, and fails on unsupported entries instead of writing broken files
- updater: error pages and site outages are no longer mistaken for a version number (pseudoboard)
- updater: fixed the version comparison (1.0.5.0 counted as newer than 1.1.0.0), downloads are no longer cut off by the 10 s version-check timeout, a failed updater download no longer leaves the updater renamed, and failed extractions are reported
- Lua API: invalid arguments raise a Lua error instead of crashing the game
- the global `pcall` returns `false, message` on errors like the standard one (it still logs them) instead of returning nothing
- `async_io.read` completions no longer read a destroyed buffer, and async IO threads can no longer exit with a task still queued
- scriptdata parser validates offsets, counts and indices, `scriptdata.recode` reports errors to Lua
- the XML tweaker and the asset hook registry are now thread-safe
- a hooked asset that can't be provided (missing file, bundle item or failing Wren loader) is logged as an error and loaded from the game instead of killing the process; invalid hashes, duplicate registrations and `IO.read` of missing files abort the calling Wren fiber instead of the process; a Lua stack deviation in an async IO callback is repaired instead of aborting
- signature search no longer underflows when the game module can't be found, and handles truncated cache files
- native plugins are unloaded again when their initialisation fails
- asset database: files that share their offset in a package (e.g. language variants) got an empty or wrong length because the wrong list was consulted, reading a bundle item without a known length used the wrong size, broken chunks are reported as errors (as a Wren or Lua error instead of crashing) and the load time in the log no longer overflows after a few days of uptime
- data stores handed to the engine: string assets are read from the requested position instead of always from the start, failed reads no longer return a huge size, and `good()` and `close()` work instead of aborting
- Wren: errors in XML functions end the call instead of carrying on with a closed node, a failed `XML.try_parse` no longer crashes, a node can't be attached to its own document, calling into Wren can't leave the Wren lock held on an error, and an import from a shallow call stack no longer reads before the start of the stack
- the list of active Lua states is thread-safe, and `blt.structid` returns the whole 64-bit address instead of its first 8 digits
- `file.DirectoryHash` no longer recurses forever into symlinks that point back into the hashed directory
- `DirectoryExists("")` no longer reads out of bounds, and directory listings no longer leak the find handle on errors
- the log is flushed to disk every 250 ms (and at exit) instead of only on warnings and errors, so a crash no longer eats the end of it; `blt.flush_log` and the flush after warnings no longer add an empty line
- log messages show source paths relative to the repository (`./src/...`) instead of absolute build paths
- removed leftover debug output from the asset loader and unused code (`RemoveFilesAndDirectory`, `IsSymlink`, `FontData`)
- build: C++23 (Visual Studio 2022 17.8 or newer), CMake 3.28 or newer, MinGW release builds are stripped and drop unused code (about 4 MB instead of 22 MB per DLL), CI also runs for changes outside of `src/` and checks out submodules recursively
- updated curl (git tag curl-8_23_0, `HTTP_ONLY` and `CURL_DISABLE_NTLM` are gone upstream, so unused protocols are disabled individually), libpsl (0.23.3), the public suffix list and Zydis (v4.1.1)
- HTTP requests: curl is now built with zlib-ng and decodes gzip and deflate responses, `ftp://` and `ftps://` URLs work (redirects to plain FTP are refused, so https can't be downgraded) and HSTS is on: once a host sent `Strict-Transport-Security`, `http://` URLs for it are upgraded. The HSTS cache is `mods/saves/sblt_hsts.txt`, `modworkshop.net` is preloaded. The unused MIME and form API support is disabled
- `dohttpreq` takes an options table (method, request headers, body, cookies and named cookie jars, so logins and APIs with tokens work from Lua), the response has the list of `Set-Cookie` headers, and `clearhttpcookies(jar)` forgets a jar. See `doc/dohttpreq.md`
- the license texts of the bundled libraries are in `THIRD-PARTY-LICENSES.md`

## Version 1.1.1.0

- updated zlib-ng (git tag v2.3.3)
- included libpsl for curl PSL support (git tag v0.21.5)
- updated curl (git tag v8.18.0)
    - Protocols: http https
    - Features: alt-svc AsynchDNS HSTS HTTPS-proxy IPv6 Largefile PSL SSL SSPI threadsafe
    - Enabled SSL backends: Schannel
- updated wren (latest git commit)
- add 10 second timeout to updater

## Version 1.1.0.1

- hotfix for zlib-ng v2.3.x breaking builds

## Version 1.1.0.0

- updated curl, wren and zlib-ng
- added external updater for dll

## Version 1.0.1.0

- blt.asset_db rewrite for RAID
- updated curl, wren and replaced zlib with zlib-ng

## Version 1.0.0.3

- new signatures for U25

## Version 1.0.0.2

- new signatures for U24.4

## Version 1.0.0.1

- added check to prevent crashing the game after updates

## Version 1.0.0.0

- based on PD2-SuperBLT DLL v3.3.8
- patched for raid, removed linux/w32/pd2/vr codes
- included luajit from leon's repo
- Removed XAudio API
- updated libcurl with enabled http(s) features: alt-svc AsynchDNS HSTS - HTTPS-proxy IPv6 Largefile libz SSL SSPI threadsafe
- updated mxml
- updated subhook
- updated wren
- updated libpng
- updated zlib, and linked curl against zlib
- removed openssl - replaced sha256 impl. with Windows CNG API impl.
