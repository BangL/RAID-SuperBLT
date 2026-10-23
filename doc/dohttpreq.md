# dohttpreq

```lua
local id = dohttpreq(url, callback [, progress_callback] [, options])
```

Starts an HTTP request on a background thread and returns its id. The callback runs in the game's update loop once the
request is done. The options table can also be passed as the third argument if there is no progress callback.

```lua
callback(body, id, request_info)
progress_callback(id, bytes, total_bytes)
```

`request_info` has these fields:

| Field | Meaning |
| --- | --- |
| `url` | The URL that was requested |
| `statusCode` | The HTTP status code (the last reply code for FTP) |
| `querySucceeded` | `true` if the transfer worked and the status code is below 400 |
| `headers` | The response headers. The names are lower case, and looking up a name in any case works (`headers["Content-Type"]`). If a header is sent more than once, only the first one is kept. |
| `set_cookies` | List with every `Set-Cookie` header of the response, as sent |

Older DLL versions don't know the options: they take a table as the third argument for the progress callback, which then
fails when the request is done. A mod that needs the options should check the DLL version first: `blt.blt_version()` returns
it as a string like `"1.1.1.0"`, to compare with the release that introduced the options.

## Protocols

`http`, `https`, `ftp` and `ftps`. Redirects are followed, but never to plain `ftp`. Responses compressed with gzip or
deflate are decompressed. Connections to a host that has sent `Strict-Transport-Security` are upgraded from `http` to
`https`; the hosts are remembered in `mods/saves/sblt_hsts.txt`.

## Options

All fields are optional.

| Field | Meaning |
| --- | --- |
| `method` | `"GET"`, `"HEAD"`, `"POST"`, `"PUT"`, `"PATCH"`, `"DELETE"` or `"OPTIONS"`. The default is `GET`, or `POST` if there is a `body`. |
| `headers` | Request headers, either `{["Name"] = "value"}`, or a list of `"Name: value"` strings, or both. |
| `body` | The request body as a string. Not allowed with `GET`, `HEAD` and `OPTIONS`. Set the `Content-Type` header yourself. |
| `cookies` | A cookie string like `"name=value; other=value"`, sent with this request only. |
| `cookie_jar` | The name of a cookie jar (1 to 64 letters, digits, `_`, `.` or `-`). All requests that use the same name share their cookies: cookies a response sets are sent along with the following requests to the same site. |

A bad option raises a Lua error. Header names and values must not contain line breaks.

## Cookie jars

Jars only live in memory, they are never written to disk. A request without `cookie_jar` neither sends nor stores
cookies (apart from what `cookies` says). Use a name that no other mod uses, since everyone who knows the name can use
the cookies in it.

```lua
clearhttpcookies(jar_name)
```

Forgets every cookie of the jar, for example to log out.

## Examples

Log in with a token and download something that needs it:

```lua
dohttpreq("https://example.org/api/download", function(body, id, info)
	if info.querySucceeded then
		-- ...
	end
end, {headers = {Authorization = "Bearer " .. token}})
```

Log in with a form, then download with the session cookie:

```lua
local JAR = "my_mod_session"

dohttpreq("https://example.org/login", function(body, id, info)
	if not info.querySucceeded then
		return
	end

	dohttpreq("https://example.org/files/my_mod.zip", function(zip, id, info)
		-- ...
	end, {cookie_jar = JAR})
end, {
	method = "POST",
	body = "user=" .. user .. "&password=" .. password,
	headers = {["Content-Type"] = "application/x-www-form-urlencoded"},
	cookie_jar = JAR,
})
```

Sending JSON:

```lua
dohttpreq(url, callback, {
	body = '{"name": "value"}',
	headers = {["Content-Type"] = "application/json"},
})
```

Uploading files (`multipart/form-data`): there is no special option for it, the body is a plain Lua string that may
contain any bytes (also `\0`), so it can be put together in Lua. This helper does it:

```lua
-- Builds the body of a multipart/form-data request. Every field is {name = "x", value = "..."}, plus filename and
-- content_type for files. Returns the body and the value for the Content-Type header.
local function multipart(fields)
	local function quoted(s)
		return (s:gsub('[%c"]', function(c) return string.format("%%%02X", c:byte()) end))
	end

	-- A boundary that isn't part of any value
	local boundary
	repeat
		boundary = string.format("----SuperBLT%08x%08x", math.random(0, 0x7fffffff), math.random(0, 0x7fffffff))
		local clash = false
		for _, field in ipairs(fields) do
			if field.value:find(boundary, 1, true) then
				clash = true
			end
		end
	until not clash

	local parts = {}
	for _, field in ipairs(fields) do
		local head = '--' .. boundary .. '\r\nContent-Disposition: form-data; name="' .. quoted(field.name) .. '"'
		if field.filename then
			head = head .. '; filename="' .. quoted(field.filename) .. '"\r\nContent-Type: ' .. (field.content_type or "application/octet-stream")
		end
		parts[#parts + 1] = head .. "\r\n\r\n" .. field.value .. "\r\n"
	end
	parts[#parts + 1] = "--" .. boundary .. "--\r\n"

	return table.concat(parts), "multipart/form-data; boundary=" .. boundary
end
```

```lua
local body, content_type = multipart({
	{name = "title", value = "My Mod"},
	{name = "file", filename = "my_mod.zip", content_type = "application/zip", value = zip_data},
})
dohttpreq(url, callback, {body = body, headers = {["Content-Type"] = content_type}})
```
