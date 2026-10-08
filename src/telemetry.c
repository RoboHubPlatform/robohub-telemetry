/*
 * telemetry.c — subscribes to the events a program names and writes them out.
 *
 * One handler is subscribed per bridged event, and its `user` is the rule it
 * belongs to, so the decimation counter is per event and there is no table to
 * search on the hot path. A frame goes to every enabled sink; a sink that fails
 * is counted and the others still get theirs.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "robohub/telemetry.h"
#include "robohub/core/event.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    robohub_event_id id;
    rh_u16 every;
    rh_u16 count;
    rh_bool active;
} rule;

static RoboHubTelemetrySink sinks[RH_TELEMETRY_MAX_SINKS];
static rh_u32 sink_count;
static rule rules[RH_TELEMETRY_MAX_RULES];
static RoboHubTelemetryStats stats;

/* The socket side of a frame is the transport's, without the byte-stuffing a
 * stream needs: a socket is already delimited by the length in the header. */
static rh_u16 fletcher16(const rh_u8* data, rh_size length)
{
    rh_u16 sum1 = 0u;
    rh_u16 sum2 = 0u;
    for (rh_size i = 0; i < length; i++) {
        sum1 = (rh_u16)((sum1 + data[i]) % 255u);
        sum2 = (rh_u16)((sum2 + sum1) % 255u);
    }
    return (rh_u16)((sum2 << 8) | sum1);
}

static void put_u16(rh_u8* out, rh_u16 value)
{
    out[0] = (rh_u8)(value & 0xFFu);
    out[1] = (rh_u8)((value >> 8) & 0xFFu);
}

/* One frame to one sink, by that sink's kind. */
static void route_to(const RoboHubTelemetrySink* sink,
                     robohub_event_id id,
                     const void* payload,
                     rh_u16 length)
{
    if (sink->type == ROBOHUB_SINK_TRANSPORT) {
        if (sink->handle.transport == 0) {
            stats.frames_dropped++;
            return;
        }
        if (robohub_transport_send(sink->handle.transport, id, 0u, payload, length) == ROBOHUB_OK) {
            stats.frames_routed++;
        } else {
            stats.frames_dropped++;
        }
        return;
    }

    if (sink->handle.socket == 0) {
        stats.frames_dropped++;
        return;
    }
    if ((rh_size)RH_TRANSPORT_HEADER_SIZE + length + RH_TRANSPORT_CRC_SIZE > RH_TRANSPORT_MAX_FRAME_SIZE) {
        stats.frames_dropped++;
        return;
    }
    rh_u8 frame[RH_TRANSPORT_MAX_FRAME_SIZE];
    put_u16(&frame[0], (rh_u16)id);
    put_u16(&frame[2], 0u);
    put_u16(&frame[4], length);
    if (length > 0u && payload != 0) {
        memcpy(&frame[RH_TRANSPORT_HEADER_SIZE], payload, length);
    }
    rh_size body = (rh_size)RH_TRANSPORT_HEADER_SIZE + length;
    put_u16(&frame[body], fletcher16(frame, body));
    body += RH_TRANSPORT_CRC_SIZE;

    if (robohub_socket_send(sink->handle.socket, frame, body) == (int)body) {
        stats.frames_routed++;
    } else {
        stats.frames_dropped++;
    }
}

static void route(robohub_event_id id, const void* payload, rh_u16 length)
{
    for (rh_u32 i = 0; i < sink_count; i++) {
        if (sinks[i].enabled) {
            route_to(&sinks[i], id, payload, length);
        }
    }
}

/* The bus callback. Its `user` is the rule, which is what carries the
 * decimation counter without a lookup. */
static RoboHubError on_event(robohub_event_id id, const void* payload, rh_size size, void* user)
{
    rule* entry = (rule*)user;
    if (entry->every <= 1u) {
        route(id, payload, (rh_u16)size);
        return ROBOHUB_OK;
    }
    entry->count++;
    if (entry->count >= entry->every) {
        entry->count = 0u;
        route(id, payload, (rh_u16)size);
    }
    return ROBOHUB_OK;
}

RoboHubError robohub_telemetry_init(void)
{
    for (rh_u32 i = 0; i < RH_TELEMETRY_MAX_RULES; i++) {
        if (rules[i].active) {
            (void)robohub_core_event_unsubscribe(rules[i].id, on_event, &rules[i]);
        }
    }
    memset(sinks, 0, sizeof(sinks));
    memset(rules, 0, sizeof(rules));
    memset(&stats, 0, sizeof(stats));
    sink_count = 0u;
    return ROBOHUB_OK;
}

RoboHubError robohub_telemetry_add_sink(const RoboHubTelemetrySink* sink)
{
    if (sink == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (sink->type == ROBOHUB_SINK_TRANSPORT && sink->handle.transport == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (sink_count >= RH_TELEMETRY_MAX_SINKS) {
        return ROBOHUB_ERR_NO_MEM;
    }
    sinks[sink_count++] = *sink;
    return ROBOHUB_OK;
}

RoboHubError robohub_telemetry_enable_event(robohub_event_id id, rh_u16 every)
{
    if (id == RH_EVENT_NONE) {
        return ROBOHUB_ERR_INVALID_ARG;
    }

    for (rh_u32 i = 0; i < RH_TELEMETRY_MAX_RULES; i++) {
        if (rules[i].active && rules[i].id == id) {
            rules[i].every = every;
            rules[i].count = 0u;
            return ROBOHUB_OK;
        }
    }
    for (rh_u32 i = 0; i < RH_TELEMETRY_MAX_RULES; i++) {
        if (!rules[i].active) {
            rules[i].id = id;
            rules[i].every = every;
            rules[i].count = 0u;
            rules[i].active = RH_TRUE;
            RoboHubError err = robohub_core_event_subscribe(id, on_event, &rules[i]);
            if (ROBOHUB_FAILED(err)) {
                rules[i].active = RH_FALSE;
                return err;
            }
            return ROBOHUB_OK;
        }
    }
    return ROBOHUB_ERR_NO_MEM;
}

RoboHubError robohub_telemetry_disable_event(robohub_event_id id)
{
    for (rh_u32 i = 0; i < RH_TELEMETRY_MAX_RULES; i++) {
        if (rules[i].active && rules[i].id == id) {
            (void)robohub_core_event_unsubscribe(id, on_event, &rules[i]);
            rules[i].active = RH_FALSE;
            return ROBOHUB_OK;
        }
    }
    return ROBOHUB_OK;
}

void robohub_telemetry_log(rh_u8 level, const char* tag, const char* message)
{
    char text[160];
    int length = snprintf(text, sizeof(text), "%u %s: %s",
                          (unsigned)level,
                          (tag != 0) ? tag : "",
                          (message != 0) ? message : "");
    if (length <= 0) {
        stats.frames_dropped++;
        return;
    }
    if ((rh_size)length >= sizeof(text)) {
        length = (int)sizeof(text) - 1; /* snprintf truncated it; send what fits */
    }
    route(ROBOHUB_TELEMETRY_LOG_EVENT, text, (rh_u16)length);
}

void robohub_telemetry_stats(RoboHubTelemetryStats* out)
{
    if (out != 0) {
        *out = stats;
    }
}
