import sensor, image, time
import math
from pyb import UART
uart = UART(3, 115200)  # 你可以根据实际接线使用 UART1 或 UART3

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)
clock = time.clock()

white_threshold = (55, 70, -10, 5, -5, 10)
black_threshold = (0, 30, -128, 127, -128, 127)

MAX_SMALL_AREA = 800
LARGE_BLACK_AREA = 10000
SQUARE_RATIO_TOLERANCE = 0.15
STABLE_COUNT_THRESHOLD = 10
MAX_TRACK_DIST = 30  # 最大追踪距离，像素

white_squares = {}
black_squares = {}

stable_white = []
stable_black = []

grid_centers = []

init_success = False
large_rect_info = None

position_detect = False
check_chess_state = False

def is_square(blob):
    ratio = blob.w() / blob.h()
    return (1.0 - SQUARE_RATIO_TOLERANCE) <= ratio <= (1.0 + SQUARE_RATIO_TOLERANCE)

def update_stability(blob, square_dict, stable_list, color):
    center = (round(blob.cx() / 5) * 5, round(blob.cy() / 5) * 5)
    key = str(center)
    if key in square_dict:
        square_dict[key][1] += 1
    else:
        square_dict[key] = [center, 1]
    if square_dict[key][1] == STABLE_COUNT_THRESHOLD:
        stable_list.append(center)
        print("%s方块锁定成功：%s" % (color, str(center)))

def distance(p1, p2):
    return math.sqrt((p1[0]-p2[0])**2 + (p1[1]-p2[1])**2)

def track_targets(targets, blobs):
    updated = [False] * len(targets)
    for blob in blobs:
        if blob.area() > MAX_SMALL_AREA or not is_square(blob):
            continue
        bx, by = blob.cx(), blob.cy()
        min_dist = MAX_TRACK_DIST
        min_idx = -1
        for i, (tx, ty) in enumerate(targets):
            d = distance((bx, by), (tx, ty))
            if d < min_dist and not updated[i]:
                min_dist = d
                min_idx = i
        if min_idx >= 0:
            targets[min_idx] = (bx, by)
            updated[min_idx] = True

def track_large_rect(blob, last_info):
    # 计算中心
    corners = blob.min_corners()
    cx = sum([p[0] for p in corners]) / 4
    cy = sum([p[1] for p in corners]) / 4
    angle = blob.rotation()  # 角度 -180~180

    if last_info is None:
        return (cx, cy), angle

    (last_cx, last_cy), last_angle = last_info
    dist_c = distance((cx, cy), (last_cx, last_cy))
    angle_diff = abs(angle - last_angle)
    if angle_diff > 180:
        angle_diff = 360 - angle_diff

    # 简单阈值判断是否匹配
    if dist_c < 50 and angle_diff < 30:
        return (cx, cy), angle
    else:
        # 如果匹配失败，保持旧位置
        return last_info

def lerp(p1, p2, t):
    """线性插值，返回p1和p2之间t比例点"""
    return (p1[0] + (p2[0] - p1[0]) * t,
            p1[1] + (p2[1] - p1[1]) * t)

def get_3x3_grid_corners(corners):
    """
    corners 是大矩形4个角，顺序OpenMV保证是左上、右上、右下、左下（逆时针）
    返回一个3x3列表，每个元素是小格子4个角点（左上、右上、右下、左下）
    """
    # 先计算左边边界三分点，右边边界三分点
    left_points = [lerp(corners[0], corners[3], t) for t in [0, 1/3, 2/3, 1]]
    right_points = [lerp(corners[1], corners[2], t) for t in [0, 1/3, 2/3, 1]]

    grid_corners = []
    for row in range(3):
        row_points = []
        # 每行左右边界两点
        left_start = left_points[row]
        left_end = left_points[row+1]
        right_start = right_points[row]
        right_end = right_points[row+1]
        # 该行左边界和右边界的三分点
        for col in range(3):
            # 四个角点（左上、右上、右下、左下）
            tl = lerp(left_start, right_start, col / 3)
            tr = lerp(left_start, right_start, (col+1) / 3)
            br = lerp(left_end, right_end, (col+1) / 3)
            bl = lerp(left_end, right_end, col / 3)
            row_points.append([tl, tr, br, bl])
        grid_corners.append(row_points)
    return grid_corners

def draw_grid(img, grid_corners):
    for row in range(3):
        for col in range(3):
            corners = grid_corners[row][col]
            # 画多边形边框
            img.draw_line(int(corners[0][0]), int(corners[0][1]), int(corners[1][0]), int(corners[1][1]), color=(0,0,255))
            img.draw_line(int(corners[1][0]), int(corners[1][1]), int(corners[2][0]), int(corners[2][1]), color=(0,0,255))
            img.draw_line(int(corners[2][0]), int(corners[2][1]), int(corners[3][0]), int(corners[3][1]), color=(0,0,255))
            img.draw_line(int(corners[3][0]), int(corners[3][1]), int(corners[0][0]), int(corners[0][1]), color=(0,0,255))
            # 计算中心点用于写编号
            cx = int(sum([p[0] for p in corners]) / 4)
            cy = int(sum([p[1] for p in corners]) / 4)
            num = row * 3 + col + 1  # 从1开始编号
            img.draw_string(cx - 5, cy - 5, str(num), color=(0,0,255), scale=2)

def sort_corners(corners):
    # corners 是4个点列表 [(x,y), ...]
    # 按x排序
    corners = sorted(corners, key=lambda p: p[0])
    left_points = corners[:2]
    right_points = corners[2:]
    # 左边两个按y排序，最小y为左上，最大y为左下
    left_top, left_bottom = sorted(left_points, key=lambda p: p[1])
    # 右边两个按y排序，最小y为右上，最大y为右下
    right_top, right_bottom = sorted(right_points, key=lambda p: p[1])
    return [left_top, right_top, right_bottom, left_bottom]

def send_data(label, data_list):
    """
    label: str, 如 'W', 'B', 'G'
    data_list: list of (x, y) 坐标对 或 (x, y, angle)
    统一格式：<label>:x1,y1;x2,y2;...#
    """
    msg = label + ':'
    for item in data_list:
        if isinstance(item, tuple) and len(item) == 3:
            msg += '{:.1f},{:.1f},{:.1f};'.format(item[0], item[1], item[2])
        else:
            msg += '{},{:.1f};'.format(int(item[0]), item[1])
    msg = msg.rstrip(';') + '#\n'
    uart.write(msg)
    
def board_state():
    global position_detect
    position_detect = False
    
def check_uart_command():
    if uart.any():
        cmd = uart.readline()
        if cmd:
            try:
                cmd_str = cmd.decode().strip()  # 解码并去除换行
                
                if cmd_str.startswith("board_state"):
                    board_state()
                elif cmd_str.startswith("chess_state"):
                    clear_state()
            except Exception as e:
                print("串口指令解析失败:", e)

def clear_state():
    global stable_white, stable_black, white_squares, black_squares, init_success, check_chess_state
    stable_white.clear()
    stable_black.clear()
    white_squares.clear()
    black_squares.clear()    
    init_success = False
    check_chess_state = True
                
def chess_state():
    global stable_white, stable_black, grid_centers
    if not grid_centers:
        uart.write("state:error_no_grid#\n")
        return

    # 初始化9格状态全为空
    state = [0] * 9

    # 定义一个函数判断点是否在格子附近
    def is_in_cell(px, py, cx, cy, threshold=20):
        return abs(px - cx) <= threshold and abs(py - cy) <= threshold

    # 对每个黑棋位置找最近格子，赋值1（黑）
    for bx, by in stable_black:
        for i, (gx, gy) in enumerate(grid_centers):
            if is_in_cell(bx, by, gx, gy):
                state[i] = 1
                break  # 找到一个格子就跳，防止覆盖

    # 对每个白棋位置找最近格子，赋值2（白），覆盖空格，但不覆盖黑棋
    for wx, wy in stable_white:
        for i, (gx, gy) in enumerate(grid_centers):
            if is_in_cell(wx, wy, gx, gy) and state[i] == 0:
                state[i] = 2
                break

    # 拼接状态字符串并发送
    msg = "state:" + ",".join(map(str, state)) + "#\n"
    uart.write(msg)
    print(msg)

while True:    
    check_uart_command()
    clock.tick()
    img = sensor.snapshot().lens_corr(strength=1.2, zoom=1.5)
    img = img.gaussian(1)

    if not init_success:
        # 锁定前阶段
        white_blobs = img.find_blobs([white_threshold], pixels_threshold=100, area_threshold=100)
        for blob in white_blobs:
            if blob.area() <= MAX_SMALL_AREA and is_square(blob):
                img.draw_rectangle(blob.rect(), color=(255, 0, 0))
                img.draw_cross(blob.cx(), blob.cy(), color=(255, 0, 0))
                update_stability(blob, white_squares, stable_white, "白")

        black_blobs = img.find_blobs([black_threshold], pixels_threshold=100, area_threshold=100)
        large_black_found = False
        arge_blob_candidate = None
        for blob in black_blobs:
            if is_square(blob):
                img.draw_rectangle(blob.rect(), color=(0, 255, 0))
                img.draw_cross(blob.cx(), blob.cy(), color=(0, 255, 0))
                if blob.area() <= MAX_SMALL_AREA:
                    update_stability(blob, black_squares, stable_black, "黑")
                elif blob.area() >= LARGE_BLACK_AREA:
                    large_black_found = True
                    large_black_rect = blob.rect()

        if len(stable_white) >= 5 and len(stable_black) >= 5 and large_black_found:
            init_success = True
            # 截取前5个后进行 y 坐标排序（从上往下）
            stable_white = sorted(stable_white[:5], key=lambda p: p[1])
            stable_black = sorted(stable_black[:5], key=lambda p: p[1])
            print("初始化成功！")
            for i, coord in enumerate(stable_white):
                print("白[%d] -> %s" % (i + 1, coord))
            for i, coord in enumerate(stable_black):
                print("黑[%d] -> %s" % (i + 1, coord))

    else:
        # 追踪大矩形
        black_blobs = img.find_blobs([black_threshold], pixels_threshold=100, area_threshold=100)
        large_black_blobs = [b for b in black_blobs if b.area() >= LARGE_BLACK_AREA and is_square(b)]
        if large_black_blobs:
            large_rect_info = track_large_rect(large_black_blobs[0], large_rect_info)

        # 初始化后进行追踪
        white_blobs = img.find_blobs([white_threshold], pixels_threshold=100, area_threshold=100)
        black_blobs = img.find_blobs([black_threshold], pixels_threshold=100, area_threshold=100)

        track_targets(stable_white, white_blobs)
        track_targets(stable_black, black_blobs)

        # 画旋转大矩形
        if large_rect_info:
            cx, cy = large_rect_info[0]
            angle = large_rect_info[1]

            # 找对应旋转矩形角点
            # 注意OpenMV里的min_corners总是四点，需要用上次大矩形的blob来绘制，如果无blob则画普通矩形

            # 这里直接用img.draw_rectangle的近似绘制（不带旋转）
            # 你也可以用img.draw_polygon用min_corners画多边形实现旋转绘制

            # 用最新大矩形blob的min_corners绘制多边形更准确：
            # 这里为了简化，绘制中心点和角点连线

            # 如果需要准确多边形绘制，得保存最新blob的min_corners
            # 为简化，假设最新blob是large_black_blobs[0]

            if large_black_blobs:
                raw_corners = large_black_blobs[0].min_corners()
                corners = sort_corners(raw_corners)
                grid_corners = get_3x3_grid_corners(corners)
                draw_grid(img, grid_corners)
                img.draw_line(corners[0][0], corners[0][1], corners[1][0], corners[1][1], color=(0,0,255), thickness=2)
                img.draw_line(corners[1][0], corners[1][1], corners[2][0], corners[2][1], color=(0,0,255), thickness=2)
                img.draw_line(corners[2][0], corners[2][1], corners[3][0], corners[3][1], color=(0,0,255), thickness=2)
                img.draw_line(corners[3][0], corners[3][1], corners[0][0], corners[0][1], color=(0,0,255), thickness=2)
                img.draw_cross(int(cx), int(cy), color=(0,0,255))

        for i, (x, y) in enumerate(stable_white):
            img.draw_cross(x, y, color=(255, 0, 0))
            img.draw_string(x + 5, y - 10, "白%d" % (i + 1), color=(255, 0, 0), scale=1)

        for i, (x, y) in enumerate(stable_black):
            img.draw_cross(x, y, color=(0, 255, 0))
            img.draw_string(x + 5, y - 10, "黑%d" % (i + 1), color=(0, 255, 0), scale=1)
            
        if not position_detect:
            # 串口发送数据
            send_data('B', stable_black)  # 黑棋
            send_data('W', stable_white)  # 白棋
    
            grid_centers = []
            for row in grid_corners:
                for cell in row:
                    cx = sum([p[0] for p in cell]) / 4
                    cy = sum([p[1] for p in cell]) / 4
                    grid_centers.append((cx, cy))
            send_data('G', grid_centers)  # G 表示 Grid
            
            position_detect = True
            
        if check_chess_state:
            chess_state()
            check_chess_state = False
