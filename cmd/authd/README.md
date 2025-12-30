# authd

Session authentication daemon for nginx's `auth_request` directive. Manages session cookies for authenticated access.

## Usage

```sh
authd --auth-user=<user> --auth-password=<pass> [--port=<n>]
```

**Options:**

- `--auth-user=<user>` - Username for authentication (required)
- `--auth-password=<pass>` - Password for authentication (required)
- `--port=<n>` - HTTP listen port (default: 8080)

**Endpoints:**

- `GET /auth` - Validate session (returns 200 if valid, 401 if not)
- `POST /login` - Create session (form fields: username, password, redirect)
- `POST /logout` - Destroy session

## Examples

Start authd on default port:

```sh
authd --auth-user=admin --auth-password=secret
```

Start on custom port:

```sh
authd --auth-user=family --auth-password=pass123 --port=9000
```

## Integration

Designed for nginx's `auth_request` directive. nginx serves HTML/files and proxies auth requests to authd:

```nginx
location /private/ {
    auth_request /auth;
    # ... serve files
}

location = /auth {
    internal;
    proxy_pass http://localhost:8080/auth;
    proxy_pass_request_body off;
    proxy_set_header Content-Length "";
}

location /login {
    proxy_pass http://localhost:8080/login;
}

location /logout {
    proxy_pass http://localhost:8080/logout;
}
```

## Session Details

- 7-day session duration
- HttpOnly cookies with SameSite=Lax
- 64-character cryptographically random session IDs
- Automatic expiry cleanup
