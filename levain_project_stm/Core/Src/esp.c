//서울기술교육센터 AIOT & Embedded System
//2024-04-16 By KSH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp.h"

static char ip_addr[16];
static char response[MAX_ESP_RX_BUFFER];

// 상태 관리를 위한 정적 변수 (0: 끊김, 1: 연결됨)
static uint8_t is_wifi_connected_prev = 1;
//================== UART 핸들러 =========================
extern UART_HandleTypeDef huart2; // PC 디버깅용
extern UART_HandleTypeDef huart6; // ESP8266 연결용

//================== 데이터 변수 =========================
volatile unsigned char rx2Flag = 0;
volatile char rx2Data[50];
uint8_t cdata; // UART2 수신 버퍼

static uint8_t data; // UART6 수신 버퍼 (1byte)
cb_data_t cb_data;   // AT 커맨드용 버퍼

// 비동기 수신용 링버퍼 인스턴스 선언
RingBuffer rb_rx = { {0}, 0, 0 };

//================== 링버퍼 함수 구현 ====================

// 버퍼에 읽을 데이터가 얼마나 있는지 반환
int ESP_Available(void) {
    return (uint16_t)(MAX_UART_RX_BUFFER + rb_rx.head - rb_rx.tail) % MAX_UART_RX_BUFFER;
}

// 버퍼에서 1바이트 읽어오기
uint8_t ESP_Read(void) {
    if (rb_rx.head == rb_rx.tail) return 0;

    uint8_t c = rb_rx.buffer[rb_rx.tail];
    rb_rx.tail = (rb_rx.tail + 1) % MAX_UART_RX_BUFFER;
    return c;
}

////==================uart2=========================
//extern UART_HandleTypeDef huart2;
//volatile unsigned char rx2Flag = 0;
//volatile char rx2Data[50];
//uint8_t cdata;
//
////==================uart6=========================
//extern volatile unsigned char rx2Flag;
//extern volatile char rx2Data[50];
//extern uint8_t cdata;
//static uint8_t data;
//cb_data_t cb_data;
//extern UART_HandleTypeDef huart6;



//================== ESP AT 커맨드 함수 ==================
static int esp_at_command(uint8_t *cmd, uint8_t *resp, uint16_t *length, int16_t time_out)
{
    *length = 0;
    memset(resp, 0x00, MAX_UART_RX_BUFFER);
    memset(&cb_data, 0x00, sizeof(cb_data_t));	// cb_data 초기화
    if(HAL_UART_Transmit(&huart6, cmd, strlen((char *)cmd), 100) != HAL_OK)
        return -1;

    while(time_out > 0)
    {
        if(cb_data.length >= MAX_UART_RX_BUFFER)
            return -2;
        else if(strstr((char *)cb_data.buf, "ERROR") != NULL)	// 연결 실패 등
            return -3;
        else if(strstr((char *)cb_data.buf, "OK") != NULL)		// OK 응답 대기
        {
            memcpy(resp, cb_data.buf, cb_data.length);			// Send Ready 대기
            *length = cb_data.length;
            return 0;
        }
        time_out -= 10;
        HAL_Delay(10);
    }
    return -4;
}

static int esp_reset(void)
{
    uint16_t length = 0;
    if(esp_at_command((uint8_t *)"AT+RST\r\n", (uint8_t *)response, &length, 1000) != 0)
    {
    	return -1;
    }
    else
    	HAL_Delay(500);	//reboot
    return 0;
}

static int esp_get_ip_addr(uint8_t is_debug)
{
    if(strlen(ip_addr) != 0)
    {
        if(strcmp(ip_addr, "0.0.0.0") == 0)
            return -1;
    }
    else
    {
        uint16_t length;
        if(esp_at_command((uint8_t *)"AT+CIPSTA?\r\n", (uint8_t *)response, &length, 1000) != 0)
            printf("ip_state command fail\r\n");
        else
        {
            char *line = strtok(response, "\r\n");

            if(is_debug)
            {
                for(int i = 0 ; i < length ; i++)
                    printf("%c", response[i]);
            }

            while(line != NULL)
            {
                if(strstr(line, "ip:") != NULL)
                {
                    char *ip;

                    strtok(line, "\"");
                    ip = strtok(NULL, "\"");
                    if(strcmp(ip, "0.0.0.0") != 0)
                    {
                        memset(ip_addr, 0x00, sizeof(ip_addr));
                        memcpy(ip_addr, ip, strlen(ip));
                        return 0;
                    }
                }
                line = strtok(NULL, "\r\n");
            }
        }

        return -1;
    }

    return 0;
}

static int request_ip_addr(uint8_t is_debug)
{
    uint16_t length = 0;

    if(esp_at_command((uint8_t *)"AT+CIFSR\r\n", (uint8_t *)response, &length, 1000) != 0)
        printf("request ip_addr command fail\r\n");
    else
    {
        char *line = strtok(response, "\r\n");

        if(is_debug)
        {
            for(int i = 0 ; i < length ; i++)
                printf("%c", response[i]);
        }

        while(line != NULL)
        {
            if(strstr(line, "CIFSR:STAIP") != NULL)
            {
                char *ip;

                strtok(line, "\"");
                ip = strtok(NULL, "\"");
                if(strcmp(ip, "0.0.0.0") != 0)
                {
                    memset(ip_addr, 0x00, sizeof(ip_addr));
                    memcpy(ip_addr, ip, strlen(ip));
                    return 0;
                }
            }
            line = strtok(NULL, "\r\n");
        }
    }
    return -1;
}
int esp_client_conn()
{
		char at_cmd[MAX_ESP_COMMAND_LEN] = {0, };
	uint16_t length = 0;

	// TCP 서버 연결
		sprintf(at_cmd,"AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",DST_IP,DST_PORT);
		if(esp_at_command((uint8_t *)at_cmd,(uint8_t *)response, &length, 1000) != 0) {
	        printf("Server Connect Failed\r\n");
	        return -1;
	    }

	    //LOGIN_ID와 LOGIN_PW를 사용하여 로그인 패킷 전송
	    char login_packet[64];
	    sprintf(login_packet, "[%s:%s]", LOGIN_ID, LOGIN_PW);
		esp_send_data(login_packet);

		return 0;

//		sprintf(at_cmd,"AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",DST_IP,DST_PORT);
//		esp_at_command((uint8_t *)at_cmd,(uint8_t *)response, &length, 1000);					//CONNECT
//
//		esp_send_data("["LOGID":"PASSWD"]");
//		return 0;
}
int esp_get_status()
{
	uint16_t length = 0;
	esp_at_command((uint8_t *)"AT+CIPSTATUS\r\n",(uint8_t *)response, &length, 1000);					//CONNECT

    if(strstr((char *)response, "STATUS:3") != NULL)  //STATUS:3 The ESP8266 Station has created a TCP or UDP transmission
    {
    	return 0;
    }
	return -1;
}
int drv_esp_init(void)
{
    memset(ip_addr, 0x00, sizeof(ip_addr));
    HAL_UART_Receive_IT(&huart6, &data, 1);

    return esp_reset();
}
void reset_func()
{
	printf("esp reset... ");
	if(esp_reset() == 0)
			printf("OK\r\n");
	else
			printf("fail\r\n");
}

void version_func()
{
  uint16_t length = 0;
  printf("esp firmware version\r\n");
  if(esp_at_command((uint8_t *)"AT+GMR\r\n", (uint8_t *)response, &length, 1000) != 0)
      printf("ap scan command fail\r\n");
  else
  {
      for(int i = 0 ; i < length ; i++)
          printf("%c", response[i]);
  }
}

void ap_conn_func(char *ssid, char *passwd)
{
  uint16_t length = 0;
  char at_cmd[MAX_ESP_COMMAND_LEN] = {0, };

  // 1. Station 모드 설정
    esp_at_command((uint8_t *)"AT+CWMODE=1\r\n", (uint8_t *)response, &length, 1000);

    // 2. AP 접속
    sprintf(at_cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid,passwd);
    if(esp_at_command((uint8_t *)at_cmd, (uint8_t *)response, &length, 6000) != 0)
        printf("AP connection failed: %s\r\n", at_cmd);
    else
        printf("AP Connected.\r\n");

//  if(ssid == NULL || passwd == NULL)
//  {
//      printf("invalid command : ap_conn <ssid> <passwd>\r\n");
//      return;
//  }
//  if(esp_at_command((uint8_t *)"AT+CWMODE=1\r\n", (uint8_t *)response, &length, 1000) != 0)
//      printf("Station mode fail\r\n");
//  sprintf(at_cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid,passwd);
//  if(esp_at_command((uint8_t *)at_cmd, (uint8_t *)response, &length, 6000) != 0)
//      printf("ap scan command fail : %s\r\n",at_cmd);
}

void ip_state_func()
{
  uint16_t length = 0;
  if(esp_at_command((uint8_t *)"AT+CWJAP?\r\n", (uint8_t *)response, &length, 1000) != 0)
      printf("ap connected info command fail\r\n");
  else
  {
      for(int i = 0 ; i < length ; i++)
          printf("%c", response[i]);
  }
  printf("\r\n");

  if(esp_get_ip_addr(1) == 0)
      printf("ip_addr = [%s]\r\n", ip_addr);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{

    if(huart->Instance == USART6)
    {
    	// 1. AT 커맨드용
        if(cb_data.length < MAX_ESP_RX_BUFFER)
        {
            cb_data.buf[cb_data.length++] = data;
        }

        // 2. 서버 데이터 수신용 링버퍼
        uint16_t next_head = (rb_rx.head + 1) % MAX_UART_RX_BUFFER;
        if (next_head != rb_rx.tail) {
		rb_rx.buffer[rb_rx.head] = data;
		rb_rx.head = next_head;
        }

        HAL_UART_Receive_IT(huart, &data, 1);
    }
    if(huart->Instance == USART2)
    {
    	// PC 디버깅용 echo
    	static int i=0;
    	rx2Data[i] = cdata;
    	if(rx2Data[i] == '\r')
    	{
    		rx2Data[i] = '\0';
    		rx2Flag = 1;
    		i = 0;
    	}
    	else
    	{
    		i++;
    	}
    	HAL_UART_Receive_IT(huart, &cdata,1);
    }
}


void AiotClient_Init()
{
//	reset_func();
//	version_func();
	ap_conn_func(SSID,PASS);
//	ip_state_func();
	request_ip_addr(1);
	esp_client_conn();
	esp_get_status();
}

void esp_send_data(char *data)
{
	char at_cmd[MAX_ESP_COMMAND_LEN] = {0, };
	uint16_t length = 0;
	sprintf(at_cmd,"AT+CIPSEND=%d\r\n",strlen(data));
	if(esp_at_command((uint8_t *)at_cmd,(uint8_t *)response, &length, 1000) == 0)
	{
		esp_at_command((uint8_t *)data,(uint8_t *)response, &length, 1000);
	}
}

void Check_Wifi_Connection(void)
{
	int current_status = esp_get_status();

	if(current_status == 0){
		// 정상 접속
		if (is_wifi_connected_prev == 0) {
			printf("Wifi Reconnected Successfully!\r\n");
		}
		is_wifi_connected_prev = 1; // 상태 업데이트: 연결됨
	}
	else if(esp_get_status() != 0) { 								// 연결 끊김 감지
        printf("Wifi Connection Lost! Trying to Reconnect...\r\n");

        // 기존 열기 닫기 시도
        uint16_t len;
        esp_at_command((uint8_t *)"AT+CIPCLOSE\r\n", (uint8_t *)response, &len, 200);
        // 재접속 시도 (이 함수가 오래 걸리므로 버튼이 잠시 멈출 수 있음)
        AiotClient_Init();

        // 재접속 후 상태 확인은 다음 주기에서 함
        is_wifi_connected_prev = 0; // 상태 업데이트: 끊김(재접속 시도 중)
    }
}

//==================uart2=========================
int drv_uart_init(void)
{
    HAL_UART_Receive_IT(&huart2, &cdata,1);
    return 0;
}

int drv_uart_tx_buffer(uint8_t *buf, uint16_t size)
{
    if(HAL_UART_Transmit(&huart2, buf, size, 100) != HAL_OK)
        return -1;

    return 0;
}
int __io_putchar(int ch)
{
    if(HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 10) == HAL_OK)
        return ch;
    return -1;
}



