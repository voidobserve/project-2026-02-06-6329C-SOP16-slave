#include "includes.h"
#include "app_config.h"
#include "lcd_spi_config.h"
#include "sc_gui.h"
#include "sc_gui_demo.h"
#include "sc_event_task.h"

#if 0
extern  lv_font_t lv_font_12;  //XBF全字库
extern  lv_font_t lv_font_14;  //部分字库

void display_draw_point(int x, int y, uint16_t rgb565)
{
    spi_dma_wait_finish();
    lcd_spi_draw_point(x, y, rgb565);
}

void LCD_DMA_Fill_COLOR(int xsta, int ysta, int xend, int yend, u16 *color)
{
    // printf("sta:%d, ysta:%d, xend:%d, yend:%d\n", xsta, ysta, xend, yend);
    lcd_spi_draw_area(xsta, ysta, xend, yend, color);
}

void display_task_handler(void* priv)
{
    SC_GUI_Init(display_draw_point, C_WHITE, C_BLUE, C_WHITE, (lv_font_t *)&lv_font_14);
    SC_Clear(0, 0, LCD_SCREEN_WIDTH - 1, LCD_SCREEN_HEIGHT - 1);

    sc_create_task(0, demo_main_task, 10); // 主线程
#if 0
    static Obj_Image    screen;
    static Obj_Canvas   Canvas;
    static Obj_Frame    Frame1;
    static Obj_Label    label1;
    static Obj_Txtbox   txtbox;

    obj_add_Screen(NULL, (Obj_t*)&screen, 0, 0, 240, 240, ALIGN_NONE);    //空屏幕
    obj_add_Canvas(&screen,&Canvas,0, 0, 240, 240,240*4,240,ALIGN_CENTER);   //容器640*240
    obj_add_Frame(&Canvas,&Frame1,0, 0,200,100,gui->fc,gui->bc,20,12,ALIGN_CENTER);
    // obj_add_Image(&Frame1,&image,&tempC_img_48, 20, 0,ALIGN_VER);
    obj_add_Label(&Frame1,&label1,100,110,50,30,ALIGN_CENTER);
    // obj_add_Label(&Frame1,&ver,0,10, 50,30,ALIGN_CENTER);
    // obj_add_Slider(&Canvas,&slider, 20,0,12,120, C_RED,C_BLUE,5,4,50,ALIGN_LEFT|ALIGN_VER);
    // obj_add_Slider(&Canvas,&slider2,0,-40,120,12,C_GREEN,C_BLUE,5,4,50,ALIGN_BOTTOM|ALIGN_HOR);
    // obj_add_Txtbox(&Canvas,&txtbox,240, 0,200,150,200,150*3, ALIGN_CENTER);    //容器200*450
    // obj_add_Chart(&Canvas, &chart,640, 0, 200, 160,gui->fc,gui->bc,10,10, ALIGN_CENTER);

    // obj_add_Button(&Canvas,&But1,650, 40, 50, 30,C_ROYAL_BLUE,C_BLUE,8,7,  0);
    // obj_add_Button(&Canvas,&But2,650, 100, 50, 30,C_ROYAL_BLUE,C_BLUE,8,7, 0);

    // obj_add_Switch(&Canvas,&SW1,650, 150, 45,30,gui->bkc,C_BLUE,0);
    // obj_add_Switch(&Canvas,&SW2,650, 200, 45,30,gui->bkc,C_BLUE,0);
    
    obj_set_Label_text(&label1, "SCGUI", C_BLACK, gui->font, ALIGN_CENTER);
    
    print_tree(&screen);
    

    #define SCREEN_FULL_GEO     0, 0, 240, 240
    #define CANVAS_FULL_PAGE(n) 240 * (n), 240

    // if (obj_create(NULL, (Obj_t*)&screen, ARER))                                                // 空屏幕
    //     obj_set_geometry((Obj_t*)&screen, SCREEN_FULL_GEO, ALIGN_NONE);

    // obj_add_Canvas(&screen, &Canvas, SCREEN_FULL_GEO, CANVAS_FULL_PAGE(1), ALIGN_CENTER);       // 容器240*240
    
    // obj_add_Frame(&Canvas, &Frame1, 0, 0, 0, 0, gui->fc, C_WHITE, 5, 4, ALIGN_CENTER);          // 圆角矩形:r=外角弧度(>0,1=90度);ir=内角弧度(>0,<=r时边框宽度=r+1-ir,>r时无边框)
    // obj_add_Frame_Sample(&Canvas, &Frame1);                             // 画布:创建
    // obj_set_Frame_r((Obj_t*)&Frame1, 5);                                // 画布:外圆角弧度(>0,1=90度)
    // obj_set_Frame_width((Obj_t*)&Frame1, 2);                            // 画布:边框宽度(最大不超过外圆角弧度值)
    // obj_set_geometry((Obj_t*)&Frame1, 0, 0, 200, 100, ALIGN_CENTER);    // 画布:坐标+长宽+对齐方式

    // obj_add_Label(&Frame1, &label1, 100, 110, 50, 30, ALIGN_CENTER);
    // obj_set_Label_text(&label1, "V2.2", C_BLUE, gui->font, ALIGN_CENTER);


    // char test_txt_buffer[512];
    // memset(test_txt_buffer, 0x00, sizeof(test_txt_buffer));
    // snprintf(test_txt_buffer, sizeof(test_txt_buffer), 
    //     "%02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X", 
    //     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99);
    // printf("test_txt_buffer[l=52 + 1]:%d\n", strlen(test_txt_buffer));

    // obj_add_Txtbox(&Canvas, &txtbox, 0, 0, 240, 240, 240, 240 * 3, ALIGN_CENTER); // 容器240*(240*3)
    // obj_set_Txtbox_text(&txtbox, (const char *)test_txt_buffer, C_RED, gui->font, ALIGN_CENTER);
#endif

    unsigned long last_time = 0;
    unsigned long pass_time = 0;
    unsigned long frame_time = 1000 / 24;
    unsigned long fps_time = 0;
    unsigned long fps_count = 0;
    while (1)
    {
        last_time = jiffies_msec();

        demo_loop();
        // obj_draw_screen(&screen);
        // os_time_dly(1);

        // fps 计算
        fps_count++;
        if (jiffies_msec() - fps_time >= 1000)
        {
            printf("fps: %lu\n", (jiffies_msec() - fps_time) / fps_count);
            fps_time = jiffies_msec();
            fps_count = 0;
        }

        pass_time = jiffies_msec() - last_time;
        if (pass_time <= (frame_time - 10))
        {
            os_time_dly((frame_time - pass_time) / 10);
            // printf("1: %lu\n", pass_time);
        }
        else
        {
            // printf("2: %lu\n", pass_time);
        }
    }
}

void display_task_init(void)
{
    os_task_create(display_task_handler, NULL, 3, 1024, 0, "lcd_spi");
}
#endif
