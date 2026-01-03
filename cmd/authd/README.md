# authd

Session authentication daemon for nginx's `auth_request` directive.

## Usage

```sh
authd --auth-user=<user>
```

**Environment:**

- `AUTH_PASSWORD` - Password for authentication

**Endpoints:**

- `GET /auth` - Validate session (200 if valid, 401 if not)
- `GET /login` - Serve HTML login form
- `POST /login` - Create session (form: username, password, redirect)
- `POST /logout` - Destroy session

## Examples

Start authd:

```sh
AUTH_PASSWORD=secret authd --auth-user=family
```

Nginx integration:

```nginx
# auth.example.com
location = /validate {
    internal;
    proxy_pass http://127.0.0.1:8080/auth;
    proxy_pass_request_body off;
    proxy_set_header Content-Length "";
    proxy_set_header X-Forwarded-Host $http_host;
}

location = /login {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header X-Forwarded-Host $http_host;
}

# files.example.com
location / {
    auth_request /validate;
    error_page 401 = @auth_error;
}

location @auth_error {
    return 302 https://auth.example.com/login?redirect=https://$http_host$request_uri;
}
```
