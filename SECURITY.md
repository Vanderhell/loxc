# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.2.x   | Yes       |

## Reporting a Vulnerability

Report security issues privately by email or through a private GitHub security
advisory.

- Email: Vanderhell@gmail.com
- GitHub Security Advisories: https://github.com/Vanderhell/loxc/security/advisories

Please include:
- a clear description of the issue
- affected files or commands
- steps to reproduce
- any proof of concept or logs

Parsing and validation code is expected to reject truncated and malformed
`.loxc` / `.loxctab` inputs; report any case where invalid data is accepted or
causes a crash.

We aim to respond within 7 days.
