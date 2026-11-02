# Telemetry

RoboHub module.

**Version:** 0.1.0

## Network devices

A telemetry sink is a byte stream. An IP device — a Shelly plug, for example —
uses a network client instead: its integration publishes over HTTP RPC or MQTT
and, behind a relay, posts the encoded frame to the gateway's
`/api/v1/frames`. The transport capabilities include `http` and `mqtt`.
