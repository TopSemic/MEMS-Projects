/**
  ******************************************************************************
  * @file    mqtt_at.c
  * @brief   DIY MQTT 3.1.1 client on top of the ML307R AT TCP/IP stack
  *
  * The MQTT packets are built by mqtt_payload.c and shipped as HEX strings in
  * AT+MIPSEND; incoming bytes arrive as +MIPURC: "rtcp",<id>,<len>,<hex> and
  * are reassembled here. The module's own AT+MQTT* interpreter is not used -
  * it never accepted the official OneNET token (connect hung in state 1),
  * while this raw path reached CONNACK rc=0 on hardware.
  ******************************************************************************
  */
#include "main.h"
#include "mqtt_at.h"
#include "mqtt_cfg.h"
#include "mqtt_payload.h"
#include <string.h>
#include <stdio.h>

#define AT_RING_SIZE   1024U
/* 1024: raw-MQTT probe sends an AT+MIPSEND line with a ~500-char hex string */
#define AT_LINE_MAX    1024U

static volatile uint8_t  s_ring[AT_RING_SIZE];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
static volatile uint16_t s_ovr;
static volatile uint32_t s_rx_count;

static uint8_t  s_connected;
static char     s_line[AT_LINE_MAX];
static int      s_verbose;   /* echo every parsed AT line to debug printf */

/* ------------------------- low level UART ---------------------------------- */

void USART3_IRQHandler(void)
{
  if (LL_USART_IsActiveFlag_RXNE(MQTT_UART) ||
      LL_USART_IsActiveFlag_ORE(MQTT_UART))
  {
    uint8_t data = (uint8_t)LL_USART_ReceiveData8(MQTT_UART);
    uint16_t next = (uint16_t)((s_head + 1U) % AT_RING_SIZE);

    if (next != s_tail)
    {
      s_ring[s_head] = data;
      s_head = next;
      s_rx_count++;
    }
    else
    {
      s_ovr++;
    }
  }
}

static volatile uint32_t s_tx_stuck;

static void uart_tx_byte(uint8_t b)
{
  uint32_t spins = 0;
  while (!LL_USART_IsActiveFlag_TXE(MQTT_UART))
  {
    /* ~100ms cap; one 115200 bit-time is ~1us. Bail out if TXE never sets. */
    if (++spins > 8000000U) { s_tx_stuck++; return; }
  }
  LL_USART_TransmitData8(MQTT_UART, b);
}

static void uart_tx_str(const char *s)
{
  while (*s)
    uart_tx_byte((uint8_t)*s++);
}

static int uart_rx_byte(uint32_t timeout_ms)
{
  uint32_t t0 = g_uwTickMs;
  uint32_t spins = 0;
  uint32_t ms_sp = 0;

  while (s_tail == s_head)
  {
    /* Fallback cycle-counted timeout: works even if SysTick IRQ is dead */
    if (++spins >= 50000U) { spins = 0; ms_sp++; }
    if ((g_uwTickMs - t0) >= timeout_ms || ms_sp >= timeout_ms)
      return -1;
  }
  uint8_t b = s_ring[s_tail];
  s_tail = (uint16_t)((s_tail + 1U) % AT_RING_SIZE);
  return b;
}

/* Blocking: read one raw AT line (terminated by '\n'), '\r' stripped,
   empty lines skipped. Returns length or -1 on timeout. */
static int at_read_line(uint32_t timeout_ms)
{
  uint32_t t0 = g_uwTickMs;
  uint32_t rounds = 0;
  uint16_t n = 0;
  int c;

  for (;;)
  {
    if ((g_uwTickMs - t0) >= timeout_ms || rounds > timeout_ms * 4U)
      return -1;

    c = uart_rx_byte(1U);
    if (c < 0)
    {
      rounds++;
      continue;
    }
    rounds = 0;
    if (c == '\n')
    {
      if (n == 0)
        continue;           /* skip empty lines between responses */
      s_line[n] = '\0';
      return n;
    }
    if (c != '\r' && n < AT_LINE_MAX - 1U)
      s_line[n++] = (char)c;
  }
}

/* Non-blocking: extract a complete buffered line, -1 if none yet. */
static int at_read_line_nb(void)
{
  uint16_t i = s_tail;
  uint16_t n = 0;

  while (i != s_head)
  {
    uint8_t b = s_ring[i];
    i = (uint16_t)((i + 1U) % AT_RING_SIZE);
    if (b == '\n')
    {
      if (n == 0)
      {
        /* empty line, drop it and rescan */
        s_tail = i;
        return at_read_line_nb();
      }
      s_line[n] = '\0';
      s_tail = i;
      return n;
    }
    if (b != '\r' && n < AT_LINE_MAX - 1U)
      s_line[n++] = (char)b;
  }
  return -1;
}

static void at_flush_rx(void)
{
  s_tail = s_head;
}

/* Send a plain AT command and wait for OK. Returns 0 OK, -1 error, -2 timeout. */
static int at_cmd(const char *cmd, uint32_t timeout_ms)
{
  uint32_t t0 = g_uwTickMs;

  at_flush_rx();
  if (s_verbose) printf("AT> %s\r\n", cmd);
  uart_tx_str(cmd);
  uart_tx_str("\r\n");

  while ((g_uwTickMs - t0) < timeout_ms)
  {
    int len = at_read_line(timeout_ms ? (timeout_ms - (g_uwTickMs - t0)) : 0U);
    if (len < 0)
      break;
    if (s_verbose) printf("AT< %s\r\n", s_line);
    if (strcmp(s_line, "OK") == 0)
      return 0;
    if (strncmp(s_line, "ERROR", 5) == 0 ||
        strncmp(s_line, "+CME ERROR", 10) == 0 ||
        strncmp(s_line, "+CMS ERROR", 10) == 0)
      return -1;
  }
  return -2;
}

/* Read lines until one contains `expect`; s_line keeps it for the caller.
   Returns 0 seen, -1 timeout. */
static int at_wait_line(const char *expect, uint32_t timeout_ms)
{
  uint32_t t0 = g_uwTickMs;

  while ((g_uwTickMs - t0) < timeout_ms)
  {
    if (at_read_line(timeout_ms - (g_uwTickMs - t0)) < 0)
      break;
    if (s_verbose) printf("AT< %s\r\n", s_line);
    if (strstr(s_line, expect))
      return 0;
  }
  return -1;
}

/* Send a command whose real answer arrives as an asynchronous URC.
   AT+MIPOPEN prints "OK" as soon as the request is merely *accepted*, the
   socket exists only when "+MIPOPEN: <id>,<result>" shows up (manual 3.3), and
   AT+MIPCLOSE does the same with "+MIPCLOSE: <id>" - waiting for the OK alone
   made MIPSEND fail with +CME 553 "TCP/IP not connected" (2026-10-24 log).
   Returns 0 when `expect` was seen, -1 on ERROR/CME/CMS, -2 on timeout. */
static int at_cmd_wait_urc(const char *cmd, const char *expect, uint32_t timeout_ms)
{
  uint32_t t0 = g_uwTickMs;

  if (s_verbose) printf("AT> %s\r\n", cmd);
  uart_tx_str(cmd);
  uart_tx_str("\r\n");

  while ((g_uwTickMs - t0) < timeout_ms)
  {
    if (at_read_line(timeout_ms - (g_uwTickMs - t0)) < 0)
      break;
    if (s_verbose) printf("AT< %s\r\n", s_line);
    if (strstr(s_line, expect))
      return 0;
    if (strncmp(s_line, "ERROR", 5) == 0 ||
        strncmp(s_line, "+CME ERROR", 10) == 0 ||
        strncmp(s_line, "+CMS ERROR", 10) == 0)
      return -1;
  }
  return -2;
}

/* ------------------------- public API -------------------------------------- */

__attribute__((weak)) void mqtt_at_on_message(const char *line)
{
  (void)line;
}

static void mqtt_at_gpio_init(void)
{
  LL_GPIO_InitTypeDef io = {0};

  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);

  /* PC4 USART3_TX (-> module RXD), PC5 USART3_RX (<- module TXD), AF7 */
  io.Pin        = LL_GPIO_PIN_4 | LL_GPIO_PIN_5;
  io.Mode       = LL_GPIO_MODE_ALTERNATE;
  io.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
  io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  io.Pull       = LL_GPIO_PULL_UP;
  io.Alternate  = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOC, &io);

#if MQTT_USE_PWRKEY
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
  io.Pin  = MQTT_PWRKEY_PIN;
  io.Mode = LL_GPIO_MODE_OUTPUT;
  io.Alternate = 0U;
  io.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(MQTT_PWRKEY_GPIO, &io);
  LL_GPIO_SetOutputPin(MQTT_PWRKEY_GPIO, MQTT_PWRKEY_PIN);
#endif

#if MQTT_USE_RST
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
  io.Pin  = MQTT_RST_PIN;
  io.Mode = LL_GPIO_MODE_OUTPUT;
  io.Alternate = 0U;
  io.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(MQTT_RST_GPIO, &io);
  LL_GPIO_SetOutputPin(MQTT_RST_GPIO, MQTT_RST_PIN);
#endif
}

int mqtt_at_init(void)
{
  LL_USART_InitTypeDef us = {0};

  mqtt_at_gpio_init();

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);

  us.BaudRate            = MQTT_UART_BAUD;
  us.DataWidth           = LL_USART_DATAWIDTH_8B;
  us.StopBits            = LL_USART_STOPBITS_1;
  us.Parity              = LL_USART_PARITY_NONE;
  us.TransferDirection   = LL_USART_DIRECTION_TX_RX;
  us.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  us.OverSampling        = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(MQTT_UART, &us);

  LL_USART_DisableOverrunDetect(MQTT_UART);
  LL_USART_EnableIT_RXNE(MQTT_UART);
  LL_USART_Enable(MQTT_UART);

  NVIC_SetPriority(USART3_IRQn, 2);
  NVIC_EnableIRQ(USART3_IRQn);

#if MQTT_USE_PWRKEY
  printf("PWRKEY power-on pulse...\r\n");
  LL_mDelay(500);
  LL_GPIO_ResetOutputPin(MQTT_PWRKEY_GPIO, MQTT_PWRKEY_PIN);
  LL_mDelay(600);
  LL_GPIO_SetOutputPin(MQTT_PWRKEY_GPIO, MQTT_PWRKEY_PIN);
  LL_mDelay(3000);
#endif

  /* Sync with the module: send AT until OK (module boots and registers). */
  {
    uint32_t t0 = g_uwTickMs;
    uint32_t try_cnt = 0;
    uint32_t t1;

    /* Probe: is the 1ms SysTick interrupt actually running? */
    {
      uint32_t a = g_uwTickMs;
      for (volatile uint32_t i = 0; i < 8000000U; i++) {}
      if (g_uwTickMs == a)
        printf("SysTick IRQ NOT running! tick stuck at %lu\r\n", (unsigned long)a);
      else
        printf("tick ok: %lu -> %lu\r\n", (unsigned long)a, (unsigned long)g_uwTickMs);
    }

    printf("Waiting for ML307R AT sync... tick=%lu\r\n", (unsigned long)g_uwTickMs);
    while ((g_uwTickMs - t0) < 30000U)
    {
      try_cnt++;
      printf("AT try #%lu: sending 'AT'... tick=%lu\r\n",
             (unsigned long)try_cnt, (unsigned long)g_uwTickMs);
      int r = at_cmd("AT", 1000U);
      printf("AT try #%lu done: r=%d rx=%lu ovr=%lu tx_stuck=%lu tick=%lu\r\n",
             (unsigned long)try_cnt, r, (unsigned long)s_rx_count,
             (unsigned long)s_ovr, (unsigned long)s_tx_stuck,
             (unsigned long)g_uwTickMs);
      if (r == 0)
      {
        at_cmd("ATE0", 1000U);   /* turn echo off */
        printf("ML307R ready.\r\n");
        return 0;
      }
      /* r==-2 and no rx bytes for a while: check if SysTick is alive */
      t1 = g_uwTickMs;
      for (volatile uint32_t i = 0; i < 4000000U; i++) {}   /* ~tick-independent wait */
      if (g_uwTickMs == t1)
      {
        printf("tick frozen at %lu - SysTick not running!\r\n", (unsigned long)t1);
        break;
      }
    }
    printf("ML307R no response!\r\n");
    return -1;
  }
}

/* A completely silent module usually means its AT interface is wedged
   (leftover MQTT/MIP client from a previous boot, or passthrough mode).
   "+++" needs 1 s of silence on both sides; then try plain AT + ATE0. */
static void at_probe_wedged(const char *context)
{
  printf("probe %s: no line at all, testing raw AT interface...\r\n", context);
  at_flush_rx();
  LL_mDelay(1100);
  uart_tx_str("+++");
  LL_mDelay(1100);
  uart_tx_str("AT\r\n");
  printf("probe %s: reply lines in next 3 s (none below = AT port dead -> full power-cycle module)\r\n",
         context);
  for (uint32_t t0 = g_uwTickMs; (g_uwTickMs - t0) < 3000U; )
  {
    if (at_read_line(500U) > 0)
      printf("AT< %s\r\n", s_line);
  }
}

/* ------------------- DIY MQTT over AT+MIP*, connect_id 0 ------------------ */

#define MIP_CONN_ID        0
#define MIP_CHUNK          200   /* <=200 B per AT+MIPSEND: 400 hex chars + cmd */
#define MQTT_RX_BUF        512   /* reassembly window for inbound MQTT packets */
#define MQTT_PUB_MAX       400   /* PUBLISH packet cap (topic + JSON payload) */
#define MQTT_PING_INTERVAL_MS  ((uint32_t)MQTT_KEEPALIVE_S * 500U) /* half of it */

static uint8_t  s_mq_rx[MQTT_RX_BUF];
static int      s_mq_rx_len;
static uint16_t s_mq_pkt_id = 1;
static uint32_t s_last_tx_ms;
static int      s_connack_seen;
static int      s_connack_rc;

/* HEX-encode raw[off,off+len) into an inline AT+MIPSEND and wait for its
   "+MIPSEND: <id>,<len>" / "OK" line. Long packets go out in MIP_CHUNK pieces -
   TCP is a byte stream and the MQTT parser reassembles it. Nothing is flushed
   here: an arriving rtcp/disconn line is still processed, so a reply can never
   be lost while a publish is in flight. static buffers because the startup
   stack frame is shared with the USART3 ISR. */
static void mqtt_process_line(void);

static int mip_send(const char *raw, int len)
{
  static char cmd[AT_LINE_MAX];
  static char hx[2 * MIP_CHUNK + 1];
  int off = 0;

  while (off < len)
  {
    uint32_t t0;
    int acked = 0;
    int n = len - off;

    if (n > MIP_CHUNK)
      n = MIP_CHUNK;
    payload_hex_upper((const uint8_t *)raw + off, hx, (uint16_t)n);
    snprintf(cmd, sizeof cmd, "AT+MIPSEND=%d,%d,\"%s\"", MIP_CONN_ID, n, hx);
    if (s_verbose) printf("AT> %s\r\n", cmd);
    uart_tx_str(cmd);
    uart_tx_str("\r\n");

    t0 = g_uwTickMs;
    while ((g_uwTickMs - t0) < 10000U)
    {
      if (at_read_line(10000U - (g_uwTickMs - t0)) < 0)
        break;
      mqtt_process_line();
      if (strncmp(s_line, "+MIPSEND", 8) == 0 || strcmp(s_line, "OK") == 0)
      {
        acked = 1;
        break;
      }
      if (strncmp(s_line, "ERROR", 5) == 0 ||
          strncmp(s_line, "+CME ERROR", 10) == 0 ||
          strncmp(s_line, "+CMS ERROR", 10) == 0)
        return -1;
    }
    if (!acked)
    {
      printf("MIPSEND chunk at %d not acknowledged\r\n", off);
      return -1;
    }
    off += n;
  }
  s_last_tx_ms = g_uwTickMs;
  return 0;
}

/* Open the TCP socket to OneNET. encoding/ackmode are per-connect-id settings
   and only take effect when written BEFORE MIPOPEN. */
static void at_network_diag(void)
{
  /* Answers "is the SIM blocked (arrears / no real-name / data exhausted)?":
     CGATT:0 or a CGPADDR without an IP = no data bearer at all.
     CGATT:1 + IP but MIPOPEN still failing = card reaches only whitelisted
     addresses (定向/流量池限制) or the server is unreachable. */
  at_cmd("AT+CGATT?", 2000U);
  at_cmd("AT+CEREG?", 2000U);
  at_cmd("AT+CGPADDR", 2000U);
  at_cmd("AT+CESQ", 2000U);
}

static int mip_open(void)
{
  static char cmd[96];
  const char *res;
  int rc;

  /* Close first, then wait for its async "+MIPCLOSE: <id>" report: opening a
     socket while a close of the same id is still in flight leaves nothing
     connected behind (the +CME 553 seen in the 2026-10-24 log). "+CME ERROR:
     551" (TCP/IP unused) means there was nothing to close and no URC follows,
     so don't sit there for 5 s waiting for one. */
  if (at_cmd("AT+MIPCLOSE=0", 3000U) == 0)
    at_wait_line("+MIPCLOSE", 5000U);

  if (at_cmd("AT+MIPCFG=\"encoding\",0,1,1", 3000U) != 0)  /* send HEX, recv HEX */
    printf("MIPCFG encoding rejected\r\n");
  if (at_cmd("AT+MIPCFG=\"ackmode\",0,1", 3000U) != 0)     /* "+ack",0,<len> = server got it */
    printf("MIPCFG ackmode rejected\r\n");

  snprintf(cmd, sizeof cmd, "AT+MIPOPEN=%d,\"TCP\",\"%s\"," ONEONET_PORT,
           MIP_CONN_ID, ONEONET_SERVER);
  rc = at_cmd_wait_urc(cmd, "+MIPOPEN: 0,", 20000U);
  if (rc != 0)
  {
    printf("MIPOPEN to %s:%s got no result URC (rc=%d, -2 = module silent)\r\n",
           ONEONET_SERVER, ONEONET_PORT, rc);
    at_network_diag();
    if (rc == -2)
      at_probe_wedged("MIPOPEN");
    return -1;
  }
  res = strrchr(s_line, ',');
  if (res == 0 || res[1] != '0')
  {
    printf("MIPOPEN failed: %s (+MIPOPEN result != 0, see TCP/IP manual codes)\r\n",
           s_line);
    at_network_diag();
    return -1;
  }
  printf("TCP connected: %s\r\n", s_line);
  return 0;
}

/* Remaining length of pkt plus the size of the fixed header it spans. */
static int mq_remaining(const uint8_t *pkt, int len, int *hdrlen)
{
  int mul = 1, val = 0, i = 1;

  for (;;)
  {
    if (i >= len)
      return -1;
    val += (pkt[i] & 0x7FU) * mul;
    if ((pkt[i] & 0x80U) == 0U)
      break;
    mul *= 128;
    if (mul > 128 * 128 * 128)
      return -1;
    i++;
  }
  *hdrlen = i + 1;
  return val;
}

static void mqtt_handle_packet(const uint8_t *pkt, int len)
{
  int type = pkt[0] >> 4;
  static char topic[128];
  static char payload[MQTT_RX_BUF];
  int hdr, qos, tl, i, pl;

  if (type == 2)
  {
    s_connack_seen = 1;
    s_connack_rc = (len >= 4) ? (int)(pkt[3] & 0x3FU) : -1;
    printf("MQTT CONNACK session_present=%d rc=%d\r\n",
           (len >= 3) ? (pkt[2] & 1) : 0, s_connack_rc);
    return;
  }
  if (type == 13)
  {
    printf("MQTT PINGRESP\r\n");
    return;
  }
  if (type == 9)
  {
    printf("MQTT SUBACK code=%d\r\n", (len >= 3) ? pkt[len - 1] : -1);
    return;
  }
  if (type == 14)
  {
    printf("MQTT server sent DISCONNECT\r\n");
    s_connected = 0;
    return;
  }
  if (type != 3)
    return;

  hdr = 0;
  if (mq_remaining(pkt, len, &hdr) < 2 || hdr + 2 > len)
    return;
  tl = (pkt[hdr] << 8) | pkt[hdr + 1];
  qos = (pkt[0] >> 1) & 3;
  i = hdr + 2 + tl;
  if (tl <= 0 || tl >= (int)sizeof topic || i + (qos > 0 ? 2 : 0) > len)
    return;
  memcpy(topic, &pkt[hdr + 2], (size_t)tl);
  topic[tl] = '\0';
  if (qos > 0)
    i += 2;
  pl = len - i;
  if (pl >= (int)sizeof payload)
    pl = (int)sizeof payload - 1;
  memcpy(payload, &pkt[i], (size_t)pl);
  payload[pl] = '\0';
  printf("MQTT PUBLISH <%s>: %s\r\n", topic, payload);
  mqtt_at_on_message(payload);
}

/* Act on one buffered AT line. Inbound MQTT bytes arrive as
   +MIPURC: "rtcp",<id>,<len>,<HEX>; an MQTT packet may straddle two URCs, so
   the decoded bytes accumulate in s_mq_rx and complete packets are dispatched. */
static void mqtt_process_line(void)
{
  const char *d;
  int n;

  if (s_verbose)
    printf("AT< %s\r\n", s_line);

  if (strstr(s_line, "\"rtcp\"") != 0)
  {
    d = strrchr(s_line, ',');
    if (d == 0)
      return;
    n = payload_hex_to_bin(d + 1, &s_mq_rx[s_mq_rx_len],
                            (int)sizeof s_mq_rx - s_mq_rx_len);
    if (n < 0)
    {
      printf("MIP rx: not hex (%s)\r\n", s_line);
      return;
    }
    s_mq_rx_len += n;
    for (;;)
    {
      int total = payload_mqtt_packet_len(s_mq_rx, s_mq_rx_len);

      if (total == 0)
        break;
      if (total < 0)
      {
        printf("MIP rx: broken stream, resync\r\n");
        s_mq_rx_len = 0;
        break;
      }
      mqtt_handle_packet(s_mq_rx, total);
      memmove(s_mq_rx, &s_mq_rx[total], (size_t)(s_mq_rx_len - total));
      s_mq_rx_len -= total;
    }
    if (s_mq_rx_len >= (int)sizeof s_mq_rx)
      s_mq_rx_len = 0;          /* oversized packet: drop and resync */
  }
  else if (strstr(s_line, "\"ack\"") != 0)
    printf("OneNET ACKed our bytes: %s\r\n", s_line);
  else if (strstr(s_line, "\"disconn\"") != 0)
  {
    printf("TCP socket closed by peer/network: %s\r\n", s_line);
    s_connected = 0;
  }
  else if (strstr(s_line, "\"drop\"") != 0)
  {
    printf("MIP rx overflow: %s\r\n", s_line);
    s_mq_rx_len = 0;
  }
}

static void mqtt_rx_pump(void)
{
  while (at_read_line_nb() >= 0)
    mqtt_process_line();
}

static int mqtt_wait_connack(uint32_t ms)
{
  uint32_t t0 = g_uwTickMs;

  while ((g_uwTickMs - t0) < ms)
  {
    mqtt_rx_pump();
    if (s_connack_seen)
      return s_connack_rc == 0 ? 0 : -2;
    LL_mDelay(10U);
  }
  return -1;
}

int mqtt_at_open(void)
{
  static char raw[MQTT_RX_BUF];
  int n, rc = -1;

  printf("MQTT DIY connect to %s:%s...\r\n", ONEONET_SERVER, ONEONET_PORT);
  s_verbose = 1;

  if (mip_open() != 0)
    goto out;

  s_mq_rx_len = 0;
  s_connack_seen = 0;
  s_connack_rc = -1;

  n = payload_mqtt_connect(raw, (int)sizeof raw, ONEONET_CLIENT_ID,
                           ONEONET_USERNAME, ONEONET_PASSWORD);
  if (n <= 0)
  {
    printf("CONNECT packet does not fit in %d B\r\n", (int)sizeof raw);
    goto out;
  }
  printf("CONNECT packet %d bytes\r\n", n);
  if (mip_send(raw, n) != 0)
  {
    printf("CONNECT send failed\r\n");
    goto out;
  }

  rc = mqtt_wait_connack(20000U);
  if (rc == -1)
    printf("No CONNACK within 20 s\r\n");
  else if (rc == -2)
    printf("OneNET refused the session (rc=%d)\r\n", s_connack_rc);
  if (rc != 0)
    goto out;

  s_connected = 1;
  printf("MQTT connected.\r\n");

  if (MQTT_SUB_TOPIC[0] != '\0')
  {
    n = payload_mqtt_subscribe(raw, (int)sizeof raw, MQTT_SUB_TOPIC, s_mq_pkt_id);
    s_mq_pkt_id++;
    if (n > 0 && mip_send(raw, n) == 0)
      printf("Subscribed to %s\r\n", MQTT_SUB_TOPIC);
    else
      printf("SUBSCRIBE failed\r\n");
  }
  rc = 0;

out:
  s_verbose = 0;
  if (rc != 0)
  {
    s_connected = 0;
    at_cmd("AT+MIPCLOSE=0", 5000U);
  }
  return rc;
}

int mqtt_at_publish(const char *topic, const char *payload)
{
  static char raw[MQTT_PUB_MAX];
  int n;

  if (!s_connected)
    return -1;

  n = payload_mqtt_publish(raw, (int)sizeof raw, topic, payload, 0, 0);
  if (n <= 0)
    return -2;
  if (mip_send(raw, n) != 0)
  {
    s_connected = 0;
    return -3;
  }
  printf("PUBLISH %d bytes -> %s\r\n", n, topic);
  return 0;
}

int mqtt_at_connected(void)
{
  return s_connected ? 1 : 0;
}

void mqtt_at_poll(void)
{
  mqtt_rx_pump();

  if (s_connected && (g_uwTickMs - s_last_tx_ms) >= MQTT_PING_INTERVAL_MS)
  {
    static char ping[2];

    if (payload_mqtt_ping(ping, (int)sizeof ping) == 2 && mip_send(ping, 2) == 0)
      printf("PINGREQ sent\r\n");
    else
      s_connected = 0;
  }
}

void mqtt_at_close(void)
{
  if (s_connected)
  {
    static const char disc[2] = { (char)0xE0, 0 };   /* MQTT DISCONNECT */

    mip_send(disc, 2);
    s_connected = 0;
  }
  at_cmd("AT+MIPCLOSE=0", 5000U);
}

