/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "OLED.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 舵机目标角度 / 当前 CCR */
static uint16_t g_target = 90;
static uint16_t g_ccr    = 1500;

/* 自动序列：0 -> 45 -> 90 -> 135 -> 180，每秒一个 */
static const uint8_t seq[5] = { 0, 45, 90, 135, 180 };
static uint8_t  g_idx  = 0;
static uint32_t g_tick = 0;
static uint8_t  g_auto = 1;          /* 1 = 自动序列，0 = 按键手动 */

/* 两个按键的消抖状态 */
static uint8_t  kp_raw = 1, kp_stable = 1;  static uint32_t kp_t = 0;
static uint8_t  km_raw = 1, km_stable = 1;  static uint32_t km_t = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint16_t AngleToCCR(float angle);
void   Servo_SetAngle(float angle);
static void OLED_ShowAll(uint16_t deg, uint16_t ccr);
static void Servo_Apply(uint16_t deg);
static uint8_t Key_Once(uint8_t *raw, uint8_t *stable, uint32_t *t,
                        GPIO_TypeDef *port, uint16_t pin);
void   App_Task(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);   /* 启动 50Hz PWM */
  OLED_Init();                                 /* 屏初始化 */
  g_tick = HAL_GetTick();
  Servo_Apply(90);                             /* 上电先停在中间位置 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* 角度 -> CCR。已配成 1 个计数 = 1µs，所以 CCR 的数值就是脉宽(µs) */
static uint16_t AngleToCCR(float angle)
{
  if (angle < 0.0f)   angle = 0.0f;
  if (angle > 180.0f) angle = 180.0f;

  /* 两端各留余量：0° 用 550µs、180° 用 2450µs，
     名义值 500/2500 会顶到机械限位，发出咔咔声 */
  return (uint16_t)(550.0f + angle * (2450.0f - 550.0f) / 180.0f + 0.5f);
}

void Servo_SetAngle(float angle)
{
  g_ccr = AngleToCCR(angle);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, g_ccr);
}

/* 三行显示：Target / CCR / Pulse，第 4 行显示当前模式 */
static void OLED_ShowAll(uint16_t deg, uint16_t ccr)
{
  OLED_ShowString(1, 1,  "Target:");
  OLED_ShowNum(1, 9, deg, 3);
  OLED_ShowString(1, 12, "deg");

  OLED_ShowString(2, 1,  "CCR   :");
  OLED_ShowNum(2, 9, ccr, 4);

  OLED_ShowString(3, 1,  "Pulse :");
  OLED_ShowNum(3, 9, ccr / 1000, 1);
  OLED_ShowString(3, 10, ".");
  OLED_ShowNum(3, 11, ccr % 1000, 3);
  OLED_ShowString(3, 14, "ms");

  OLED_ShowString(4, 1, g_auto ? "MODE: AUTO    " : "MODE: MANUAL");
}

/* 统一入口：设角度 + 刷屏 + 串口打印（三处数值一定一致） */
static void Servo_Apply(uint16_t deg)
{
  g_target = deg;
  Servo_SetAngle((float)deg);

  OLED_ShowAll(deg, g_ccr);

  printf("Target=%3d  CCR=%4d  Pulse=%d.%03dms\r\n",
         (int)deg, (int)g_ccr, (int)(g_ccr / 1000), (int)(g_ccr % 1000));
}

/* 按键：稳定 15ms 才算一次，按住不放只触发一次 */
static uint8_t Key_Once(uint8_t *raw, uint8_t *stable, uint32_t *t,
                        GPIO_TypeDef *port, uint16_t pin)
{
  uint8_t now = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 0 : 1;

  if (now != *raw) { *raw = now; *t = HAL_GetTick(); }

  if (HAL_GetTick() - *t >= 15 && now != *stable) {
    *stable = now;
    if (*stable == 0) return 1;
  }
  return 0;
}

/* 主循环任务：按键 ±15°，无按键时自动跑序列 */
void App_Task(void)
{
  /* PA1 加 15°，PA2 减 15°，范围 0~180，按下立即生效 */
  if (Key_Once(&kp_raw, &kp_stable, &kp_t, GPIOA, GPIO_PIN_1)) {
    g_auto = 0;
    Servo_Apply((g_target + 15 > 180) ? 180 : (uint16_t)(g_target + 15));
  }
  if (Key_Once(&km_raw, &km_stable, &km_t, GPIOA, GPIO_PIN_2)) {
    g_auto = 0;
    Servo_Apply((g_target < 15) ? 0 : (uint16_t)(g_target - 15));
  }

  /* 自动序列：每 1 秒走下一个角度，到 180° 后回到 0° */
  if (g_auto && (HAL_GetTick() - g_tick >= 1000)) {
    g_tick = HAL_GetTick();
    g_idx  = (uint8_t)((g_idx + 1) % 5);
    Servo_Apply(seq[g_idx]);
  }
}

/* GCC 下的 printf 重定向：走 USART1（Keil 的 __io_putchar 在这里不适用） */
int _write(int fd, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
  return len;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
