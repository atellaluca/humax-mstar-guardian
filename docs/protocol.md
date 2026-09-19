# Host / guardian protocol

The host sends newline-terminated ASCII control messages over the ESP32 USB serial connection.

## Local commands

```text
@ON
@OFF
@ARM
@DISARM
@STATUS
@CR
@EXEC <exact allowlisted target command>
@HELP
```

Only `@EXEC` can request a command to be sent to the target. The ESP32 performs the final exact-match allowlist check.

## Representative telemetry

```text
@@GUARDIAN:1.0.0:READY
@@POWER:OFF
@@HUNTER:ARMED
@@HUNTER:TX:CR
@@PATTERN:CHANGELIST
@@CONSOLE:READY:k5tn#
@@EXEC:TX:INSPECT:version
@@EXEC:BLOCKED:NOT_WHITELISTED
```

The target UART output is otherwise copied from the ESP32 to the host for logging and inspection.

## Security boundary

The host's displayed command list is informational only. Editing the Python script does not add target capabilities: a new command also has to be deliberately added to the ESP32's compiled allowlist.
