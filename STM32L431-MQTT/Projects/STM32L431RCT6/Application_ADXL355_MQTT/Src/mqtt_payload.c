/**
  ******************************************************************************
  * @file    mqtt_payload.c
  * @brief   Pure payload helpers (host-testable, no MCU dependency)
  ******************************************************************************
  */
#include "mqtt_payload.h"
#include "mqtt_cfg.h"
#include <stdio.h>
#include <string.h>

static int payload_hex_nibble(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Write the MQTT fixed header: 1 type/flags byte + remaining-length varint.
   Returns its length (out must have room for 5 bytes). */
static int put_fixed_header(char *out, uint8_t type_flags, int remaining)
{
  int n = 0;

  out[n++] = (char)type_flags;
  do
  {
    int k = remaining % 128;

    remaining /= 128;
    if (remaining)
      k |= 0x80;
    out[n++] = (char)k;
  } while (remaining);
  return n;
}

static int put_len16(char *out, int len)
{
  out[0] = (char)(len >> 8);
  out[1] = (char)(len & 0xFF);
  return 2;
}

void payload_fmt_mdeg(char *out, int32_t mdeg)
{
  uint32_t a = (mdeg < 0) ? (uint32_t)(-(int64_t)mdeg) : (uint32_t)mdeg;
  uint32_t whole = a / 1000U;
  uint32_t frac  = a % 1000U;

  if (mdeg < 0)
    *out++ = '-';
  sprintf(out, "%lu.%03lu", (unsigned long)whole, (unsigned long)frac);
}

int payload_build_dp(char *buf, int cap, uint16_t msg_id,
                     int32_t roll_mdeg, int32_t pitch_mdeg)
{
  char rs[16];
  char ps[16];
  int n;

  payload_fmt_mdeg(rs, roll_mdeg);
  payload_fmt_mdeg(ps, pitch_mdeg);

  /* Identifiers are case sensitive and this product's TSL spells them
     "Roll"/"Pitch" (lowercase gets code 2306 "identifier not exist").
     version must be "1.0": code 2407 "invalid version" for 2018-10-31.
     Both measured live on 2026-10-24 with test/post_variant.js. */
  n = snprintf(buf, (size_t)cap,
               "{\"id\":\"%u\",\"version\":\"1.0\",\"params\":"
               "{\"Roll\":{\"value\":%s},\"Pitch\":{\"value\":%s}}}",
               (unsigned)msg_id, rs, ps);
  if (n < 0 || n >= cap)
    return -1;
  return n;
}

void payload_hex_upper(const uint8_t *in, char *out, uint16_t len)
{
  static const char tab[] = "0123456789ABCDEF";
  uint16_t i;

  for (i = 0; i < len; i++)
  {
    out[2 * i]     = tab[(in[i] >> 4) & 0x0FU];
    out[2 * i + 1] = tab[in[i] & 0x0FU];
  }
  out[2 * len] = '\0';
}

int payload_hex_to_bin(const char *hex, uint8_t *out, int cap)
{
  int hl = (int)strlen(hex);
  int n = hl / 2;
  int i;

  if (hl != 2 * n || n > cap)
    return -1;
  for (i = 0; i < n; i++)
  {
    int hi = payload_hex_nibble(hex[2 * i]);
    int lo = payload_hex_nibble(hex[2 * i + 1]);

    if (hi < 0 || lo < 0)
      return -1;
    out[i] = (uint8_t)(hi * 16 + lo);
  }
  return n;
}

int payload_mqtt_connect(char *out, int cap, const char *client_id,
                         const char *username, const char *password)
{
  const char *fld[3] = { client_id, username, password };
  int len[3];
  int var = 10; /* protocol name(6) + level + flags + keepalive(2) */
  int i, n;

  for (i = 0; i < 3; i++)
  {
    len[i] = (int)strlen(fld[i]);
    var += 2 + len[i];
  }
  if (cap < var + 2) /* type byte + 2 length bytes covers any varint here */
    return -1;

  n = put_fixed_header(out, 0x10, var);
  n += put_len16(&out[n], 4);
  memcpy(&out[n], "MQTT", 4U); n += 4;
  out[n++] = 4;            /* protocol level = 3.1.1 */
  out[n++] = (char)0xC2;   /* username + password + clean session */
  n += put_len16(&out[n], MQTT_KEEPALIVE_S);
  for (i = 0; i < 3; i++)
  {
    n += put_len16(&out[n], len[i]);
    memcpy(&out[n], fld[i], (size_t)len[i]);
    n += len[i];
  }
  return n;
}

int payload_mqtt_publish(char *out, int cap, const char *topic,
                         const char *payload, uint16_t pkt_id, int qos)
{
  int tl = (int)strlen(topic);
  int pl = (int)strlen(payload);
  int var = 2 + tl + pl + (qos > 0 ? 2 : 0);
  int n;

  if (cap < var + 3)
    return -1;
  n = put_fixed_header(out, (uint8_t)(0x30 | (qos << 1)), var);
  n += put_len16(&out[n], tl);
  memcpy(&out[n], topic, (size_t)tl); n += tl;
  if (qos > 0)
    n += put_len16(&out[n], pkt_id);
  memcpy(&out[n], payload, (size_t)pl);
  return n + pl;
}

int payload_mqtt_subscribe(char *out, int cap, const char *topic, uint16_t pkt_id)
{
  int tl = (int)strlen(topic);
  int var = 2 + 2 + tl + 1; /* packet id + topic filter + requested qos */
  int n;

  if (cap < var + 3)
    return -1;
  n = put_fixed_header(out, 0x82, var); /* SUBSCRIBE header flags = 0x02 */
  n += put_len16(&out[n], pkt_id);
  n += put_len16(&out[n], tl);
  memcpy(&out[n], topic, (size_t)tl); n += tl;
  out[n++] = 0;                        /* requested QoS 0 */
  return n;
}

int payload_mqtt_ping(char *out, int cap)
{
  if (cap < 2)
    return -1;
  out[0] = (char)0xC0;
  out[1] = 0;
  return 2;
}

int payload_mqtt_packet_len(const uint8_t *buf, int avail)
{
  int mul = 1, len = 0, i = 1;
  unsigned char b;

  if (avail < 2)
    return 0;
  do
  {
    if (i >= avail)
      return 0;            /* length field still incomplete */
    b = buf[i++];
    len += (b & 0x7FU) * mul;
    mul *= 128;
    if (mul > 128 * 128 * 128)
      return -1;           /* more than 4 length bytes: stream is broken */
  } while (b & 0x80);
  return (i + len > avail) ? 0 : i + len;
}
