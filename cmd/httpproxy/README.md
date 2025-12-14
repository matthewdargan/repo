# httpproxy

HTTP reverse proxy with TLS termination and automatic ACME certificate provisioning.

## Usage

```sh
httpproxy [options]
```

**Options:**

- `--port=<n>` - HTTPS listen port (default: 8080, or 443 when ACME enabled)
- `--threads=<n>` - Number of worker threads (default: max(4, cores/4))
- `--cert=<path>` - TLS certificate path (manual TLS configuration)
- `--key=<path>` - TLS private key path (manual TLS configuration)
- `--acme-domain=<domain>` - Enable ACME for domain (listens on ports 80 and 443)
- `--acme-cert=<path>` - ACME certificate path (default: /var/lib/httpproxy/cert.pem)
- `--acme-key=<path>` - ACME private key path (default: /var/lib/httpproxy/key.pem)
- `--acme-account-key=<path>` - ACME account key path (default: /var/lib/httpproxy/acme-account.key)
- `--acme-directory=<url>` - ACME directory URL (default: Let's Encrypt production)

## Examples

Basic HTTP proxy on port 8080:

```sh
httpproxy
```

HTTPS proxy with manual certificate:

```sh
httpproxy --cert /etc/ssl/cert.pem --key /etc/ssl/key.pem
```

HTTPS proxy with automatic ACME certificates (listens on ports 80 and 443):

```sh
httpproxy --acme-domain dargs.dev
```

ACME with custom certificate paths:

```sh
httpproxy --port 443 \
  --acme-domain dargs.dev \
  --acme-cert /etc/httpproxy/cert.pem \
  --acme-key /etc/httpproxy/key.pem \
  --acme-account-key /etc/httpproxy/account.key
```

ACME with Let's Encrypt staging (for testing):

```sh
httpproxy --port 443 \
  --acme-domain dargs.dev \
  --acme-directory https://acme-staging-v02.api.letsencrypt.org/directory
```

Custom worker threads:

```sh
httpproxy --threads 16
```

## ACME Certificate Provisioning

When `--acme-domain` is specified, httpproxy automatically:

1. Generates or loads an ACME account key
2. Creates a certificate order with Let's Encrypt
3. Handles http-01 challenge validation at `/.well-known/acme-challenge/`
4. Downloads and installs the certificate
5. Reloads the TLS context with the new certificate
6. Monitors certificate expiration daily
7. Automatically renews certificates 30 days before expiry

The first certificate provisioning happens on startup. Subsequent renewals happen automatically in the background.

## Backend Configuration

Currently hard-coded to proxy all requests to `127.0.0.1:8000`. Backend configuration will be made configurable in a future version.
