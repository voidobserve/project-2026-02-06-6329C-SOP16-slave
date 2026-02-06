#ifndef _EYELID_LUT_H_
#define _EYELID_LUT_H_

#include <stdint.h>

// 屏幕尺寸
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// 眼帘关键帧数量
#define EYELID_KEYFRAMES 31

// ==================== 修复：眼帘锚点位置 ====================
// 眼帘的中心位置（眼球中心）
#define EYE_CENTER_X 160  // 屏幕水平中心
#define EYE_CENTER_Y 120  // 屏幕垂直中心

// 为了兼容旧代码，保留这个定义
#define EYE_CORNER_Y EYE_CENTER_Y

// 眼帘的水平范围（从中心向两侧延伸）
#define EYELID_WIDTH 160  // 眼帘宽度的一半（总宽320）
// ===========================================================

// 眼帘关键帧数据（外部定义）
extern const int16_t *upper_eyelid_keyframes[EYELID_KEYFRAMES];
extern const int16_t *lower_eyelid_keyframes[EYELID_KEYFRAMES];

#endif // _EYELID_LUT_H_