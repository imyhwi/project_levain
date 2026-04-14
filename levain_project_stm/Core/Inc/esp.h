//서울기술교육센터 AIOT & Embedded System
//2024-04-16 By KSH


#ifndef __ESP_H__
#define __ESP_H__

#include "stm32f4xx_hal.h"

//#define MAX_ESP_RX_BUFFER      1024
//#define MAX_ESP_COMMAND_LEN    64
//#define MAX_ESP_CLIENT_NUM     10

// 1. 와이파이 및 서버 설정
#define SSID "robotA"
#define PASS "robotA1234"
#define DST_IP "10.10.141.77"
#define DST_PORT 5000

// 2. ID 설정
#define LOGIN_ID    "LEV_STM"  	// 서버 접속(로그인)용 ID
#define LOGIN_PW    "PASSWD"      // 서버 접속 비번

// 3. 버퍼 크기 설정
#define MAX_ESP_RX_BUFFER 1024
#define MAX_ESP_COMMAND_LEN 100
#define MAX_UART_RX_BUFFER 1024
#define MAX_ESP_CLIENT_NUM 10



typedef struct _cb_data_t
{
    uint8_t buf[MAX_ESP_RX_BUFFER];
    uint16_t length;
}cb_data_t;

int drv_esp_init(void);
int drv_esp_test_command(void);
void AiotClient_Init(void);
int esp_client_conn(void);
void esp_send_data(char *data);
int esp_get_status(void);
void Check_Wifi_Connection(void);

//==================uart3=========================
//#define MAX_UART_RX_BUFFER      512
#define MAX_UART_COMMAND_LEN    64

int drv_uart_init(void);
int drv_uart_rx_buffer(uint8_t *buf, uint16_t size);
int drv_uart_tx_buffer(uint8_t *buf, uint16_t size);
//==================uart3=========================


// 원형 버퍼 구조체
typedef struct {
    uint8_t buffer[MAX_UART_RX_BUFFER];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuffer;

// 수신 데이터 처리 함수
int ESP_Available(void);
uint8_t ESP_Read(void);

#endif
