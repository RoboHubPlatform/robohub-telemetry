/**
 * @file telemetry.h
 * @brief Bridges the runtime's event bus to the outside: sinks, decimation, logs.
 *
 * A program publishes a reading on the event bus and is done; it does not know
 * whether anything leaves the board, and it should not have to. This is the
 * other half: it subscribes to the events a program names, and writes each one,
 * unchanged, to whatever channels are registered — a transport for a serial
 * link, a socket for an IP one, several at once.
 *
 * Decimation is the point of the middleman. A control loop publishes at a rate a
 * radio or a 115200-baud UART cannot carry, and the answer is not to slow the
 * loop but to send one reading in N. The rule is per event, so a fast signal is
 * thinned and a slow one passes whole.
 *
 * It carries bytes. A message's payload is the packed struct a rho schema
 * generated; the sink adds framing, not meaning.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef INCLUDE_ROBOHUB_TELEMETRY_H
#define INCLUDE_ROBOHUB_TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"
#include "robohub/core/events.h"
#include "robohub/transport.h"
#include "robohub/net.h"

#if defined(__has_include)
#  if __has_include("autoconf.h")
#    include "autoconf.h"
#  endif
#endif

/** How many sinks one telemetry engine drives at once. */
#ifndef RH_TELEMETRY_MAX_SINKS
#  ifdef CONFIG_RH_TELEMETRY_MAX_SINKS
#    define RH_TELEMETRY_MAX_SINKS CONFIG_RH_TELEMETRY_MAX_SINKS
#  else
#    define RH_TELEMETRY_MAX_SINKS 4
#  endif
#endif

/** How many events may be bridged at once. */
#ifndef RH_TELEMETRY_MAX_RULES
#  ifdef CONFIG_RH_TELEMETRY_MAX_RULES
#    define RH_TELEMETRY_MAX_RULES CONFIG_RH_TELEMETRY_MAX_RULES
#  else
#    define RH_TELEMETRY_MAX_RULES 8
#  endif
#endif

/**
 * The event a structured log is sent as. It is in the framework range, and a
 * gateway that reads it knows the payload is `level tag: message` as text.
 */
#define ROBOHUB_TELEMETRY_LOG_EVENT ((robohub_event_id)0x0010u)

/* -------------------------------------------------------------------------- */
/*  Types                                                                     */
/* -------------------------------------------------------------------------- */

/** What a sink is attached to. */
typedef enum {
    ROBOHUB_SINK_TRANSPORT = 0,
    ROBOHUB_SINK_SOCKET
} RoboHubSinkType;

/**
 * @brief One channel telemetry leaves by.
 *
 * A transport sink writes a COBS frame; a socket sink writes the same header,
 * payload and check without the byte-stuffing a stream needs. Either may be
 * disabled without being removed, which is how a link that is down is quiet
 * without a rule being torn up.
 */
typedef struct {
    RoboHubSinkType type;
    union {
        RoboHubTransport* transport;
        RoboHubSocket     socket;
    } handle;
    rh_bool enabled;
} RoboHubTelemetrySink;

/** Counters, so a link that is dropping everything can say so. */
typedef struct {
    rh_u32 frames_routed;
    rh_u32 frames_dropped;
} RoboHubTelemetryStats;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/** Clear the sinks and the rules. */
RoboHubError robohub_telemetry_init(void);

/** Register a sink. Returns `ROBOHUB_ERR_NO_MEM` when the table is full. */
RoboHubError robohub_telemetry_add_sink(const RoboHubTelemetrySink* sink);

/**
 * @brief Bridge an event to the sinks.
 *
 * `every` is the decimation: 0 or 1 sends every event, N sends one in N. An
 * event already bridged has its rule replaced, so this is safe to call again.
 */
RoboHubError robohub_telemetry_enable_event(robohub_event_id id, rh_u16 every);

/** Stop bridging an event. */
RoboHubError robohub_telemetry_disable_event(robohub_event_id id);

/**
 * @brief Send a log line to every sink, as `ROBOHUB_TELEMETRY_LOG_EVENT`.
 *
 * `tag` and `message` are read during the call. The payload is the text
 * `level tag: message`, which a gateway parses without a schema.
 */
void robohub_telemetry_log(rh_u8 level, const char* tag, const char* message);

/** Copy the counters out. */
void robohub_telemetry_stats(RoboHubTelemetryStats* out);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_ROBOHUB_TELEMETRY_H */
