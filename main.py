import sensor, image, time

# 初始化摄像头
sensor.reset()
sensor.set_pixformat(sensor.RGB565)  # 颜色图像
sensor.set_framesize(sensor.QVGA)    # 320x240
sensor.skip_frames(time=2000)
clock = time.clock()

white_threshold = (50, 70, -20, 20, -20, 20)
black_threshold = (0, 30, -128, 127, -128, 127)

MAX_AREA = 800
SQUARE_RATIO_TOLERANCE = 0.15 
STABLE_COUNT_THRESHOLD = 10

def is_square(blob):
    ratio = blob.w() / blob.h()
    return (1.0 - SQUARE_RATIO_TOLERANCE) <= ratio <= (1.0 + SQUARE_RATIO_TOLERANCE)

while True:
    clock.tick()
    img = sensor.snapshot().lens_corr(strength = 1.2, zoom = 1.5) 

    # 找白色区域
    white_blobs = img.find_blobs([white_threshold], pixels_threshold=100, area_threshold=100)
    for blob in white_blobs:
        if blob.area() <= MAX_AREA and is_square(blob):
            img.draw_rectangle(blob.rect(), color=(255, 0, 0))      # 用红框标记白色
            img.draw_cross(blob.cx(), blob.cy(), color=(255, 0, 0))
            print("白色中心：", blob.cx(), blob.cy(), "面积：", blob.area())

    # 找黑色区域
    black_blobs = img.find_blobs([black_threshold], pixels_threshold=100, area_threshold=100)
    for blob in black_blobs:
        if is_square(blob):
            img.draw_rectangle(blob.rect(), color=(0, 255, 0))      # 用绿框标记黑色
            img.draw_cross(blob.cx(), blob.cy(), color=(0, 255, 0))
            print("黑色中心：", blob.cx(), blob.cy(), "面积：", blob.area())