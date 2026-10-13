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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_SYSVIEW_Conf.h"

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
/* USER CODE BEGIN BRICK_TYPES */
// ---------- Bricks (HP-based, 1/2/3-hit) ----------
#define BRICK_W      14
#define BRICK_H      6
#define BRICKS_MAX   40

typedef struct {
  uint8_t alive;
  uint8_t hp;     // 1,2,3 hits
  uint8_t x, y;   // top-left
} Brick;
/* USER CODE END BRICK_TYPES */
/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = { .name = "defaultTask",
		.stack_size = 128 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for InputTask */
osThreadId_t InputTaskHandle;
const osThreadAttr_t InputTask_attributes = {
    .name = "InputTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t) osPriorityHigh,
};

/* Definitions for GameTask */
osThreadId_t GameTaskHandle;
const osThreadAttr_t GameTask_attributes = {
    .name = "GameTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for RenderTask */
osThreadId_t RenderTaskHandle;
const osThreadAttr_t RenderTask_attributes = {
    .name = "RenderTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for InitTask */
osThreadId_t InitTaskHandle;
const osThreadAttr_t InitTask_attributes = { .name = "InitTask", .stack_size =
		256 * 4, .priority = (osPriority_t) osPriorityHigh, };
/* Definitions for MediumPriorityT */
osThreadId_t MediumPriorityTHandle;
const osThreadAttr_t MediumPriorityT_attributes = { .name = "MediumPriorityT",
		.stack_size = 256 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for LowPriorityTask */
osThreadId_t LowPriorityTaskHandle;
const osThreadAttr_t LowPriorityTask_attributes = { .name = "LowPriorityTask",
		.stack_size = 256 * 4, .priority = (osPriority_t) osPriorityLow, };


/* USER CODE BEGIN PV */
#define DWT_CTRL (*(volatile uint32_t*)0xE0001000)

static uint16_t adc_raw[2];          // [0]=VRX(PA0), [1]=VRY(PA1)
static float joy_x_f = 2048.0f;      // filtered joystick X (0..4095)
// ---- RTOS IPC objects ----
typedef struct {
  uint16_t joy_adc_x;
  uint8_t  sw_pressed;
} InputMsg_t;


static osMessageQueueId_t inputQ;   // Queue: InputTask -> GameTask
static osSemaphoreId_t frameSem;    // Semaphore: GameTask -> RenderTask
static osMutexId_t viewMutex;       // Mutex: protect shared view/state

/* ---------- Game internal state (owned by GameTask) ---------- */
static const int MAX_LEVEL = 6;

static int g_level = 1;
static int g_lives = 5;
static int g_score = 0;

static uint8_t g_game_started = 0;

static int g_paddle_w0 = 24;     // Level 1 width
static int g_paddle_w  = 24;
static int g_paddle_x  = 0;
static const int g_paddle_h = 3;
static const int g_paddle_y = 60;

static float g_ball_x = 64.0f;
static float g_ball_y = 58.0f;
static float g_ball_vx = 0.0f;
static float g_ball_vy = 0.0f;
static uint8_t g_ball_launched = 0;

static uint8_t g_level_cleared = 0;
static uint8_t g_game_over = 0;
static uint8_t g_game_complete = 0;
static int g_final_lives = 0;

/* Ball tuning */
static const float BASE_SPEED = 1.0f;
static const float MAX_VX = 2.4f;
static const float MIN_VY = 1.0f;

/* ---------- Shared view for rendering (protected by viewMutex) ---------- */
typedef enum {
  VIEW_START = 0,
  VIEW_PLAY,
  VIEW_LEVEL_CLEAR,
  VIEW_GAME_OVER,
  VIEW_GAME_COMPLETE
} GameViewScreen_t;


typedef struct {
  GameViewScreen_t screen;

  int level;
  int lives;
  int score;

  int paddle_x;
  int paddle_w;

  float ball_x;
  float ball_y;

  Brick bricks[BRICKS_MAX];
  int bricks_count;
} GameView_t;

static GameView_t g_view;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
void StartDefaultTask(void *argument);
void StartInitTask(void *argument);
void StartMediumPriorityTask(void *argument);
void StartLowPriorityTask(void *argument);
void StartInputTask(void *argument);
void StartGameTask(void *argument);
void StartRenderTask(void *argument);


/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

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
	MX_DMA_Init();
	MX_USART2_UART_Init();
	MX_ADC1_Init();
	MX_I2C1_Init();
	/* USER CODE BEGIN 2 */
	SEGGER_UART_init(500000);

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	NVIC_SetPriorityGrouping(0);
	SEGGER_SYSVIEW_Conf();

	//SEGGER_SYSVIEW_Start();

	// Start ADC + DMA for joystick (2 channels: X, Y)
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw, 2);

	// OLED boot screen (Option B)
	ssd1306_Init();
	ssd1306_Fill(Black);
	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString("BOOT", Font_11x18, White);
	ssd1306_UpdateScreen();

	/* USER CODE END 2 */

	/* Init scheduler */
	osKernelInitialize();

	/* USER CODE BEGIN RTOS_MUTEX */
	viewMutex = osMutexNew(NULL);
	/* USER CODE END RTOS_MUTEX */


	/* USER CODE BEGIN RTOS_SEMAPHORES */
	frameSem = osSemaphoreNew(1, 0, NULL); // max=1, initial=0 (render waits)
	/* USER CODE END RTOS_SEMAPHORES */


	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* USER CODE BEGIN RTOS_QUEUES */
	inputQ = osMessageQueueNew(8, sizeof(InputMsg_t), NULL); // 8 msgs deep
	/* USER CODE END RTOS_QUEUES */
	if (viewMutex == NULL || frameSem == NULL || inputQ == NULL) {
	  Error_Handler();
	}

	/* Create the thread(s) */
	InputTaskHandle  = osThreadNew(StartInputTask,  NULL, &InputTask_attributes);
	GameTaskHandle   = osThreadNew(StartGameTask,   NULL, &GameTask_attributes);
	RenderTaskHandle = osThreadNew(StartRenderTask, NULL, &RenderTask_attributes);


	/* creation of InitTask */
	//InitTaskHandle = osThreadNew(StartInitTask, NULL, &InitTask_attributes);

	/* creation of MediumPriorityT */
	//MediumPriorityTHandle = osThreadNew(StartMediumPriorityTask, NULL, &MediumPriorityT_attributes);

	/* creation of LowPriorityTask */
	//LowPriorityTaskHandle = osThreadNew(StartLowPriorityTask, NULL, &LowPriorityTask_attributes);

	/* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	/* USER CODE END RTOS_THREADS */

	/* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
	/* USER CODE END RTOS_EVENTS */

	/* Start scheduler */
	osKernelStart();

	/* We should never get here as control is now taken by the scheduler */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	/** Configure the main internal regulator output voltage
	 */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 16;
	RCC_OscInitStruct.PLL.PLLN = 336;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
	RCC_OscInitStruct.PLL.PLLQ = 7;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void) {

	/* USER CODE BEGIN ADC1_Init 0 */

	/* USER CODE END ADC1_Init 0 */

	ADC_ChannelConfTypeDef sConfig = { 0 };

	/* USER CODE BEGIN ADC1_Init 1 */

	/* USER CODE END ADC1_Init 1 */

	/** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
	 */
	hadc1.Instance = ADC1;
	hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV8;
	hadc1.Init.Resolution = ADC_RESOLUTION_12B;
	hadc1.Init.ScanConvMode = ENABLE;
	hadc1.Init.ContinuousConvMode = ENABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion = 2;
	hadc1.Init.DMAContinuousRequests = ENABLE;
	hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) {
		Error_Handler();
	}

	/** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	 */
	sConfig.Channel = ADC_CHANNEL_0;
	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}

	/** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	 */
	sConfig.Channel = ADC_CHANNEL_1;
	sConfig.Rank = 2;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN ADC1_Init 2 */

	/* USER CODE END ADC1_Init 2 */

}

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C1_Init(void) {

	/* USER CODE BEGIN I2C1_Init 0 */

	/* USER CODE END I2C1_Init 0 */

	/* USER CODE BEGIN I2C1_Init 1 */

	/* USER CODE END I2C1_Init 1 */
	hi2c1.Instance = I2C1;
	hi2c1.Init.ClockSpeed = 400000;
	hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
	hi2c1.Init.OwnAddress1 = 0;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN I2C1_Init 2 */

	/* USER CODE END I2C1_Init 2 */

}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void) {

	/* USER CODE BEGIN USART2_Init 0 */

	/* USER CODE END USART2_Init 0 */

	/* USER CODE BEGIN USART2_Init 1 */

	/* USER CODE END USART2_Init 1 */
	huart2.Instance = USART2;
	huart2.Init.BaudRate = 500000;
	if (HAL_UART_Init(&huart2) != HAL_OK) {
	  Error_Handler();
	}
	huart2.Init.WordLength = UART_WORDLENGTH_8B;
	huart2.Init.StopBits = UART_STOPBITS_1;
	huart2.Init.Parity = UART_PARITY_NONE;
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart2) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USART2_Init 2 */

	/* USER CODE END USART2_Init 2 */

}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

	/* DMA controller clock enable */
	__HAL_RCC_DMA2_CLK_ENABLE();

	/* DMA interrupt init */
	/* DMA2_Stream0_IRQn interrupt configuration */
	HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

	/*Configure GPIO pin : B1_Pin */
	GPIO_InitStruct.Pin = B1_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

	/*Configure GPIO pin : LD2_Pin */
	GPIO_InitStruct.Pin = LD2_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

	/*Configure GPIO pin : PA6 */
	GPIO_InitStruct.Pin = GPIO_PIN_6;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// Storage (owned/updated by GameTask)
static Brick g_bricks[BRICKS_MAX];
static int   g_bricks_count = 0;
static int   g_bricks_remaining = 0;

static void Bricks_Clear(void)
{
  for (int i = 0; i < BRICKS_MAX; i++) {
    g_bricks[i].alive = 0;
    g_bricks[i].hp = 0;
    g_bricks[i].x = 0;
    g_bricks[i].y = 0;
  }
  g_bricks_count = 0;
  g_bricks_remaining = 0;
}

static void Bricks_Add(uint8_t x, uint8_t y, uint8_t hp)
{
  if (g_bricks_count >= BRICKS_MAX) return;
  g_bricks[g_bricks_count].alive = 1;
  g_bricks[g_bricks_count].hp = hp;
  g_bricks[g_bricks_count].x = x;
  g_bricks[g_bricks_count].y = y;
  g_bricks_count++;
  g_bricks_remaining++;
}

static void Bricks_GridAdd(int col, int row, int x0, int y0, int dx, int dy, uint8_t hp)
{
  uint8_t x = (uint8_t)(x0 + col * dx);
  uint8_t y = (uint8_t)(y0 + row * dy);
  Bricks_Add(x, y, hp);
}

static void Bricks_LoadLevel(int lvl)
{
  Bricks_Clear();

  // Common spacing matching your patterns
  const int dx = BRICK_W + 3;
  const int dy = BRICK_H + 2;
  const int y0 = 16; // leave HUD space

  if (lvl == 1)
  {
    int xL = 14;
    Bricks_GridAdd(0,0,xL,y0,dx,dy,1); Bricks_GridAdd(1,0,xL,y0,dx,dy,1);
    Bricks_GridAdd(0,1,xL,y0,dx,dy,1); Bricks_GridAdd(1,1,xL,y0,dx,dy,1);
    Bricks_GridAdd(0,2,xL,y0,dx,dy,1);

    int xR = 128 - xL - (2*BRICK_W + 1*3);
    Bricks_GridAdd(0,0,xR,y0,dx,dy,1); Bricks_GridAdd(1,0,xR,y0,dx,dy,1);
    Bricks_GridAdd(0,1,xR,y0,dx,dy,1); Bricks_GridAdd(1,1,xR,y0,dx,dy,1);
    Bricks_GridAdd(1,2,xR,y0,dx,dy,1);

    Bricks_Add(58, (uint8_t)(y0 + dy), 1);
  }
  else if (lvl == 2)
  {
    Bricks_Add(56, (uint8_t)(y0 + 0*dy), 1);

    Bricks_Add(56, (uint8_t)(y0 + 1*dy), 1);
    Bricks_Add(56, (uint8_t)(y0 + 2*dy), 2);
    Bricks_Add(56, (uint8_t)(y0 + 3*dy), 1);

    Bricks_Add(40, (uint8_t)(y0 + 1*dy), 1);
    Bricks_Add(72, (uint8_t)(y0 + 1*dy), 1);
    Bricks_Add(24, (uint8_t)(y0 + 2*dy), 1);
    Bricks_Add(88, (uint8_t)(y0 + 2*dy), 1);

    Bricks_Add(40, (uint8_t)(y0 + 3*dy), 1);
    Bricks_Add(72, (uint8_t)(y0 + 3*dy), 1);
  }
  else if (lvl == 3)
  {
    int x0bar = 8;
    for (int i = 0; i < 7; i++) {
      uint8_t hp = (i == 3) ? 2 : 1;
      Bricks_Add((uint8_t)(x0bar + i*(BRICK_W+3)), (uint8_t)(y0 + 0*dy), hp);
    }

    int xL = 12;
    Bricks_GridAdd(0,1,xL,y0,dx,dy,1); Bricks_GridAdd(1,1,xL,y0,dx,dy,1);
    Bricks_GridAdd(0,2,xL,y0,dx,dy,2); Bricks_GridAdd(1,2,xL,y0,dx,dy,1);
    Bricks_GridAdd(0,3,xL,y0,dx,dy,1); Bricks_GridAdd(1,3,xL,y0,dx,dy,1);

    int xR = 128 - xL - (2*BRICK_W + 3);
    Bricks_GridAdd(0,1,xR,y0,dx,dy,1); Bricks_GridAdd(1,1,xR,y0,dx,dy,1);
    Bricks_GridAdd(0,2,xR,y0,dx,dy,1); Bricks_GridAdd(1,2,xR,y0,dx,dy,2);
    Bricks_GridAdd(0,3,xR,y0,dx,dy,1); Bricks_GridAdd(1,3,xR,y0,dx,dy,1);
  }
  else if (lvl == 4)
  {
    int xL = 10;
    for (int r = 0; r < 4; r++) {
      Bricks_GridAdd(0,r,xL,y0,dx,dy, (r==2)?2:1);
      Bricks_GridAdd(1,r,xL,y0,dx,dy, 1);
    }

    int xR = 128 - xL - (2*BRICK_W + 3);
    for (int r = 0; r < 4; r++) {
      Bricks_GridAdd(0,r,xR,y0,dx,dy, 1);
      Bricks_GridAdd(1,r,xR,y0,dx,dy, (r==0)?2:1);
    }

    Bricks_Add(56, (uint8_t)(y0 + 1*dy), 1);
    Bricks_Add(40, (uint8_t)(y0 + 2*dy), 1);
    Bricks_Add(72, (uint8_t)(y0 + 2*dy), 1);
    Bricks_Add(56, (uint8_t)(y0 + 3*dy), 1);

    Bricks_Add(56, (uint8_t)(y0 + 0*dy), 2);
    Bricks_Add(56, (uint8_t)(y0 + 4*dy), 2);
  }
  else if (lvl == 5)
  {
    int xb = 8;
    int yb = y0 + 5*dy;
    for (int i = 0; i < 7; i++) Bricks_Add((uint8_t)(xb + i*(BRICK_W+3)), (uint8_t)yb, 1);

    Bricks_Add(56, (uint8_t)(y0 + 3*dy), 3);

    Bricks_Add(16, (uint8_t)(y0 + 0*dy), 2);
    Bricks_Add(24, (uint8_t)(y0 + 1*dy), 2);
    Bricks_Add(32, (uint8_t)(y0 + 2*dy), 2);

    Bricks_Add(56, (uint8_t)(y0 + 1*dy), 1);
    Bricks_Add(40, (uint8_t)(y0 + 2*dy), 1);
    Bricks_Add(72, (uint8_t)(y0 + 2*dy), 1);

    Bricks_Add(96, (uint8_t)(y0 + 1*dy), 2);
    Bricks_Add(88, (uint8_t)(y0 + 3*dy), 2);

    Bricks_Add(112, (uint8_t)(y0 + 0*dy), 1);
  }
  else /* lvl == 6 */
  {
    int xL = 10;
    Bricks_GridAdd(0,0,xL,y0,dx,dy,1); Bricks_GridAdd(1,0,xL,y0,dx,dy,1);
    Bricks_GridAdd(0,1,xL,y0,dx,dy,1); Bricks_GridAdd(1,1,xL,y0,dx,dy,3);
    Bricks_GridAdd(0,2,xL,y0,dx,dy,1); Bricks_GridAdd(1,2,xL,y0,dx,dy,1);

    Bricks_Add(48, (uint8_t)(y0 + 0*dy), 3);
    Bricks_Add(64, (uint8_t)(y0 + 0*dy), 2);
    Bricks_Add(56, (uint8_t)(y0 + 1*dy), 3);
    Bricks_Add(72, (uint8_t)(y0 + 2*dy), 3);
    Bricks_Add(56, (uint8_t)(y0 + 2*dy), 1);

    int xR = 128 - xL - (2*BRICK_W + 3);
    Bricks_GridAdd(0,1,xR,y0,dx,dy,1); Bricks_GridAdd(1,1,xR,y0,dx,dy,1);
    Bricks_GridAdd(0,2,xR,y0,dx,dy,1); Bricks_GridAdd(1,2,xR,y0,dx,dy,1);
  }
}

// returns 1 if a brick was hit this frame (also applies damage + scoring)
// NOTE: needs access to g_ball_vy and score, so we will handle scoring in GameTask.
// For now, just damage and report hit.
static int Bricks_HitAt(float bx, float by)
{
  for (int i = 0; i < g_bricks_count; i++)
  {
    Brick *b = &g_bricks[i];
    if (!b->alive) continue;

    if (bx >= b->x && bx <= (b->x + BRICK_W) &&
        by >= b->y && by <= (b->y + BRICK_H))
    {
      if (b->hp > 0) b->hp--;
      if (b->hp == 0) {
        b->alive = 0;
        g_bricks_remaining--;
      }
      return 1;
    }
  }
  return 0;
}

static void Bricks_DrawFromSnapshot(const Brick *br, int count)
{
  for (int i = 0; i < count; i++)
  {
    const Brick *b = &br[i];
    if (!b->alive) continue;

    ssd1306_FillRectangle(b->x, b->y, b->x + BRICK_W, b->y + BRICK_H, White);

    if (b->hp == 2) {
      ssd1306_Line(b->x, b->y + (BRICK_H/2), b->x + BRICK_W, b->y + (BRICK_H/2), Black);
    } else if (b->hp >= 3) {
      ssd1306_Line(b->x, b->y + 1,           b->x + BRICK_W, b->y + 1,           Black);
      ssd1306_Line(b->x, b->y + (BRICK_H-2), b->x + BRICK_W, b->y + (BRICK_H-2), Black);
    }
  }
}

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static void Paddle_ApplyLevel(int lvl)
{
  float scale = 1.0f;
  for (int i = 1; i < lvl; i++) scale *= 0.9f;

  g_paddle_w = (int)(g_paddle_w0 * scale);
  if (g_paddle_w < 10) g_paddle_w = 10;

  g_paddle_x = clampi(g_paddle_x, 0, 127 - g_paddle_w);
}

/* USER CODE END 4 */



/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  for(;;) { osDelay(1000); }
  /* USER CODE END 5 */
}
void StartInputTask(void *argument)
{
  uint8_t prev_sw = 1;

  for (;;)
  {
    // Joystick X filtering (ADC DMA already running)
    float x = (float)adc_raw[0];
    joy_x_f = 0.8f * joy_x_f + 0.2f * x;

    // Button PA6 (pull-up). sw=1 not pressed, sw=0 pressed
    uint8_t sw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t pressed = (prev_sw == 1 && sw == 0) ? 1 : 0;
    prev_sw = sw;

    InputMsg_t msg;
    msg.joy_adc_x  = (uint16_t)joy_x_f;  // 0..4095
    msg.sw_pressed = pressed;

    (void)osMessageQueuePut(inputQ, &msg, 0, 0);

    osDelay(5); // ~200 Hz input sampling
  }
}


void StartGameTask(void *argument)
{
  // Fixed tick: 16 ms (~60 Hz)
  uint32_t next = osKernelGetTickCount();

  // Local latest input (drain queue)
  InputMsg_t in;
  uint16_t latest_adc_x = 2048;
  uint8_t  sw_edge = 0;

  // Init game state
  g_level = 1;
  g_lives = 5;
  g_score = 0;
  g_game_over = 0;
  g_game_complete = 0;
  g_final_lives = 0;

  g_ball_launched = 0;
  g_level_cleared = 0;

  g_paddle_x = 0;
  Paddle_ApplyLevel(g_level);
  g_paddle_x = (127 - g_paddle_w) / 2;

  g_ball_x = 64.0f;
  g_ball_y = (float)(g_paddle_y - 2);
  g_ball_vx = 0.0f;
  g_ball_vy = 0.0f;

  Bricks_LoadLevel(g_level);

  for (;;)
  {
    next += 16;
    osDelayUntil(next);

    // ---- Read latest input from queue (drain, keep last) ----
    sw_edge = 0;
    while (osMessageQueueGet(inputQ, &in, NULL, 0) == osOK) {
      latest_adc_x = in.joy_adc_x;
      if (in.sw_pressed) sw_edge = 1; // any press since last tick
    }

    // ---- Map joystick ADC to paddle X using current paddle width ----
    // map 0..4095 -> 0..(127 - g_paddle_w)
    int max_x = 127 - g_paddle_w;
    if (max_x < 0) max_x = 0;
    g_paddle_x = (int)((latest_adc_x / 4095.0f) * (float)max_x);
    g_paddle_x = clampi(g_paddle_x, 0, max_x);

    // ---- Handle SW press ----
    if (sw_edge)
    {
      if (g_game_over)
      {
        // Restart whole game
        g_level = 1;
        g_lives = 5;
        g_score = 0;
        g_game_started = 0;
        g_game_over = 0;
        g_game_complete = 0;
        g_final_lives = 0;

        g_level_cleared = 0;
        g_ball_launched = 0;
        g_ball_vx = 0.0f;
        g_ball_vy = 0.0f;

        Paddle_ApplyLevel(g_level);
        g_paddle_x = (127 - g_paddle_w) / 2;

        Bricks_LoadLevel(g_level);
      }
      else if (g_level_cleared)
      {
        if (g_level >= MAX_LEVEL)
        {
          // Finish final level -> final screen
          g_game_over = 1;
          g_game_complete = 1;
          g_final_lives = g_lives;
        }
        else
        {
          // Next level
          g_level++;
          g_level_cleared = 0;

          Paddle_ApplyLevel(g_level);
          g_paddle_x = clampi(g_paddle_x, 0, 127 - g_paddle_w);

          Bricks_LoadLevel(g_level);

          // Reset ball
          g_ball_launched = 0;
          g_ball_vx = 0.0f;
          g_ball_vy = 0.0f;
          g_ball_x = (float)g_paddle_x + (g_paddle_w * 0.5f);
          g_ball_y = (float)(g_paddle_y - 2);
        }
      }
      else if (!g_game_started)
      {
        g_game_started = 1;
        g_ball_launched = 0;
      }
      else if (!g_ball_launched)
      {
        // launch ball
        g_ball_launched = 1;

        float paddle_center = (float)g_paddle_x + (g_paddle_w * 0.5f);
        float offset = (g_ball_x - paddle_center) / (g_paddle_w * 0.5f);
        if (offset < -1.0f) offset = -1.0f;
        if (offset >  1.0f) offset =  1.0f;

        g_ball_vx = BASE_SPEED * offset;
        g_ball_vy = -BASE_SPEED;
      }
    }

    // ---- Ball follows paddle if not launched ----
    if (!g_ball_launched)
    {
      g_ball_x = (float)g_paddle_x + (g_paddle_w * 0.5f);
      g_ball_y = (float)(g_paddle_y - 2);
    }
    else if (!g_game_over && !g_level_cleared)
    {
      // ---- Physics update ----
      g_ball_x += g_ball_vx;
      g_ball_y += g_ball_vy;

      // Walls
      if (g_ball_x < 1.0f)   { g_ball_x = 1.0f;   g_ball_vx = -g_ball_vx; }
      if (g_ball_x > 126.0f) { g_ball_x = 126.0f; g_ball_vx = -g_ball_vx; }
      if (g_ball_y < 1.0f)   { g_ball_y = 1.0f;   g_ball_vy = -g_ball_vy; }

      // ---- Bricks (ONLY ONCE per tick) ----
      int before = g_bricks_remaining;
      if (Bricks_HitAt(g_ball_x, g_ball_y))
      {
        g_ball_vy = -g_ball_vy;

        if (g_bricks_remaining < before) {
          // brick destroyed (this hit)
          g_score += 10;
        } else {
          // brick damaged (still alive)
          g_score += 5;
        }
      }

      // Level cleared?
      if (g_bricks_remaining <= 0)
      {
        g_level_cleared = 1;
        g_ball_launched = 0;
        g_ball_vx = 0.0f;
        g_ball_vy = 0.0f;
      }

      // Paddle collision (only when ball moving down)
      if (g_ball_vy > 0.0f)
      {
        float px1 = (float)g_paddle_x;
        float px2 = (float)(g_paddle_x + g_paddle_w);
        float py1 = (float)g_paddle_y;

        if (g_ball_y >= (py1 - 1.0f) && g_ball_y <= (py1 + 2.0f) &&
            g_ball_x >= px1 && g_ball_x <= px2)
        {
          float paddle_center = (px1 + px2) * 0.5f;
          float hit = (g_ball_x - paddle_center) / (g_paddle_w * 0.5f);
          if (hit < -1.0f) hit = -1.0f;
          if (hit >  1.0f) hit =  1.0f;

          g_ball_vy = -fabsf(g_ball_vy);
          if (-g_ball_vy < MIN_VY) g_ball_vy = -MIN_VY;

          g_ball_vx = BASE_SPEED * 1.2f * hit;
          if (g_ball_vx >  MAX_VX) g_ball_vx =  MAX_VX;
          if (g_ball_vx < -MAX_VX) g_ball_vx = -MAX_VX;

          g_ball_y = py1 - 2.0f;
        }
      }

      // Miss / lose life
      if (g_ball_y > 63.0f)
      {
        g_lives--;
        g_ball_launched = 0;
        g_ball_vx = 0.0f;
        g_ball_vy = 0.0f;

        if (g_lives <= 0)
        {
          g_game_over = 1;
          g_level_cleared = 0;
          g_game_complete = 0;
        }
      }
    }

    // ---- Update shared view snapshot under mutex ----
    osMutexAcquire(viewMutex, osWaitForever);

    if (g_game_over)
    {
      g_view.screen = g_game_complete ? VIEW_GAME_COMPLETE : VIEW_GAME_OVER;
      g_view.level  = g_level;
      g_view.lives  = g_game_complete ? g_final_lives : 0;
      g_view.score  = g_score;
      g_view.paddle_x = g_paddle_x;
      g_view.paddle_w = g_paddle_w;
      g_view.ball_x = g_ball_x;
      g_view.ball_y = g_ball_y;
      g_view.bricks_count = 0; // don't draw bricks on end screens
    }
    else if (g_level_cleared)
    {
      g_view.screen = VIEW_LEVEL_CLEAR;
      g_view.level  = g_level;
      g_view.lives  = g_lives;
      g_view.score  = g_score;
      g_view.paddle_x = g_paddle_x;
      g_view.paddle_w = g_paddle_w;
      g_view.ball_x = g_ball_x;
      g_view.ball_y = g_ball_y;

      // optional: don't draw bricks on level clear screen
      g_view.bricks_count = 0;
    }
    else if (!g_game_started)
    {
      g_view.screen = VIEW_START;
    }
    else
    {
      g_view.screen = VIEW_PLAY;

      g_view.level  = g_level;
      g_view.lives  = g_lives;
      g_view.score  = g_score;

      g_view.paddle_x = g_paddle_x;
      g_view.paddle_w = g_paddle_w;

      g_view.ball_x = g_ball_x;
      g_view.ball_y = g_ball_y;

      // Copy bricks snapshot for render
      g_view.bricks_count = g_bricks_count;
      for (int i = 0; i < g_bricks_count; i++) {
        g_view.bricks[i] = g_bricks[i];
      }
    }

    osMutexRelease(viewMutex);

    // Wake render task
    osSemaphoreRelease(frameSem);
  }
}


void StartRenderTask(void *argument)
{
  GameView_t v; // local snapshot

  for (;;)
  {
    osSemaphoreAcquire(frameSem, osWaitForever);

    // Copy snapshot under mutex (short lock)
    osMutexAcquire(viewMutex, osWaitForever);
    v = g_view;
    osMutexRelease(viewMutex);

    // ---- Render based on snapshot ----
    ssd1306_Fill(Black);

    if (v.screen == VIEW_START)
    {
      ssd1306_SetCursor(10, 10);
      ssd1306_WriteString("BRICK BREAKER", Font_7x10, White);

      ssd1306_SetCursor(20, 36);
      ssd1306_WriteString("SW to start", Font_6x8, White);
    }

    else if (v.screen == VIEW_GAME_OVER)
    {
      ssd1306_SetCursor(0, 0);
      ssd1306_WriteString("GAME OVER", Font_11x18, White);

      char buf[32];
      ssd1306_SetCursor(0, 24);
      snprintf(buf, sizeof(buf), "Score: %d", v.score);
      ssd1306_WriteString(buf, Font_6x8, White);

      ssd1306_SetCursor(0, 34);
      ssd1306_WriteString("SW=Restart", Font_6x8, White);
    }
    else if (v.screen == VIEW_GAME_COMPLETE)
    {
      ssd1306_SetCursor(0, 0);
      ssd1306_WriteString("COMPLETE!", Font_11x18, White);

      char buf[32];
      ssd1306_SetCursor(0, 24);
      snprintf(buf, sizeof(buf), "Score: %d", v.score);
      ssd1306_WriteString(buf, Font_6x8, White);

      ssd1306_SetCursor(0, 34);
      snprintf(buf, sizeof(buf), "Lives: %d", v.lives);
      ssd1306_WriteString(buf, Font_6x8, White);

      ssd1306_SetCursor(0, 44);
      ssd1306_WriteString("SW=Restart", Font_6x8, White);
    }
    else if (v.screen == VIEW_LEVEL_CLEAR)
    {
      char buf[32];
      ssd1306_SetCursor(0, 0);
      snprintf(buf, sizeof(buf), "LEVEL %d CLEAR", v.level);
      ssd1306_WriteString(buf, Font_6x8, White);

      ssd1306_SetCursor(0, 12);
      if (v.level >= MAX_LEVEL) ssd1306_WriteString("SW=Finish", Font_6x8, White);
      else                     ssd1306_WriteString("SW=Next", Font_6x8, White);

      ssd1306_SetCursor(0, 24);
      snprintf(buf, sizeof(buf), "Score: %d", v.score);
      ssd1306_WriteString(buf, Font_6x8, White);
    }
    else
    {
      // HUD
      char hud[32];
      ssd1306_SetCursor(0, 0);
      snprintf(hud, sizeof(hud), "Lv:%d L:%d S:%d", v.level, v.lives, v.score);
      ssd1306_WriteString(hud, Font_6x8, White);


      // Bricks (from snapshot)
      Bricks_DrawFromSnapshot(v.bricks, v.bricks_count);

      // Paddle
      ssd1306_FillRectangle((uint8_t)v.paddle_x,
                            (uint8_t)g_paddle_y,
                            (uint8_t)(v.paddle_x + v.paddle_w),
                            (uint8_t)(g_paddle_y + g_paddle_h),
                            White);

      // Ball
      ssd1306_DrawPixel((uint8_t)v.ball_x, (uint8_t)v.ball_y, White);
    }

    ssd1306_UpdateScreen();
  }
}






/* USER CODE BEGIN Header_StartInitTask */
/**
 * @brief Function implementing the InitTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartInitTask */
void StartInitTask(void *argument) {
	/* USER CODE BEGIN StartInitTask */
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
	/* USER CODE END StartInitTask */
}

/* USER CODE BEGIN Header_StartMediumPriorityTask */
/**
 * @brief Function implementing the MediumPriorityT thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartMediumPriorityTask */
void StartMediumPriorityTask(void *argument) {
	/* USER CODE BEGIN StartMediumPriorityTask */
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
	/* USER CODE END StartMediumPriorityTask */
}

/* USER CODE BEGIN Header_StartLowPriorityTask */
/**
 * @brief Function implementing the LowPriorityTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartLowPriorityTask */
void StartLowPriorityTask(void *argument) {
	/* USER CODE BEGIN StartLowPriorityTask */
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
	/* USER CODE END StartLowPriorityTask */
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM2 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	/* USER CODE BEGIN Callback 0 */

	/* USER CODE END Callback 0 */
	if (htim->Instance == TIM2) {
		HAL_IncTick();
	}
	/* USER CODE BEGIN Callback 1 */

	/* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
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
