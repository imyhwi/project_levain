#ifndef ILI9341_H_
#define ILI9341_H_

#include "main.h"
#include "fonts.h"

// LCD 크기
#define ILI9341_WIDTH   240
#define ILI9341_HEIGHT  320

// 기본 색상 정의 (RGB 565 포맷)
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define ORANGE  0xFD20
#define OLIVE 	0x7BE0
#define MAROON 	0x7800
#define GREENYELLOW 0xAFE5



// CLCD 호환용 매크로(main.c에서 에러나지 않도록 추가)
#define LCD_DISPLAY_CLEAR 0x01

// ================================================================
// 1. ILI9341 하드웨어 제어 및 그리기 함수 (ili9341.c 내부용)
// ================================================================
void ILI9341_Init(void);
void ILI9341_FillScreen(uint16_t color);
void ILI9341_DrawPixel(int x, int y, uint16_t color);

// 좌표는 int로 통일합니다.
void ILI9341_DrawChar(int x, int y, char ch, FontDef font, uint16_t color, uint16_t bgcolor);
void ILI9341_WriteString(int x, int y, const char *str, FontDef font, uint16_t color, uint16_t bgcolor);

// ================================================================
// 2. 래퍼(Wrapper) 함수 선언 (main.c에서 호출하는 함수들)
// ================================================================

// main.c에서 사용하는 TFT_ 함수들 선언 추가
void TFT_writeStringXY(uint8_t row, uint8_t col, char *str);
void TFT_Clear(void);

// 기존 CLCD 호환 함수
void LCD_init(void);
void LCD_writeCmdData(uint8_t cmd);								// 화면 지우기용 함수
void LCD_writeStringXY(uint8_t row, uint8_t col, char *str); 	// 글자 출력

#endif /* ILI9341_H_ */
