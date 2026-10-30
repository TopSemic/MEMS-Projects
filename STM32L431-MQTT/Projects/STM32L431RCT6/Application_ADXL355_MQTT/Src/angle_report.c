/**
  ******************************************************************************
  * @file    angle_report.c
  * @brief   Tilt-angle threshold trigger logic (pure, host-testable)
  ******************************************************************************
  */
#include "angle_report.h"
#include "mqtt_payload.h"

static int32_t iabs32(int32_t v)
{
  return (v < 0) ? -v : v;
}

void angle_report_init(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg)
{
  r->base_roll_mdeg   = roll_mdeg;
  r->base_pitch_mdeg  = pitch_mdeg;
  r->pending_cnt      = 0;
  r->started          = 1;
  r->last_report_ms   = 0;
  r->next_msg_id      = 1;
}

int angle_report_update(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg,
                        uint32_t now_ms, char *json, int cap)
{
  int32_t dr, dp;

  if (!r->started)
  {
    angle_report_init(r, roll_mdeg, pitch_mdeg);
    return 0;
  }

  dr = iabs32(roll_mdeg - r->base_roll_mdeg);
  dp = iabs32(pitch_mdeg - r->base_pitch_mdeg);

  if (dr >= (int32_t)ANGLE_TRIGGER_DELTA_MDEG || dp >= (int32_t)ANGLE_TRIGGER_DELTA_MDEG)
  {
    if (r->pending_cnt < ANGLE_TRIGGER_DEBOUNCE_CNT)
      r->pending_cnt++;
  }
  else
  {
    r->pending_cnt = 0;
  }

  if (r->pending_cnt >= ANGLE_TRIGGER_DEBOUNCE_CNT &&
      (now_ms - r->last_report_ms) >= ANGLE_REPORT_MIN_INTERVAL_MS)
  {
    if (payload_build_dp(json, cap, r->next_msg_id, roll_mdeg, pitch_mdeg) < 0)
      return 0;
    r->next_msg_id = (uint16_t)(r->next_msg_id + 1U);
    return 1;
  }

  return 0;
}

void angle_report_mark_sent(angle_report_t *r, int32_t roll_mdeg, int32_t pitch_mdeg,
                            uint32_t now_ms)
{
  r->base_roll_mdeg  = roll_mdeg;
  r->base_pitch_mdeg = pitch_mdeg;
  r->pending_cnt     = 0;
  r->last_report_ms  = now_ms;
}
