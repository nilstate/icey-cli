# Changelog

## Unreleased

## 0.1.3 - 2026-04-14

- Bumped the pinned `icey` dependency release from `2.4.4` to `2.4.5`
- Hardened release packaging so `ICEY_SOURCE_DIR` no longer silently drifts from the pinned `icey` tag when release metadata is available

## 0.1.2 - 2026-04-06

- Hardened `icey-server` as a standalone product repo on top of `icey`
- Added runtime intelligence event plumbing for `vision` and `speech`
- Added browser smoke coverage and release-check scaffolding
- Added operator-facing health and status endpoints
