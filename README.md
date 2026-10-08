# robohub-telemetry

Binds the runtime's event bus to the outside. A program publishes a reading and
is done; telemetry subscribes to the events it names, thins them, and writes each
one to whatever channels are registered.

```
robohub-core        synchronous event bus
      │  events & logs
robohub-telemetry   filter · decimator · router
      ├── robohub-transport   COBS frames over UART / USB CDC / RS-485
      └── robohub-net         length-framed packets over TCP / UDP
```

## Responsibility

- **Automatic bridging.** A driver publishes `EVENT_MOTOR_TELEMETRY` and nothing
  else; telemetry is what puts it on the wire. No driver knows about a socket.
- **Decimation.** A 1 kHz control loop overflows a 115200 baud link. Each event
  is bridged with its own rate — `robohub_telemetry_enable_event(id, N)` sends
  one in N — so a fast signal is thinned and a slow one passes whole.
- **Multi-sink routing.** The same frame goes to every enabled sink at once:
  raw binary over USB CDC for the edge controller and a thinned copy over Wi-Fi,
  from one publish.
- **Log interception.** `robohub_telemetry_log` sends `level tag: message` to the
  same sinks, under the log event, so a console and a cloud dashboard see the
  same line.

## API

```c
robohub_telemetry_init();
robohub_telemetry_add_sink(&(RoboHubTelemetrySink){
    .type = ROBOHUB_SINK_TRANSPORT, .handle.transport = &usb, .enabled = true });
robohub_telemetry_enable_event(EVENT_MOTOR_TELEMETRY, 1);   /* every event */
robohub_telemetry_enable_event(EVENT_IMU, 10);              /* one in ten */
robohub_telemetry_log(2, "motor", "overcurrent");
```

It carries bytes. The payload is the packed struct a rho schema generated; the
sink adds framing, not meaning.

## Counters

`robohub_telemetry_stats` reports `frames_routed` and `frames_dropped`, so a link
that is failing says so rather than going quiet.

## License

MIT. See `LICENSE`.
