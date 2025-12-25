# httpproxy

Static file server with TLS termination and automatic ACME certificate provisioning.

## Usage

```sh
httpproxy --file-root=<path> [options]
```

**Options:**

- `--file-root=<path>` - Directory to serve files from (required)
- `--port=<n>` - HTTPS listen port (default: 8080, or 443 when ACME enabled)
- `--threads=<n>` - Number of worker threads (default: max(4, cores/4))
- `--cert=<path>` - TLS certificate path (default: /var/lib/httpproxy/cert.pem when ACME enabled)
- `--key=<path>` - TLS private key path (default: /var/lib/httpproxy/key.pem when ACME enabled)
- `--acme-domain=<domain>` - Enable ACME for domain (listens on ports 80 and 443)
- `--acme-account-key=<path>` - ACME account key path (default: /var/lib/httpproxy/acme-account.key)
- `--acme-directory=<url>` - ACME directory URL (default: Let's Encrypt production)

## Examples

Serve files with ACME (production):

```sh
httpproxy --file-root=/var/www --acme-domain=dargs.dev
```

Serve files with manual certificate:

```sh
httpproxy --file-root=/var/www \
  --cert=/etc/ssl/cert.pem \
  --key=/etc/ssl/key.pem
```

ACME with Let's Encrypt staging (for testing):

```sh
httpproxy --file-root=/var/www \
  --acme-domain=dargs.dev \
  --acme-directory=https://acme-staging-v02.api.letsencrypt.org/directory
```

Custom worker threads:

```sh
httpproxy --file-root=/var/www --threads=16
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
