#include <pebble.h>

#define PERSIST_KEY_TEMPERATURE 1
#define PERSIST_KEY_CONDITIONS 2

#define DATE_BAND_HEIGHT 38
#define BOTTOM_BAND_HEIGHT 80

static Window *s_window;
static TextLayer *s_date_layer;
static TextLayer *s_time_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_steps_label_layer;
static TextLayer *s_temp_layer;
static TextLayer *s_conditions_layer;
static GFont s_time_font;

static char s_date_buffer[32];
static char s_time_buffer[8];
static char s_steps_buffer[16];
static char s_temp_buffer[8];
static char s_conditions_buffer[16];

static int s_minutes_since_weather = 0;

static void prv_update_time(struct tm *tick_time) {
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%l:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  static char s_day_buffer[16];
  static char s_mday_buffer[16];
  strftime(s_day_buffer, sizeof(s_day_buffer), "%A", tick_time);
  strftime(s_mday_buffer, sizeof(s_mday_buffer), "%b", tick_time);
  snprintf(s_mday_buffer + strlen(s_mday_buffer),
           sizeof(s_mday_buffer) - strlen(s_mday_buffer), " %d", tick_time->tm_mday);
  // Uppercase the weekday for the band
  for (char *c = s_day_buffer; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c -= 32;
  }
  for (char *c = s_mday_buffer; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c -= 32;
  }
  snprintf(s_date_buffer, sizeof(s_date_buffer), "%s · %s", s_day_buffer, s_mday_buffer);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void prv_update_steps(void) {
  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  int s = (int)steps;
  if (s >= 1000) {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d,%03d", s / 1000, s % 1000);
  } else {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d", s);
  }
  text_layer_set_text(s_steps_layer, s_steps_buffer);
}

static void prv_request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
  prv_update_steps();
  if (++s_minutes_since_weather >= 30) {
    s_minutes_since_weather = 0;
    prv_request_weather();
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate) {
    prv_update_steps();
  }
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  Tuple *cond_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);

  if (temp_tuple) {
    snprintf(s_temp_buffer, sizeof(s_temp_buffer), "%d°", (int)temp_tuple->value->int32);
    text_layer_set_text(s_temp_layer, s_temp_buffer);
    persist_write_int(PERSIST_KEY_TEMPERATURE, (int)temp_tuple->value->int32);
  }
  if (cond_tuple) {
    snprintf(s_conditions_buffer, sizeof(s_conditions_buffer), "%s", cond_tuple->value->cstring);
    text_layer_set_text(s_conditions_layer, s_conditions_buffer);
    persist_write_string(PERSIST_KEY_CONDITIONS, s_conditions_buffer);
  }
}

static void prv_restore_weather(void) {
  if (persist_exists(PERSIST_KEY_TEMPERATURE)) {
    snprintf(s_temp_buffer, sizeof(s_temp_buffer), "%d°",
             (int)persist_read_int(PERSIST_KEY_TEMPERATURE));
    text_layer_set_text(s_temp_layer, s_temp_buffer);
  }
  if (persist_exists(PERSIST_KEY_CONDITIONS)) {
    persist_read_string(PERSIST_KEY_CONDITIONS, s_conditions_buffer,
                        sizeof(s_conditions_buffer));
    text_layer_set_text(s_conditions_layer, s_conditions_buffer);
  }
}

static TextLayer *prv_make_text_layer(Layer *parent, GRect frame, GColor bg, GColor fg,
                                      const char *font_key) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, bg);
  text_layer_set_text_color(layer, fg);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, GTextAlignmentCenter);
  layer_add_child(parent, text_layer_get_layer(layer));
  return layer;
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int w = bounds.size.w;
  const int h = bounds.size.h;
  const int bottom_top = h - BOTTOM_BAND_HEIGHT;
  const int half_w = w / 2;

  window_set_background_color(window, GColorBlack);

  // Top band: weekday · date on orange (fill layer + vertically nudged text)
  prv_make_text_layer(window_layer,
      GRect(0, 0, w, DATE_BAND_HEIGHT),
      GColorYellow, GColorWhite, FONT_KEY_GOTHIC_14);
  s_date_layer = prv_make_text_layer(window_layer,
      GRect(0, 7, w, DATE_BAND_HEIGHT - 7),
      GColorYellow, GColorBlack, FONT_KEY_GOTHIC_18_BOLD);

  // Center: big time on black (custom Roboto Bold, 56 px)
  s_time_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_56));
  const int time_zone_h = bottom_top - DATE_BAND_HEIGHT;
  const int time_font_h = 56;
  const int time_y = DATE_BAND_HEIGHT + (time_zone_h - time_font_h) / 2 - 12;
  s_time_layer = prv_make_text_layer(window_layer,
      GRect(0, time_y, w, time_font_h + 16),
      GColorClear, GColorWhite, FONT_KEY_GOTHIC_14);
  text_layer_set_font(s_time_layer, s_time_font);

  // Bottom block background fills (text layers are laid over these)
  prv_make_text_layer(window_layer,
      GRect(0, bottom_top, half_w, BOTTOM_BAND_HEIGHT),
      GColorIslamicGreen, GColorWhite, FONT_KEY_GOTHIC_14);
  prv_make_text_layer(window_layer,
      GRect(half_w, bottom_top, w - half_w, BOTTOM_BAND_HEIGHT),
      GColorBlueMoon, GColorWhite, FONT_KEY_GOTHIC_14);

  // Bottom-left block: steps on green
  s_steps_layer = prv_make_text_layer(window_layer,
      GRect(0, bottom_top + 12, half_w, 32),
      GColorIslamicGreen, GColorWhite, FONT_KEY_GOTHIC_28_BOLD);
  s_steps_label_layer = prv_make_text_layer(window_layer,
      GRect(0, bottom_top + 46, half_w, BOTTOM_BAND_HEIGHT - 46),
      GColorIslamicGreen, GColorWhite, FONT_KEY_GOTHIC_14);
  text_layer_set_text(s_steps_label_layer, "STEPS");

  // Bottom-right block: weather on blue
  s_temp_layer = prv_make_text_layer(window_layer,
      GRect(half_w, bottom_top + 12, w - half_w, 32),
      GColorBlueMoon, GColorWhite, FONT_KEY_GOTHIC_28_BOLD);
  s_conditions_layer = prv_make_text_layer(window_layer,
      GRect(half_w, bottom_top + 46, w - half_w, BOTTOM_BAND_HEIGHT - 46),
      GColorBlueMoon, GColorWhite, FONT_KEY_GOTHIC_14);

  text_layer_set_text(s_temp_layer, "--°");
  text_layer_set_text(s_conditions_layer, "LOADING");
  prv_restore_weather();

  time_t now = time(NULL);
  prv_update_time(localtime(&now));
  prv_update_steps();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_steps_label_layer);
  text_layer_destroy(s_temp_layer);
  text_layer_destroy(s_conditions_layer);
  fonts_unload_custom_font(s_time_font);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  health_service_events_subscribe(prv_health_handler, NULL);

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(128, 32);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
