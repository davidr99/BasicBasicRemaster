#ifndef BBASIC_RUNTIME_H
#define BBASIC_RUNTIME_H

#include <stddef.h>

#define BB_MAX_ARRAY_DIMENSIONS 4
#define BB_STRING_CAPACITY 2049
#define BB_NUL_SENTINEL ((char)0xff)

typedef struct BbNumArray {
    double *data;
    size_t dimensions[BB_MAX_ARRAY_DIMENSIONS];
    size_t dimension_count;
    size_t total;
} BbNumArray;

typedef struct BbStringArray {
    char *data;
    size_t dimensions[BB_MAX_ARRAY_DIMENSIONS];
    size_t dimension_count;
    size_t total;
} BbStringArray;

#ifdef __cplusplus
extern "C" {
#endif

double bb_ostype(void);
double bb_system(double selector);
int bb_gui_inkey(char *destination, size_t capacity);
int bb_gui_active(void);
void bb_gui_pump(void);
void bb_gui_input_line(char *destination, size_t capacity, const char *prompt);
double bb_gui_system(double selector);
void bb_gui_print(const char *text);
void bb_gui_newline(void);
void bb_gui_cls(void);
void bb_gui_color(double foreground, double background);
void bb_gui_locate(double row, double column);
void bb_scroll_area(double left, double top, double right, double bottom);
void bb_screen(double mode, double colors);
void bb_window_name(const char *name);
void bb_window_size_hint(double left, double top, double right, double bottom);
void bb_on_paint(void);
void bb_position(double x, double y, double width, double height,
                 double repaint);
void bb_graphics_line(double x1, double y1, double x2, double y2,
                      double color, double style);
void bb_graphics_circle(double x, double y, double radius, double color,
                        double start_angle, double end_angle);
void bb_graphics_paint(double x, double y, double color, double border);
void bb_graphics_pset(double x, double y, double color);
void bb_palette(double index, double color);
void bb_control(const char *text, double identifier, double status,
                const char *type, double extra, double x, double y,
                double width, double height, double foreground,
                double background);
void bb_delete_control(double identifier);
void bb_set_control_text(double identifier, const char *text);
const char *bb_get_control_text(double identifier);
void bb_radio(double identifier, double enabled);
double bb_set_focus(double identifier);
double bb_get_focus(void);
double bb_list(double identifier, double operation, double index,
               const char *text);
void bb_create_font(double identifier, double height, double width,
                    double escapement, double orientation, double weight,
                    double italic, double underline, double strikeout,
                    double charset, double output_precision,
                    double clipping_precision, double quality,
                    double pitch_family, const char *face);
void bb_select_font(double identifier);
double bb_text_length(const char *text);
double bb_font_info(double selector);
double bb_device_info(double selector);
double bb_bitmap_header(const char *filename, BbNumArray *information);
double bb_bitmap_colors(const char *filename, BbNumArray *colors);
void bb_create_bitmap(double identifier, double mode, double width,
                      double height);
void bb_select_bitmap(double identifier);
void bb_select_display(void);
void bb_load_bitmap(const char *filename, double source, double x, double y,
                    double source_x, double source_y, double width,
                    double height, double convert, double x_scale,
                    double y_scale);
void bb_copy_bits(double source, double source_x, double source_y,
                  double width, double height, double destination,
                  double destination_x, double destination_y,
                  double operation);
void bb_stretch_bits(double source, double source_x, double source_y,
                     double source_width, double source_height,
                     double destination, double destination_x,
                     double destination_y, double destination_width,
                     double destination_height, double operation);
void bb_store_bitmap(double source, const char *filename, double x, double y,
                     double width, double height, double compression,
                     double reserved);
void bb_graphics_get(double x1, double y1, double x2, double y2,
                     BbNumArray *storage);
void bb_graphics_put(double x, double y, BbNumArray *storage,
                     double operation);
void bb_select_print(void);
void bb_print_control(double operation, double *result, double from_page,
                      double to_page, double minimum_page,
                      double maximum_page, double copies);
void bb_main_menu(const char *one, const char *two, const char *three,
                  const char *four, const char *five, const char *six);
void bb_add_submenu(double menu, const char *text, double identifier);
void bb_menu_item_state(double identifier, double enabled);
void bb_message_box(const char *message, const char *title, double flags);
void bb_open_file_dialog(const char *filter, const char *filename,
                         const char *directory, const char *title,
                         double save_dialog);
const char *bb_dialog_value(double index);
const char *bb_directory(const char *pattern, double attributes);
void bb_custom_dialog(BbStringArray *controls, double count, double x,
                      double y, double width, double height,
                      const char *title);
void bb_sleep(double seconds);
double bb_mouse_on(void);
double bb_mouse_x(void);
double bb_mouse_y(void);
double bb_mouse_button(void);
double bb_sound_device(double selector);
void bb_play_sound(const char *filename, double asynchronous);
const char *bb_inkey(void);
double bb_len(const char *value);
double bb_asc(const char *value);
const char *bb_chr(double value);
const char *bb_left(const char *value, double count);
const char *bb_right(const char *value, double count);
const char *bb_mid(const char *value, double start, double count);
const char *bb_str(double value);
const char *bb_space(double count);
const char *bb_ucase(const char *value);
const char *bb_format_using(const char *format, double value);
double bb_instr(double start, const char *haystack, const char *needle);
double bb_int(double value);
double bb_abs(double value);
double bb_val(const char *value);
const char *bb_concat(const char *left, const char *right);
double bb_freemem(void);
double bb_rnd(void);
double bb_timer(void);
const char *bb_time_string(void);
const char *bb_date_string(void);

void bb_set_string(char *destination, size_t capacity, const char *value);
void bb_print_string(const char *value);
void bb_print_number(double value);
void bb_print_newline(void);
void bb_cls(void);
void bb_color(double foreground, double background);
void bb_locate(double row, double column);
void bb_beep(void);
void bb_randomize(double seed);
void bb_input_begin(const char *prompt);
const char *bb_input_next(void);
int bb_file_open(const char *path, double file_number, double mode,
                 double record_length);
void bb_file_close(double file_number);
void bb_file_print_string(double file_number, const char *value);
void bb_file_print_number(double file_number, double value);
void bb_file_print_separator(double file_number, double separator);
void bb_file_print_newline(double file_number);
const char *bb_file_input_next(double file_number);
const char *bb_file_input_string(double count, double file_number);
double bb_file_eof(double file_number);
double bb_file_loc(double file_number);
void bb_file_field_clear(double file_number);
void bb_file_field_bind(double file_number, double width, char *destination,
                        size_t capacity);
void bb_file_get(double file_number, double record_number);
void bb_file_put(double file_number, double record_number);
void bb_lset(char *destination, size_t capacity, const char *value);
void bb_set_com(double file_number, const char *settings);
int bb_runtime_error(const char *message);
void bb_set_error_level(double level);

void bb_num_array_dim(BbNumArray *array, size_t dimension_count, ...);
double bb_num_array_get(const BbNumArray *array, size_t dimension_count, ...);
void bb_num_array_set(BbNumArray *array, double value,
                      size_t dimension_count, ...);
void bb_num_array_fill(BbNumArray *array, double value);
void bb_string_array_dim(BbStringArray *array, size_t dimension_count, ...);
const char *bb_string_array_get(const BbStringArray *array,
                                size_t dimension_count, ...);
void bb_string_array_set(BbStringArray *array, const char *value,
                         size_t dimension_count, ...);
void bb_string_array_fill(BbStringArray *array, const char *value);

#ifdef __cplusplus
}
#endif

#endif
