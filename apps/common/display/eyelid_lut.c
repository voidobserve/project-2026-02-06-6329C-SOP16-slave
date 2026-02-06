#include "eyelid_lut.h"
#include "demon_eye.h"
#include <string.h>
#include <stdio.h> // 添加头文件以支持 printf

// 临时占位数据，运行 Python 脚本后替换
extern const int16_t *upper_eyelid_keyframes[EYELID_KEYFRAMES];
extern const int16_t *lower_eyelid_keyframes[EYELID_KEYFRAMES];

// 快速线性插值
static inline int16_t lerp_i16(int16_t a, int16_t b, uint8_t t) {
    // t: 0-255
    int32_t result = a + (((int32_t)(b - a) * t) >> 8);
    return (int16_t)result;
}

// 分段绘制上下眼帘
void draw_eyelids_segment(uint8_t state, uint16_t *segment_buffer, 
                          uint8_t segment_idx, uint16_t color) {
    
    int16_t segment_y_start = segment_idx * SEGMENT_HEIGHT;
    int16_t segment_y_end = segment_y_start + SEGMENT_HEIGHT;
    
    // 修正：计算关键帧索引（确保255映射到最后一帧）
    uint16_t frame_idx;
    uint16_t next_idx;
    uint8_t local_t;
    
    if (state >= 255) {
        // 完全闭合
        frame_idx = EYELID_KEYFRAMES - 1;  // frame 7
        next_idx = EYELID_KEYFRAMES - 1;   // frame 7
        local_t = 0;
    } else {
        // 计算插值
        // state: 0~254 映射到 frame 0~6
        uint32_t scaled = (uint32_t)state * (EYELID_KEYFRAMES - 1);
        frame_idx = scaled / 255;
        
        // 防止越界
        if (frame_idx >= EYELID_KEYFRAMES - 1) {
            frame_idx = EYELID_KEYFRAMES - 2;
            next_idx = EYELID_KEYFRAMES - 1;
            local_t = 255;
        } else {
            next_idx = frame_idx + 1;
            // 计算帧内插值 (0~255)
            uint16_t frame_start = (frame_idx * 255) / (EYELID_KEYFRAMES - 1);
            uint16_t frame_end = (next_idx * 255) / (EYELID_KEYFRAMES - 1);
            uint16_t frame_range = frame_end - frame_start;
            local_t = ((state - frame_start) * 255) / frame_range;
        }
    }
    
    // // 调试输出
    // static uint8_t last_state = 0;
    // if (abs(state - last_state) > 15 || state == 0 || state == 255) {
    //     printf("State:%3d -> Frame:%d->%d, t:%3d\n", state, frame_idx, next_idx, local_t);
    //     last_state = state;
    // }
    
    const int16_t *upper_frame1 = upper_eyelid_keyframes[frame_idx];
    const int16_t *upper_frame2 = upper_eyelid_keyframes[next_idx];
    const int16_t *lower_frame1 = lower_eyelid_keyframes[frame_idx];
    const int16_t *lower_frame2 = lower_eyelid_keyframes[next_idx];
    
    // 逐列处理
    for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
        // 插值获取上下眼帘位置（相对于眼角的偏移）
        int16_t upper_offset = lerp_i16(upper_frame1[x], upper_frame2[x], local_t);
        int16_t lower_offset = lerp_i16(lower_frame1[x], lower_frame2[x], local_t);
        
        // 计算实际屏幕坐标
        int16_t upper_y = EYE_CORNER_Y + upper_offset;
        int16_t lower_y = EYE_CORNER_Y + lower_offset;
        
        // 限制在屏幕范围内
        if (upper_y < 0) upper_y = 0;
        if (lower_y >= SCREEN_HEIGHT) lower_y = SCREEN_HEIGHT - 1;
        
        // 绘制上眼帘（从屏幕顶部到上眼帘曲线）
        if (upper_y > segment_y_start) {
            int16_t draw_start = segment_y_start;
            int16_t draw_end = (upper_y < segment_y_end) ? upper_y : segment_y_end;
            
            for (int16_t y = draw_start; y < draw_end; y++) {
                int16_t buffer_y = y - segment_y_start;
                if (buffer_y >= 0 && buffer_y < SEGMENT_HEIGHT) {
                    segment_buffer[buffer_y * SCREEN_WIDTH + x] = color;
                }
            }
        }
        
        // 绘制下眼帘（从下眼帘曲线到屏幕底部）
        if (lower_y < segment_y_end) {
            int16_t draw_start = (lower_y > segment_y_start) ? lower_y : segment_y_start;
            int16_t draw_end = segment_y_end;
            
            for (int16_t y = draw_start; y < draw_end; y++) {
                int16_t buffer_y = y - segment_y_start;
                if (buffer_y >= 0 && buffer_y < SEGMENT_HEIGHT) {
                    segment_buffer[buffer_y * SCREEN_WIDTH + x] = color;
                }
            }
        }
    }
}