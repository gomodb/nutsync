# Security policy

## Supported versions

Security fixes are provided for the latest release on the `main` branch.

## Reporting a vulnerability

Please report security issues privately through GitHub's
[security advisory](https://github.com/gomodb/nutsync/security/advisories/new)
feature. Do not open a public issue for a vulnerability.

## Security model

- WebDAV credentials are sent with HTTP Basic authentication only over HTTPS.
- Cleartext HTTP is accepted only for loopback hosts (`localhost`, `127.0.0.1`,
  `::1`) and only when the user explicitly configures such a pair.
- Cross-origin redirects are not followed, preventing the `Authorization` header
  from being forwarded to another scheme, host, or port.
- TLS certificate verification is always enabled. An additional PEM CA bundle
  can be configured, but certificate errors cannot be ignored.
- Passwords are stored in KWallet / Secret Service through QtKeychain. They are
  never written to `config.json`.
- The configuration file is written with owner-only permissions.
- Remote paths containing `..` are rejected, and local symlinks/special files
  are skipped during synchronization.

## Known limitations

- Only Basic authentication is implemented; Digest and OAuth2 are not supported.
- Proxy passwords are stored in the system keychain, but there is no per-proxy
  bypass list or SOCKS support.
- WebDAV request retries are intended for transient network/server failures.
  Permanent HTTP failures such as 401, 403, 404, 409, 412, 423, 507 are not
  retried; network errors, 408, 425, 429, and 5xx are retryable.
