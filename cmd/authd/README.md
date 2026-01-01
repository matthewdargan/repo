# authd

Session authentication daemon for nginx's `auth_request` directive.

## Usage

```sh
authd [options]
```

**Options:**

- `--auth-user=<user>` - Username for authentication (required)
- `--port=<n>` - HTTP listen port (default: 8080)

**Environment:**

- `AUTH_PASSWORD` - Password for authentication (required)

**Endpoints:**

- `GET /auth` - Validate session (200 if valid, 401 if not)
- `POST /login` - Create session (form: username, password, redirect)
- `POST /logout` - Destroy session

## Examples

Start authd on default port:

```sh
AUTH_PASSWORD=secret authd --auth-user=admin
```

Start on custom port:

```sh
AUTH_PASSWORD=secret authd --auth-user=family --port=9000
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
