/**
  ******************************************************************************
  * @file    mqtt_cfg.h
  * @brief   OneNET / ML307R application configuration (edit values here only)
  ******************************************************************************
  */
#ifndef MQTT_CFG_H
#define MQTT_CFG_H

/* ==================== OneNET platform (new thing-model product) ============ */
#define ONEONET_SERVER        "183.230.40.96"
#define ONEONET_PORT          "1883"

/* MQTT clientid = device name (the device 名称/SN you created) */
#define ONEONET_CLIENT_ID     "Vibration_001"
/* MQTT username = 产品ID (Product ID on the product page) */
#define ONEONET_USERNAME      "F0wguvAinT"
/* MQTT password = long-term token generated from the AccessKey.
   Official format per SDK dev_token.c: res uses %2F, sign is percent-escaped
   base64 WITH == padding. Keep this token verbatim: mqtt_at.c puts it byte for
   byte into the DIY CONNECT packet (validated rc=0 from a PC and from the
   board). Removing the % escapes or the == padding makes OneNET reject it. */
#define ONEONET_PASSWORD      "把 tools/生成Token工具.bat 生成的 token 整串粘在这里"

/* Property post topic for the new thing model. The TSL 标识符 of this product
   are case sensitive: "Roll" / "Pitch" (lowercase gets code 2306). Their data
   type must be 浮点型(float) - as long as it is int32 the platform rejects the
   decimal value with code 2211 "int32 type error". */
#define MQTT_PUB_TOPIC        "$sys/F0wguvAinT/Vibration_001/thing/property/post"

/* Reply topic for property/post: the official thing-model API spells it
   /property/post/reply. Set "" to skip the SUBSCRIBE. */
#define MQTT_SUB_TOPIC        "$sys/F0wguvAinT/Vibration_001/thing/property/post/reply"

/* Keepalive in seconds (numeric: the PINGREQ period is half of it) */
#define MQTT_KEEPALIVE_S      120

/* ==================== ML307R serial / hardware ==================== */
/* AT UART: USART3, PC4=TX (MCU TX -> module RXD), PC5=RX (MCU RX <- module TXD) */
#define MQTT_UART             USART3
#define MQTT_UART_BAUD        115200U

/* Module PWRKEY power-on control: set MQTT_USE_PWRKEY to 1 and adjust pins
   if wired, otherwise the module must already be powered on. */
#define MQTT_USE_PWRKEY       0
#define MQTT_PWRKEY_GPIO      GPIOB
#define MQTT_PWRKEY_PIN       LL_GPIO_PIN_0

/* Module RST control (optional, active low pulse resets the module) */
#define MQTT_USE_RST          0
#define MQTT_RST_GPIO         GPIOB
#define MQTT_RST_PIN          LL_GPIO_PIN_1

/* ==================== Angle trigger / reporting ==================== */
/* Report when |roll-roll_base| or |pitch-pitch_base| reaches this (millideg) */
#define ANGLE_TRIGGER_DELTA_MDEG      5000
/* Consecutive samples must exceed the threshold before firing (anti-jitter) */
#define ANGLE_TRIGGER_DEBOUNCE_CNT    3
/* Minimum time between two reports (ms) */
#define ANGLE_REPORT_MIN_INTERVAL_MS  10000U

#endif /* MQTT_CFG_H */
