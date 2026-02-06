#ifndef _DEMON_EYE_H_
#define _DEMON_EYE_H_

#include <stdint.h>

// 屏幕尺寸
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// 分段扫描配置
#define SEGMENT_HEIGHT 40
#define SEGMENT_COUNT 6

// 眼球颜色定义
#define COLOR_BG 0xFFFF          // 白色背景
#define COLOR_PUPIL 0x0000       // 黑色瞳孔
#define COLOR_IRIS 0xF800        // 红色虹膜
#define COLOR_WHITE 0xFFFF       // 眼白
#define COLOR_TRANSPARENT 0xF81F // 透明色（品红）

// ==================== 关键修复：眼帘状态映射 ====================
// 眼帘状态值（内部使用）
#define EYELID_OPEN_VALUE 255      // 睁开状态：255 -> keyframe 7（最后一帧）
#define EYELID_CLOSED_VALUE 0      // 闭合状态：0 -> keyframe 0（第一帧）

// 眨眼判断阈值
#define EYELID_FULLY_CLOSED  5     // <= 5 认为完全闭合
#define EYELID_FULLY_OPEN    250   // >= 250 认为完全睁开

// 眨眼速度
#define BLINK_CLOSE_SPEED   85     // 闭合速度（减小）
#define BLINK_OPEN_SPEED    65     // 睁开速度（增大）
#define BLINK_CLOSED_DURATION  180  // 闭合持续时间（毫秒）
#define BLINK_INTERVAL         2500 // 眨眼间隔（毫秒）
// ================================================================

// 眨眼状态枚举
typedef enum {
    BLINK_STATE_IDLE = 0,
    BLINK_STATE_CLOSING,
    BLINK_STATE_CLOSED,
    BLINK_STATE_OPENING
} blink_state_e;

// 眼球位置状态
typedef struct {
    int16_t x, y;
    int16_t target_x, target_y;
    int16_t speed;
} eye_position_t;

// 眼帘状态
typedef struct {
    uint8_t current;
    uint8_t target;
    uint8_t speed;
} eyelid_state_t;

// 恶魔眼配置
typedef struct {
    uint16_t eye_width;
    uint16_t eye_height;
    uint16_t move_range_x;
    uint16_t move_range_y;
    uint32_t blink_interval;
    uint32_t blink_duration;
} demon_eye_config_t;

// 区域跟踪结构
typedef struct {
    int16_t top;
    int16_t bottom;
    int16_t left;
    int16_t right;
} eye_bounds_t;

// 恶魔眼状态
typedef struct {
    eye_position_t position;
    eyelid_state_t eyelid;
    demon_eye_config_t config;
    uint32_t last_blink_time;
    uint32_t blink_close_start_time;
    uint8_t is_blinking;
    blink_state_e blink_state;
    uint16_t *segment_buffer;
    const uint16_t *eye_image;
    uint8_t current_segment;
    
    // 智能更新相关
    uint8_t last_eyelid_state;
    uint8_t eyelid_dirty;
    eye_bounds_t last_bounds;
    eye_bounds_t current_bounds;
    uint8_t position_dirty;
} demon_eye_t;

// 函数声明
void demon_eye_init(demon_eye_t *eye, uint16_t *fb, const uint16_t *eye_img);
void demon_eye_update(demon_eye_t *eye, uint32_t current_time);
void demon_eye_render_segment(demon_eye_t *eye, uint8_t segment_idx);
void demon_eye_blink(demon_eye_t *eye);
void demon_eye_look_at(demon_eye_t *eye, int16_t target_x, int16_t target_y);
void demon_eye_update_bounds(demon_eye_t *eye);
uint8_t demon_eye_segment_needs_update(demon_eye_t *eye, uint8_t segment_idx);

#endif // _DEMON_EYE_H_
