#include "display.h"

#if PICO_RP2350
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

// Pimoroni Pico Display Pack (1.14in ST7789, 240x135).
#define LCD_SPI spi0
#define LCD_SCK 18u
#define LCD_MOSI 19u
#define LCD_CS 17u
#define LCD_DC 16u
#define LCD_BL 20u
#define LCD_X_OFFSET 40u
#define LCD_Y_OFFSET 53u
static unsigned display_generation;
static char old_breadcrumb[10];

static void command(uint8_t value) {
    gpio_put(LCD_DC, 0); gpio_put(LCD_CS, 0);
    spi_write_blocking(LCD_SPI, &value, 1); gpio_put(LCD_CS, 1);
}
static void data(const uint8_t *bytes, size_t length) {
    gpio_put(LCD_DC, 1); gpio_put(LCD_CS, 0);
    spi_write_blocking(LCD_SPI, bytes, length); gpio_put(LCD_CS, 1);
}
static void window(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint8_t col[] = {(uint8_t)((x + LCD_X_OFFSET) >> 8), (uint8_t)(x + LCD_X_OFFSET),
                     (uint8_t)((x + width - 1u + LCD_X_OFFSET) >> 8),
                     (uint8_t)(x + width - 1u + LCD_X_OFFSET)};
    uint8_t row[] = {(uint8_t)((y + LCD_Y_OFFSET) >> 8), (uint8_t)(y + LCD_Y_OFFSET),
                     (uint8_t)((y + height - 1u + LCD_Y_OFFSET) >> 8),
                     (uint8_t)(y + height - 1u + LCD_Y_OFFSET)};
    command(0x2a); data(col, sizeof(col)); command(0x2b); data(row, sizeof(row)); command(0x2c);
}
static uint8_t glyph(char c, unsigned column) {
    static const char *letters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:";
    static const uint8_t table[][5] = {
        {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
        {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
        {54,73,73,73,54},{6,73,73,41,30},
        {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
        {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
        {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
        {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
        {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
        {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
        {7,8,112,8,7},{97,81,73,69,67},{8,8,8,8,8},{0,0,0,0,0},
    };
    const char *match = strchr(letters, c);
    if (match == NULL || column >= 5u) return 0;
    return table[(unsigned)(match - letters)][column];
}
static void text(const char *message, uint16_t x, uint16_t y, uint16_t colour) {
    uint16_t cursor = x;
    const uint16_t scale = 2u;
    while (*message != '\0' && cursor + 12u <= 240u) {
        uint8_t pixels[12u * 16u * 2u] = {0};
        for (unsigned row = 0; row < 7u; ++row) for (unsigned col = 0; col < 5u; ++col)
            for (unsigned sy = 0; sy < scale; ++sy) for (unsigned sx = 0; sx < scale; ++sx) {
                unsigned px = col * scale + sx;
                unsigned py = row * scale + sy;
                uint16_t p = (glyph(*message, col) >> row) & 1u ? colour : 0u;
                pixels[(py * 12u + px) * 2u] = (uint8_t)(p >> 8);
                pixels[(py * 12u + px) * 2u + 1u] = (uint8_t)p;
            }
        // The sixth column and unused bottom row are black separators.
        window(cursor, y, 12, 16); gpio_put(LCD_DC, 1); gpio_put(LCD_CS, 0);
        spi_write_blocking(LCD_SPI, pixels, sizeof(pixels));
        gpio_put(LCD_CS, 1); cursor += 12u; ++message;
    }
}
static void block(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  uint16_t colour) {
    uint8_t pixels[32u * 2u];
    unsigned count = width * height;
    if (count > 32u) count = 32u;
    for (unsigned i = 0; i < count; ++i) {
        pixels[i * 2u] = (uint8_t)(colour >> 8);
        pixels[i * 2u + 1u] = (uint8_t)colour;
    }
    window(x, y, width, height); gpio_put(LCD_DC, 1); gpio_put(LCD_CS, 0);
    spi_write_blocking(LCD_SPI, pixels, count * 2u); gpio_put(LCD_CS, 1);
}
void display_init(void) {
    spi_init(LCD_SPI, 40000000u);
    gpio_set_function(LCD_SCK, GPIO_FUNC_SPI); gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);
    gpio_init(LCD_CS); gpio_set_dir(LCD_CS, GPIO_OUT); gpio_put(LCD_CS, 1);
    gpio_init(LCD_DC); gpio_set_dir(LCD_DC, GPIO_OUT); gpio_put(LCD_DC, 1);
    gpio_init(LCD_BL); gpio_set_dir(LCD_BL, GPIO_OUT); gpio_put(LCD_BL, 1);
    sleep_ms(20); command(0x01); sleep_ms(120); command(0x11); sleep_ms(120);
    uint8_t madctl = 0x60; command(0x36); data(&madctl, 1);
    uint8_t colmod = 0x55; command(0x3a); data(&colmod, 1); command(0x21); command(0x29);
    // Clear once at boot; subsequent status changes update only their text
    // cells so the audio scheduler is not blocked by a full-frame transfer.
    window(0, 0, 240, 135); uint8_t black[2] = {0, 0};
    gpio_put(LCD_DC, 1); gpio_put(LCD_CS, 0);
    for (unsigned i = 0; i < 240u * 135u; ++i) spi_write_blocking(LCD_SPI, black, 2);
    gpio_put(LCD_CS, 1);
}
void display_show_parameter(const char *name, int value, int minimum, int maximum,
                            unsigned program, unsigned bank, bool simulated_sensor) {
    char line[40];
    static bool shown;
    static unsigned old_program, old_bank;
    static int old_value, old_minimum, old_maximum;
    static bool old_sensor;
    static char old_name[20];
    static unsigned old_generation;
    char shown_name[20];
    snprintf(shown_name, sizeof(shown_name), "%.19s", name);
    if (!shown || old_generation != display_generation || old_program != program || old_bank != bank) {
        snprintf(line, sizeof(line), "BANK %u  INST %u", bank + 1u, program + 1u);
        text(line, 4, 6, 0xffff);
    }
    if (!shown || old_generation != display_generation || strcmp(old_name, shown_name) != 0) { text("                    ", 4, 32, 0); text(shown_name, 4, 32, 0xffe0); }
    if (!shown || old_generation != display_generation || old_value != value) {
        snprintf(line, sizeof(line), "%d", value); text("      ", 4, 52, 0); text(line, 4, 52, 0xffff);
    }
    if (!shown || old_generation != display_generation || old_minimum != minimum || old_maximum != maximum) {
        snprintf(line, sizeof(line), "%d..%d", minimum, maximum); text("          ", 4, 72, 0); text(line, 4, 72, 0x7bef);
    }
    if (!shown || old_sensor != simulated_sensor)
        text(simulated_sensor ? "SENSOR SIM" : "SENSOR LIVE", 4, 108, 0x07ff);
    shown = true; old_program = program; old_bank = bank; old_value = value;
    old_minimum = minimum; old_maximum = maximum; old_sensor = simulated_sensor;
    old_generation = display_generation;
    strncpy(old_name, shown_name, sizeof(old_name) - 1u); old_name[sizeof(old_name) - 1u] = '\0';
}
void display_show_menu_node(const char *name, unsigned index, unsigned count,
                            unsigned program, unsigned bank, bool simulated_sensor) {
    char line[40];
    char shown_name[20];
    snprintf(shown_name, sizeof(shown_name), "%.19s", name);
    snprintf(line, sizeof(line), "BANK %u  INST %u", bank + 1u, program + 1u);
    text(line, 4, 6, 0xffff);
    text("                    ", 4, 32, 0); text(shown_name, 4, 32, 0xffe0);
    text("                    ", 4, 52, 0);
    snprintf(line, sizeof(line), "%u OF %u", index, count); text(line, 4, 52, 0x07ff);
    text("                    ", 4, 72, 0); text("HOLD A: ENTER", 4, 72, 0x7bef);
    text(simulated_sensor ? "SENSOR SIM" : "SENSOR LIVE", 4, 108, 0x07ff);
    ++display_generation;
}
void display_set_breadcrumb(const char *path) {
    char shown[10];
    snprintf(shown, sizeof(shown), "%.9s", path);
    if (strcmp(shown, old_breadcrumb) == 0) return;
    text("          ", 120, 108, 0);
    size_t length = strlen(shown);
    uint16_t x = (uint16_t)(236u - (length > 9u ? 9u : length) * 12u);
    text(shown, x, 108, 0xf81f);
    strncpy(old_breadcrumb, shown, sizeof(old_breadcrumb) - 1u);
    old_breadcrumb[sizeof(old_breadcrumb) - 1u] = '\0';
}
void display_clear_band(unsigned band) {
    if (band >= 14u) return;
    uint16_t y = (uint16_t)(band * 10u);
    uint16_t height = y + 10u <= 135u ? 10u : (uint16_t)(135u - y);
    window(0, y, 240, height);
    uint8_t black[64] = {0};
    gpio_put(LCD_DC, 1); gpio_put(LCD_CS, 0);
    unsigned bytes = 240u * height * 2u;
    while (bytes != 0u) {
        unsigned chunk = bytes > sizeof(black) ? sizeof(black) : bytes;
        spi_write_blocking(LCD_SPI, black, chunk); bytes -= chunk;
    }
    gpio_put(LCD_CS, 1);
    if (band == 13u) { old_breadcrumb[0] = '\0'; ++display_generation; }
}
void display_screensaver_step(uint8_t phase, uint8_t motion,
                              uint8_t density, uint8_t sensor) {
#define LISSAJOUS_POINT_COUNT 256u
#define LISSAJOUS_POINTS_PER_STEP 32u
    static const int8_t sine[64] = {
        0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126,
        127,126,125,122,117,112,106,98,90,81,71,60,49,37,25,12,
        0,-12,-25,-37,-49,-60,-71,-81,-90,-98,-106,-112,-117,-122,-125,-126,
        -127,-126,-125,-122,-117,-112,-106,-98,-90,-81,-71,-60,-49,-37,-25,-12
    };
    static uint8_t old_x[LISSAJOUS_POINT_COUNT], old_y[LISSAJOUS_POINT_COUNT];
    static uint8_t drawn[LISSAJOUS_POINT_COUNT];
    static uint16_t cursor;
    static uint8_t curve_phase, curve_motion, curve_density, curve_sensor;
    if (cursor == 0u) {
        curve_phase = phase; curve_motion = motion;
        curve_density = density; curve_sensor = sensor;
    }
    // Higher, deliberately separated ratios create multi-lobed Lissajous
    // figures instead of spending most of the time near an ellipse.
    unsigned a = 3u + (curve_motion % 6u);
    unsigned b = 7u + ((curve_density + curve_sensor / 24u) % 7u);
    if (b == a || b == a * 2u) ++b;
    const uint16_t colour = 0x07ffu;
    for (unsigned step = 0; step < LISSAJOUS_POINTS_PER_STEP; ++step) {
        unsigned i = (cursor + step) & 255u;
        if (drawn[i] != 0u) block(old_x[i], old_y[i], 2, 2, 0);
        unsigned qx = (((unsigned)curve_phase << 2u) + i * a) & 255u;
        unsigned qy = (((unsigned)curve_phase << 2u) + i * b + 52u + ((unsigned)curve_motion << 2u)) & 255u;
        unsigned bx = qx >> 2u, by = qy >> 2u;
        unsigned fx = qx & 3u, fy = qy & 3u;
        int sx = (sine[bx] * (int)(4u - fx) + sine[(bx + 1u) & 63u] * (int)fx) / 4;
        int sy = (sine[by] * (int)(4u - fy) + sine[(by + 1u) & 63u] * (int)fy) / 4;
        // The ratios multiply the curve parameter, not merely time. Applying
        // them only to phase translates an ellipse; applying them to p creates
        // the intended multi-lobed Lissajous geometry.
        int x = 118 + sx * (88 + curve_sensor / 8) / 127;
        int y = 66 + sy * 54 / 127;
        if (x < 1) x = 1;
        if (x > 236) x = 236;
        if (y < 1) y = 1;
        if (y > 131) y = 131;
        old_x[i] = (uint8_t)x; old_y[i] = (uint8_t)y;
        drawn[i] = 1u;
        block(old_x[i], old_y[i], 2, 2, colour);
    }
    cursor = (uint16_t)((cursor + LISSAJOUS_POINTS_PER_STEP) & 255u);
#undef LISSAJOUS_POINTS_PER_STEP
#undef LISSAJOUS_POINT_COUNT
}
#else
void display_init(void) {}
void display_show_parameter(const char *name, int value, int minimum, int maximum,
                            unsigned program, unsigned bank, bool simulated_sensor) {
    (void)name; (void)value; (void)minimum; (void)maximum; (void)program; (void)bank; (void)simulated_sensor;
}
void display_show_menu_node(const char *name, unsigned index, unsigned count,
                            unsigned program, unsigned bank, bool simulated_sensor) {
    (void)name; (void)index; (void)count; (void)program; (void)bank; (void)simulated_sensor;
}
void display_set_breadcrumb(const char *path) { (void)path; }
void display_clear_band(unsigned band) { (void)band; }
void display_screensaver_step(uint8_t phase, uint8_t motion,
                              uint8_t density, uint8_t sensor) {
    (void)phase; (void)motion; (void)density; (void)sensor;
}
#endif
