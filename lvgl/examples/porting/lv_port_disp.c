/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>

#include "dma2d.h"
#include "ltdc.h"
#include "cmsis_os.h"
/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES    800
#define MY_DISP_VER_RES    480
#define BytesPerPixel_0		2
#define LCD_MemoryAdd       0xC0000000
#define LVGL_MemoryAdd	( LCD_MemoryAdd + MY_DISP_HOR_RES*MY_DISP_VER_RES*BytesPerPixel_0 )
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    460
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    460
#endif

/**********************
 *      TYPEDEFS
 **********************/
static lv_disp_drv_t *s_flush_drv;

static void disp_wait(lv_disp_drv_t *drv)
{
    LV_UNUSED(drv);
    osThreadYield();
}

static void dma2d_flush_done(DMA2D_HandleTypeDef *hdma2d)
{
    LV_UNUSED(hdma2d);
    if(s_flush_drv) {
        lv_disp_drv_t *drv = s_flush_drv;
        s_flush_drv = NULL;
        lv_disp_flush_ready(drv);
    }
}

/* 旧寄存器中断路径（保留）
int LCD_DMA2D_IRQHandler(void)
{
    if(s_flush_drv == NULL) {
        return 0;
    }

    DMA2D->IFCR = 0x3FU;
    DMA2D->CR &= ~(DMA2D_CR_TCIE | DMA2D_CR_TEIE);

    lv_disp_drv_t *drv = s_flush_drv;
    s_flush_drv = NULL;
    lv_disp_flush_ready(drv);
    return 1;
}
*/

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,uint32_t *color)
{
    uint32_t dest_addr = LCD_MemoryAdd + BytesPerPixel_0 * (MY_DISP_HOR_RES * y + x);

    SCB_CleanDCache_by_Addr(color,width*height*BytesPerPixel_0);

    while(hdma2d.State != HAL_DMA2D_STATE_READY) {
        osThreadYield();
    }

    /* HAL_DMA2D_Start_IT 只写 FGMAR/OMAR/NLR/START，不写行距。
       必须像最初那样直接配 FGOR/OOR，否则非全宽脏矩形会按错误 stride 写入显存而花屏。
       不要每次 HAL_DMA2D_Init：CubeMX 未填 LineOffsetMode，Init 可能把 LOM 搞成字节模式。 */
    DMA2D->FGPFCCR = DMA2D_INPUT_RGB565;
    DMA2D->OPFCCR  = DMA2D_OUTPUT_RGB565;
    DMA2D->FGOR    = 0;
    DMA2D->OOR     = (uint32_t)(MY_DISP_HOR_RES - width);

    hdma2d.Init.Mode = DMA2D_M2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = (uint32_t)(MY_DISP_HOR_RES - width);
    hdma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    hdma2d.XferCpltCallback = dma2d_flush_done;
    hdma2d.XferErrorCallback = dma2d_flush_done;

    if(HAL_DMA2D_Start_IT(&hdma2d, (uint32_t)color, dest_addr, width, height) != HAL_OK) {
        dma2d_flush_done(&hdma2d);
    }

    /* 纯 HAL 每次 Init+ConfigLayer（保留）
    hdma2d.LayerCfg[1].InputOffset = 0;
    hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
    if((HAL_DMA2D_Init(&hdma2d) != HAL_OK) ||
       (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)) {
        dma2d_flush_done(&hdma2d);
        return;
    }
    */

    /* 旧寄存器启动（保留）
    DMA2D->CR      = DMA2D_M2M | DMA2D_CR_TCIE | DMA2D_CR_TEIE;
    DMA2D->FGPFCCR = DMA2D_INPUT_RGB565;
    DMA2D->OPFCCR  = DMA2D_OUTPUT_RGB565;
    DMA2D->FGOR    = 0;
    DMA2D->OOR     = MY_DISP_HOR_RES - width;
    DMA2D->FGMAR   = (uint32_t)color;
    DMA2D->OMAR    = dest_addr;
    DMA2D->NLR     = ((uint32_t)width << 16) | height;
    DMA2D->IFCR    = 0x3FU;
    DMA2D->CR     |= DMA2D_CR_START;
    */

    /* 旧 DMA2D 轮询 + HAL 混用（保留）
    DMA2D->CR	  &=	~(DMA2D_CR_START);
    DMA2D->CR		=	DMA2D_M2M;
    DMA2D->FGPFCCR	=	DMA2D_INPUT_RGB565;
    DMA2D->FGOR    =  0;
    DMA2D->OOR		=	MY_DISP_HOR_RES - width;
    DMA2D->FGMAR   =  (uint32_t)color;
    DMA2D->OMAR		=	LCD_MemoryAdd + BytesPerPixel_0*(MY_DISP_HOR_RES * y + x);
    DMA2D->NLR		=	(width<<16)|(height);
    DMA2D->CR	  |=	DMA2D_CR_START;
    HAL_DMA2D_Start(&hdma2d,(uint32_t)color,LCD_MemoryAdd + BytesPerPixel_0*(MY_DISP_HOR_RES * y + x), width,height);
    HAL_DMA2D_PollForTransfer(&hdma2d, 1000);
    */
}


/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_disp_drv_t disp_drv;


/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/

    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */

    /* Example for 1) */
    // static lv_disp_draw_buf_t draw_buf_dsc_1;
    // __attribute__((section(".ram"))) static lv_color_t buf_1[MY_DISP_HOR_RES * 200];                          /*A buffer for 10 rows*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, MY_DISP_HOR_RES * 200);   /*Initialize the display buffer*/

    /* Example for 2) DMA2D 异步 flush + 双缓冲：CPU/GPU 画下一块时 DMA2D 搬运上一块 */
    static lv_disp_draw_buf_t draw_buf_dsc_2;
    __attribute__((section(".ram"))) static lv_color_t buf_2_1[MY_DISP_HOR_RES * 100];                        /*A buffer for 10 rows*/
    __attribute__((section(".ram"))) static lv_color_t buf_2_2[MY_DISP_HOR_RES * 100];                        /*An other buffer for 10 rows*/
     lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_2_1, buf_2_2, MY_DISP_HOR_RES * 100);   /*Initialize the display buffer*/

    /* Example for 3) also set disp_drv.full_refresh = 1 below*/
    // static lv_disp_draw_buf_t draw_buf_dsc_3;
    // static lv_color_t *buf_3_1 = (lv_color_t * )(0xC0000000);            /*A screen sized buffer*/
    // static lv_color_t *buf_3_2 = (lv_color_t * )(0xC0000000+ MY_DISP_HOR_RES*MY_DISP_VER_RES*sizeof(lv_color_t));;            /*Another screen sized buffer*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2,MY_DISP_HOR_RES*MY_DISP_VER_RES);   /*Initialize the display buffer*/

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

                            /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set up the functions to access to your display*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to your display*/
    disp_drv.flush_cb = disp_flush;
    disp_drv.wait_cb = disp_wait;

    /*Set a display buffer*/
    // disp_drv.draw_buf = &draw_buf_dsc_1;
    disp_drv.draw_buf = &draw_buf_dsc_2;

    /*Required for Example 3)*/
    // disp_drv.full_refresh = 0;

    /* Fill a memory array with a color if you have GPU.
     * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
     * But if you have a different GPU you can use with this callback.*/
    //disp_drv.gpu_fill_cb = gpu_fill;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {

    if(!disp_flush_enabled) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    s_flush_drv = disp_drv;
    LCD_CopyBuffer(area->x1, area->y1, area->x2 - area->x1+1, area->y2 - area->y1 +1,(uint32_t*)color_p);
    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    // lv_disp_flush_ready(disp_drv); /* 改到 HAL_DMA2D XferCpltCallback: dma2d_flush_done() */
}


/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
