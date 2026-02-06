#include "demon_eye.h"
#include "eyelid_lut.h"
#include <string.h>
#include <stdlib.h>

// ==================== 32位快速清屏 ====================
static inline void fast_clear_segment(uint16_t *buffer, uint16_t color) {
    if (color == 0xFFFF) {
        memset(buffer, 0xFF, SCREEN_WIDTH * SEGMENT_HEIGHT * sizeof(uint16_t));
        return;
    }
    
    if (color == 0x0000) {
        memset(buffer, 0x00, SCREEN_WIDTH * SEGMENT_HEIGHT * sizeof(uint16_t));
        return;
    }
    
    uint32_t color32 = ((uint32_t)color << 16) | color;
    uint32_t *ptr32 = (uint32_t *)buffer;
    uint32_t count = (SCREEN_WIDTH * SEGMENT_HEIGHT) >> 1;
    
    while (count--) {
        *ptr32++ = color32;
    }
    
    if ((SCREEN_WIDTH * SEGMENT_HEIGHT) & 1) {
        buffer[SCREEN_WIDTH * SEGMENT_HEIGHT - 1] = color;
    }
}

// 初始化恶魔眼
void demon_eye_init(demon_eye_t *eye, uint16_t *fb, const uint16_t *eye_img) {
    memset(eye, 0, sizeof(demon_eye_t));
    
    eye->segment_buffer = fb;
    eye->eye_image = eye_img;
    
    eye->config.eye_width = 80;
    eye->config.eye_height = 80;
    eye->config.move_range_x = 40;
    eye->config.move_range_y = 30;
    eye->config.blink_interval = BLINK_INTERVAL;
    eye->config.blink_duration = 0;
    
    eye->position.x = SCREEN_WIDTH / 2;
    eye->position.y = SCREEN_HEIGHT / 2;
    eye->position.target_x = eye->position.x;
    eye->position.target_y = eye->position.y;
    eye->position.speed = 3;
    
    // ==================== 关键修复：初始化为睁开状态 ====================
    eye->eyelid.current = EYELID_OPEN_VALUE;   // 255 = 睁开
    eye->eyelid.target = EYELID_OPEN_VALUE;    // 255 = 睁开
    eye->eyelid.speed = 20;
    // ====================================================================
    
    eye->current_segment = 0;
    eye->blink_state = BLINK_STATE_IDLE;
    eye->last_blink_time = 0;
    eye->blink_close_start_time = 0;
    
    eye->last_eyelid_state = EYELID_OPEN_VALUE;
    eye->eyelid_dirty = 1;
    eye->position_dirty = 1;
    
    demon_eye_update_bounds(eye);
    eye->last_bounds = eye->current_bounds;
}

void demon_eye_update_bounds(demon_eye_t *eye) {
    int16_t half_width = eye->config.eye_width >> 1;
    int16_t half_height = eye->config.eye_height >> 1;
    
    eye->current_bounds.left = eye->position.x - half_width;
    eye->current_bounds.right = eye->position.x + half_width;
    eye->current_bounds.top = eye->position.y - half_height;
    eye->current_bounds.bottom = eye->position.y + half_height;
    
    if (eye->current_bounds.left < 0) eye->current_bounds.left = 0;
    if (eye->current_bounds.right >= SCREEN_WIDTH) eye->current_bounds.right = SCREEN_WIDTH - 1;
    if (eye->current_bounds.top < 0) eye->current_bounds.top = 0;
    if (eye->current_bounds.bottom >= SCREEN_HEIGHT) eye->current_bounds.bottom = SCREEN_HEIGHT - 1;
}

uint8_t demon_eye_segment_needs_update(demon_eye_t *eye, uint8_t segment_idx) {
    if (segment_idx >= SEGMENT_COUNT) return 0;
    
    int16_t segment_top = segment_idx * SEGMENT_HEIGHT;
    int16_t segment_bottom = segment_top + SEGMENT_HEIGHT - 1;
    
    if (eye->eyelid_dirty) {
        return 1;
    }
    
    if (!eye->position_dirty) {
        return 0;
    }
    
    uint8_t current_overlaps = (eye->current_bounds.bottom >= segment_top && 
                                eye->current_bounds.top <= segment_bottom);
    
    uint8_t last_overlaps = (eye->last_bounds.bottom >= segment_top && 
                            eye->last_bounds.top <= segment_bottom);
    
    return (current_overlaps || last_overlaps);
}

static void update_eye_position(demon_eye_t *eye) {
    uint8_t position_changed = 0;
    
    if (eye->position.x != eye->position.target_x) {
        int16_t diff = eye->position.target_x - eye->position.x;
        if (abs(diff) <= eye->position.speed) {
            eye->position.x = eye->position.target_x;
        } else {
            eye->position.x += (diff > 0 ? eye->position.speed : -eye->position.speed);
        }
        position_changed = 1;
    }
    
    if (eye->position.y != eye->position.target_y) {
        int16_t diff = eye->position.target_y - eye->position.y;
        if (abs(diff) <= eye->position.speed) {
            eye->position.y = eye->position.target_y;
        } else {
            eye->position.y += (diff > 0 ? eye->position.speed : -eye->position.speed);
        }
        position_changed = 1;
    }
    
    if (position_changed) {
        eye->position_dirty = 1;
        demon_eye_update_bounds(eye);
    }
}

static void update_eyelid(demon_eye_t *eye) {
    uint8_t old_state = eye->eyelid.current;
    
    if (eye->eyelid.current != eye->eyelid.target) {
        int16_t diff = eye->eyelid.target - eye->eyelid.current;
        if (abs(diff) <= eye->eyelid.speed) {
            eye->eyelid.current = eye->eyelid.target;
        } else {
            eye->eyelid.current += (diff > 0 ? eye->eyelid.speed : -eye->eyelid.speed);
        }
    }
    
    if (old_state != eye->eyelid.current) {
        eye->eyelid_dirty = 1;
    }
}

// ==================== 关键修复：自动眨眼逻辑 ====================
static void auto_blink(demon_eye_t *eye, uint32_t current_time) {
    switch (eye->blink_state) {
        case BLINK_STATE_IDLE:
            // 检查是否该眨眼了
            if (current_time - eye->last_blink_time >= eye->config.blink_interval) {
                demon_eye_blink(eye);
                eye->last_blink_time = current_time;
            }
            break;
            
        case BLINK_STATE_CLOSING:
            // 判断是否闭合完成（255 -> 0，减小到阈值）
            if (eye->eyelid.current <= EYELID_FULLY_CLOSED) {
                eye->blink_state = BLINK_STATE_CLOSED;
                eye->blink_close_start_time = current_time;
            }
            break;
            
        case BLINK_STATE_CLOSED:
            // 保持闭合一段时间
            if (current_time - eye->blink_close_start_time >= BLINK_CLOSED_DURATION) {
                eye->eyelid.target = EYELID_OPEN_VALUE;  // 255 = 睁开
                eye->eyelid.speed = BLINK_OPEN_SPEED;
                eye->blink_state = BLINK_STATE_OPENING;
            }
            break;
            
        case BLINK_STATE_OPENING:
            // 判断是否睁开完成（0 -> 255，增大到阈值）
            if (eye->eyelid.current >= EYELID_FULLY_OPEN) {
                eye->blink_state = BLINK_STATE_IDLE;
                eye->is_blinking = 0;
            }
            break;
    }
}

void demon_eye_update(demon_eye_t *eye, uint32_t current_time) {
    update_eye_position(eye);
    update_eyelid(eye);
    auto_blink(eye, current_time);
}

// ==================== 关键修复：眨眼触发 ====================
void demon_eye_blink(demon_eye_t *eye) {
    if (!eye->is_blinking) {
        eye->is_blinking = 1;
        eye->blink_state = BLINK_STATE_CLOSING;
        eye->eyelid.target = EYELID_CLOSED_VALUE;  // 0 = 闭合
        eye->eyelid.speed = BLINK_CLOSE_SPEED;
    }
}

void demon_eye_look_at(demon_eye_t *eye, int16_t target_x, int16_t target_y) {
    int16_t center_x = SCREEN_WIDTH >> 1;
    int16_t center_y = SCREEN_HEIGHT >> 1;
    
    int16_t offset_x = target_x - center_x;
    int16_t offset_y = target_y - center_y;
    
    if (offset_x > eye->config.move_range_x) offset_x = eye->config.move_range_x;
    if (offset_x < -eye->config.move_range_x) offset_x = -eye->config.move_range_x;
    if (offset_y > eye->config.move_range_y) offset_y = eye->config.move_range_y;
    if (offset_y < -eye->config.move_range_y) offset_y = -eye->config.move_range_y;
    
    eye->position.target_x = center_x + offset_x;
    eye->position.target_y = center_y + offset_y;
}

static void draw_eye_image_segment(demon_eye_t *eye, uint8_t segment_idx) {
    int16_t segment_y_start = segment_idx * SEGMENT_HEIGHT;
    int16_t segment_y_end = segment_y_start + SEGMENT_HEIGHT - 1;
    
    int16_t eye_x = eye->position.x - (eye->config.eye_width >> 1);
    int16_t eye_y = eye->position.y - (eye->config.eye_height >> 1);
    
    if (eye_y + eye->config.eye_height <= segment_y_start || 
        eye_y > segment_y_end) {
        return;
    }
    
    int16_t src_y_start = 0;
    int16_t src_y_end = eye->config.eye_height - 1;
    
    if (eye_y < segment_y_start) {
        src_y_start = segment_y_start - eye_y;
    }
    
    if (eye_y + eye->config.eye_height > segment_y_end) {
        src_y_end = segment_y_end - eye_y;
    }
    
    uint16_t *buffer = eye->segment_buffer;
    const uint16_t *img = eye->eye_image;
    
    for (int16_t src_y = src_y_start; src_y <= src_y_end; src_y++) {
        int16_t screen_y = eye_y + src_y;
        int16_t buffer_y = screen_y - segment_y_start;
        
        if (buffer_y < 0 || buffer_y >= SEGMENT_HEIGHT) continue;
        
        uint16_t *row_ptr = buffer + buffer_y * SCREEN_WIDTH;
        const uint16_t *img_row = img + src_y * eye->config.eye_width;
        
        for (int16_t src_x = 0; src_x < eye->config.eye_width; src_x++) {
            int16_t screen_x = eye_x + src_x;
            
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;
            
            uint16_t color = img_row[src_x];
            
            if (color != COLOR_TRANSPARENT) {
                row_ptr[screen_x] = color;
            }
        }
    }
}

// ==================== 完全修复的眼帘绘制 ====================
static void draw_eyelid_segment(demon_eye_t *eye, uint8_t segment_idx) {
    int16_t segment_y_start = segment_idx * SEGMENT_HEIGHT;
    int16_t segment_y_end = segment_y_start + SEGMENT_HEIGHT - 1;
    
    uint8_t keyframe_idx = (eye->eyelid.current * (EYELID_KEYFRAMES - 1)) >> 8;
    if (keyframe_idx >= EYELID_KEYFRAMES) {
        keyframe_idx = EYELID_KEYFRAMES - 1;
    }
    
    const int16_t *upper_curve = upper_eyelid_keyframes[keyframe_idx];
    const int16_t *lower_curve = lower_eyelid_keyframes[keyframe_idx];
#if 0
    // ==================== 调试输出（使用新常量）====================
    static uint8_t debug_counter = 0;
    if (segment_idx == 3 && debug_counter++ % 30 == 0) {
        // 检查中心点
        int16_t center_upper = EYE_CENTER_Y + upper_curve[EYE_CENTER_X];
        int16_t center_lower = EYE_CENTER_Y + lower_curve[EYE_CENTER_X];
        int16_t center_gap = center_lower - center_upper;
        
        // 检查左眼角
        int16_t left_upper = EYE_CENTER_Y + upper_curve[0];
        int16_t left_lower = EYE_CENTER_Y + lower_curve[0];
        
        // 检查右眼角
        int16_t right_upper = EYE_CENTER_Y + upper_curve[SCREEN_WIDTH-1];
        int16_t right_lower = EYE_CENTER_Y + lower_curve[SCREEN_WIDTH-1];
        
        const char *state_str = "UNKNOWN";
        switch (eye->blink_state) {
            case BLINK_STATE_IDLE: state_str = "IDLE"; break;
            case BLINK_STATE_CLOSING: state_str = "CLOSING"; break;
            case BLINK_STATE_CLOSED: state_str = "CLOSED"; break;
            case BLINK_STATE_OPENING: state_str = "OPENING"; break;
        }
        
        printf("State=%d -> KF[%d/%d] | Center: U=%d L=%d gap=%d | ",
               eye->eyelid.current, keyframe_idx, EYELID_KEYFRAMES-1,
               center_upper, center_lower, center_gap);
        printf("Corners: (%d,%d) (%d,%d) | %s\n",
               left_upper, left_lower, right_upper, right_lower, state_str);
        
        if (center_gap <= 0) {
            printf("  ★ FULLY CLOSED!\n");
        } else if (center_gap >= 100) {
            printf("  ★ FULLY OPEN!\n");
        }
    }
    // ==============================================================
#endif
    uint16_t *buffer = eye->segment_buffer;
    
    for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
        // ==================== 使用新常量 ====================
        int16_t upper_y_absolute = EYE_CENTER_Y + upper_curve[x];
        int16_t lower_y_absolute = EYE_CENTER_Y + lower_curve[x];
        // ====================================================
        
        // 完全闭合检测
        if (upper_y_absolute >= lower_y_absolute) {
            for (int16_t buffer_y = 0; buffer_y < SEGMENT_HEIGHT; buffer_y++) {
                buffer[buffer_y * SCREEN_WIDTH + x] = 0x0000;
            }
            continue;
        }
        
        // 快速路径：段完全在眼帘外
        if (segment_y_end < upper_y_absolute || segment_y_start > lower_y_absolute) {
            for (int16_t buffer_y = 0; buffer_y < SEGMENT_HEIGHT; buffer_y++) {
                buffer[buffer_y * SCREEN_WIDTH + x] = 0x0000;
            }
            continue;
        }
        
        // 快速路径：段完全在眼内
        if (segment_y_start > upper_y_absolute && segment_y_end < lower_y_absolute) {
            continue;
        }
        
        // 逐像素判断
        for (int16_t buffer_y = 0; buffer_y < SEGMENT_HEIGHT; buffer_y++) {
            int16_t screen_y = segment_y_start + buffer_y;
            
            if (screen_y <= upper_y_absolute || screen_y >= lower_y_absolute) {
                buffer[buffer_y * SCREEN_WIDTH + x] = 0x0000;
            }
        }
    }
}

void demon_eye_render_segment(demon_eye_t *eye, uint8_t segment_idx) {
    if (segment_idx >= SEGMENT_COUNT) return;
    
    fast_clear_segment(eye->segment_buffer, COLOR_BG);
    draw_eye_image_segment(eye, segment_idx);
    draw_eyelid_segment(eye, segment_idx);
    
    if (segment_idx == SEGMENT_COUNT - 1) {
        eye->eyelid_dirty = 0;
        eye->position_dirty = 0;
        eye->last_eyelid_state = eye->eyelid.current;
        eye->last_bounds = eye->current_bounds;
    }
}
