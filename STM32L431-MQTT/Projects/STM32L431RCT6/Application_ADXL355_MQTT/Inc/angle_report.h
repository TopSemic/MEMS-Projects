/**
  ******************************************************************************
  * @file    angle_report.h
  * @brief   Tilt-angle threshold trigger logic (pure, host-testable)
  ******************************************************************************
  */
#ifndef ANGLE_REPORT_H
#define ANGLE_REPORT_H

#include <stdint.h>
#include "mqtt_cfg.h"

typedef struct
{
  int32_t  base_roll_mdeg;
  int32_t  base_pitch_mdeg;
  uint8_t  pending_cnt;
  uint8_t  started;
  uint32_t last_report_ms;
  uint16_t next_msg_id;      /* OneNET request "id", wraps every 65535 posts */
} angle_report_t;

void angle_report_init(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg);

/* Returns 1 when a report is due (json filled), 0 otherwise.
   Call angle_report_mark_sent() only after the upload really succeeded. */
int angle_report_update(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg,
                        uint32_t now_ms, char *json, int cap);

void angle_report_mark_sent(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg,
                            uint32_t now_ms);

#endif /* ANGLE_REPORT_H */
