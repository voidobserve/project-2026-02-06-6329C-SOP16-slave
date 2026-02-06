#include "includes.h"
#include "app_config.h"
#include "lcd_spi_config.h"
#include "demon_eye.h"
#include "eyelid_lut.h"

// 双缓冲区
static uint16_t segment_buffer_0[SCREEN_WIDTH * SEGMENT_HEIGHT];
static uint16_t segment_buffer_1[SCREEN_WIDTH * SEGMENT_HEIGHT];

static uint16_t *render_buffer = segment_buffer_0;
static uint16_t *display_buffer = segment_buffer_1;

// 恶魔眼实例
static demon_eye_t demon_eye;
static uint16_t eye_image[80 * 80];

// 智能更新：分段脏标记
static uint8_t segment_dirty_flags[SEGMENT_COUNT] = {0};
static uint8_t force_full_update = 1;

void LCD_DMA_Fill_COLOR(int xsta, int ysta, int xend, int yend, u16 *color)
{
    lcd_spi_draw_area(xsta, ysta, xend, yend, color);
}

// 快速整数平方根
static uint16_t fast_sqrt(uint32_t x) {
    if (x == 0) return 0;
    
    uint32_t root = 0;
    uint32_t bit = 1 << 30;
    
    while (bit > x) {
        bit >>= 2;
    }
    
    while (bit != 0) {
        if (x >= root + bit) {
            x -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    
    return (uint16_t)root;
}

// 生成测试眼球图片
static void generate_test_eye_image(void) {
    int16_t center_x = 40;
    int16_t center_y = 40;
    int16_t eye_radius = 38;
    int16_t iris_radius = 20;
    int16_t pupil_radius = 10;
    
    int16_t highlight_x = center_x - 8;
    int16_t highlight_y = center_y - 8;
    int16_t highlight_radius = 6;
    
    for (int y = 0; y < 80; y++) {
        for (int x = 0; x < 80; x++) {
            int16_t dx = x - center_x;
            int16_t dy = y - center_y;
            int16_t dist_sq = dx * dx + dy * dy;
            
            int16_t hl_dx = x - highlight_x;
            int16_t hl_dy = y - highlight_y;
            int16_t hl_dist_sq = hl_dx * hl_dx + hl_dy * hl_dy;
            
            uint16_t color = COLOR_TRANSPARENT;
            
            if (dist_sq < eye_radius * eye_radius) {
                if (dist_sq < pupil_radius * pupil_radius) {
                    color = COLOR_PUPIL;
                }
                else if (dist_sq < iris_radius * iris_radius) {
                    color = COLOR_IRIS;
                    int16_t dist = fast_sqrt(dist_sq);
                    uint8_t brightness = 255 - ((dist - pupil_radius) * 40 / (iris_radius - pupil_radius));
                    color = ((brightness >> 3) << 11) | ((brightness >> 5) << 5) | (brightness >> 6);
                }
                else {
                    color = COLOR_WHITE;
                    int16_t dist = fast_sqrt(dist_sq);
                    if (dist > eye_radius - 3) {
                        uint8_t shadow = 200 - (eye_radius - dist) * 40;
                        color = ((shadow >> 3) << 11) | ((shadow >> 2) << 5) | (shadow >> 3);
                    }
                }
                
                if (hl_dist_sq < highlight_radius * highlight_radius) {
                    uint8_t blend = 255 - (hl_dist_sq * 255 / (highlight_radius * highlight_radius));
                    color = (color & 0xF7DE) + ((blend >> 4) << 11) + ((blend >> 3) << 5) + (blend >> 4);
                }
            }
            
            eye_image[y * 80 + x] = color;
        }
    }
}

// 定时器回调
static void demon_eye_timer_callback(void *priv) {
    static uint32_t last_move_time = 0;
    uint32_t current_time = jiffies_msec();
    
    // 随机眼球移动（每2秒）
    if (current_time - last_move_time >= 2000) {
        int16_t target_x = rand() % SCREEN_WIDTH;
        int16_t target_y = rand() % SCREEN_HEIGHT;
        demon_eye_look_at(&demon_eye, target_x, target_y);
        last_move_time = current_time;
    }
    
    // 更新状态
    demon_eye_update(&demon_eye, current_time);
}

// 交换缓冲区
static inline void swap_buffers(void) {
    uint16_t *temp = render_buffer;
    render_buffer = display_buffer;
    display_buffer = temp;
}

// 标记需要更新的段
static void mark_dirty_segments(void) {
    memset(segment_dirty_flags, 0, sizeof(segment_dirty_flags));
    
    if (force_full_update) {
        for (uint8_t seg = 0; seg < SEGMENT_COUNT; seg++) {
            segment_dirty_flags[seg] = 1;
        }
        force_full_update = 0;
        return;
    }
    
    for (uint8_t seg = 0; seg < SEGMENT_COUNT; seg++) {
        if (demon_eye_segment_needs_update(&demon_eye, seg)) {
            segment_dirty_flags[seg] = 1;
        }
    }
}

// ==================== 优化：三级流水线渲染 ====================
// 时间线优化：
// 段N: [渲染] -> [等待DMA N-1] -> [启动DMA N] -> [渲染段N+1]
//                  ^ 在这段时间内渲染下一段，充分利用CPU
// ===========================================================
static void demon_eye_render_all_segments_optimized(void) {
    // 等待上一帧最后一段DMA完成
    spi_dma_wait_isr_finish();
    
    // 标记需要更新的段
    mark_dirty_segments();
    
    // 收集所有脏段到连续数组（避免循环中判断）
    uint8_t dirty_count = 0;
    uint8_t dirty_segments[SEGMENT_COUNT];
    
    for (uint8_t seg = 0; seg < SEGMENT_COUNT; seg++) {
        if (segment_dirty_flags[seg]) {
            dirty_segments[dirty_count++] = seg;
        }
    }
    
    // 如果没有脏段，直接返回
    if (dirty_count == 0) {
        return;
    }
    
    // ==================== 第一段：预渲染并启动DMA ====================
    uint8_t first_seg = dirty_segments[0];
    demon_eye.segment_buffer = render_buffer;
    demon_eye_render_segment(&demon_eye, first_seg);
    
    int y_start = first_seg * SEGMENT_HEIGHT;
    int y_end = y_start + SEGMENT_HEIGHT - 1;
    LCD_DMA_Fill_COLOR(0, y_start, SCREEN_WIDTH - 1, y_end, render_buffer);
    swap_buffers();
    // =================================================================
    
    // ==================== 后续段：三级流水线 ====================
    // 在DMA传输第N段时，渲染第N+1段
    for (uint8_t i = 1; i < dirty_count; i++) {
        uint8_t segment = dirty_segments[i];
        
        // 【并行1】在DMA传输上一段的同时，渲染当前段
        demon_eye.segment_buffer = render_buffer;
        demon_eye_render_segment(&demon_eye, segment);
        
        // 【同步点】等待上一段DMA完成
        spi_dma_wait_isr_finish();
        
        // 【并行2】立即启动当前段DMA（此时CPU可以继续渲染下一段）
        y_start = segment * SEGMENT_HEIGHT;
        y_end = y_start + SEGMENT_HEIGHT - 1;
        LCD_DMA_Fill_COLOR(0, y_start, SCREEN_WIDTH - 1, y_end, render_buffer);
        
        swap_buffers();
    }
    // ===========================================================
    
    // 注意：最后一段DMA仍在传输中，不等待（留到下一帧开始时等待）
    // 这样可以利用最后一段DMA时间做其他计算
}

// 初始化恶魔眼测试
void demon_eye_test_init(void) {
    generate_test_eye_image();
    demon_eye_init(&demon_eye, render_buffer, eye_image);
    
    printf("=======================================================\n");
    printf("Demon Eye Optimized (Smart Update + Pipeline)\n");
    printf("=======================================================\n");
    
    // ==================== 验证眼帘数据 ====================
    printf("\nVerifying eyelid data...\n");
    printf("EYELID_KEYFRAMES: %d\n", EYELID_KEYFRAMES);
    printf("Eye center: (%d, %d)\n", EYE_CENTER_X, EYE_CENTER_Y);
    printf("Eyelid width: ±%d pixels\n\n", EYELID_WIDTH);
    
    // 检查第一帧（闭合）
    int16_t kf0_center_upper = EYE_CENTER_Y + upper_eyelid_keyframes[0][EYE_CENTER_X];
    int16_t kf0_center_lower = EYE_CENTER_Y + lower_eyelid_keyframes[0][EYE_CENTER_X];
    int16_t kf0_gap = kf0_center_lower - kf0_center_upper;
    
    int16_t kf0_left_upper = EYE_CENTER_Y + upper_eyelid_keyframes[0][0];
    int16_t kf0_left_lower = EYE_CENTER_Y + lower_eyelid_keyframes[0][0];
    
    printf("Keyframe 0 (CLOSED):\n");
    printf("  Center: upper=%d, lower=%d, gap=%d\n", 
           kf0_center_upper, kf0_center_lower, kf0_gap);
    printf("  Left corner: upper=%d, lower=%d\n", kf0_left_upper, kf0_left_lower);
    if (kf0_gap > 5) {
        printf("  ⚠ WARNING: Frame 0 should be closed!\n");
    } else if (kf0_left_upper != EYE_CENTER_Y || kf0_left_lower != EYE_CENTER_Y) {
        printf("  ⚠ WARNING: Corners should be at y=%d\n", EYE_CENTER_Y);
    } else {
        printf("  ✓ OK\n");
    }
    
    // 检查最后一帧（睁开）
    int16_t kfN_center_upper = EYE_CENTER_Y + upper_eyelid_keyframes[EYELID_KEYFRAMES-1][EYE_CENTER_X];
    int16_t kfN_center_lower = EYE_CENTER_Y + lower_eyelid_keyframes[EYELID_KEYFRAMES-1][EYE_CENTER_X];
    int16_t kfN_gap = kfN_center_lower - kfN_center_upper;
    
    int16_t kfN_left_upper = EYE_CENTER_Y + upper_eyelid_keyframes[EYELID_KEYFRAMES-1][0];
    int16_t kfN_left_lower = EYE_CENTER_Y + lower_eyelid_keyframes[EYELID_KEYFRAMES-1][0];
    
    printf("\nKeyframe %d (OPEN):\n", EYELID_KEYFRAMES-1);
    printf("  Center: upper=%d, lower=%d, gap=%d\n", 
           kfN_center_upper, kfN_center_lower, kfN_gap);
    printf("  Left corner: upper=%d, lower=%d\n", kfN_left_upper, kfN_left_lower);
    if (kfN_gap < 100) {
        printf("  ⚠ WARNING: Last frame should be open!\n");
    } else if (kfN_left_upper != EYE_CENTER_Y || kfN_left_lower != EYE_CENTER_Y) {
        printf("  ⚠ WARNING: Corners should be at y=%d\n", EYE_CENTER_Y);
    } else {
        printf("  ✓ OK\n");
    }
    
    printf("\nMapping test:\n");
    printf("  State 255 (OPEN)   -> KF[%d]\n", (255 * (EYELID_KEYFRAMES-1)) >> 8);
    printf("  State 0   (CLOSED) -> KF[%d]\n", (0 * (EYELID_KEYFRAMES-1)) >> 8);
    printf("=======================================================\n");
    // ======================================================
    
    printf("Segment height: %d pixels\n", SEGMENT_HEIGHT);
    printf("Segment count: %d\n", SEGMENT_COUNT);
    
    printf("\nSegment boundaries:\n");
    for (uint8_t seg = 0; seg < SEGMENT_COUNT; seg++) {
        int y_start = seg * SEGMENT_HEIGHT;
        int y_end = y_start + SEGMENT_HEIGHT - 1;
        printf("  Segment %d: Y=%d to Y=%d (height=%d)\n", 
               seg, y_start, y_end, y_end - y_start + 1);
    }
    
    int total_height = SEGMENT_COUNT * SEGMENT_HEIGHT;
    printf("\nTotal coverage: %d pixels (Screen: %d)\n", total_height, SCREEN_HEIGHT);
    
    printf("\nBuffer configuration:\n");
    printf("  Buffer 0: %p (%d KB)\n", segment_buffer_0, sizeof(segment_buffer_0) / 1024);
    printf("  Buffer 1: %p (%d KB)\n", segment_buffer_1, sizeof(segment_buffer_1) / 1024);
    printf("  Total: %d KB\n", (sizeof(segment_buffer_0) + sizeof(segment_buffer_1)) / 1024);
    
    printf("\nOptimizations enabled:\n");
    printf("  [x] Smart segment update\n");
    printf("  [x] 3-stage pipeline rendering\n");
    printf("  [x] 32-bit fast clear\n");
    printf("  [x] Optimized eyelid drawing\n");
    printf("=======================================================\n\n");
}

// 外部接口
void demon_eye_test_blink(void) {
    demon_eye_blink(&demon_eye);
}

void demon_eye_test_look(int16_t x, int16_t y) {
    demon_eye_look_at(&demon_eye, x, y);
}

// ==================== 主任务：混合模式自适应帧率控制 ====================
static void display_task_handler(void* priv)
{
    unsigned long fps_time = 0;
    unsigned long fps_count = 0;
    unsigned long total_updates = 0;
    unsigned long total_segments_updated = 0;
    unsigned long min_frame_time = 0xFFFFFFFF;
    unsigned long max_frame_time = 0;
    
    demon_eye_test_init();

    while (1)
    {
        unsigned long frame_start = jiffies_msec();
        
        // 使用优化的三级流水线渲染
        demon_eye_render_all_segments_optimized();
        
        // 统计本帧更新的段数
        uint8_t segments_this_frame = 0;
        for (uint8_t seg = 0; seg < SEGMENT_COUNT; seg++) {
            if (segment_dirty_flags[seg]) {
                segments_this_frame++;
            }
        }
        
        // 利用最后一段DMA传输时间进行其他运算
        demon_eye_timer_callback(NULL);
        
        // ==================== 混合模式：三级帧率控制 ====================
        // 根据眨眼状态和更新情况动态调整延迟
        uint8_t adaptive_delay;
        
        if (demon_eye.is_blinking) {
            // 【高优先级】眨眼动画：2个系统tick = 20ms ≈ 50 FPS
            // 确保眨眼动画流畅
            adaptive_delay = 2;
        } else if (segments_this_frame > 0) {
            // 【中优先级】有更新（眼球移动等）：3个tick = 30ms ≈ 33 FPS
            // 平衡流畅度和CPU占用
            adaptive_delay = 3;
        } else {
            // 【低优先级】完全空闲：5个tick = 50ms ≈ 20 FPS
            // 最大化省电，降低CPU负载
            adaptive_delay = 5;
        }
        // =================================================================
        
        os_time_dly(adaptive_delay);
        
        unsigned long frame_time = jiffies_msec() - frame_start;
        if (frame_time < min_frame_time) min_frame_time = frame_time;
        if (frame_time > max_frame_time) max_frame_time = frame_time;

        fps_count++;
        total_updates++;
        total_segments_updated += segments_this_frame;
        
        // 每秒统计一次
        if (jiffies_msec() - fps_time >= 1000)
        {
            unsigned long avg_segments_x10 = (total_segments_updated * 10) / total_updates;
            unsigned long avg_int = avg_segments_x10 / 10;
            unsigned long avg_frac = avg_segments_x10 % 10;
            
            unsigned long percent_x10 = (total_segments_updated * 1000) / (total_updates * SEGMENT_COUNT);
            unsigned long percent_int = percent_x10 / 10;
            unsigned long percent_frac = percent_x10 % 10;
            
            unsigned long actual_fps = fps_count * 1000 / (jiffies_msec() - fps_time);
            
            // ==================== 增强日志：显示帧率模式 ====================
            const char* fps_mode;
            if (actual_fps >= 45) {
                fps_mode = "BLINK";  // 眨眼模式
            } else if (actual_fps >= 28) {
                fps_mode = "MOVE ";  // 移动模式
            } else {
                fps_mode = "IDLE ";  // 空闲模式
            }
            
            printf("[%s] FPS:%lu | Seg:%lu.%lu/%d (%lu.%lu%%) | Frame:%lu-%lums\n", 
                   fps_mode,
                   actual_fps,
                   avg_int, avg_frac,
                   SEGMENT_COUNT,
                   percent_int, percent_frac,
                   min_frame_time, max_frame_time);
            // ==============================================================
            
            fps_time = jiffies_msec();
            fps_count = 0;
            total_updates = 0;
            total_segments_updated = 0;
            min_frame_time = 0xFFFFFFFF;
            max_frame_time = 0;
        }
    }
}

void display_task_init(void)
{
    os_task_create(display_task_handler, NULL, 3, 1024, 0, "lcd_spi");
}
