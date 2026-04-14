/* Inc/fonts.h */
#ifndef FONTS_H_
#define FONTS_H_

#include <stdint.h>

typedef struct {
    uint8_t FontWidth;    // 폰트 가로 폭 (픽셀)
    uint8_t FontHeight;   // 폰트 세로 높이 (픽셀)
    const uint16_t *data; // 폰트 데이터 배열 포인터
} FontDef;

// 외부에서 사용할 폰트 선언
extern FontDef Font_8x10;
extern FontDef Font_11x18;

#endif /* FONTS_H_ */
