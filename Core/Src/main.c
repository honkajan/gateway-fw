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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0

#define FW_VERSION_STR  "v0.2.0"

#define FW_ID_STRING "STM32F103C8 UART ping-pong demo " FW_VERSION_STR

// Pins
#define NRF_CE_PORT   GPIOB
#define NRF_CE_PIN    GPIO_PIN_0
#define NRF_CSN_PORT  GPIOB
#define NRF_CSN_PIN   GPIO_PIN_1

static inline void nrf_ce(int on)
{
  HAL_GPIO_WritePin(NRF_CE_PORT, NRF_CE_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static inline void nrf_csn(int on)
{
  HAL_GPIO_WritePin(NRF_CSN_PORT, NRF_CSN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// Commands + regs
#define NRF_CMD_R_REGISTER     0x00
#define NRF_CMD_W_REGISTER     0x20
#define NRF_CMD_R_RX_PAYLOAD   0x61
#define NRF_CMD_W_TX_PAYLOAD   0xA0
#define NRF_CMD_FLUSH_TX       0xE1
#define NRF_CMD_FLUSH_RX       0xE2
#define NRF_CMD_NOP            0xFF

#define NRF_REG_CONFIG         0x00
#define NRF_REG_EN_AA          0x01
#define NRF_REG_EN_RXADDR      0x02
#define NRF_REG_SETUP_RETR     0x04
#define NRF_REG_RF_CH          0x05
#define NRF_REG_RF_SETUP       0x06
#define NRF_REG_STATUS         0x07
#define NRF_REG_OBSERVE_TX     0x08
#define NRF_REG_RPD            0x09
#define NRF_REG_RX_ADDR_P0     0x0A
#define NRF_REG_TX_ADDR        0x10
#define NRF_REG_RX_PW_P0       0x11
#define NRF_REG_FIFO_STATUS    0x17




/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
uint8_t ch;
static char line[64];
static size_t line_len = 0;



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_send_str(UART_HandleTypeDef *huart, const char *s)
{
  HAL_UART_Transmit(huart, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

static void uart_send_version(UART_HandleTypeDef *huart)
{
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "%d.%d.%d\n",
                   FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
  if (n > 0) {
    uart_send_str(huart, buf);
  } else {
    uart_send_str(huart, "ERR\n");
  }
}

static void uart_send_uptime(UART_HandleTypeDef *huart)
{
  uint32_t ms = HAL_GetTick();  // milliseconds since boot
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)ms);
  if (n > 0) {
    uart_send_str(huart, buf);
  } else {
    uart_send_str(huart, "ERR\n");
  }
}


static void uart_printf(const char *fmt, ...)
{
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (n < 0) return;
  if (n > (int)sizeof(buf)) n = sizeof(buf);

  HAL_UART_Transmit(&huart1, (uint8_t*)buf, (uint16_t)strlen(buf), HAL_MAX_DELAY);
}

// SPI xfer
static uint8_t nrf_spi_xfer(uint8_t b)
{
  uint8_t rx = 0;
  HAL_SPI_TransmitReceive(&hspi1, &b, &rx, 1, HAL_MAX_DELAY);
  return rx;
}

// Register IO
static uint8_t nrf_get_status_cmd(void)
{
  uint8_t st;
  nrf_csn(0);
  st = nrf_spi_xfer(NRF_CMD_NOP);
  nrf_csn(1);
  return st;
}

static uint8_t nrf_read_reg(uint8_t reg)
{
  uint8_t v;
  nrf_csn(0);
  nrf_spi_xfer(NRF_CMD_R_REGISTER | (reg & 0x1F));
  v = nrf_spi_xfer(NRF_CMD_NOP);
  nrf_csn(1);
  return v;
}

static void nrf_write_reg(uint8_t reg, uint8_t v)
{
  nrf_csn(0);
  nrf_spi_xfer(NRF_CMD_W_REGISTER | (reg & 0x1F));
  nrf_spi_xfer(v);
  nrf_csn(1);
}

static void nrf_write_reg_buf(uint8_t reg, const uint8_t *buf, uint8_t len)
{
  nrf_csn(0);
  nrf_spi_xfer(NRF_CMD_W_REGISTER | (reg & 0x1F));
  for (uint8_t i = 0; i < len; i++) nrf_spi_xfer(buf[i]);
  nrf_csn(1);
}

// FIFO flush + IRQ clear
static void nrf_flush_rx(void)
{
  nrf_csn(0); nrf_spi_xfer(NRF_CMD_FLUSH_RX); nrf_csn(1);
}

static void nrf_flush_tx(void)
{
  nrf_csn(0); nrf_spi_xfer(NRF_CMD_FLUSH_TX); nrf_csn(1);
}

static void nrf_clear_irqs(void)
{
  // Clear RX_DR(6), TX_DS(5), MAX_RT(4)
  nrf_write_reg(NRF_REG_STATUS, 0x70);
}

// Payload IO
static int nrf_rx_available(void)
{
  uint8_t fifo = nrf_read_reg(NRF_REG_FIFO_STATUS);
  return ((fifo & 0x01u) == 0u); // RX_EMPTY bit0 == 0 => data available
}

static void nrf_read_payload32(uint8_t *p32)
{
  nrf_csn(0);
  nrf_spi_xfer(NRF_CMD_R_RX_PAYLOAD);
  for (int i = 0; i < 32; i++) p32[i] = nrf_spi_xfer(NRF_CMD_NOP);
  nrf_csn(1);
}

static void nrf_write_payload32(const uint8_t *p32)
{
  nrf_csn(0);
  nrf_spi_xfer(NRF_CMD_W_TX_PAYLOAD);
  for (int i = 0; i < 32; i++) nrf_spi_xfer(p32[i]);
  nrf_csn(1);
}


// Mode switching
static void nrf_set_rx_mode(void)
{
  nrf_ce(0);
  nrf_write_reg(NRF_REG_CONFIG, 0x0F); // PWR_UP=1, PRIM_RX=1, CRC enabled
  HAL_Delay(2);
  nrf_ce(1);
}

static void nrf_set_tx_mode(void)
{
  nrf_ce(0);
  nrf_write_reg(NRF_REG_CONFIG, 0x0E); // PWR_UP=1, PRIM_RX=0, CRC enabled
  HAL_Delay(2);
}


// Common link init (call on both nodes)
static void nrf_init_link_common(void)
{
  static const uint8_t addr[5] = {0xE7,0xE7,0xE7,0xE7,0xE7};

  // Electrical idle + settle
  nrf_ce(0);
  nrf_csn(1);
  HAL_Delay(10);

  // Your “sync kick” (good!)
  nrf_flush_rx();
  nrf_flush_tx();
  nrf_clear_irqs();

  // Channel
  nrf_write_reg(NRF_REG_RF_CH, 76);

  // Auto-ack on pipe0
  nrf_write_reg(NRF_REG_EN_AA, 0x01);
  // Enable pipe0
  nrf_write_reg(NRF_REG_EN_RXADDR, 0x01);

  // Retries: ARD=4 (1500us), ARC=15
  nrf_write_reg(NRF_REG_SETUP_RETR, 0x4F);

  // RF_SETUP: 250kbps, -6dBm
  nrf_write_reg(NRF_REG_RF_SETUP, 0x24);

  // Addresses for pipe0 and TX (must match for auto-ack)
  nrf_write_reg_buf(NRF_REG_RX_ADDR_P0, addr, 5);
  nrf_write_reg_buf(NRF_REG_TX_ADDR, addr, 5);

  // Fixed payload size
  nrf_write_reg(NRF_REG_RX_PW_P0, 32);

  nrf_flush_rx();
  nrf_flush_tx();
  nrf_clear_irqs();

  // Start in RX mode
  nrf_set_rx_mode();
}

typedef struct
{
  int     rc;          // 0=PONG OK, -1=MAX_RT, -2=TIMEOUT, -3=TXWAIT timeout
  uint8_t st_tx;       // STATUS byte snapshot at TX completion
  uint8_t observe_tx;  // OBSERVE_TX snapshot at TX completion (ARC_CNT + PLOS_CNT)
  uint8_t rpd;         // RPD snapshot in RX mode (0/1)
  uint8_t tx_acked;    // 1 if TX_DS observed (ACK received), else 0
} gw_ping_res_t;


// Living-room node logic: send “PING” once per second and wait for “PONG”
static gw_ping_res_t gateway_send_ping_wait_pong(void)
{
  gw_ping_res_t res;
  memset(&res, 0, sizeof(res));
  res.rc = -2; // default TIMEOUT

  uint8_t tx[32] = {0};
  tx[0]='P'; tx[1]='I'; tx[2]='N'; tx[3]='G';

  // --- TX phase ---
  nrf_set_tx_mode();
  nrf_clear_irqs();
  nrf_flush_tx();
  nrf_write_payload32(tx);

  nrf_ce(1);
  HAL_Delay(1);
  nrf_ce(0);

  // Wait TX done or max retries
  uint32_t start = HAL_GetTick();
  while ((HAL_GetTick() - start) < 100) {
    uint8_t st = nrf_get_status_cmd();

    if (st & (1u<<5)) { // TX_DS
      res.st_tx = st;
      res.tx_acked = 1;
      nrf_clear_irqs();
      res.observe_tx = nrf_read_reg(NRF_REG_OBSERVE_TX);
      break;
    }

    if (st & (1u<<4)) { // MAX_RT
      res.st_tx = st;
      res.tx_acked = 0;
      nrf_clear_irqs();
      res.observe_tx = nrf_read_reg(NRF_REG_OBSERVE_TX);
      nrf_flush_tx();
      nrf_set_rx_mode();
      res.rc = -1;
      return res;
    }
  }

  // Neither TX_DS nor MAX_RT seen within window
  if (res.st_tx == 0 && res.observe_tx == 0) {
    res.st_tx = nrf_get_status_cmd();
    res.observe_tx = nrf_read_reg(NRF_REG_OBSERVE_TX);
    nrf_set_rx_mode();
    res.rc = -3; // TXWAIT timeout
    return res;
  }

  // --- RX wait phase (for PONG) ---
  nrf_set_rx_mode();

  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < 200) {
    if (nrf_rx_available()) {
      uint8_t rx[32];
      nrf_read_payload32(rx);
      nrf_clear_irqs();
      if (rx[0]=='P' && rx[1]=='O' && rx[2]=='N' && rx[3]=='G') {
        res.rc = 0;
        break;
      }
    }
  }

  // RPD is meaningful while in RX mode
  res.rpd = (uint8_t)(nrf_read_reg(NRF_REG_RPD) & 1u);
  return res;
}

typedef struct {
  uint32_t ping_total;
  uint32_t pong_ok;
  uint32_t fail_timeout;
  uint32_t fail_maxrt;
  uint32_t retries_sum;
} gw_stats_t;

static void gw_handle_ping_result(gw_stats_t *s, const gw_ping_res_t *r)
{
  // Decode nRF OBSERVE_TX + RPD
  const uint8_t arc_cnt  = (uint8_t)(r->observe_tx & 0x0F);         // retries used for last TX
  const uint8_t plos_cnt = (uint8_t)((r->observe_tx >> 4) & 0x0F);  // 4-bit trend counter
  const uint8_t rpd      = (uint8_t)(r->rpd & 0x01);

  s->ping_total++;
  s->retries_sum += arc_cnt;

  if (r->rc == 0) {
    s->pong_ok++;
    uart_printf("PONG OK   retries=%u plos=%u RPD=%u ack=%u st=0x%02X  ok=%lu/%lu rt_fail=%lu\r\n",
                arc_cnt, plos_cnt, rpd,
                (unsigned)r->tx_acked, (unsigned)r->st_tx,
                (unsigned long)s->pong_ok, (unsigned long)s->ping_total,
                (unsigned long)(s->fail_timeout + s->fail_maxrt));
  } else if (r->rc == -1) {
    s->fail_maxrt++;
    uart_printf("FAIL MAX_RT   retries=%u plos=%u RPD=%u ack=%u st=0x%02X  maxrt=%lu/%lu\r\n",
                arc_cnt, plos_cnt, rpd,
                (unsigned)r->tx_acked, (unsigned)r->st_tx,
                (unsigned long)s->fail_maxrt, (unsigned long)s->ping_total);
  } else if (r->rc == -2) {
    s->fail_timeout++;
    uart_printf("FAIL TIMEOUT  retries=%u plos=%u RPD=%u ack=%u st=0x%02X  timeout=%lu/%lu\r\n",
                arc_cnt, plos_cnt, rpd,
                (unsigned)r->tx_acked, (unsigned)r->st_tx,
                (unsigned long)s->fail_timeout, (unsigned long)s->ping_total);
  } else {
    // TXWAIT timeout or other internal code
    uart_printf("FAIL rc=%d   retries=%u plos=%u RPD=%u ack=%u st=0x%02X\r\n",
                r->rc, arc_cnt, plos_cnt, rpd,
                (unsigned)r->tx_acked, (unsigned)r->st_tx);
  }
}

static void gw_maybe_print_stats(const gw_stats_t *s, uint32_t period)
{
  if (period == 0u) {
    return;
  }

  if ((s->ping_total % period) == 0u) {
    const uint32_t rt_fail = s->fail_timeout + s->fail_maxrt;
    const uint32_t avg_retry_x100 = (s->ping_total ? (s->retries_sum * 100u) / s->ping_total : 0u);

    uart_printf("STATS: ok=%lu fail=%lu (timeout=%lu maxrt=%lu) avgRetry=%lu.%02lu\r\n",
                (unsigned long)s->pong_ok,
                (unsigned long)rt_fail,
                (unsigned long)s->fail_timeout,
                (unsigned long)s->fail_maxrt,
                (unsigned long)(avg_retry_x100 / 100u),
                (unsigned long)(avg_retry_x100 % 100u));
  }
}



static void nrf_print_rf_setup(void)
{
  uint8_t rf = nrf_read_reg(NRF_REG_RF_SETUP);

  const char *rate = "1Mbps";
  if (rf & (1u << 5)) rate = "250kbps";
  else if (rf & (1u << 3)) rate = "2Mbps";

  uint8_t pwr = (rf >> 1) & 0x03;
  const char *pwr_s = "?";
  switch (pwr) {
    case 0: pwr_s = "-18dBm"; break;
    case 1: pwr_s = "-12dBm"; break;
    case 2: pwr_s = "-6dBm";  break;
    case 3: pwr_s = "0dBm";   break;
  }

  uint8_t retr = nrf_read_reg(NRF_REG_SETUP_RETR);
  uint8_t ard = (retr >> 4) & 0x0F;
  uint8_t arc = retr & 0x0F;

  uart_printf(
    "NRF: ch=%u rate=%s txpwr=%s retr=%uus x%u\r\n",
    nrf_read_reg(NRF_REG_RF_CH),
    rate,
    pwr_s,
    (ard + 1) * 250,
    arc
  );
}

static void host_cmd_poll_uart1(void)
{
  static uint8_t ch;
  static char line[64];
  static size_t line_len = 0;

  // Non-blocking poll: read as many available bytes as possible
  while (HAL_UART_Receive(&huart1, &ch, 1, 0) == HAL_OK)
  {
    if (ch == '\r')
    {
      // ignore CR
    }
    else if (ch == '\n')
    {
      line[line_len] = '\0';

      if (strcmp(line, "PING") == 0)
      {
        uart_send_str(&huart1, "PONG\n");
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // optional
      }
      else if (strcmp(line, "ID?") == 0)
      {
        uart_send_str(&huart1, FW_ID_STRING "\n");
      }
      else if (strcmp(line, "VER?") == 0)
      {
        uart_send_version(&huart1);
      }
      else if (strcmp(line, "UPTIME?") == 0)
      {
        uart_send_uptime(&huart1);
      }
      else
      {
        uart_send_str(&huart1, "ERR\n");
      }

      line_len = 0;
    }
    else
    {
      if (line_len < (sizeof(line) - 1))
      {
        line[line_len++] = (char)ch;
      }
      else
      {
        // overflow -> reset and complain
        line_len = 0;
        uart_send_str(&huart1, "ERR\n");
      }
    }
  }
}



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
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  nrf_init_link_common();
  nrf_print_rf_setup();
  uart_printf("Gateway NRF STATUS=0x%02X\r\n", nrf_get_status_cmd());


//  uart_send_str(&huart1, "READY\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  static gw_stats_t s = {0};
	  static uint32_t next_ping_ms = 0;

	  // Always keep UART responsive
	  host_cmd_poll_uart1();

	  // Run RF ping once per second
	  const uint32_t now = HAL_GetTick();
	  if ((int32_t)(now - next_ping_ms) >= 0)
	  {
	    next_ping_ms = now + 1000u;

	    gw_ping_res_t r = gateway_send_ping_wait_pong();
	    gw_handle_ping_result(&s, &r);
	    gw_maybe_print_stats(&s, 60u);
	  }

	  // Optional: tiny sleep to reduce CPU burn (still responsive)
	  // HAL_Delay(1);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
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

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
