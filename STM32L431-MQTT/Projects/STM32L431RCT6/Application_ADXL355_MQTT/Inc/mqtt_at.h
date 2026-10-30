/**
  ******************************************************************************
  * @file    mqtt_at.h
  * @brief   DIY MQTT 3.1.1 client over the ML307R AT TCP/IP stack (AT+MIP*)
  ******************************************************************************
  */
#ifndef MQTT_AT_H
#define MQTT_AT_H

#include <stdint.h>

/* Init the AT UART + optional power control, sync "AT" until OK.
   Returns 0 on success. Prints progress on the debug UART. */
int mqtt_at_init(void);

/* Open the TCP socket and the MQTT session on top of it (CONNECT -> CONNACK),
   then SUBSCRIBE when MQTT_SUB_TOPIC is non-empty. Returns 0 on success. */
int mqtt_at_open(void);

/* Publish one ASCII payload as a QoS 0 PUBLISH packet.
   Returns 0 on success, negative on failure. */
int mqtt_at_publish(const char *topic, const char *payload);

/* 1 while the MQTT session is alive. */
int mqtt_at_connected(void);

/* Decode inbound MQTT packets and send PINGREQ when due; call from the main
   loop. A "disconn" URC clears mqtt_at_connected(). */
void mqtt_at_poll(void);

/* Close the MQTT connection. */
void mqtt_at_close(void);

/* Downlink message callback (override by implementing it). */
void mqtt_at_on_message(const char *line);

#endif /* MQTT_AT_H */
