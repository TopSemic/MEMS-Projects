/**
  ******************************************************************************
  * @file    Templates_LL/Src/main.c 
  * @author  MCD Application Team
  * @brief   Main program body through the LL API
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "common.h"
#include "adxl355/adxl355.h"
#include "mqtt_at.h"
#include "angle_report.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>

#define RAD_TO_MDEG (57295.779513)
#define ACC_MOVAVG_WIN (10U)
#define ANGLE_LPF_ALPHA (0.1)

/* SPI pins (NSS: PA15, SCK: PC10, MISO: PC11, MOSI: PC12) */
static void SPI3_GPIO_Init(void)
{
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* PC10 SCK, PC11 MISO, PC12 MOSI */
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_6;
  GPIO_InitStruct.Pin = LL_GPIO_PIN_10 | LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* PA15 as CS (NSS) - default high */
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Pin = LL_GPIO_PIN_15;
  GPIO_InitStruct.Alternate = 0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_15);
}

static void SPI3_Init(void)
{
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI3);

  LL_SPI_InitTypeDef SPI_InitStruct = {0};
  SPI_InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
  SPI_InitStruct.Mode = LL_SPI_MODE_MASTER;
  SPI_InitStruct.DataWidth = LL_SPI_DATAWIDTH_8BIT;
  SPI_InitStruct.ClockPolarity = LL_SPI_POLARITY_LOW;
  SPI_InitStruct.ClockPhase = LL_SPI_PHASE_1EDGE;
  SPI_InitStruct.NSS = LL_SPI_NSS_SOFT;
  SPI_InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV8;
  SPI_InitStruct.BitOrder = LL_SPI_MSB_FIRST;
  LL_SPI_Init(SPI3, &SPI_InitStruct);
  LL_SPI_Enable(SPI3);
}

static uint8_t spi3_transfer_byte(uint8_t tx)
{
  while (!LL_SPI_IsActiveFlag_TXE(SPI3));
  LL_SPI_TransmitData8(SPI3, tx);
  while (!LL_SPI_IsActiveFlag_RXNE(SPI3));
  return LL_SPI_ReceiveData8(SPI3);
}

static void adxl_cs_low(void) { LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_15); }
static void adxl_cs_high(void){ LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_15); }

static int spi3_transfer_byte_cb(uint8_t tx, uint8_t *rx)
{
  if (!rx)
    return -1;

  *rx = spi3_transfer_byte(tx);
  return 0;
}

static int32_t sign_extend_20(uint32_t raw)
{
  return (raw & (1UL << 19)) ? (int32_t)(raw | 0xFFF00000UL) : (int32_t)raw;
}

static int32_t radians_to_mdeg(double rad)
{
  return (int32_t)(rad * RAD_TO_MDEG);
}

/** @addtogroup STM32L4xx_LL_Examples
  * @{
  */

/** @addtogroup Templates_LL
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);


/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
  int ret;
  struct adxl355_dev *adxl = NULL;
  struct no_os_spi_platform_ops spi_ops;
  struct adxl355_init_param adxl_init;

  /* Configure the system clock to 80 MHz */
  SystemClock_Config();
  
  /* Add your application code here */
  // 1. 使能GPIOB时钟
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);

  // 2. 配置PB14为推挽输出
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = LL_GPIO_PIN_14;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);  
  
  /* 初始化USART2引脚和外设 */
  USART2_GPIO_Init();
  USART2_Init();
  LL_USART_SendString(USART2, "Hello STM32L431 LL USART!\r\n");   
   

  /* 初始化 SPI3 与 ADXL355 CS */
  SPI3_GPIO_Init();
  SPI3_Init();

  spi_ops.transfer_byte = spi3_transfer_byte_cb;
  spi_ops.cs_low = adxl_cs_low;
  spi_ops.cs_high = adxl_cs_high;

  adxl_init.comm_type = ADXL355_SPI_COMM;
  adxl_init.dev_type = ID_ADXL355;
  adxl_init.comm_init.spi_init.max_speed_hz = 10000000U;
  adxl_init.comm_init.spi_init.bit_order = 0;
  adxl_init.comm_init.spi_init.mode = 0;
  adxl_init.comm_init.spi_init.extra = &spi_ops;

  ret = adxl355_init(&adxl, adxl_init);
  if (ret) {
    printf("adxl355_init failed: %d\r\n", ret);
    while (1) {
      LL_mDelay(500);
    }
  }

  ret = adxl355_soft_reset(adxl);
  if (ret) {
    printf("adxl355_soft_reset failed: %d\r\n", ret);
    while (1) {
      LL_mDelay(500);
    }
  }

  ret = adxl355_set_op_mode(adxl, ADXL355_MEAS_TEMP_ON_DRDY_ON);
  if (ret) {
    printf("adxl355_set_op_mode failed: %d\r\n", ret);
    while (1) {
      LL_mDelay(500);
    }
  }

  /* Bring up ML307R + MQTT (module keeps the MQTT session itself) */
  angle_report_t ang_rep;
  char pub_json[160];
  uint8_t angles_valid = 0;

  if (mqtt_at_init() == 0)
    mqtt_at_open();

  /* Infinite loop: read and print ADXL355 data */
    while (1)
    {
      int32_t ax, ay, az;
      uint32_t raw_x, raw_y, raw_z;
      uint16_t rt;
      ret = adxl355_get_raw_xyz(adxl, &raw_x, &raw_y, &raw_z);
      if (ret)
        continue;

      ret = adxl355_get_raw_temp(adxl, &rt);
      if (ret)
        continue;

      ax = sign_extend_20(raw_x);
      ay = sign_extend_20(raw_y);
      az = sign_extend_20(raw_z);

      /* convert accel to milli m/s^2: accel_milli = raw * 38245 / 1000000 */
      int64_t ax_mmps = (int64_t)ax * ADXL355_ACC_SCALE_FACTOR_MUL / 1000000LL;
      int64_t ay_mmps = (int64_t)ay * ADXL355_ACC_SCALE_FACTOR_MUL / 1000000LL;
      int64_t az_mmps = (int64_t)az * ADXL355_ACC_SCALE_FACTOR_MUL / 1000000LL;
      int32_t roll_mdeg;
      int32_t pitch_mdeg;
      static double roll_filt_mdeg = 0.0;
      static double pitch_filt_mdeg = 0.0;
      static uint8_t angle_filt_init = 0;

      /* Light denoise: moving average on acceleration in SI units. */
      static double ax_hist[ACC_MOVAVG_WIN] = {0.0};
      static double ay_hist[ACC_MOVAVG_WIN] = {0.0};
      static double az_hist[ACC_MOVAVG_WIN] = {0.0};
      static double ax_sum = 0.0, ay_sum = 0.0, az_sum = 0.0;
      static uint8_t acc_idx = 0;
      static uint8_t acc_cnt = 0;

      double ax_si_raw = (double)ax_mmps / 1000.0;
      double ay_si_raw = (double)ay_mmps / 1000.0;
      double az_si_raw = (double)az_mmps / 1000.0;

      ax_sum -= ax_hist[acc_idx];
      ay_sum -= ay_hist[acc_idx];
      az_sum -= az_hist[acc_idx];

      ax_hist[acc_idx] = ax_si_raw;
      ay_hist[acc_idx] = ay_si_raw;
      az_hist[acc_idx] = az_si_raw;

      ax_sum += ax_hist[acc_idx];
      ay_sum += ay_hist[acc_idx];
      az_sum += az_hist[acc_idx];

      if (acc_cnt < ACC_MOVAVG_WIN)
        acc_cnt++;
      acc_idx = (uint8_t)((acc_idx + 1U) % ACC_MOVAVG_WIN);

      /* Tilt from gravity vector (static/slow motion use case). */
      double ax_si = ax_sum / (double)acc_cnt;
      double ay_si = ay_sum / (double)acc_cnt;
      double az_si = az_sum / (double)acc_cnt;
      int32_t roll_raw_mdeg = radians_to_mdeg(atan2(ay_si, az_si));
      int32_t pitch_raw_mdeg = radians_to_mdeg(atan2(-ax_si, sqrt(ay_si * ay_si + az_si * az_si)));

      /* First-order LPF (EMA): filtered = alpha*new + (1-alpha)*filtered */
      if (!angle_filt_init) {
        roll_filt_mdeg = (double)roll_raw_mdeg;
        pitch_filt_mdeg = (double)pitch_raw_mdeg;
        angle_filt_init = 1;
      } else {
        roll_filt_mdeg = ANGLE_LPF_ALPHA * (double)roll_raw_mdeg + (1.0 - ANGLE_LPF_ALPHA) * roll_filt_mdeg;
        pitch_filt_mdeg = ANGLE_LPF_ALPHA * (double)pitch_raw_mdeg + (1.0 - ANGLE_LPF_ALPHA) * pitch_filt_mdeg;
      }

      roll_mdeg = (int32_t)roll_filt_mdeg;
      pitch_mdeg = (int32_t)pitch_filt_mdeg;

      /* convert temp to millidegrees Celsius using the same divisor as the official driver */
      int64_t temp_mdeg = (((int64_t)rt * ADXL355_TEMP_OFFSET_DIV + ADXL355_TEMP_OFFSET) *
        (int64_t)ADXL355_TEMP_SCALE_FACTOR) /
           ((int64_t)ADXL355_TEMP_OFFSET_DIV * ADXL355_TEMP_SCALE_FACTOR_DIV);

        printf("RAW X=%ld Y=%ld Z=%ld | aX=%ld.%03ld m/s^2 aY=%ld.%03ld aZ=%ld.%03ld T=%ld.%03ld C Roll=%ld.%03lddeg Pitch=%ld.%03lddeg\r\n",
          (long)ax, (long)ay, (long)az,
          (long)(ax_mmps/1000), (long)llabs(ax_mmps%1000),
          (long)(ay_mmps/1000), (long)llabs(ay_mmps%1000),
          (long)(az_mmps/1000), (long)llabs(az_mmps%1000),
          (long)(temp_mdeg/1000), (long)llabs(temp_mdeg%1000),
          (long)(roll_mdeg/1000), (long)labs(roll_mdeg%1000),
          (long)(pitch_mdeg/1000), (long)labs(pitch_mdeg%1000));

     // LL_GPIO_TogglePin(GPIOB, LL_GPIO_PIN_14); // 翻转PB14电平
      /* --- tilt change beyond threshold -> report over MQTT --- */
      if (!angles_valid) {
        angle_report_init(&ang_rep, roll_mdeg, pitch_mdeg);
        angles_valid = 1;
      }

      mqtt_at_poll();

      if (angle_report_update(&ang_rep, roll_mdeg, pitch_mdeg,
                              g_uwTickMs, pub_json, sizeof pub_json))
      {
        if (!mqtt_at_connected())
          mqtt_at_open();
        if (mqtt_at_publish(MQTT_PUB_TOPIC, pub_json) == 0) {
          angle_report_mark_sent(&ang_rep, roll_mdeg, pitch_mdeg, g_uwTickMs);
          printf("Angle reported: %s\r\n", pub_json);
        } else {
          printf("Angle report failed, retry next cycle.\r\n");
        }
      }

      LL_mDelay(500);
    }
}

/* ==============   BOARD SPECIFIC CONFIGURATION CODE BEGIN    ============== */
/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follows :
  *            System Clock source            = PLL (MSI)
  *            SYSCLK(Hz)                     = 80000000
  *            HCLK(Hz)                       = 80000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 1
  *            APB2 Prescaler                 = 1
  *            MSI Frequency(Hz)              = 4000000
  *            PLL_M                          = 1
  *            PLL_N                          = 40
  *            PLL_R                          = 2
  *            Flash Latency(WS)              = 4
  * @param  None
  * @retval None
  */
void SystemClock_Config(void)
{
  /* MSI configuration and activation */
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
  LL_RCC_MSI_Enable();
  while(LL_RCC_MSI_IsReady() != 1) 
  {
  };
  
  /* Main PLL configuration and activation */
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_MSI, LL_RCC_PLLM_DIV_1, 40, LL_RCC_PLLR_DIV_2);
  LL_RCC_PLL_Enable();
  LL_RCC_PLL_EnableDomain_SYS();
  while(LL_RCC_PLL_IsReady() != 1) 
  {
  };
  
  /* Sysclk activation on the main PLL */
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) 
  {
  };
  
  /* Set APB1 & APB2 prescaler*/
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);

  /* Set systick to 1ms in using frequency set to 80MHz */
  /* This frequency can be calculated through LL RCC macro */
  /* ex: __LL_RCC_CALC_PLLCLK_FREQ(__LL_RCC_CALC_MSI_FREQ(LL_RCC_MSIRANGESEL_RUN, LL_RCC_MSIRANGE_6), 
                                  LL_RCC_PLLM_DIV_1, 40, LL_RCC_PLLR_DIV_2)*/
  LL_Init1msTick(80000000);

  /* This LL version's LL_InitTick enables the counter but not TICKINT/NVIC.
     Enable the SysTick interrupt so SysTick_Handler (g_uwTickMs) runs. */
  SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
  NVIC_SetPriority(SysTick_IRQn, 3);
  
  /* Update CMSIS variable (which can be updated also through SystemCoreClockUpdate function) */
  LL_SetSystemCoreClock(80000000);
}
/* ==============   BOARD SPECIFIC CONFIGURATION CODE END      ============== */

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
  * @}
  */

/**
  * @}
  */

