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
#include <stdint.h>
#include <stdlib.h>
#include "ili9341.h"
#include "esp.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

// 시스템 상태 정의 (화면)
typedef enum{
	STATE_ROOT,             // 메인 메뉴
	STATE_STATUS_VIEW,      // 모니터링 화면
	STATE_DOUGH_MENU,       // 반죽 선택 화면
	STATE_POWER_MENU        // 전원 제어 화면
} SystemState;

// 운전 상태 정의 (서버 권한 로직 적용) (RUN, PAUSE, END, PENDING)
typedef enum {
	OP_END = 0,			// 정지 (초기화 상태)
	OP_RUN,					// 가동 중 (시간 흐름)
	OP_PAUSE,				// 일시 정지 (시간 멈춤)
} OperationMode;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CONTAINER_DEPTH_MM 90.0 - 20.0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */

OperationMode opState = OP_END;			//현재 운전 상태 변수

SystemState currentState = STATE_ROOT;

// [1] 메뉴 텍스트
const char *rootMenu[]  = {"1. View Status", "2. Select Dough", "3. Control Power"};
const char *doughMenu[] = {"1. Liquid", "2. Dure", "3. P.M.", "4. Return"};
const char *powerMenu[] = {"1. Run", "2. Pause", "3. End", "4. Return"};

// [2] UI 제어 변수
uint16_t menuCursor = 0;
uint16_t isLcdOn = 1;

// [3] 데이터 변수
char val_state[20] = "LOADING...";  // 화면에 표시될 상태 문자열 (RUNNING, PAUSED, STOPPED)
char val_doughType[20] = "None";
char targetDeviceID[10] = "0001"; // 내가 모니터링할 타겟 ID

// [3-1] 환경 데이터
char val_temp[10] = "00.0";
char val_humi[10] = "00";
char val_co2[10] = "Wait";

// [3-2] 시간 및 높이 데이터
float val_height = 0.0;
char val_startTime[25] = "--/-- --:--";

uint32_t elapsed_sec = 0;      // 경과 시간 (초 단위 카운트)

// [4] 와이파이 버퍼
#define CMD_BUFFER_SIZE 256
char wifiCmdBuf[CMD_BUFFER_SIZE];
uint16_t wifiCmdIdx = 0;
uint8_t isCmdRecording = 0;

	// 재접속 타이머 변수
uint32_t lastConnectionCheck = 0;
	// 데이터 전송용 타이머 변수
uint32_t lastDataSendTick = 0;
	// 와이파이 상태 추적 변수 (0: 연결됨, 1: 끊김)
static uint8_t prevWifiStatus = 0;


static uint8_t isCheckingStatus = 0; // 0: 데이터 수신 모드, 1: 연결 점검 모드

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
void Reset_To_MainScreen(void);
void Process_Wifi_Messages(void);
void Show_Popup(char *str1, uint16_t color1, char *str2, uint16_t color2);
void ILI9341_WriteCentered(uint16_t y_pos, char *str, FontDef font, uint16_t color, uint16_t bg_color);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern UART_HandleTypeDef huart2;		//PC 디버깅 용
extern UART_HandleTypeDef huart6;		//WIFI 용
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
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  printf("Start main() - wifi\r\n");
  int ret =0;
   ret |= drv_uart_init();
   ret |= drv_esp_init();
   if(ret != 0)
   {
 	  printf("Esp response error\r\n");
 	  Error_Handler();
   }

  	  // 1. LCD 초기화
   printf("TFT Initializing...\r\n");
   ILI9341_Init();
   ILI9341_FillScreen(BLACK);

   printf("TFT Init Done.\r\n");

  	  // 2. 와이파이 초기화
   	  	  // 부팅 로고
  ILI9341_WriteCentered(140, "Welcome, my baker!", Font_11x18, WHITE, BLACK);

  	  	  // 물리적 하드 리셋 (PA1) - 모듈 먹통 방지 (이 동안 환영 메시지 표시됨)
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET); // Low
  HAL_Delay(100);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);   // High
  HAL_Delay(1800); // 부팅 대기

  // ---------------------------------------------------------
  // [STEP 1] 접속 시작 전 안내
  // ---------------------------------------------------------
  TFT_Clear(); // 화면을 한번 지우고 시작
  // 접속 함수(AiotClient_Init) 호출 전에 안내 문구 출력
  ILI9341_WriteString(40, 130, "Connecting Network...", Font_8x10, CYAN, BLACK);
  ILI9341_WriteString(70, 150, "Please Wait...", Font_8x10, WHITE, BLACK);

  // 와이파이 접속 시도 (시간 소요됨)
  AiotClient_Init();

  // ---------------------------------------------------------
  // [STEP 2] 접속 완료 메시지
  // ---------------------------------------------------------
  TFT_Clear(); // 이전 "Connecting..." 메시지 삭제

  // 조금 더 큰 폰트(Font_11x18)를 써서 강조하면 보기 좋습니다. (없으면 Font_8x10 사용)
  ILI9341_WriteCentered(140, "Connected!", Font_11x18, GREEN, BLACK);

  HAL_Delay(1000); // 사용자가 "Connected!"를 확인할 시간 부여

  // ---------------------------------------------------------
  // [STEP 3] 메인 메뉴 진입 전 정리 (요청하신 대로 화면 지우기)
  // ---------------------------------------------------------
  TFT_Clear(); // 깨끗한 상태로 만들기

  // 3. 메인 화면 로직 진입
  Reset_To_MainScreen();


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  char lcdBuf[64];
	  // ====================================================
	  // TIME COUNTING (1초 타이머)
	  // ====================================================
	  static uint32_t last_sec_tick = 0;

	  // RUN 상태일 때만 시간 증가 (END, PAUSE 때는 멈춤)
	  if (opState == OP_RUN){
		  if (HAL_GetTick() - last_sec_tick >= 1000) { 				// 1초 지남
			  last_sec_tick = HAL_GetTick();
			  elapsed_sec++;
		  }
	  } else {
		  // END이나 PAUSE 상태일 때는 틱만 갱신하여,
		  // 다시 RUN이 되었을 때 갑자기 시간이 점프하는 것을 방지
		  last_sec_tick = HAL_GetTick();			//정지 상태일 때 동기화
	  }

	  // ====================================================
	  // [LOGIC A & B] SLEEP MODE & WAKE UP
	  // ====================================================
	  int btn_sleep_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2);

	  if (btn_sleep_state == 0) { // Sleep 버튼 눌림
		  if(isLcdOn) {
			  TFT_Clear(); // 화면 끄기 전 검은색으로
			  isLcdOn = 0;
		  } else {
			  isLcdOn = 1;
			  Reset_To_MainScreen();
		  }

		  //버튼을 뗄 때까지 여기서만 잠깐 대기 (중복 입력 방지)
		  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == 0) {
			  HAL_Delay(10); // CPU 점유율 낮춤
		  }
		  HAL_Delay(50); // 디바운싱
	  }

	  if (isLcdOn == 0) {
		  // LCD 꺼져있으면 키 입력만 감지하여 깨어나기
		  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == 0 || HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == 0) {
			  isLcdOn = 1;
			  Reset_To_MainScreen();
			  HAL_Delay(300);
		  }
		  continue;
	  }

	  // ====================================================
	  // [LOGIC C] DISPLAY UI (TFT LCD Logic)
	  // ====================================================

	  // 화면 깜빡임 방지: 커서나 상태가 변했을 때 + 주기적으로 데이터 갱신
	  static uint32_t last_ui_update = 0;

	  if (HAL_GetTick() - last_ui_update > 200)
	  {
		  last_ui_update = HAL_GetTick();

		  // 배경색: 검정(BLACK), 글자색: 흰색(WHITE), 선택색: 노랑(YELLOW)
		  // 폰트: Font_8x10 (세로 10px) -> 줄간격 20px로 설정하여 여유 있게 배치

		  // ------------------------------------------------
		  // [STATE 1] ROOT MENU (메인 메뉴)
		  // ------------------------------------------------
		  if (currentState == STATE_ROOT) {
			  ILI9341_WriteCentered(20, "[ MAIN MENU ]", Font_11x18, GREEN, BLACK);

			  // 반복문으로 메뉴 리스트 출력
			  for(int i = 0; i < 3; i++) {
				  uint16_t y_pos = 60 + (i * 30); // 30픽셀 간격

				  if(i == menuCursor) {
					  // 선택된 항목: 노란색 + 화살표
					  snprintf(lcdBuf, sizeof(lcdBuf), "> %s", rootMenu[i]);
					  ILI9341_WriteString(10, y_pos, lcdBuf, Font_11x18, YELLOW, BLACK);
				  } else {
					  // 선택 안 된 항목: 흰색
					  snprintf(lcdBuf, sizeof(lcdBuf), "  %s", rootMenu[i]);
					  ILI9341_WriteString(10, y_pos, lcdBuf, Font_11x18, WHITE, BLACK);
				  }
			  }
		  }

		  // ------------------------------------------------
		  // [STATE 2] STATUS VIEW (상태 모니터링)
		  // ------------------------------------------------
		  else if (currentState == STATE_STATUS_VIEW) {
			  // [Row 1(Y=20)] 제목
			  snprintf(lcdBuf, sizeof(lcdBuf), "[ MONITORING : %s ]", targetDeviceID);
			  ILI9341_WriteString(20, 20, lcdBuf, Font_8x10, GREEN, BLACK);

			  char buf[50];

			  // 1. [Row 2(Y=50)] 상태 표시
			  uint16_t stateColor = RED;
			  if (strcmp(val_state, "LOADING...") == 0) {
				  stateColor = WHITE;
			  }
			  else{
				  if (opState == OP_RUN) stateColor = CYAN;
				  else if (opState == OP_PAUSE) stateColor = ORANGE;
				  else if (opState == OP_END) stateColor = RED;
			  }

			  snprintf(buf, sizeof(buf), "State: %-10s", val_state);
			  ILI9341_WriteString(20, 50, buf, Font_8x10, stateColor, BLACK);

			  // 2. [Row 3(Y=80)] 반죽 종류
			  snprintf(buf, sizeof(buf), "Dough: %s", val_doughType);
			  ILI9341_WriteString(20, 80, buf, Font_8x10, WHITE, BLACK);

			  // 3. [Row 4(Y=110)] 시작 날짜/시간 (서버 동기화 값)
			  snprintf(buf, sizeof(buf), "Start: %s", val_startTime);
			  ILI9341_WriteString(20, 110, buf, Font_8x10, YELLOW, BLACK);

			  // 4. Duration
			  // 4-1. [Row 5(Y=140)] Duration 라벨
			  ILI9341_WriteString(20, 140, "Duration:", Font_8x10, MAGENTA, BLACK);

			  	  // --- 경과 시간 계산 (일/시/분 변환) ---
			  uint32_t d, h, m, temp_sec;
			  temp_sec = elapsed_sec;
			  d = temp_sec / 86400; temp_sec %= 86400; // 1일 = 86400초
			  h = temp_sec / 3600;  temp_sec %= 3600;
			  m = temp_sec / 60;
			  // 4-2. [ROW 6(Y=170)] 경과시간
			  snprintf(buf, sizeof(buf), "%dd %02dh %02dm", (int)d, (int)h, (int)m);
			  ILI9341_WriteString(40, 170, buf, Font_8x10, MAGENTA, BLACK);

			  // 4. [Row 7(Y=200)] 내부 온도 / 습도
			  snprintf(buf, sizeof(buf), "T: %s C   H: %s %%", val_temp, val_humi);
			  ILI9341_WriteString(20, 200, buf, Font_8x10, WHITE, BLACK);

			  // 5. [Row 8(Y=230)] CO2 농도
			  if (strcmp(val_co2, "Wait") == 0) {
				  ILI9341_WriteString(20, 230, "CO2  : Warming Up...", Font_8x10, WHITE, BLACK);
			  } else {
				  snprintf(buf, sizeof(buf), "CO2  : %s ppm", val_co2);
				  ILI9341_WriteString(20, 230, buf, Font_8x10, WHITE, BLACK);
			  }

			  // 6. [Row 9(Y=260)] 반죽 높이
			  snprintf(buf, sizeof(buf), "Height: %.1f cm", val_height);
			  ILI9341_WriteString(20, 260, buf, Font_8x10, BLUE, BLACK);

			  ILI9341_WriteString(20, 290, "< Press Select to Back", Font_8x10, RED, BLACK);
		  }
		  // ------------------------------------------------
		  // [STATE 3] DOUGH MENU (반죽 선택)
		  // ------------------------------------------------
		  else if (currentState == STATE_DOUGH_MENU){
			  ILI9341_WriteString(20, 20, "[ SELECT DOUGH ]", Font_8x10, GREEN, BLACK);
			  for(int i = 0; i < 4; i++) {
				  uint16_t y_pos = 60 + (i * 30);

				  if(i == menuCursor) {
					  char buf[30];
					  snprintf(buf, sizeof(buf), "> %s", doughMenu[i]);
					  ILI9341_WriteString(20, y_pos, buf, Font_8x10, YELLOW, BLACK);
				  } else {
					  char buf[30];
					  snprintf(buf, sizeof(buf), "  %s", doughMenu[i]);
					  ILI9341_WriteString(20, y_pos, buf, Font_8x10, WHITE, BLACK);
				  }
			  }
		  }

		  // ------------------------------------------------
		  // [STATE 4] POWER MENU (전원 제어)
		  // ------------------------------------------------
		  else if (currentState == STATE_POWER_MENU) {
			  ILI9341_WriteString(20, 20, "[ POWER CONTROL ]", Font_8x10, GREEN, BLACK);
			  for(int i = 0; i < 4; i++) {
				  uint16_t y_pos = 60 + (i * 30);
				  if(i == menuCursor) {
					  char buf[30]; snprintf(buf, sizeof(buf), "> %s", powerMenu[i]);
					  ILI9341_WriteString(20, y_pos, buf, Font_8x10, YELLOW, BLACK);
				  } else {
					  char buf[30]; snprintf(buf, sizeof(buf), "  %s", powerMenu[i]);
					  ILI9341_WriteString(20, y_pos, buf, Font_8x10, WHITE, BLACK);
				  }
			  }
		  }
	  } // End UI Update

	  // ====================================================
	  // [LOGIC D] BUTTON OPERATION
	  // ====================================================

	  // 1. MOVE 버튼 (커서 이동)
	  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == 0) {
		  menuCursor++;

		  // 각 상태별 최대 커서 수 제한
		  if (currentState == STATE_ROOT) {
			  if (menuCursor >= 3) menuCursor = 0;
		  }
		  else if (currentState == STATE_DOUGH_MENU) {
			  if (menuCursor >= 4) menuCursor = 0;
		  }
		  else if (currentState == STATE_POWER_MENU) {
			  if (menuCursor >= 4) menuCursor = 0;
		  }
		  else if (currentState == STATE_STATUS_VIEW) {
			  // 상태창에서는 커서 이동 기능 없음 (페이지 넘김 제거)
			  menuCursor = 0;
		  }

		  last_ui_update = 0;			// 버튼 누르자마자 UI가 즉시 바뀌도록 강제 업데이트

		  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == 0); // Delay 없이 대기 (CPU 속도 활용)
		  HAL_Delay(20);									// 뗀 후 아주 짧게만 대기 (채터링 방지)
	  }

	  // 2. SELECT 버튼 (선택)
	  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == 0) {

		  static char txBuf[32];
		  // 화면 전환 시 잔상 제거를 위해 화면 지우기 (필요시)
		  TFT_Clear();

		  if (currentState == STATE_ROOT) {
			  if (menuCursor == 0) currentState = STATE_STATUS_VIEW;
			  else if (menuCursor == 1) { currentState = STATE_DOUGH_MENU; menuCursor = 0; }
			  else if (menuCursor == 2) { currentState = STATE_POWER_MENU; menuCursor = 0;}
		  }
		  // ============================================================
		  // [1] 모니터링 화면 (STATUS_VIEW)
		  // ============================================================
		  else if (currentState == STATE_STATUS_VIEW) {
			  Reset_To_MainScreen(); // 돌아가기
		  }
		  // ============================================================
		  // [2] 반죽 선택 메뉴 (DOUGH MENU) -> 명령 전송
		  // ============================================================
		  else if (currentState == STATE_DOUGH_MENU) {
			  if (menuCursor != 3) { // 4. Return이 아닌 경우
				  char *cmdStr = "";
				  char *doughName = "";

				  // 메뉴 순서에 따라 명령어 매핑
				  if (menuCursor == 0) cmdStr = "LIQ";       // 1. Liquid
				  else if (menuCursor == 1) cmdStr = "DURE"; // 2. Dure
				  else if (menuCursor == 2) cmdStr = "PM";   // 3. P.M.

				  // 명령어 전송
				  // [형식] [0001]LIQ (반드시 뒤에 \r\n 포함)
				  snprintf(txBuf, sizeof(txBuf), "[%s]%s\r\n", targetDeviceID, cmdStr);
				  // 서버로 전송
				  esp_send_data(txBuf);
				  // 화면 알림
				  Show_Popup("Dough Selected!", CYAN, doughName, WHITE);
				  Show_Popup("System Info", WHITE, "Command Sent!", GREEN);
			  }
			  Reset_To_MainScreen();
		  }
		  // ============================================================
		  // 전원 제어 메뉴 (POWER MENU) -> 명령 전송
		  // ============================================================
		  else if (currentState == STATE_POWER_MENU) {
			  if (menuCursor == 3) {
				  Reset_To_MainScreen();
			  } else {
				  char *cmdStr = "";
				  char *statusMsg = "";

				  if(menuCursor == 0)      { cmdStr = "RUN";   statusMsg = "Starting Run..."; }
				  else if (menuCursor == 1){ cmdStr = "PAUSE"; statusMsg = "Pausing..."; }
				  else if (menuCursor == 2){ cmdStr = "END";   statusMsg = "Terminating..."; }

				  // 1. 명령 전송
				  snprintf(txBuf, sizeof(txBuf), "[%s]%s\r\n", targetDeviceID, cmdStr);
				  esp_send_data(txBuf);

				  // 2. 팝업 화면
				  // Show_Popup의 세 번째 인자에 추가 메시지 전달
				  Show_Popup(statusMsg, CYAN, "Control cmd Sent", GREEN);
				  Reset_To_MainScreen();
			}
		}
		  last_ui_update = 0;		// 버튼 누르고 난 뒤 화면 즉시 갱신

		  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == 0); // Delay 없이 대기 (CPU 속도 활용)
		  HAL_Delay(20);									// 뗀 후 아주 짧게만 대기 (채터링 방지)
	  }
	  // ====================================================
	  // [LOGIC E] WIFI PROCESSING (비동기 처리 & 재접속)
	  // ====================================================
	  // 1. [교통 정리] 연결 상태 점검 중이 아닐 때만 데이터 읽기
	  	  if (isCheckingStatus == 0) {
	  		  Process_Wifi_Messages();
	  	  }

	  	  // 2. 10초마다 연결 상태 확인
	  	  if (HAL_GetTick() - lastConnectionCheck > 10000) {
	  		  lastConnectionCheck = HAL_GetTick();

	  		  // [교통 정리 시작] 데이터 읽기 잠시 중단
	  		  isCheckingStatus = 1;

	  		  // 연결 상태 확인 (Blocking)
	  		  int wifi_stat = esp_get_status();

	  		  if (wifi_stat != 0) {
	  			  // ---------------------------------------------------------
	  			  // [Case 1] 처음 끊겼을 때 (UI 표시 O)
				  // ---------------------------------------------------------
	  			  if (prevWifiStatus == 0) {
	  				  TFT_Clear();
	  				  ILI9341_WriteCentered(130, "Server Disconnected!", Font_11x18, RED, BLACK);
	  				  ILI9341_WriteCentered(160, "Reconnecting...", Font_8x10, YELLOW, BLACK);

	  				  // 재접속 시도
	  				  AiotClient_Init();

	  				  // 재접속 결과 확인
	  				  if(esp_get_status() == 0) {
	  					  // [바로 복구]
	  					  TFT_Clear();
	  					  ILI9341_WriteCentered(140, "Reconnected!", Font_11x18, GREEN, BLACK);
	  					  HAL_Delay(1000);

	  					  prevWifiStatus = 0;
						  TFT_Clear();
						  Reset_To_MainScreen();
	  				  } else{
	  					  //[재접속 실패] 실패 팝업 띄우고 메인으로 복귀
	  					  TFT_Clear();
	  					Show_Popup("Retry Failed", RED, "Please check network", ORANGE);

	  					prevWifiStatus = 1; // 이제부터는 '이미 끊긴 상태'로 간주
					  Reset_To_MainScreen();
	  				  }

	  			  } else {
	  				  	  // ---------------------------------------------------------
						  // [Case 2] 이미 끊겨서 재시도 중일 때 (UI 표시 X -> Silent Mode)
						  // ---------------------------------------------------------
	  				  	  // 화면을 지우거나 메시지를 띄우지 않고 백그라운드에서 재접속 시도
					  	  // (주의: AiotClient_Init이 Blocking 함수라면 화면이 잠깐 멈출 수는 있음)
	  				  	  AiotClient_Init();

	  				  	  // 재접속 성공 여부 확인
						  if(esp_get_status() == 0) {
							  // [복구] 팝업
							  TFT_Clear();
							  ILI9341_WriteCentered(140, "Wifi Recovered!", Font_11x18, GREEN, BLACK);
							  HAL_Delay(1000);

							  prevWifiStatus = 0; // 상태를 '연결됨'으로 변경
							  TFT_Clear();
							  Reset_To_MainScreen();
						  } else {
	  				  						  // [여전히 실패]
	  				  						  // 아무것도 하지 않음 (사용자는 메인화면을 계속 볼 수 있음)
	  				  					  }
	  			  }
	  		  }
	  		  else {
	                // [연결 정상]
	  			  if (prevWifiStatus == 1) { // 끊겼다가 돌아온 경우
	  				  TFT_Clear();
	  				  ILI9341_WriteCentered(140, "Connected!", Font_11x18, GREEN, BLACK);
					  HAL_Delay(1000);
					  Reset_To_MainScreen();
	  			  }
	  			  prevWifiStatus = 0;
	  		  }

	  		  // [교통 정리 끝] 다시 데이터 읽기 허용
	  		  isCheckingStatus = 0;
	  	  }


//	  // 1. 수신된 데이터 처리 (Non-blocking)
//
//
//	  Process_Wifi_Messages();
//
//	  // 2. 10초마다 연결 상태 확인 및 자동 재접속
//	  if (HAL_GetTick() - lastConnectionCheck > 10000) {
//		  lastConnectionCheck = HAL_GetTick();
//		  Check_Wifi_Connection(); // esp.c에 구현된 함수 호출
//	  }
//	  if (HAL_GetTick() - lastConnectionCheck > 10000) {
//		  lastConnectionCheck = HAL_GetTick();
//
//		  int wifi_stat = esp_get_status(); // 0:Connected, Other:Error
//
//		  // 1. 연결 끊김 감지 (Connected -> Disconnected)
//		  if (wifi_stat != 0) {
//			  if (prevWifiStatus == 0) {
//				  // 처음 끊겼을 때 팝업
//				  Show_Popup("WiFi Lost", "Reconnecting...", RED);
//			  }
//


//			  // 재접속 시도 (Blocking 함수일 경우 팝업이 유지됨)
//			  AiotClient_Init();
//			  prevWifiStatus = 1; // 상태 업데이트: 끊김
//		  }
//		  // 2. 재연결 성공 감지 (Disconnected -> Connected)
//		  else {
//			  if (prevWifiStatus == 1) {
//				  // 방금 연결되었을 때 팝업
//				  Show_Popup("WiFi Status", "Connected!", GREEN);
//
//				  // 화면 복구 (팝업 잔상 제거 및 메인화면 갱신)
//				  Reset_To_MainScreen();
//			  }
//			  prevWifiStatus = 0; // 상태 업데이트: 연결됨
//		  }
//	  }
	  // ====================================================
	  // [LOGIC F] 데이터 전송부
	  // ====================================================
	  if (HAL_GetTick() - lastDataSendTick > 5000) {
		  lastDataSendTick = HAL_GetTick();

//		  char sendBuf[1024];

		  // 현재 상태 문자열 변환
//		  char *opStateStr;
//		  if (opState == OP_RUN) opStateStr = "RUN";
//		  else if (opState == OP_PAUSE) opStateStr = "PAUSE";
//		  else opStateStr = "END";
		  char sendBuf[1024];
		  char *opStr = (opState == OP_RUN) ? "RUNNING" : (opState == OP_PAUSE ? "PAUSED" : "END");

		  snprintf(sendBuf, sizeof(sendBuf),
		               "[ALLMSG]ID:%s|MODE:MONITOR|STATE:%s|T:%s|H:%s|D:%.1f|CO2:%s\n",
		               LOGIN_ID, opStr, val_temp, val_humi, val_height,
		               (strcmp(val_co2, "Wait") == 0) ? "0" : val_co2);

		  esp_send_data(sendBuf);
	  }

  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
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
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 38400;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ESP_RST_GPIO_Port, ESP_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_MOVE_Pin BTN_SELECT_Pin BTN_SLEEP_Pin */
  GPIO_InitStruct.Pin = BTN_MOVE_Pin|BTN_SELECT_Pin|BTN_SLEEP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : ESP_RST_Pin */
  GPIO_InitStruct.Pin = ESP_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ESP_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_CS_Pin */
  GPIO_InitStruct.Pin = TFT_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TFT_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_RST_Pin */
  GPIO_InitStruct.Pin = TFT_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TFT_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_DC_Pin */
  GPIO_InitStruct.Pin = TFT_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TFT_DC_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void Reset_To_MainScreen(){
	currentState = STATE_ROOT;
	menuCursor = 0;
	isLcdOn = 1;
}

void Show_Popup(char *str1, uint16_t color1, char *str2, uint16_t color2) {
    TFT_Clear(); // 배경 지우기

    // 1. 첫 번째 줄 출력 (Y=130)
    if (str1 != NULL) {
        int x1 = (240 - (strlen(str1) * 8)) / 2; // 중앙 정렬
        if(x1 < 0) x1 = 0;
        ILI9341_WriteString(x1, 130, str1, Font_8x10, color1, BLACK);
    }

    // 2. 두 번째 줄 출력 (Y=160)
    if (str2 != NULL) {
        int x2 = (240 - (strlen(str2) * 8)) / 2; // 중앙 정렬
        if(x2 < 0) x2 = 0;
        ILI9341_WriteString(x2, 160, str2, Font_8x10, color2, BLACK);
    }

    HAL_Delay(800); // 화면 유지
    TFT_Clear();     // 닫기
}

void Process_Wifi_Messages() {
    while (ESP_Available()) {
        char c = (char)ESP_Read();

        if (c == '[') {
            isCmdRecording = 1; wifiCmdIdx = 0;
            memset(wifiCmdBuf, 0, CMD_BUFFER_SIZE);
            wifiCmdBuf[wifiCmdIdx++] = c;
        }
        else if (isCmdRecording) {
            if (c == '\n' || wifiCmdIdx >= CMD_BUFFER_SIZE - 1) {
                wifiCmdBuf[wifiCmdIdx] = '\0';
                isCmdRecording = 0;

                // 디버깅을 위해 수신된 원본 데이터 출력
                printf("UART6 Rx: %s\r\n", wifiCmdBuf);

                if (strstr(wifiCmdBuf, "CIPSEND") || strstr(wifiCmdBuf, "OK")) return;

                char *startBracket = strchr(wifiCmdBuf, '[');
                char *endBracket = strchr(wifiCmdBuf, ']');

                if (startBracket && endBracket && endBracket > startBracket) {
                    char senderID[20] = {0};
                    strncpy(senderID, startBracket + 1, endBracket - startBracket - 1);

                    char *body = endBracket + 1;

                    // ALLMSG 태그도 수용하도록 조건 변경
                    if (strcmp(senderID, targetDeviceID) == 0 || strcmp(senderID, "ALLMSG") == 0) {
                        char tempBody[CMD_BUFFER_SIZE];
                        strcpy(tempBody, body);

                        char *token = strtok(tempBody, "|");
                        while (token != NULL) {
                            // 아두이노 전송 규격에 맞춘 파싱 키워드 체크
                            if (strncmp(token, "ID:", 3) == 0) {
                                // ID는 확인용으로만 사용하거나 필요시 저장
                            }
                            else if (strncmp(token, "MODE:", 5) == 0) {
                                strncpy(val_doughType, token + 5, 19);
                            }
                            else if (strncmp(token, "STATE:", 6) == 0) {
                                strncpy(val_state, token + 6, 19);
                                if (strstr(val_state, "RUNNING")) opState = OP_RUN;
                                else if (strstr(val_state, "PAUSED")) opState = OP_PAUSE;
                                else opState = OP_END;
                            }
                            else if (strncmp(token, "T:", 2) == 0) strncpy(val_temp, token + 2, 9);
                            else if (strncmp(token, "H:", 2) == 0) strncpy(val_humi, token + 2, 9);
                            else if (strncmp(token, "CO2:", 4) == 0) strncpy(val_co2, token + 4, 9);
                            else if (strncmp(token, "D:", 2) == 0) {
                                float dist = atof(token + 2);
                                // 거리(dist)를 높이(height)로 변환하는 로직
                                val_height = (CONTAINER_DEPTH_MM - dist) / 10.0;
                                if(val_height < 0) val_height = 0;
                            }
                            else if (strncmp(token, "STIME:", 6) == 0) strncpy(val_startTime, token + 6, 24);

                            token = strtok(NULL, "|");
                        }
                        // 데이터 갱신 완료 로그
                        printf(">> Data Sync Complete from %s\r\n", senderID);
                    }
                }
            }
            else {
                if (c != '\r') wifiCmdBuf[wifiCmdIdx++] = c;
            }
        }
    }
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
