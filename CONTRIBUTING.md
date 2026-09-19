# Contributing

Changes should preserve the project's deny-by-default design.

- Do not introduce transparent host-to-target UART forwarding.
- Keep target-bound commands exact-match allowlisted.
- Clearly classify commands that modify external/removable media.
- Do not commit proprietary firmware dumps, extracted third-party binaries, credentials, device identifiers or private logs.
- Keep target-specific assumptions documented.

For changes to the command allowlist, explain why the command is needed and what state it can modify.
