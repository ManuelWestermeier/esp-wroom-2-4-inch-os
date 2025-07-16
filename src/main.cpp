// ---------------------- LVGL KONFIGURATION ----------------------
#define LV_CONF_INCLUDE_SIMPLE
#define LV_COLOR_DEPTH 16
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_LOG 0
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_MEM_CUSTOM 0
#define LV_USE_PERF_MONITOR 1

// ---------------------- INCLUDES -------------------------------
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>

// ---------------------- PIN DEFINITIONEN -----------------------
#define TFT_MOSI 32
#define TFT_SCLK 25
#define TFT_CS 33
#define TFT_DC 39
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// ---------------------- OBJEKTE & BUFFER -----------------------
TFT_eSPI tft = TFT_eSPI(); // TFT instance
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[240 * 10]; // Zeilenpuffer
static lv_disp_drv_t disp_drv;

int counter = 0;
lv_obj_t *label;

// ---------------------- FUNKTIONEN -----------------------------
void update_label()
{
    lv_label_set_text_fmt(label, "%d", counter);
}

void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (strcmp(txt, "+") == 0)
        counter++;
    else if (strcmp(txt, "-") == 0)
        counter--;
    update_label();
}

void lvgl_setup()
{
    lv_init();

    tft.begin();
    tft.setRotation(1);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, sizeof(buf) / sizeof(lv_color_t));
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = [](lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
    {
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
        tft.pushColors((uint16_t *)&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
        tft.endWrite();
        lv_disp_flush_ready(disp);
    };

    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch Input
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = [](lv_indev_drv_t *drv, lv_indev_data_t *data)
    {
        static uint16_t x, y;
        bool touched = tft.getTouch(&x, &y);
        data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        data->point.x = x;
        data->point.y = y;
    };
    lv_indev_drv_register(&indev_drv);
}

void create_gui()
{
    label = lv_label_create(lv_scr_act());
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);
    update_label();

    lv_obj_t *btn_plus = lv_btn_create(lv_scr_act());
    lv_obj_align(btn_plus, LV_ALIGN_CENTER, 60, 40);
    lv_obj_t *label_plus = lv_label_create(btn_plus);
    lv_label_set_text(label_plus, "+");
    lv_obj_center(label_plus);
    lv_obj_add_event_cb(btn_plus, btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_minus = lv_btn_create(lv_scr_act());
    lv_obj_align(btn_minus, LV_ALIGN_CENTER, -60, 40);
    lv_obj_t *label_minus = lv_label_create(btn_minus);
    lv_label_set_text(label_minus, "-");
    lv_obj_center(label_minus);
    lv_obj_add_event_cb(btn_minus, btn_event_cb, LV_EVENT_CLICKED, NULL);
}

// ---------------------- SETUP & LOOP ---------------------------
void setup()
{
    Serial.begin(115200);
    lvgl_setup();
    create_gui();
}

void loop()
{
    lv_timer_handler(); // Update GUI
    delay(5);
}
