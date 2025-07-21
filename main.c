#include "led.h"
#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "lcd.h"
#include "key.h"  
#include "24cxx.h" 
#include "myiic.h"
#include "touch.h" 

#define RECT_WIDTH   112
#define RECT_HEIGHT  50
#define RECT_GAP_X   5
#define RECT_GAP_Y   210

extern u8 USART_RX_BUF[USART_REC_LEN]; // 接收缓冲区
extern u16 USART_RX_STA;               // 接收状态

typedef struct {
    int circle_idx; // 0~8
    int grid_idx;   // 0~8
} TouchPair;

TouchPair recorded_pairs[4]; // 最多4组
int record_count = 0;
int last_circle_idx = -1;
int last_grid_idx = -1;

// 九宫格起始坐标
#define GRID_X0 60
#define GRID_Y0 30
#define GRID_X1 180
#define GRID_Y1 150

typedef enum { NONE = 0, BLACK_CHESS = 1, WHITE_CHESS = 2 } PieceColor;

int board[9];  // 九宫格棋盘
int new_board[9];  // 九宫格棋盘

PieceColor device_color = NONE;      // 装置所执颜色
PieceColor player_color = NONE;      // 玩家颜色
PieceColor current_turn = BLACK_CHESS;     // 当前轮到谁下（黑先）

int game_over = 0;                   // 游戏结束标志

typedef struct {
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u16 color;
} TouchRect;

typedef struct {
    u16 x;
    u16 y;
    u16 r;
    u16 color;
} TouchCircle;

TouchCircle circles[10]; // 前5个是左黑圆，后5个是右白圆

TouchRect rects[4] = {
    {RECT_GAP_X, RECT_GAP_Y, RECT_WIDTH, RECT_HEIGHT, RED},
    {RECT_GAP_X * 2 + RECT_WIDTH, RECT_GAP_Y, RECT_WIDTH, RECT_HEIGHT, GREEN},
    {RECT_GAP_X, RECT_GAP_Y + 5 + RECT_HEIGHT, RECT_WIDTH, RECT_HEIGHT, BLUE},
    {RECT_GAP_X * 2 + RECT_WIDTH, RECT_GAP_Y + 5 + RECT_HEIGHT, RECT_WIDTH, RECT_HEIGHT, YELLOW},
};

TouchRect ok_button = {190, 175, 40, 28};
TouchRect cancel_button = {5, 175, 50, 28};

#define MAX_POINTS 20

typedef struct {
    int x;
    float y;
} Point;

Point B_points[MAX_POINTS];
int B_points_count = 0;

Point W_points[MAX_POINTS];
int W_points_count = 0;

Point G_points[MAX_POINTS];
int G_points_count = 0;

const int win_lines[8][3] = {
        {0,1,2}, {3,4,5}, {6,7,8}, // rows
        {0,3,6}, {1,4,7}, {2,5,8}, // cols
        {0,4,8}, {2,4,6}           // diagonals
    };


int check_touch_grid(int x, int y)
{
    int col_width = (GRID_X1 - GRID_X0) / 3;
    int row_height = (GRID_Y1 - GRID_Y0) / 3;
		int col;
		int row;

    if (x < GRID_X0 || x >= GRID_X1 || y < GRID_Y0 || y >= GRID_Y1)
        return -1;

    col = (x - GRID_X0) / col_width;
    row = (y - GRID_Y0) / row_height;

    return row * 3 + col; // 0~8
}

void USART2_SendChar(char c)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, c);
}

void USART2_SendString(const char *str)
{
    while (*str)
    {
        USART2_SendChar(*str++);
    }
}

void send_to_servo(char source_type, int source_idx, char target_type, int target_idx)
{
    Point source_point, target_point;
    char msg[64];

    // 解析 source 点
    if (source_type == 'B') {
        if (source_idx < 1 || source_idx > B_points_count) return;
        source_point = B_points[source_idx - 1];
    } else if (source_type == 'W') {
        if (source_idx < 1 || source_idx > W_points_count) return;
        source_point = W_points[source_idx - 1];
    } else if (source_type == 'G') {
        if (source_idx < 1 || source_idx > G_points_count) return;
        source_point = G_points[source_idx - 1];
    } else return;

    // 解析 target 点
    if (target_type == 'B') {
        if (target_idx < 1 || target_idx > B_points_count) return;
        target_point = B_points[target_idx - 1];
    } else if (target_type == 'W') {
        if (target_idx < 1 || target_idx > W_points_count) return;
        target_point = W_points[target_idx - 1];
    } else if (target_type == 'G') {
        if (target_idx < 1 || target_idx > G_points_count) return;
        target_point = G_points[target_idx - 1];
    } else return;

    // 格式化发送：S:x1,y1;T:x2,y2#
    sprintf(msg, "S:%d,%d;T:%d,%d#", (int)source_point.x, (int)source_point.y,
                                     (int)target_point.x, (int)target_point.y);

    USART2_SendString(msg);
}

////////////////////////////////////////////////////////////

//ALIENTEK Mini STM32开发板范例代码21
//触摸屏实验  
//技术支持：www.openedv.com
//广州市星翼电子科技有限公司
   	
void Load_Drow_Dialog(void)
{
	LCD_Clear(WHITE);//清屏   
 	POINT_COLOR=BLUE;//设置字体为蓝色 
	LCD_ShowString(lcddev.width-24,0,200,16,16,"RST");//显示清屏区域
  	POINT_COLOR=RED;//设置画笔蓝色 
}
////////////////////////////////////////////////////////////////////////////////
//电容触摸屏专有部分
//画水平线
//x0,y0:坐标
//len:线长度
//color:颜色
void gui_draw_hline(u16 x0,u16 y0,u16 len,u16 color)
{
	if(len==0)return;
	LCD_Fill(x0,y0,x0+len-1,y0,color);	
}
//画实心圆
//x0,y0:坐标
//r:半径
//color:颜色
void gui_fill_circle(u16 x0,u16 y0,u16 r,u16 color)
{											  
	u32 i;
	u32 imax = ((u32)r*707)/1000+1;
	u32 sqmax = (u32)r*(u32)r+(u32)r/2;
	u32 x=r;
	gui_draw_hline(x0-r,y0,2*r,color);
	for (i=1;i<=imax;i++) 
	{
		if ((i*i+x*x)>sqmax)// draw lines from outside  
		{
 			if (x>imax) 
			{
				gui_draw_hline (x0-i+1,y0+x,2*(i-1),color);
				gui_draw_hline (x0-i+1,y0-x,2*(i-1),color);
			}
			x--;
		}
		// draw lines from inside (center)  
		gui_draw_hline(x0-x,y0+i,2*x,color);
		gui_draw_hline(x0-x,y0-i,2*x,color);
	}
}  
//两个数之差的绝对值 
//x1,x2：需取差值的两个数
//返回值：|x1-x2|
u16 my_abs(u16 x1,u16 x2)
{			 
	if(x1>x2)return x1-x2;
	else return x2-x1;
}  
//画一条粗线
//(x1,y1),(x2,y2):线条的起始坐标
//size：线条的粗细程度
//color：线条的颜色
void lcd_draw_bline(u16 x1, u16 y1, u16 x2, u16 y2,u8 size,u16 color)
{
	u16 t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance; 
	int incx,incy,uRow,uCol; 
	if(x1<size|| x2<size||y1<size|| y2<size)return; 
	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1; 
	uRow=x1; 
	uCol=y1; 
	if(delta_x>0)incx=1; //设置单步方向 
	else if(delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;} 
	if(delta_y>0)incy=1; 
	else if(delta_y==0)incy=0;//水平线 
	else{incy=-1;delta_y=-delta_y;} 
	if( delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y; 
	for(t=0;t<=distance+1;t++ )//画线输出 
	{  
		gui_fill_circle(uRow,uCol,size,color);//画点 
		xerr+=delta_x ; 
		yerr+=delta_y ; 
		if(xerr>distance) 
		{ 
			xerr-=distance; 
			uRow+=incx; 
		} 
		if(yerr>distance) 
		{ 
			yerr-=distance; 
			uCol+=incy; 
		} 
	}  
}   
////////////////////////////////////////////////////////////////////////////////
//5个触控点的颜色												 
//电阻触摸屏测试函数
void rtp_test(void)
{
	u8 key;
	u8 i=0;	  
	while(1)
	{
	 	key=KEY_Scan(0);
		tp_dev.scan(0); 		 
		if(tp_dev.sta&TP_PRES_DOWN)			//触摸屏被按下
		{	
		 	if(tp_dev.x[0]<lcddev.width&&tp_dev.y[0]<lcddev.height)
			{	
				if(tp_dev.x[0]>(lcddev.width-24)&&tp_dev.y[0]<16)Load_Drow_Dialog();//清除
				else TP_Draw_Big_Point(tp_dev.x[0],tp_dev.y[0],RED);		//画图	  			   
			}
		}else delay_ms(10);	//没有按键按下的时候 	    
		if(key==KEY0_PRES)	//KEY0按下,则执行校准程序
		{
			LCD_Clear(WHITE);//清屏
		    TP_Adjust();  //屏幕校准 
			TP_Save_Adjdata();	 
			Load_Drow_Dialog();
		}
		i++;
		if(i%20==0)LED0=!LED0;
	}
}
const u16 POINT_COLOR_TBL[CT_MAX_TOUCH]={RED,GREEN,BLUE,BROWN,GRED};  
//电容触摸屏测试函数
void ctp_test(void)
{
	u8 t=0;
	u8 i=0;	  	    
 	u16 lastpos[5][2];		//最后一次的数据 
	while(1)
	{
		tp_dev.scan(0);
		for(t=0;t<CT_MAX_TOUCH;t++)//最多5点触摸
		{
			if((tp_dev.sta)&(1<<t))//判断是否有点触摸？
			{
				if(tp_dev.x[t]<lcddev.width&&tp_dev.y[t]<lcddev.height)//在LCD范围内
				{
					if(lastpos[t][0]==0XFFFF)
					{
						lastpos[t][0] = tp_dev.x[t];
						lastpos[t][1] = tp_dev.y[t];
					}
					lcd_draw_bline(lastpos[t][0],lastpos[t][1],tp_dev.x[t],tp_dev.y[t],2,POINT_COLOR_TBL[t]);//画线
					lastpos[t][0]=tp_dev.x[t];
					lastpos[t][1]=tp_dev.y[t];
					if(tp_dev.x[t]>(lcddev.width-24)&&tp_dev.y[t]<16)
					{
						Load_Drow_Dialog();//清除
					}
				}
			}else lastpos[t][0]=0XFFFF;
		}
		
		delay_ms(5);i++;
		if(i%20==0)LED0=!LED0;
	}	
}

/////////////////////////////////////////////////
void clear_region(u16 x, u16 y, u16 width, u16 height)
{
    LCD_Fill(x, y, x + width - 1, y + height - 1, WHITE);
}

void gui_draw_rect(u16 x0, u16 y0, u16 width, u16 height, u16 color)
{
		u16 y;
    gui_draw_hline(x0, y0, width, color);                     // 上边
    gui_draw_hline(x0, y0 + height - 1, width, color);        // 下边
    for(y = y0; y < y0 + height; y++)                     // 左右边
    {
				POINT_COLOR=color;
        LCD_DrawPoint(x0, y);
        LCD_DrawPoint(x0 + width - 1, y);
    }
}

void draw_touch_rects(void)
{
		int i;
		clear_region(0, 200, 240, 200);
    for(i = 0; i < 4; i++) {
        gui_draw_rect(rects[i].x, rects[i].y, rects[i].width, rects[i].height, rects[i].color);
    }
}

void show_info(void)
{
		LCD_ShowString(15, 220, 90, 32, 16, "put 1 black in NO5");
		LCD_ShowString(130, 220, 90, 32, 16, "put 2 black 2 white");
		LCD_ShowString(15, 275, 90, 32, 16, "device plays black");
		LCD_ShowString(130, 275, 90, 32, 16, "device plays white");
}

void LCD_DrawCircle(u16 x0, u16 y0, u16 r, u16 color)
{
    int x = 0;
    int y = r;
    int d = 1 - r;

    while (x <= y)
    {
        LCD_DrawPoint(x0 + x, y0 + y);
        LCD_DrawPoint(x0 - x, y0 + y);
        LCD_DrawPoint(x0 + x, y0 - y);
        LCD_DrawPoint(x0 - x, y0 - y);
        LCD_DrawPoint(x0 + y, y0 + x);
        LCD_DrawPoint(x0 - y, y0 + x);
        LCD_DrawPoint(x0 + y, y0 - x);
        LCD_DrawPoint(x0 - y, y0 - x);

        x++;
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
}


void LCD_Fill_Circle(u16 x0, u16 y0, u16 r, u16 color)
{
    int a = 0;
    int b = r;
    int di = 3 - (r << 1);

    while (a <= b)
    {
        // 垂直线填充上下对称区域
        LCD_DrawLine(x0 - a, y0 - b, x0 - a, y0 + b);
        LCD_DrawLine(x0 + a, y0 - b, x0 + a, y0 + b);
        LCD_DrawLine(x0 - b, y0 - a, x0 - b, y0 + a);
        LCD_DrawLine(x0 + b, y0 - a, x0 + b, y0 + a);

        a++;
        if (di < 0)
            di += 4 * a + 6;
        else
        {
            di += 4 * (a - b) + 10;
            b--;
        }
    }
}

void draw_and_init_circles(void)
{
    const u16 r = 10;
    const u16 spacing = 33;
    const u16 start_y = 20;

		int i;
    for (i = 0; i < 5; i++)
    {
        u16 y = start_y + i * spacing;

        // 左边黑圆
				POINT_COLOR=BLACK;
        circles[i].x = 25;
        circles[i].y = y;
        circles[i].r = r;
        circles[i].color = BLACK;
        LCD_Fill_Circle(circles[i].x, circles[i].y, r, BLACK);

        // 右边白圆
				POINT_COLOR=GRAY;
        circles[i + 5].x = 205;
        circles[i + 5].y = y;
        circles[i + 5].r = r;
        circles[i + 5].color = GRAY;
        LCD_Fill_Circle(circles[i + 5].x, circles[i + 5].y, r, GRAY);
    }
}

int check_touch_circle(u16 x, u16 y)
{
		int i;
    for (i = 0; i < 10; i++)
    {
        int dx = x - circles[i].x;
        int dy = y - circles[i].y;
        if (dx * dx + dy * dy <= circles[i].r * circles[i].r)
        {
            return i; // 返回第几个圆被点中
        }
    }
    return -1;
}

int check_touch_rect(u16 x, u16 y)
{
		int i;
    for(i = 0; i < 4; i++) {
        if(x >= rects[i].x && x <= rects[i].x + rects[i].width &&
           y >= rects[i].y && y <= rects[i].y + rects[i].height) {
            return i; // 返回点击到的矩形编号
        }
    }
    return -1; // 没有点到
}

void draw_9_grid(u16 x0, u16 y0, u16 x1, u16 y1, u16 color)
{
    u16 cell_width = (x1 - x0) / 3;
    u16 cell_height = (y1 - y0) / 3;

    // 画外框
    gui_draw_rect(x0, y0, x1 - x0, y1 - y0, color);

    // 画竖线
    LCD_DrawLine(x0 + cell_width, y0, x0 + cell_width, y1);
    LCD_DrawLine(x0 + 2 * cell_width, y0, x0 + 2 * cell_width, y1);

    // 画横线
    LCD_DrawLine(x0, y0 + cell_height, x1, y0 + cell_height);
    LCD_DrawLine(x0, y0 + 2 * cell_height, x1, y0 + 2 * cell_height);
}

int is_in_rect(TouchRect* rect, u16 tx, u16 ty)
{
    return (tx >= rect->x && tx <= rect->x + rect->width &&
            ty >= rect->y && ty <= rect->y + rect->height);
}

void put_1_black_in_NO5(void)
{	
		clear_region(60, 176, 200, 32);
		POINT_COLOR=rects[0].color;
		gui_draw_rect(rects[0].x + 1, rects[0].y + 1, rects[0].width - 2, rects[0].height - 2, rects[0].color);
    gui_draw_rect(190, 175, 40, 28, RED);     // OK 按钮框
    LCD_ShowString(200, 182, 40, 16, 16, "OK");

    gui_draw_rect(5, 175, 50, 28, RED);       // Cancel 按钮框
    LCD_ShowString(7, 182, 48, 16, 16, "cancel");

    while (1)
    {
        tp_dev.scan(0); // 扫描触摸
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            u16 tx = tp_dev.x[0];
            u16 ty = tp_dev.y[0];

            if (is_in_rect(&ok_button, tx, ty))
            {
                printf("board_state\r\n");
								clear_region(190, 175, 40, 28);     // OK 按钮框
								clear_region(5, 175, 50, 28);    // Cancel 按钮框
                break;  // 跳出循环
            }
            else if (is_in_rect(&cancel_button, tx, ty))
            {
								clear_region(190, 175, 40, 28);     // OK 按钮框
								clear_region(5, 175, 50, 28);    // Cancel 按钮框
                break;  // 跳出循环
            }
        }
    }
}

void draw_highlight_circle(u16 x, u16 y, u16 r)
{
		POINT_COLOR=GREEN;
    LCD_DrawCircle(x, y, r, GREEN);
}

void put_chess(void)
{
		int i;
    int selected_circle = -1;
		int grid_idx;
    while (record_count < 4)
    {
        tp_dev.scan(0);
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            u16 tx = tp_dev.x[0];
            u16 ty = tp_dev.y[0];

            int circle_idx = check_touch_circle(tx, ty);
            if (circle_idx >= 0 && circle_idx != last_circle_idx)
            {
                last_circle_idx = circle_idx;
                selected_circle = circle_idx;

                // 重画全部圆和高亮
                for (i = 0; i < 10; i++)
                {
                    POINT_COLOR = WHITE;
                    LCD_Fill_Circle(circles[i].x, circles[i].y, circles[i].r + 4, WHITE);
                    POINT_COLOR = circles[i].color;
                    LCD_Fill_Circle(circles[i].x, circles[i].y, circles[i].r, circles[i].color);
                }
                draw_highlight_circle(circles[circle_idx].x, circles[circle_idx].y, circles[circle_idx].r + 3);
                LCD_ShowString(60, 175, 200, 16, 16, "Touched Circle: ");
                LCD_ShowNum(200, 175, circle_idx + 1, 2, 16);
								LCD_ShowString(60, 191, 200, 16, 16, "Grid Selected: ");
                LCD_ShowNum(200, 191, 0, 2, 16);
            }

            grid_idx = check_touch_grid(tx, ty);
            if (grid_idx >= 0 && grid_idx != last_grid_idx && selected_circle >= 0)
            {
                last_grid_idx = grid_idx;

                // 保存数据
                recorded_pairs[record_count].circle_idx = selected_circle;
                recorded_pairs[record_count].grid_idx = grid_idx;
                record_count++;

                LCD_ShowString(60, 191, 200, 16, 16, "Grid Selected: ");
                LCD_ShowNum(200, 191, grid_idx + 1, 2, 16);

                selected_circle = -1;
							
								// 重画全部圆
								for (i = 0; i < 10; i++)
                {
                    POINT_COLOR = WHITE;
                    LCD_Fill_Circle(circles[i].x, circles[i].y, circles[i].r + 4, WHITE);
                    POINT_COLOR = circles[i].color;
                    LCD_Fill_Circle(circles[i].x, circles[i].y, circles[i].r, circles[i].color);
                }
            }
        }
    }
	
		clear_region(60, 176, 200, 32);
		LCD_ShowString(60, 175, 200, 16, 16, "select success!");

    // 发送数据
    for (i = 0; i < 4; i++)
    {
        printf("Pair %d: Circle %d -> Grid %d\r\n", i + 1,
               recorded_pairs[i].circle_idx + 1,
               recorded_pairs[i].grid_idx + 1);
    }
		POINT_COLOR=WHITE;
		gui_draw_rect(rects[1].x + 1, rects[1].y + 1, rects[1].width - 2, rects[1].height - 2, WHITE);
		
		// 清空 recorded_pairs 中的数据
		for (i = 0; i < 4; i++) {
				recorded_pairs[i].circle_idx = -1;
				recorded_pairs[i].grid_idx = -1;
		}

		// 重置记录数量
		record_count = 0;
}

void put_first_chess(void)
{
    int grid_idx = -1;
		int new_idx;
    last_grid_idx = -1;
		
    while (1)
    {
        tp_dev.scan(0);
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            u16 tx = tp_dev.x[0];
            u16 ty = tp_dev.y[0];

            // 检查是否点击了 OK
            if (is_in_rect(&ok_button, tx, ty))
            {
                if (grid_idx >= 0)
                {
                    // 输出选中的格子编号
										board[grid_idx] = BLACK_CHESS;
//										printf("BLACK MOVE: CIRCLE %d -> GRID %d\r\n", 1, grid_idx + 1);
										send_to_servo('B', 1, 'G', grid_idx + 1);
										clear_region(190, 175, 40, 28);     // OK 按钮框
										clear_region(5, 175, 50, 28);    // Cancel 按钮框

                    clear_region(60, 176, 200, 32);
                    LCD_ShowString(60, 175, 200, 16, 16, "Select success!");
                }
                else
                {
                    clear_region(60, 176, 200, 32);
                    LCD_ShowString(60, 175, 200, 16, 16, "No grid selected");
                }
                break;
            }

            // 检查是否点击了 Cancel
            if (is_in_rect(&cancel_button, tx, ty))
            {
                clear_region(190, 175, 40, 28);     // OK 按钮框
								clear_region(5, 175, 50, 28);    // Cancel 按钮框
								POINT_COLOR=WHITE;
								gui_draw_rect(rects[2].x + 1, rects[2].y + 1, rects[2].width - 2, rects[2].height - 2, WHITE);
                break;
            }

            // 检查是否点击了格子
            new_idx = check_touch_grid(tx, ty);
            if (new_idx >= 0 && new_idx != last_grid_idx)
            {
                grid_idx = new_idx;
                last_grid_idx = new_idx;

                LCD_ShowString(60, 191, 200, 16, 16, "Grid Selected: ");
                LCD_ShowNum(200, 191, grid_idx + 1, 2, 16);
							
								gui_draw_rect(190, 175, 40, 28, RED);     // OK 按钮框
								LCD_ShowString(200, 182, 40, 16, 16, "OK");

								gui_draw_rect(5, 175, 50, 28, RED);       // Cancel 按钮框
								LCD_ShowString(7, 182, 48, 16, 16, "cancel");
            }
        }
    }
}

void put_2_black_2_white(void)
{	
		clear_region(60, 176, 200, 32);
		POINT_COLOR=rects[1].color;
		gui_draw_rect(rects[1].x + 1, rects[1].y + 1, rects[1].width - 2, rects[1].height - 2, rects[1].color);
		put_chess();
}

void device_move(void)
{
    // 简单策略：第一个空格落子
		int i;
    for (i = 0; i < 9; i++)
    {
        if (board[i] == 0)
        {
            board[i] = device_color;

            // 执行物理落子动作（如移动伺服、推子）
//            place(i, device_color);

            break;
        }
    }
}

void parse_board_state(const char *str)
{
		const char *p = str;
		const char *next;
		int i;
    // 清空计数
    B_points_count = 0;
    W_points_count = 0;
    G_points_count = 0;
		
	
		if (strncmp(p, "state:", 6) == 0)
		{
				p += 6; // 跳过 "state:"
				for (i = 0; i < 9; i++)
				{
						int value = 0;
						if (sscanf(p, "%d", &value) == 1)
						{
								new_board[i] = value;
						}

						// 找到下一个 ',' 或 '#'，移动指针
						next = strchr(p, (i < 8) ? ',' : '#');
						if (!next) break;
						p = next + 1;
				}

				LCD_ShowString(60, 191, 200, 16, 16, "State Parsed");
				return; // state 解析完成后退出
		}

    while (*p)
    {
        char color = *p;  // B W G
        if (color != 'B' && color != 'W' && color != 'G') break;

        p += 2; // 跳过 'B:' 或 'W:' 或 'G:'

        while (*p && *p != '#')
        {
            // 解析 x,y 坐标，格式：x,y;
            int x = 0;
            float y = 0.0f;
						char pair[32];
						int len;
	
            // 先找到分号或#结束
            const char *semicolon = strchr(p, ';');
            const char *hash = strchr(p, '#');

            const char *end = NULL;
            if (semicolon && (!hash || semicolon < hash)) end = semicolon;
            else if (hash) end = hash;
            else break; // 格式错误

            len = end - p;
            if (len >= sizeof(pair)) len = sizeof(pair) - 1;
            strncpy(pair, p, len);
            pair[len] = '\0';

            // 解析 "x,y"
            sscanf(pair, "%d,%f", &x, &y);

            // 存储到对应数组
            switch (color)
            {
                case 'B':
                    if (B_points_count < MAX_POINTS)
                    {
                        B_points[B_points_count].x = x;
                        B_points[B_points_count].y = y;
                        B_points_count++;
												LCD_ShowString(60, 191, 200, 16, 16, "B");
                    }
                    break;
                case 'W':
                    if (W_points_count < MAX_POINTS)
                    {
                        W_points[W_points_count].x = x;
                        W_points[W_points_count].y = y;
                        W_points_count++;
												LCD_ShowString(60, 191, 200, 16, 16, "W");
                    }
                    break;
                case 'G':
                    if (G_points_count < MAX_POINTS)
                    {
                        G_points[G_points_count].x = x;
                        G_points[G_points_count].y = y;
                        G_points_count++;
											LCD_ShowString(60, 191, 200, 16, 16, "G");
                    }
                    break;
            }

						
            if (*end == '#') 
						{
								LCD_ShowString(60, 191, 200, 16, 16, "parse success: ");
								break;
						}
            p = end + 1;
        }

        // 跳过 '#'
        while (*p && *p != 'B' && *p != 'W' && *p != 'G') p++;
    }
}

void wait_for_player_move(void)
{
    while (1)
    {
//        if (receive_board_update())  // 例如串口接收到新的 state:... 数据
//        {
//            for (int i = 0; i < 9; i++)
//            {
//                if (player_color == WHITE && board_state[i] == WHITE)
//                {
//                    // 玩家落子成功
//                    return;
//                }
//            }
//        }
				if (USART_RX_STA & 0x8000) // 判断串口接收完成
        {
            USART_RX_BUF[USART_RX_STA & 0x3FFF] = 0;  // 字符串结尾
            parse_board_state((const char *)USART_RX_BUF);	
            USART_RX_STA = 0; // 清空状态，准备下一次接收
//						if (player_color == WHITE && board[i] == WHITE)
//						{
//								// 玩家落子成功
//								return;
//						}
        }
    }
}

void show_winner(void)
{
		int i;
	
    LCD_ShowString(60, 191, 200, 16, 16, "Game Over");	
    for (i = 0; i < 8; i++)
    {
        int a = win_lines[i][0];
        int b = win_lines[i][1];
        int c = win_lines[i][2];

        if (board[a] != 0 &&
            board[a] == board[b] &&
            board[b] == board[c])
        {
            if (board[a] == device_color)
                LCD_ShowString(60, 210, 200, 16, 16, "Device Wins");
            else
                LCD_ShowString(60, 210, 200, 16, 16, "Player Wins");
            return;
        }
    }
    LCD_ShowString(60, 210, 200, 16, 16, "Draw");
}


int check_win(void)
{
		int full = 1;
		int i;
    for (i = 0; i < 8; i++)
    {
        int a = win_lines[i][0];
        int b = win_lines[i][1];
        int c = win_lines[i][2];

        if (board[a] != 0 &&
            board[a] == board[b] &&
            board[b] == board[c])
        {
            return 1;
        }
    }

    // 是否平局
    for (i = 0; i < 9; i++)
    {
        if (board[i] == 0) {
            full = 0;
            break;
        }
    }

    if (full) return 1;

    return 0;
}


void play_game(void)
{
    while (!game_over)
    {
        if (current_turn == device_color)
        {
            device_move();       // 装置下棋
            current_turn = player_color;
        }
        else if (current_turn == player_color)
        {
            wait_for_player_move();  // 等待并解析玩家落子
            current_turn = device_color;
        }

        if (check_win())  // 检查是否胜利或平局
        {
            game_over = 1;
            show_winner();
        }
    }
}

void device_plays_black(void)
{
		clear_region(60, 176, 200, 32);
	
		// 设置装置为黑棋
    device_color = BLACK_CHESS;
    player_color = WHITE_CHESS;
    current_turn = BLACK_CHESS;

		POINT_COLOR=rects[2].color;
		gui_draw_rect(rects[2].x + 1, rects[2].y + 1, rects[2].width - 2, rects[2].height - 2, rects[2].color);
		put_first_chess();
	
		// 进入主下棋循环
    play_game();
}

void device_plays_white(void)
{
		clear_region(60, 176, 200, 32);
}

void menu(void)
{
//		while(1)
//		{
		tp_dev.scan(0); // 扫描触摸
		if(tp_dev.sta & TP_PRES_DOWN)
		{
				int index = check_touch_rect(tp_dev.x[0], tp_dev.y[0]);
				if (index == 0)
				{
						put_1_black_in_NO5();
				}
				else if (index == 1)
				{
						put_2_black_2_white();
				}
				else if (index == 2)
				{
						device_plays_black();
				}
				else if (index == 3)
				{
						device_plays_white();
				}
		}
//		}
}



////////////////////////////////////////////////////
int main(void)
{ 
		delay_init();	    	 //延时函数初始化	  
		NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); //设置NVIC中断分组2:2位抢占优先级，2位响应优先级
		uart_init(9600);	 	//串口初始化为9600
		uart2_init(9600);	 	//串口初始化为9600
		LED_Init();		  		//初始化与LED连接的硬件接口
		LCD_Init();			   	//初始化LCD 	
		KEY_Init();				//按键初始化		 	
		tp_dev.init();			//触摸屏初始化
		POINT_COLOR=RED;//设置字体为红色 
	  gui_draw_rect(5, 5, 320 * 0.7, 240 * 0.7, RED);
	
		draw_touch_rects();
		draw_and_init_circles();
		POINT_COLOR=BLACK;
		draw_9_grid(60, 30, 180, 150, BLACK);
		
		POINT_COLOR=RED;
		show_info();
	  while(1)
    {
        menu(); // 只执行一次触摸判断和操作

        if (USART_RX_STA & 0x8000) // 判断串口接收完成
        {
            USART_RX_BUF[USART_RX_STA & 0x3FFF] = 0;  // 字符串结尾
            parse_board_state((const char *)USART_RX_BUF);
						
            USART_RX_STA = 0; // 清空状态，准备下一次接收
        }

        // 其它周期性处理，比如刷新显示、状态检测等
    }
//	LCD_ShowString(60,50,200,16,16,"Mini STM32");	
//	LCD_ShowString(60,70,200,16,16,"TOUCH TEST");	
//	LCD_ShowString(60,90,200,16,16,"ATOM@ALIENTEK");
//	LCD_ShowString(60,110,200,16,16,"2014/3/11");
//	if(tp_dev.touchtype!=0XFF)LCD_ShowString(60,130,200,16,16,"Press KEY0 to Adjust");//电阻屏才显示
//	delay_ms(1500);
//	Load_Drow_Dialog();	 	
//	if(tp_dev.touchtype&0X80)ctp_test();	//电容屏测试
//	else rtp_test(); 						//电阻屏测试
}

