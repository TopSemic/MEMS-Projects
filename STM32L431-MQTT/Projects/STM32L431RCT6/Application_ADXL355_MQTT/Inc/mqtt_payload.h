/**
  ******************************************************************************
  * @file    mqtt_payload.h
  * @brief   Pure payload helpers (host-testable, no MCU dependency)
  ******************************************************************************
  */
#ifndef MQTT_PAYLOAD_H
#define MQTT_PAYLOAD_H

#include <stdint.h>

/* Format millidegrees as decimal string, 3 decimals, e.g. 12345 -> "12.345",
   -500 -> "-0.500". out needs at least 12 bytes. */
void payload_fmt_mdeg(char *out, int32_t mdeg);

/* Build the OneNET thing-model data-point JSON:
   {"id":"<msg_id>","version":"1.0","params":{"Roll":{"value":x},"Pitch":{"value":y}}}
   Returns number of bytes written (excluding NUL) or -1 if cap too small. */
int payload_build_dp(char *buf, int cap, uint16_t msg_id,
                     int32_t roll_mdeg, int32_t pitch_mdeg);

/* Convert bytes to uppercase hex string (out needs 2*len+1 bytes). */
void payload_hex_upper(const uint8_t *in, char *out, uint16_t len);

/* Decode hex string (even length, 0-9A-Fa-f) into out; returns byte count
   or -1 on odd length / non-hex character / cap too small. */
int payload_hex_to_bin(const char *hex, uint8_t *out, int cap);

/* Build a MQTT 3.1.1 CONNECT packet: protocol name/level 4, flags 0xC2
   (username + password + clean session), keepalive 120 s, then clientid,
   username, password as 2-byte-length UTF-8 fields.
   Returns the packet length, or -1 if cap is too small. */
int payload_mqtt_connect(char *out, int cap, const char *client_id,
                         const char *username, const char *password);

/* Build a PUBLISH packet. QoS 0 needs no packet id (pass pkt_id 0).
   Returns the packet length, or -1 if cap is too small. */
int payload_mqtt_publish(char *out, int cap, const char *topic,
                         const char *payload, uint16_t pkt_id, int qos);

/* Build a SUBSCRIBE packet (single topic filter, requested QoS 0).
   Returns the packet length, or -1 if cap is too small. */
int payload_mqtt_subscribe(char *out, int cap, const char *topic, uint16_t pkt_id);

/* Build a PINGREQ (2 bytes). Returns 2 or -1. */
int payload_mqtt_ping(char *out, int cap);

/* Length of the whole MQTT packet starting at buf, or 0 when buf/avail does
   not hold it yet, or -1 when the remaining-length varint is broken. */
int payload_mqtt_packet_len(const uint8_t *buf, int avail);

#endif /* MQTT_PAYLOAD_H */
