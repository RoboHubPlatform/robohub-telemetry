/*
 * test_telemetry.c — decimation, multi-sink routing, and log interception.
 *
 * A transport sink is driven into a capture buffer, and the socket side of the
 * net contract is overridden with a capture too, so both channels are observed
 * without a board.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "robohub/telemetry.h"
#include "robohub/core/event.h"
#include "core_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define TEST_EVENT ((robohub_event_id)0x2233u)

/* -------------------------------------------------------------------------- */
/*  Captures                                                                  */
/* -------------------------------------------------------------------------- */

static rh_u8 transport_bytes[1024];
static rh_size transport_len;
static int transport_frames;

static int capture_write(const rh_u8* data, rh_size length, void* context)
{
    (void)context;
    assert(transport_len + length <= sizeof(transport_bytes));
    memcpy(&transport_bytes[transport_len], data, length);
    transport_len += length;
    transport_frames++;
    return 0;
}

/* The socket side of the net contract, strongly, so it replaces the default. */
static rh_u8 socket_bytes[1024];
static rh_size socket_len;
static int socket_frames;

int robohub_socket_send(RoboHubSocket socket, const void* data, rh_size len)
{
    (void)socket;
    assert(socket_len + len <= sizeof(socket_bytes));
    memcpy(&socket_bytes[socket_len], data, len);
    socket_len += len;
    socket_frames++;
    return (int)len;
}

static bool contains(const rh_u8* haystack, rh_size haystack_len, const void* needle, rh_size needle_len)
{
    if (needle_len == 0u || needle_len > haystack_len) {
        return false;
    }
    for (rh_size i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(&haystack[i], needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

int main(void)
{
    assert(robohub_core_event_init() == ROBOHUB_OK);
    assert(robohub_telemetry_init() == ROBOHUB_OK);

    RoboHubTransport link;
    const RoboHubTransportPhy phy = { .write = capture_write, .context = 0 };
    assert(robohub_transport_init(&link, &phy) == ROBOHUB_OK);

    RoboHubTelemetrySink to_transport = {
        .type = ROBOHUB_SINK_TRANSPORT,
        .handle.transport = &link,
        .enabled = RH_TRUE,
    };
    RoboHubTelemetrySink to_socket = {
        .type = ROBOHUB_SINK_SOCKET,
        .handle.socket = (RoboHubSocket)1,
        .enabled = RH_TRUE,
    };
    assert(robohub_telemetry_add_sink(&to_transport) == ROBOHUB_OK);
    assert(robohub_telemetry_add_sink(&to_socket) == ROBOHUB_OK);

    /* One event in three reaches the sinks. */
    assert(robohub_telemetry_enable_event(TEST_EVENT, 3u) == ROBOHUB_OK);

    const rh_u8 payload[] = { 0x11, 0x22, 0x33 };
    for (int i = 0; i < 9; i++) {
        assert(robohub_core_event_publish(TEST_EVENT, payload, (rh_size)sizeof(payload)) == ROBOHUB_OK);
    }
    assert(transport_frames == 3);
    assert(socket_frames == 3);
    assert(contains(transport_bytes, transport_len, payload, sizeof(payload)));
    assert(contains(socket_bytes, socket_len, payload, sizeof(payload)));

    /* A log line goes to every sink under the log event. */
    robohub_telemetry_log(1u, "motor", "overcurrent");
    assert(transport_frames == 4);
    assert(socket_frames == 4);
    assert(contains(socket_bytes, socket_len, "overcurrent", 11u));

    RoboHubTelemetryStats stats;
    robohub_telemetry_stats(&stats);
    assert(stats.frames_routed == 8u); /* four frames, two sinks */
    assert(stats.frames_dropped == 0u);

    /* Disabling the rule stops the bridge. */
    assert(robohub_telemetry_disable_event(TEST_EVENT) == ROBOHUB_OK);
    assert(robohub_core_event_publish(TEST_EVENT, payload, (rh_size)sizeof(payload)) == ROBOHUB_OK);
    assert(transport_frames == 4);

    return 0;
}
