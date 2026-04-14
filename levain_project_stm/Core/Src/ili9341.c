#include "ili9341.h"
#include "fonts.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

// =================================================================
// 1. Low Level Driver (하드웨어 제어)
// =================================================================

void ILI9341_WriteCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET); // Command
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET); // CS Select
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);   // CS Deselect
}

void ILI9341_WriteData(uint8_t data) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);   // Data
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET); // CS Select
    HAL_SPI_Transmit(&hspi1, &data, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);   // CS Deselect
}

void ILI9341_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    ILI9341_WriteCommand(0x2A); // Column Addr
    ILI9341_WriteData(x0 >> 8); ILI9341_WriteData(x0 & 0xFF);
    ILI9341_WriteData(x1 >> 8); ILI9341_WriteData(x1 & 0xFF);

    ILI9341_WriteCommand(0x2B); // Page Addr
    ILI9341_WriteData(y0 >> 8); ILI9341_WriteData(y0 & 0xFF);
    ILI9341_WriteData(y1 >> 8); ILI9341_WriteData(y1 & 0xFF);

    ILI9341_WriteCommand(0x2C); // Memory Write
}

// [중요] 점 하나를 찍는 함수 (DrawChar에서 사용됨)
void ILI9341_DrawPixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= ILI9341_WIDTH || y < 0 || y >= ILI9341_HEIGHT) return;

    ILI9341_SetAddressWindow(x, y, x, y);

    uint8_t data[2] = {color >> 8, color & 0xFF};

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, data, 2, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

void ILI9341_Init(void) {
    // 하드웨어 리셋
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    // 초기화 명령어 (매직 코드)
    ILI9341_WriteCommand(0x01); HAL_Delay(100);
    ILI9341_WriteCommand(0xCB); ILI9341_WriteData(0x39); ILI9341_WriteData(0x2C); ILI9341_WriteData(0x00); ILI9341_WriteData(0x34); ILI9341_WriteData(0x02);
    ILI9341_WriteCommand(0xCF); ILI9341_WriteData(0x00); ILI9341_WriteData(0xC1); ILI9341_WriteData(0x30);
    ILI9341_WriteCommand(0xE8); ILI9341_WriteData(0x85); ILI9341_WriteData(0x00); ILI9341_WriteData(0x78);
    ILI9341_WriteCommand(0xEA); ILI9341_WriteData(0x00); ILI9341_WriteData(0x00);
    ILI9341_WriteCommand(0xED); ILI9341_WriteData(0x64); ILI9341_WriteData(0x03); ILI9341_WriteData(0x12); ILI9341_WriteData(0x81);
    ILI9341_WriteCommand(0xF7); ILI9341_WriteData(0x20);
    ILI9341_WriteCommand(0xC0); ILI9341_WriteData(0x23);
    ILI9341_WriteCommand(0xC1); ILI9341_WriteData(0x10);
    ILI9341_WriteCommand(0xC5); ILI9341_WriteData(0x3e); ILI9341_WriteData(0x28);
    ILI9341_WriteCommand(0xC7); ILI9341_WriteData(0x86);
    ILI9341_WriteCommand(0x36); ILI9341_WriteData(0x48); // 방향
    ILI9341_WriteCommand(0x3A); ILI9341_WriteData(0x55); // 16bit Pixel
    ILI9341_WriteCommand(0xB1); ILI9341_WriteData(0x00); ILI9341_WriteData(0x18);
    ILI9341_WriteCommand(0x11); HAL_Delay(120);
    ILI9341_WriteCommand(0x29);
}

void ILI9341_FillScreen(uint16_t color) {
    ILI9341_SetAddressWindow(0, 0, ILI9341_WIDTH - 1, ILI9341_HEIGHT - 1);
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);

    uint8_t data[2] = {color >> 8, color & 0xFF};
    // 성능을 위해 루프 최적화가 필요하지만 일단 동작 우선
    for(uint32_t i = 0; i < ILI9341_WIDTH * ILI9341_HEIGHT; i++) {
         HAL_SPI_Transmit(&hspi1, data, 2, 10);
    }
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

// =================================================================
// 2. Font & Text Functions (글자 그리기)
// =================================================================

void ILI9341_DrawChar(int x, int y, char ch, FontDef font, uint16_t color, uint16_t bgcolor) {
    uint32_t i, b, j;
    if (ch < 32 || ch > 126) return; // ASCII 범위 체크

    int index = (ch - 32) * font.FontHeight; // 인덱스 계산

//    for (i = 0; i < font.FontHeight; i++) {
//    	b = font.data[index + i];
//    	for (j = 0; j < font.FontWidth; j++) {
//		// [핵심] 점 하나를 찍을 때 가로세로 2배(2x2)로 찍습니다.
//		if ((b << j) & 0x8000) {
//			// 글자색 (2x2 블록)
//			ILI9341_DrawPixel(x + (j * 2),     y + (i * 2),     color);
//			ILI9341_DrawPixel(x + (j * 2) + 1, y + (i * 2),     color);
//			ILI9341_DrawPixel(x + (j * 2),     y + (i * 2) + 1, color);
//			ILI9341_DrawPixel(x + (j * 2) + 1, y + (i * 2) + 1, color);
//		} else {
//			// 배경색 (2x2 블록)
//			ILI9341_DrawPixel(x + (j * 2),     y + (i * 2),     bgcolor);
//			ILI9341_DrawPixel(x + (j * 2) + 1, y + (i * 2),     bgcolor);
//			ILI9341_DrawPixel(x + (j * 2),     y + (i * 2) + 1, bgcolor);
//			ILI9341_DrawPixel(x + (j * 2) + 1, y + (i * 2) + 1, bgcolor);
//			}
//    	}
//    }

    for (i = 0; i < font.FontHeight; i++) {
        b = font.data[index + i];
        for (j = 0; j < font.FontWidth; j++) {
            if ((b << j) & 0x8000) {
                ILI9341_DrawPixel(x + j, y + i, color);
            } else {
                ILI9341_DrawPixel(x + j, y + i, bgcolor);
            }
        }
    }
}

void ILI9341_WriteString(int x, int y, const char* str, FontDef font, uint16_t color, uint16_t bgcolor) {
    while (*str) {
//    	// 화면 범위를 넘어가면 줄바꿈 (가로폭 2배 고려)
//		if (x + (font.FontWidth * 2) >= ILI9341_WIDTH) {
//			x = 0;
//			y += (font.FontHeight * 2);
//		}
//
//		ILI9341_DrawChar(x, y, *str, font, color, bgcolor);
//
//		// [핵심] 다음 글자로 이동할 때: (폰트폭 x 2배) + (2픽셀 여유 공간)
//		// +1를 해줌으로써 글자가 짤리는(겹치는) 현상을 막습니다.
//		x += (font.FontWidth * 2)+1;
//		str++;
//	}

        if (x + font.FontWidth >= ILI9341_WIDTH) { // 줄바꿈 (선택사항)
            x = 0;
            y += font.FontHeight;
        }
        ILI9341_DrawChar(x, y, *str, font, color, bgcolor);
        x += font.FontWidth;
        str++;
    }
}

// =================================================================
// 3. Wrapper Functions for CLCD Compatibility (핵심!)
//    기존 LCD_xxx 함수 이름을 그대로 사용하여 TFT를 제어합니다.
// =================================================================

// [초기화] LCD_init() -> ILI9341_Init()
void LCD_init(void) {
    ILI9341_Init();
    ILI9341_FillScreen(0x0000); // BLACK
}

// [명령어] LCD_writeCmdData() -> 주로 화면 지우기(Clear)용
void LCD_writeCmdData(uint8_t cmd) {
    // 0x01은 CLCD의 Clear Display 명령어입니다.
    if (cmd == 0x01) {
        ILI9341_FillScreen(0x0000); // 화면 전체를 검은색으로
    }
}

// [글자 출력] LCD_writeStringXY() -> TFT 좌표 매핑 후 출력
void LCD_writeStringXY(uint8_t row, uint8_t col, char *str) {
    uint16_t x_pos, y_pos;

    // 1. Row(줄) -> Y 좌표 매핑
    if (row == 0)      y_pos = 80;
	else if (row == 1) y_pos = 140; // 60픽셀 차이 (20px * 2배 = 40px 글자높이 고려)
	else               y_pos = 200;

//    if (row == 0)      y_pos = 100; // 윗줄
//    else if (row == 1) y_pos = 140; // 아랫줄
//    else               y_pos = 180; // (혹시 모를 추가 줄)

    // 2. Col(칸) -> X 좌표 매핑 (가운데 정렬 느낌)
    x_pos = 10 + (col * 16);
//    x_pos = 20 + (col * 15);

    // 3. 폰트 출력 (흰색 글씨, 검은 배경)
    ILI9341_WriteString(x_pos, y_pos, str, Font_8x10, 0xFFFF, 0x0000);
}

void ILI9341_WriteCentered(uint16_t y_pos, char *str, FontDef font, uint16_t color, uint16_t bg_color) {
    // 1. 화면 너비 (세로 모드 기준 240)
    uint16_t screen_width = 240;

    // 2. 문자열 길이 계산
    uint16_t str_len = strlen(str);

    // 3. 전체 문자열의 픽셀 너비 계산
    uint16_t str_width_px = str_len * font.FontWidth;

    // 4. 시작 X 좌표 계산 (음수가 나오면 0으로 처리)
    int16_t x_pos = (screen_width - str_width_px) / 2;
    if (x_pos < 0) x_pos = 0;

    // 5. 출력
    ILI9341_WriteString((uint16_t)x_pos, y_pos, str, font, color, bg_color);
}

// =================================================================
// 4. Added Wrappers for Main.c compatibility
//    (main.c에서 호출하는 TFT_ 함수들 구현)
// =================================================================

// 화면 전체 지우기
void TFT_Clear(void) {
    ILI9341_FillScreen(0x0000); // BLACK
}

// 좌표를 이용해 문자열 출력 (기존 LCD_writeStringXY 로직 재사용)
void TFT_writeStringXY(uint8_t row, uint8_t col, char *str) {
    // 위에 정의된 LCD_writeStringXY 함수가 이미 좌표 계산 로직을 갖고 있으므로
    // 그대로 호출해서 사용합니다.
    LCD_writeStringXY(row, col, str);
}
