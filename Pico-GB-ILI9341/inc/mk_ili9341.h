/**
 * ILI9341 (240x320, SPI 4-wire) driver for Pico-GB.
 *
 * Drop-in replacement for mk_ili9225 (original by Mahyar Koshkouei
 * <mk@deltabeard.com>, ISC-style license). The hardware glue functions
 * (pins / SPI) are implemented in main.c, like in the original driver.
 *
 * The display is used in landscape mode: 320 (x) by 240 (y) pixels.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define ILI9341_WIDTH   320u
#define ILI9341_HEIGHT  240u

/**
 * MADCTL (0x36) value: orientation + colour order.
 *   0x28 = landscape, BGR panel   (MV | BGR)  <- default
 *   0xE8 = landscape rotated 180 deg (MY | MX | MV | BGR)
 *   0x68 = landscape mirrored      (MX | MV | BGR)
 *   0x88 = landscape mirrored      (MY | MV | BGR)
 * If the picture is mirrored/upside-down, or red and blue are swapped
 * (remove / add the 0x08 bit), change it here.
 */
#ifndef ILI9341_MADCTL
# define ILI9341_MADCTL 0x28
#endif

/* ---- Hardware glue, implemented in main.c ---- */

/** Reset pin. state = 0 -> low, else high. */
extern void mk_ili9341_set_rst(bool state);
/** D/C pin (called RS on the ILI9225 board). 0 = command, 1 = data. */
extern void mk_ili9341_set_rs(bool state);
/** Chip select pin. 0 = selected. */
extern void mk_ili9341_set_cs(bool state);
/** Backlight pin. */
extern void mk_ili9341_set_led(bool state);
/**
 * Sends 16-bit frames (MSB first) over SPI. The SPI peripheral is normally
 * kept in 16-bit mode; a 16-bit RGB565 pixel is therefore sent high byte
 * first, which is what the ILI9341 expects.
 */
extern void mk_ili9341_spi_write16(const uint16_t *halfwords, size_t len);
/**
 * Sends plain bytes over SPI (used for init only). The implementation must
 * switch the SPI to 8-bit mode for the transfer and back to 16-bit after.
 */
extern void mk_ili9341_spi_write8(const uint8_t *bytes, size_t len);
/** Delay in milliseconds. */
extern void mk_ili9341_delay_ms(unsigned ms);

/* ---- Driver API ---- */

/** Initialise the LCD. Always returns 0. */
unsigned mk_ili9341_init(void);

/**
 * Set the drawing window (inclusive coordinates). Pixels written with
 * mk_ili9341_write_pixels() fill it left-to-right, top-to-bottom.
 */
void mk_ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/** Write RGB565 pixels into the current window. */
void mk_ili9341_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb);

/** Write one horizontal line of w RGB565 pixels at (x, y). */
void mk_ili9341_write_line(uint16_t x, uint16_t y,
			   const uint16_t *pixels, uint16_t w);

/** Idle mode: limits the display to 8 colours when true. */
void mk_ili9341_set_idle(bool idle);

/** Inverts the display colours. */
void mk_ili9341_invert_display(bool invert);

void mk_ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
			  uint16_t color);
void mk_ili9341_fill(uint16_t color);
void mk_ili9341_pixel(uint16_t x, uint16_t y, uint16_t color);
void mk_ili9341_blit(const uint16_t *fbuf, uint16_t x, uint16_t y,
		     uint16_t w, uint16_t h);

/** Builds an 8x8 RGB565 bitmap for the given character. */
void mk_ili9341_get_letter(uint16_t *fbuf, char letter,
			   uint16_t color, uint16_t bgcolor);
/** Writes text (8x8 font), (x, y) is the upper-left corner. */
void mk_ili9341_text(char *s, uint16_t x, uint16_t y,
		     uint16_t color, uint16_t bgcolor);

/** Exit and stop using LCD. Currently does nothing. */
void mk_ili9341_exit(void);
