import sensor, image, time
import math

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)
clock = time.clock()

white_threshold = (50, 70, -20, 20, -20, 20)
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

init_success = False
large_rect_info = None

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

while True:
    clock.tick()
    img = sensor.snapshot().lens_corr(strength=1.2, zoom=1.5)

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
                corners = large_black_blobs[0].min_corners()
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
