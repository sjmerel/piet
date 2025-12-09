#include <pebble.h>

static Window* g_window = NULL;
static BitmapLayer* g_bg_layer = NULL;
static TextLayer* g_time_layer = NULL;
static TextLayer* g_date_layer = NULL;
static TextLayer* g_battery_layer = NULL;
static BitmapLayer* g_bluetooth_layer = NULL;

static GBitmap* g_bg_bitmap = NULL;
static GBitmap* g_bluetooth_bitmap = NULL;

static AppTimer* g_show_timer = NULL;
static bool g_battery_on = false;

GRect g_date_on_rect;
GRect g_date_off_rect;
GRect g_battery_on_rect;
GRect g_battery_off_rect;
GRect g_bluetooth_on_rect;
GRect g_bluetooth_off_rect;

#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168


const char* time_str(struct tm* time)
{
    int h = time->tm_hour;
    if (!clock_is_24h_style())
    {
        if (h == 0)
        {
            h = 12;
        }
        else if (h > 12)
        {
            h -= 12;
        }
    }

    static char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d", h, time->tm_min);
    return buf;
}

const char* date_str(struct tm* time)
{
    static char buf[32];
    int len = strftime(buf, sizeof(buf), "%B ", time);
    snprintf(buf + len, sizeof(buf) - len, "%d", time->tm_mday);
    return buf;
}

const char* battery_str(BatteryChargeState charge)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%s%d%%", charge.is_charging ? "+" : "", charge.charge_percent);
    return buf;
}

static void animate(Layer* layer, GRect* end)
{
    Animation* anim = (Animation*) property_animation_create_layer_frame(layer, NULL, end);
    animation_set_duration(anim, 300);
    animation_schedule(anim);
}

static void bluetooth_connection_handler(bool connected)
{
    animate((Layer*) g_bluetooth_layer, connected ? &g_bluetooth_off_rect : &g_bluetooth_on_rect);
}

static void battery_state_handler(BatteryChargeState charge)
{
    text_layer_set_text(g_battery_layer, battery_str(charge));
    bool battery_on = (charge.charge_percent < 30 || charge.is_charging);
    if (battery_on != g_battery_on)
    {
        animate((Layer*) g_battery_layer, battery_on ? &g_battery_on_rect : &g_battery_off_rect);
    }
    g_battery_on = battery_on;
}

static void app_timer_handler(void* data)
{
    g_show_timer = NULL;
    battery_state_handler(battery_state_service_peek());
    animate((Layer*) g_date_layer, &g_date_off_rect);
    if (!g_battery_on)
    {
        animate((Layer*) g_battery_layer, &g_battery_off_rect);
    }
}

static void show_date()
{
    animate((Layer*) g_date_layer, &g_date_on_rect);
    if (!g_battery_on)
    {
        animate((Layer*) g_battery_layer, &g_battery_on_rect);
    }

    if (g_show_timer)
    {
        app_timer_cancel(g_show_timer);
    }
    g_show_timer = app_timer_register(3000, app_timer_handler, NULL);
}

static void accel_data_handler(AccelData* data, uint32_t n)
{
    for (int i = 0; i < (int) n; ++i)
    {
        static AccelData prev_accel = { 0 };
        AccelData accel = data[i];

        if (prev_accel.timestamp)
        {
            int dy = accel.y - prev_accel.y;
            if (dy > 2500 || dy < -2500)
            {
                show_date();
            }
        }

        prev_accel = accel;
    }
}

static void tick_timer_handler(struct tm* tick_time, TimeUnits units_changed) 
{
    // tweak position
    GRect time_rect = layer_get_frame((Layer*) g_time_layer);

    int h = tick_time->tm_hour;
    if (h > 12 && !clock_is_24h_style())
    {
        h -= 12;
    }

    if (h >= 10)
    {
        time_rect.origin.x = 1;
    }
    else
    {
        time_rect.origin.x = 10;
    }
    layer_set_frame((Layer*) g_time_layer, time_rect);

    text_layer_set_text(g_time_layer, time_str(tick_time));
    text_layer_set_text(g_date_layer, date_str(tick_time));
    accel_data_service_subscribe(1, accel_data_handler);
}

////////////////////////////////////////

static void init()
{
    // window
    g_window = window_create();
    window_stack_push(g_window, true);

    Layer* window_layer = window_get_root_layer(g_window);
    GRect bounds = layer_get_frame(window_layer);

    // background
    g_bg_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
    g_bg_layer = bitmap_layer_create(bounds);
    bitmap_layer_set_bitmap(g_bg_layer, g_bg_bitmap);
    layer_add_child(window_layer, (Layer*) g_bg_layer);

    // time layer
    GFont time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_FUTURA_64));
    GRect time_rect;
    time_rect.size.w = bounds.size.w;
    time_rect.size.h = 80;
    time_rect.origin.x = 10;
    time_rect.origin.y = 50;

    g_time_layer = text_layer_create(time_rect);
    text_layer_set_text_color(g_time_layer, GColorWhite);
    text_layer_set_background_color(g_time_layer, GColorClear);
    text_layer_set_font(g_time_layer, time_font);
    text_layer_set_text_alignment(g_time_layer, GTextAlignmentLeft);
    layer_add_child(window_layer, text_layer_get_layer(g_time_layer));

    // date layer
    GFont date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_FUTURA_22));
    g_date_on_rect = GRect(0, 10, 110, 30);
    g_date_off_rect = GRect(-110, 10, 0, 30);

    g_date_layer = text_layer_create(g_date_off_rect);
    text_layer_set_text_color(g_date_layer, GColorWhite);
    text_layer_set_background_color(g_date_layer, GColorRed);
    text_layer_set_font(g_date_layer, date_font);
    text_layer_set_text_alignment(g_date_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(g_date_layer));

    // battery charge layer
    GFont battery_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_FUTURA_20));
    g_battery_on_rect = GRect(100, 145, SCREEN_WIDTH-100, SCREEN_HEIGHT-145);
    g_battery_off_rect = GRect(SCREEN_WIDTH, 145, SCREEN_WIDTH-100, SCREEN_HEIGHT-145);

    g_battery_layer = text_layer_create(g_battery_off_rect);
    text_layer_set_text_color(g_battery_layer, GColorWhite);
    text_layer_set_background_color(g_battery_layer, GColorRed);
    text_layer_set_font(g_battery_layer, battery_font);
    text_layer_set_text_alignment(g_battery_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(g_battery_layer));

    // bluetooth disconnected layer
    g_bluetooth_on_rect = GRect(0, 145, 30, SCREEN_HEIGHT-145);
    g_bluetooth_off_rect = GRect(-30, 145, 30, SCREEN_HEIGHT-145);
    g_bluetooth_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH);
    g_bluetooth_layer = bitmap_layer_create(g_bluetooth_off_rect);
    bitmap_layer_set_bitmap(g_bluetooth_layer, g_bluetooth_bitmap);
    bitmap_layer_set_background_color(g_bluetooth_layer, GColorRed);
    bitmap_layer_set_alignment(g_bluetooth_layer, GAlignCenter);
    layer_add_child(window_layer, bitmap_layer_get_layer(g_bluetooth_layer));

    // initialize layer text
    time_t tt = time(NULL);
    tick_timer_handler(localtime(&tt), MINUTE_UNIT);
    bluetooth_connection_handler(bluetooth_connection_service_peek());
    battery_state_handler(battery_state_service_peek());

    // subscribe to services
    tick_timer_service_subscribe(MINUTE_UNIT, &tick_timer_handler);
    bluetooth_connection_service_subscribe(bluetooth_connection_handler);
    battery_state_service_subscribe(battery_state_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    accel_data_service_subscribe(1, accel_data_handler);
}

static void deinit()
{
    tick_timer_service_unsubscribe();
    bluetooth_connection_service_unsubscribe();
    battery_state_service_unsubscribe();
    accel_data_service_unsubscribe();

    text_layer_destroy(g_time_layer);
    text_layer_destroy(g_date_layer);
    text_layer_destroy(g_battery_layer);
    bitmap_layer_destroy(g_bluetooth_layer);
    bitmap_layer_destroy(g_bg_layer);

    gbitmap_destroy(g_bluetooth_bitmap);
    gbitmap_destroy(g_bg_bitmap);

    window_destroy(g_window);
}

int main()
{
    init();
    app_event_loop();
    deinit();
}
