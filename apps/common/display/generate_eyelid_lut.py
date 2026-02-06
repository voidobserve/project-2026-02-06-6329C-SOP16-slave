import numpy as np
import math
import matplotlib.pyplot as plt
import matplotlib.patches as patches

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240
EYE_CENTER_X = 148
EYE_CENTER_Y = 120
EYELID_KEYFRAMES = 31

# ==================== 眼角位置配置 ====================
# 可以调整这些值来控制眼睛的水平范围
EYE_LEFT_MARGIN = 10        # 左眼角距离屏幕左边缘的距离
EYE_RIGHT_MARGIN = 34       # 右眼角距离屏幕右边缘的距离（10 + 24）
# ====================================================

def bezier_cubic(t, p0, p1, p2, p3):
    """
    三次贝塞尔曲线
    t: 参数 [0, 1]
    p0, p1, p2, p3: 控制点
    """
    return (1-t)**3 * p0 + 3*(1-t)**2*t * p1 + 3*(1-t)*t**2 * p2 + t**3 * p3

def find_control_offset(target_center_offset, tolerance=0.5, max_iterations=30):
    """
    二分查找法：找到合适的控制点偏移，使中心点达到目标偏移
    """
    if abs(target_center_offset) < 0.1:
        return 0.0
    
    control_min = target_center_offset * 0.1
    control_max = target_center_offset * 3.0
    
    for iteration in range(max_iterations):
        control_offset = (control_min + control_max) / 2.0
        
        # 计算 t=0.5 时的Y值（中心点）
        center_y = bezier_cubic(0.5, 0, control_offset, control_offset, 0)
        
        error = center_y - target_center_offset
        
        if abs(error) < tolerance:
            return control_offset
        
        if (target_center_offset > 0 and center_y > target_center_offset) or \
           (target_center_offset < 0 and center_y < target_center_offset):
            control_max = control_offset
        else:
            control_min = control_offset
    
    return control_offset

def generate_eyelid_curve(openness, is_upper):
    """
    生成眼帘曲线（贝塞尔曲线）
    openness: 0.0(完全闭合) ~ 1.0(完全睁开)
    is_upper: True=上眼帘, False=下眼帘
    
    返回值：相对于EYE_CENTER_Y的偏移量数组（320个值）
    """
    # ==================== 修复：使用配置的眼角位置 ====================
    eye_left_x = EYE_LEFT_MARGIN
    eye_right_x = SCREEN_WIDTH - 1 - EYE_RIGHT_MARGIN
    eye_width = eye_right_x - eye_left_x
    # =================================================================
    
    # 计算中心点的目标偏移量
    if is_upper:
        target_center_offset = -60.0 * openness
    else:
        target_center_offset = 60.0 * openness
    
    # 精确计算控制点偏移
    if abs(openness) < 0.001:
        control_point_offset = 0
    else:
        control_point_offset = find_control_offset(target_center_offset)
    
    # ==================== 关键修复：贝塞尔曲线基于眼角坐标 ====================
    # 贝塞尔曲线控制点（相对于眼睛左边缘）
    # P0: 左眼角 (0, 0)
    # P1: 左侧控制点 (eye_width * 0.25, control_offset)
    # P2: 右侧控制点 (eye_width * 0.75, control_offset)
    # P3: 右眼角 (eye_width, 0)
    p0_y = 0
    p1_y = control_point_offset
    p2_y = control_point_offset
    p3_y = 0
    # =========================================================================
    
    curve = []
    
    # 生成320个点
    for x in range(SCREEN_WIDTH):
        # ==================== 关键修复：根据X坐标判断是否在眼睛范围内 ====================
        if x < eye_left_x or x > eye_right_x:
            # 眼睛外部区域：偏移为0（保持在中心线）
            y_offset = 0
        else:
            # 眼睛内部区域：计算贝塞尔曲线
            # 将X坐标映射到 [0, 1] 范围
            # x = eye_left_x  -> t = 0
            # x = eye_right_x -> t = 1
            t = (x - eye_left_x) / eye_width
            
            # 使用贝塞尔曲线计算Y偏移
            y_offset = bezier_cubic(t, p0_y, p1_y, p2_y, p3_y)
        # ================================================================================
        
        curve.append(int(round(y_offset)))
    
    return curve

def verify_curve(curve, openness, is_upper):
    """验证曲线数据的正确性"""
    errors = []
    
    eye_left_x = EYE_LEFT_MARGIN
    eye_right_x = SCREEN_WIDTH - 1 - EYE_RIGHT_MARGIN
    
    # ==================== 修复：检查眼角位置 ====================
    # 检查左眼角
    if curve[eye_left_x] != 0:
        errors.append(f"Left corner (x={eye_left_x}) offset should be 0, got {curve[eye_left_x]}")
    
    # 检查右眼角
    if curve[eye_right_x] != 0:
        errors.append(f"Right corner (x={eye_right_x}) offset should be 0, got {curve[eye_right_x]}")
    
    # 检查眼睛外部区域（应该都是0）
    for x in range(0, eye_left_x):
        if curve[x] != 0:
            errors.append(f"Outside left (x={x}) should be 0, got {curve[x]}")
            break
    
    for x in range(eye_right_x + 1, SCREEN_WIDTH):
        if curve[x] != 0:
            errors.append(f"Outside right (x={x}) should be 0, got {curve[x]}")
            break
    # ==========================================================
    
    # 检查中心点
    center_offset = curve[EYE_CENTER_X]
    expected_center = -60 * openness if is_upper else 60 * openness
    
    tolerance = 2
    if abs(center_offset - expected_center) > tolerance:
        errors.append(f"Center offset should be ≈{expected_center:.1f}, got {center_offset}")
    
    return errors

def plot_preview(upper_keyframes, lower_keyframes):
    """生成预览图"""
    print("\n" + "=" * 70)
    print("Generating preview images...")
    print("=" * 70)
    
    eye_left_x = EYE_LEFT_MARGIN
    eye_right_x = SCREEN_WIDTH - 1 - EYE_RIGHT_MARGIN
    
    preview_frames = [0, 5, 10, 15, 20, 25, 30]
    
    fig, axes = plt.subplots(2, len(preview_frames), figsize=(20, 8))
    fig.suptitle(f'Eyelid Animation Preview (Eye: x={eye_left_x} to {eye_right_x})', fontsize=16)
    
    for idx, kf in enumerate(preview_frames):
        openness = kf / (EYELID_KEYFRAMES - 1)
        
        # 上半部分：完整视图
        ax_full = axes[0, idx]
        ax_full.set_xlim(0, SCREEN_WIDTH)
        ax_full.set_ylim(0, SCREEN_HEIGHT)
        ax_full.invert_yaxis()
        ax_full.set_aspect('equal')
        ax_full.set_title(f'KF{kf}\nOpen={openness:.2f}')
        ax_full.set_xlabel('X')
        ax_full.set_ylabel('Y')
        ax_full.grid(True, alpha=0.3)
        
        rect = patches.Rectangle((0, 0), SCREEN_WIDTH, SCREEN_HEIGHT, 
                                 linewidth=2, edgecolor='black', facecolor='white')
        ax_full.add_patch(rect)
        
        # ==================== 标注眼角位置 ====================
        ax_full.axvline(x=eye_left_x, color='green', linestyle='--', alpha=0.5, label='Eye Corners')
        ax_full.axvline(x=eye_right_x, color='green', linestyle='--', alpha=0.5)
        # ===================================================
        
        ax_full.plot(EYE_CENTER_X, EYE_CENTER_Y, 'ro', markersize=8, label='Eye Center')
        
        upper_curve = upper_keyframes[kf]
        lower_curve = lower_keyframes[kf]
        
        x_coords = range(SCREEN_WIDTH)
        upper_y = [EYE_CENTER_Y + offset for offset in upper_curve]
        lower_y = [EYE_CENTER_Y + offset for offset in lower_curve]
        
        ax_full.plot(x_coords, upper_y, 'b-', linewidth=2, label='Upper Eyelid')
        ax_full.plot(x_coords, lower_y, 'r-', linewidth=2, label='Lower Eyelid')
        ax_full.fill_between(x_coords, upper_y, lower_y, alpha=0.3, color='cyan')
        
        if idx == 0:
            ax_full.legend(loc='upper right', fontsize=8)
        
        # 下半部分：局部放大
        ax_zoom = axes[1, idx]
        ax_zoom.set_xlim(0, SCREEN_WIDTH)
        ax_zoom.set_ylim(60, 180)
        ax_zoom.invert_yaxis()
        ax_zoom.set_aspect('equal')
        ax_zoom.set_title(f'Zoom (60-180)')
        ax_zoom.set_xlabel('X')
        ax_zoom.set_ylabel('Y')
        ax_zoom.grid(True, alpha=0.3)
        
        ax_zoom.axhline(y=EYE_CENTER_Y, color='gray', linestyle='--', alpha=0.5)
        ax_zoom.axvline(x=EYE_CENTER_X, color='gray', linestyle='--', alpha=0.5)
        ax_zoom.axvline(x=eye_left_x, color='green', linestyle='--', alpha=0.5)
        ax_zoom.axvline(x=eye_right_x, color='green', linestyle='--', alpha=0.5)
        
        ax_zoom.plot(x_coords, upper_y, 'b-', linewidth=2, label='Upper')
        ax_zoom.plot(x_coords, lower_y, 'r-', linewidth=2, label='Lower')
        ax_zoom.fill_between(x_coords, upper_y, lower_y, alpha=0.3, color='cyan')
        
        center_upper = EYE_CENTER_Y + upper_curve[EYE_CENTER_X]
        center_lower = EYE_CENTER_Y + lower_curve[EYE_CENTER_X]
        gap = center_lower - center_upper
        
        ax_zoom.plot([EYE_CENTER_X, EYE_CENTER_X], [center_upper, center_lower], 
                    'g-', linewidth=2)
        ax_zoom.text(EYE_CENTER_X + 10, (center_upper + center_lower) / 2, 
                    f'gap={gap}px', fontsize=9, color='green')
    
    plt.tight_layout()
    
    output_image = 'eyelid_preview.png'
    plt.savefig(output_image, dpi=150, bbox_inches='tight')
    print(f"✓ Saved preview: {output_image}")
    
    try:
        plt.show()
        print("✓ Preview window closed")
    except:
        print("✓ Preview saved (display not available)")

def main():
    eye_left_x = EYE_LEFT_MARGIN
    eye_right_x = SCREEN_WIDTH - 1 - EYE_RIGHT_MARGIN
    eye_width = eye_right_x - eye_left_x
    
    print("=" * 70)
    print("Generating Eyelid Lookup Table (Bezier Curves)")
    print("=" * 70)
    print(f"Screen size: {SCREEN_WIDTH}x{SCREEN_HEIGHT}")
    print(f"Eye center: ({EYE_CENTER_X}, {EYE_CENTER_Y})")
    print(f"Eye corners: Left={eye_left_x}, Right={eye_right_x}, Width={eye_width}")
    print(f"Keyframes: {EYELID_KEYFRAMES}")
    print(f"Curve type: Cubic Bezier")
    print()
    
    upper_keyframes = []
    lower_keyframes = []
    all_errors = []
    
    for i in range(EYELID_KEYFRAMES):
        openness = i / (EYELID_KEYFRAMES - 1)
        
        upper_curve = generate_eyelid_curve(openness, is_upper=True)
        lower_curve = generate_eyelid_curve(openness, is_upper=False)
        
        upper_errors = verify_curve(upper_curve, openness, is_upper=True)
        lower_errors = verify_curve(lower_curve, openness, is_upper=False)
        
        if upper_errors or lower_errors:
            all_errors.append((i, upper_errors + lower_errors))
        
        upper_keyframes.append(upper_curve)
        lower_keyframes.append(lower_curve)
        
        center_upper_y = EYE_CENTER_Y + upper_curve[EYE_CENTER_X]
        center_lower_y = EYE_CENTER_Y + lower_curve[EYE_CENTER_X]
        center_gap = center_lower_y - center_upper_y
        
        left_upper_y = EYE_CENTER_Y + upper_curve[eye_left_x]
        left_lower_y = EYE_CENTER_Y + lower_curve[eye_left_x]
        
        right_upper_y = EYE_CENTER_Y + upper_curve[eye_right_x]
        right_lower_y = EYE_CENTER_Y + lower_curve[eye_right_x]
        
        status = "CLOSED" if center_gap <= 0 else f"gap={center_gap:3d}"
        corner_status = "✓"
        if left_upper_y != EYE_CENTER_Y or left_lower_y != EYE_CENTER_Y:
            corner_status = f"⚠L({left_upper_y},{left_lower_y})"
        if right_upper_y != EYE_CENTER_Y or right_lower_y != EYE_CENTER_Y:
            corner_status = f"⚠R({right_upper_y},{right_lower_y})"
        
        print(f"KF{i:2d}: open={openness:.3f} | Center: U={center_upper_y:3d} L={center_lower_y:3d} {status:10s} | " +
              f"Corners: L({left_upper_y},{left_lower_y}) R({right_upper_y},{right_lower_y}) {corner_status}")
    
    if all_errors:
        print("\n" + "=" * 70)
        print("⚠ ERRORS FOUND:")
        for kf_idx, errors in all_errors:
            print(f"\nKeyframe {kf_idx}:")
            for error in errors:
                print(f"  - {error}")
        print("=" * 70)
        print("\n❌ Generation aborted due to errors!")
        return
    
    print()
    
    try:
        plot_preview(upper_keyframes, lower_keyframes)
    except Exception as e:
        print(f"⚠ Preview generation failed: {e}")
        print("  (Continuing with C code generation...)")
    
    output_file = 'eyelid_lut_data.c'
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('#include "eyelid_lut.h"\n\n')
        f.write('// Auto-generated eyelid lookup table (Bezier curves)\n')
        f.write(f'// Eye center: ({EYE_CENTER_X}, {EYE_CENTER_Y})\n')
        f.write(f'// Eye corners: Left={eye_left_x}, Right={eye_right_x}, Width={eye_width}\n')
        f.write(f'// Curve type: Cubic Bezier\n')
        f.write(f'// Keyframes: {EYELID_KEYFRAMES}\n\n')
        
        for i, curve in enumerate(upper_keyframes):
            f.write(f'// Upper eyelid keyframe {i}\n')
            f.write(f'static const int16_t upper_eyelid_keyframe_{i}[{SCREEN_WIDTH}] = {{\n')
            for j in range(0, len(curve), 16):
                f.write('    ')
                chunk = curve[j:j+16]
                f.write(', '.join(f'{val:4d}' for val in chunk))
                if j + 16 < len(curve):
                    f.write(',')
                f.write('\n')
            f.write('};\n\n')
        
        for i, curve in enumerate(lower_keyframes):
            f.write(f'// Lower eyelid keyframe {i}\n')
            f.write(f'static const int16_t lower_eyelid_keyframe_{i}[{SCREEN_WIDTH}] = {{\n')
            for j in range(0, len(curve), 16):
                f.write('    ')
                chunk = curve[j:j+16]
                f.write(', '.join(f'{val:4d}' for val in chunk))
                if j + 16 < len(curve):
                    f.write(',')
                f.write('\n')
            f.write('};\n\n')
        
        f.write(f'const int16_t *upper_eyelid_keyframes[{EYELID_KEYFRAMES}] = {{\n')
        for i in range(EYELID_KEYFRAMES):
            f.write(f'    upper_eyelid_keyframe_{i}')
            if i < EYELID_KEYFRAMES - 1:
                f.write(',')
            f.write('\n')
        f.write('};\n\n')
        
        f.write(f'const int16_t *lower_eyelid_keyframes[{EYELID_KEYFRAMES}] = {{\n')
        for i in range(EYELID_KEYFRAMES):
            f.write(f'    lower_eyelid_keyframe_{i}')
            if i < EYELID_KEYFRAMES - 1:
                f.write(',')
            f.write('\n')
        f.write('};\n')
    
    print()
    print("=" * 70)
    print(f"✓ Generated: {output_file}")
    print(f"✓ All {EYELID_KEYFRAMES} keyframes validated successfully")
    print(f"✓ Eye range: x={eye_left_x} to {eye_right_x} (width={eye_width}px)")
    print(f"✓ Preview image: eyelid_preview.png")
    print("=" * 70)

if __name__ == '__main__':
    main()