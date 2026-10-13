/**
 * ILI9341 (240x320, SPI) driver for Pico-GB.
 *
 * Based on the structure of mk_ili9225.c, Copyright (C) 2019-2022 by
 * Mahyar Koshkouei <mk@deltabeard.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "mk_ili9341.h"

/* ILI9341 commands. */
#define CMD_NOP		0x00
#define CMD_SWRESET	0x01
#define CMD_SLPOUT	0x11
#define CMD_INVOFF	0x20
#define CMD_INVON	0x21
#define CMD_DISPOFF	0x28
#define CMD_DISPON	0x29
#define CMD_CASET	0x2A
#define CMD_PASET	0x2B
#define CMD_RAMWR	0x2C
#define CMD_IDMOFF	0x38
#define CMD_IDMON	0x39

/*
 * Runtime transfers (everything except init) use only 16-bit SPI frames, so
 * the SPI peripheral never has to change format while the game is running.
 *
 *  - A command is sent as the frame 0x00CC: the first byte (0x00) is the NOP
 *    command, the second is the real command.
 *  - Parameters of CASET/PASET are two 16-bit values (start, end), which is
 *    exactly how they are laid out on the wire (high byte first).
 *  - Pixels are RGB565, high byte first.
 */

/* Column window currently programmed in the LCD (cache, saves SPI time). */
static uint16_t cur_x0 = 0xFFFF, cur_x1 = 0xFFFF;

static void cmd16(uint8_t cmd, const uint16_t *params, size_t nparams)
{
	uint16_t frame = cmd;

	mk_ili9341_set_rs(0);
	mk_ili9341_set_cs(0);
	mk_ili9341_spi_write16(&frame, 1);
	if(nparams)
	{
		mk_ili9341_set_rs(1);
		mk_ili9341_spi_write16(params, nparams);
	}
	mk_ili9341_set_cs(1);
}

/* Init-time command: 8-bit SPI frames. */
static void cmd8(uint8_t cmd, const uint8_t *params, size_t nparams)
{
	mk_ili9341_set_rs(0);
	mk_ili9341_set_cs(0);
	mk_ili9341_spi_write8(&cmd, 1);
	if(nparams)
	{
		mk_ili9341_set_rs(1);
		mk_ili9341_spi_write8(params, nparams);
	}
	mk_ili9341_set_cs(1);
}

#define CMD8(c, ...) \
	do { \
		const uint8_t p_[] = { __VA_ARGS__ }; \
		cmd8((c), p_, sizeof(p_)); \
	} while(0)

unsigned mk_ili9341_init(void)
{
	/* Pins to a safe state. RST must be high before the reset pulse. */
	mk_ili9341_set_rst(1);
	mk_ili9341_set_cs(1);
	mk_ili9341_set_rs(0);
	mk_ili9341_delay_ms(5);

	/* Hardware reset. */
	mk_ili9341_set_rst(0);
	mk_ili9341_delay_ms(20);
	mk_ili9341_set_rst(1);
	mk_ili9341_delay_ms(120);

	/* Backlight off while initialising. */
	mk_ili9341_set_led(0);

	cmd8(CMD_SWRESET, NULL, 0);
	mk_ili9341_delay_ms(150);
	cmd8(CMD_DISPOFF, NULL, 0);

	CMD8(0xEF, 0x03, 0x80, 0x02);
	CMD8(0xCF, 0x00, 0xC1, 0x30);			/* Power control B */
	CMD8(0xED, 0x64, 0x03, 0x12, 0x81);		/* Power on sequence */
	CMD8(0xE8, 0x85, 0x00, 0x78);			/* Driver timing A */
	CMD8(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);	/* Power control A */
	CMD8(0xF7, 0x20);				/* Pump ratio */
	CMD8(0xEA, 0x00, 0x00);				/* Driver timing B */
	CMD8(0xC0, 0x23);				/* Power control 1 */
	CMD8(0xC1, 0x10);				/* Power control 2 */
	CMD8(0xC5, 0x3E, 0x28);				/* VCOM 1 */
	CMD8(0xC7, 0x86);				/* VCOM 2 */
	CMD8(0x36, ILI9341_MADCTL);			/* Orientation + colour order */
	CMD8(0x37, 0x00, 0x00);				/* Vertical scroll start */
	CMD8(0x3A, 0x55);				/* 16 bits per pixel (RGB565) */
	CMD8(0xB1, 0x00, 0x18);				/* Frame rate (79 Hz) */
	CMD8(0xB6, 0x08, 0x82, 0x27);			/* Display function */
	CMD8(0xF2, 0x00);				/* 3-gamma disabled */
	CMD8(0x26, 0x01);				/* Gamma curve 1 */
	CMD8(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
	     0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);	/* Positive gamma */
	CMD8(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
	     0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);	/* Negative gamma */

	cmd8(CMD_SLPOUT, NULL, 0);
	mk_ili9341_delay_ms(120);
	cmd8(CMD_DISPON, NULL, 0);
	mk_ili9341_delay_ms(20);

	/* Force the first set_window() to program the column address. */
	cur_x0 = cur_x1 = 0xFFFF;

	mk_ili9341_set_led(1);
	return 0;
}

void mk_ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint16_t p[2];

	if(x0 != cur_x0 || x1 != cur_x1)
	{
		p[0] = x0;
		p[1] = x1;
		cmd16(CMD_CASET, p, 2);
		cur_x0 = x0;
		cur_x1 = x1;
	}

	p[0] = y0;
	p[1] = y1;
	cmd16(CMD_PASET, p, 2);
}

void mk_ili9341_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb)
{
	uint16_t frame = CMD_RAMWR;

	mk_ili9341_set_rs(0);
	mk_ili9341_set_cs(0);
	mk_ili9341_spi_write16(&frame, 1);
	mk_ili9341_set_rs(1);
	mk_ili9341_spi_write16(pixels, nmemb);
	mk_ili9341_set_cs(1);
}

void mk_ili9341_write_line(uint16_t x, uint16_t y,
			   const uint16_t *pixels, uint16_t w)
{
	mk_ili9341_set_window(x, y, x + w - 1, y);
	mk_ili9341_write_pixels(pixels, w);
}

void mk_ili9341_set_idle(bool idle)
{
	cmd16(idle ? CMD_IDMON : CMD_IDMOFF, NULL, 0);
}

void mk_ili9341_invert_display(bool invert)
{
	cmd16(invert ? CMD_INVON : CMD_INVOFF, NULL, 0);
}

void mk_ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
			  uint16_t color)
{
	uint16_t chunk[128];
	uint16_t frame = CMD_RAMWR;
	uint32_t total = (uint32_t)w * h;

	if(w == 0 || h == 0)
		return;

	for(unsigned i = 0; i < 128; i++)
		chunk[i] = color;

	mk_ili9341_set_window(x, y, x + w - 1, y + h - 1);

	mk_ili9341_set_rs(0);
	mk_ili9341_set_cs(0);
	mk_ili9341_spi_write16(&frame, 1);
	mk_ili9341_set_rs(1);
	while(total)
	{
		uint16_t n = total > 128 ? 128 : (uint16_t)total;
		mk_ili9341_spi_write16(chunk, n);
		total -= n;
	}
	mk_ili9341_set_cs(1);
}

void mk_ili9341_fill(uint16_t color)
{
	mk_ili9341_fill_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

void mk_ili9341_pixel(uint16_t x, uint16_t y, uint16_t color)
{
	mk_ili9341_set_window(x, y, x, y);
	mk_ili9341_write_pixels(&color, 1);
}

void mk_ili9341_blit(const uint16_t *fbuf, uint16_t x, uint16_t y,
		     uint16_t w, uint16_t h)
{
	mk_ili9341_set_window(x, y, x + w - 1, y + h - 1);
	mk_ili9341_write_pixels(fbuf, (uint_fast16_t)w * h);
}

void mk_ili9341_exit(void)
{
}

void mk_ili9341_get_letter(uint16_t *fbuf,char l,uint16_t color,uint16_t bgcolor) {
	uint8_t letter[8];
	uint8_t row;
	
	switch(l)
	{
		case 'a':
		case 'A':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case 'b':
		case 'B':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'c':
		case 'C':
		{
			const uint8_t letter_[8]={0b00011110,
						              0b00110000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00110000,
						              0b00011110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'd':
		case 'D':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01101100,
						              0b01111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'e':
		case 'E':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'f':
		case 'F':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'g':
		case 'G':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100000,
						              0b01101110,
						              0b01100110,
						              0b01100110,
						              0b00111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'h':
		case 'H':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'i':
		case 'I':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'j':
		case 'J':
		{
			const uint8_t letter_[8]={0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'k':
		case 'K':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11001100,
						              0b11011000,
						              0b11110000,
						              0b11011000,
						              0b11001100,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'l':
		case 'L':
		{
			const uint8_t letter_[8]={0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'm':
		case 'M':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11101110,
						              0b11111110,
						              0b11010110,
						              0b11000110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'n':
		case 'N':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11100110,
						              0b11110110,
						              0b11011110,
						              0b11001110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'o':
		case 'O':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'p':
		case 'P':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'q':
		case 'Q':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11011100,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'r':
		case 'R':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 's':
		case 'S':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01110000,
						              0b00111100,
						              0b00001110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 't':
		case 'T':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'u':
		case 'U':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'v':
		case 'V':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00111100,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'w':
		case 'W':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11000110,
						              0b11000110,
						              0b11010110,
						              0b11111110,
						              0b11101110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'x':
		case 'X':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00111100,
						              0b01100110,
						              0b11000011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'y':
		case 'Y':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'z':
		case 'Z':
		{
			const uint8_t letter_[8]={0b11111110,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01100000,
						              0b11000000,
						              0b11111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '-':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b01111110,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '(':
		case '[':
		case '{':
		{
			const uint8_t letter_[8]={0b00001100,
						              0b00011000,
						              0b00110000,
									  0b00110000,
						              0b00110000,
						              0b00011000,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case ')':
		case ']':
		case '}':
		{
			const uint8_t letter_[8]={0b00110000,
						              0b00011000,
						              0b00001100,
									  0b00001100,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case ',':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00110000};
			memcpy(letter,letter_,8);
			break;
		}

		case '.':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '!':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00011000,
									  0b00011000,
						              0b00011000,
						              0b00000000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '&':
		{
			const uint8_t letter_[8]={0b00111000,
						              0b01101100,
						              0b01101000,
									  0b01110110,
						              0b11011100,
						              0b11001110,
						              0b01111011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '\'':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00110000,
									  0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '0':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01101110,
									  0b01111110,
						              0b01110110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '1':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00111000,
						              0b01111000,
									  0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '2':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '3':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00011100,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '4':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00111100,
						              0b01101100,
									  0b11001100,
						              0b11111110,
						              0b00001100,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '5':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01111100,
									  0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '6':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00110000,
						              0b01100000,
									  0b01111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '7':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00000110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '8':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '9':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111110,
						              0b00000110,
						              0b00001100,
						              0b00111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		default:
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}
	}

	for(uint8_t y=0;y<8;y++) {
		row=letter[y];
		for(uint8_t x=0;x<8;x++) {
			if(row & 128) {
				fbuf[y*8+x]=color;
			} else {
				fbuf[y*8+x]=bgcolor;
			}
			row=row<<1;
		}
	}
}

void mk_ili9341_text(char *s, uint16_t x, uint16_t y,
		     uint16_t color, uint16_t bgcolor)
{
	uint16_t fbuf[8*8];

	for(size_t i = 0; i < strlen(s); i++)
	{
		if(x + 8 > ILI9341_WIDTH)
			break;
		mk_ili9341_get_letter(fbuf, s[i], color, bgcolor);
		mk_ili9341_blit(fbuf, x, y, 8, 8);
		x += 8;
	}
}
