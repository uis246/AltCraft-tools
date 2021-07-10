#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <png.h>

#include <arm_neon.h>

static void failmsg(char *reason) {
	fprintf(stderr, "%s\n", reason);
	exit(-1);
}

int main(int argc, char **argv) {
	if(argc < 2)
	failmsg("Not enough arguments");

	size_t slen = strlen(argv[1]);

	if(strcmp(&argv[1][slen-4], ".png") != 0)
		failmsg("Wrong extension");

	char *mask = alloca(slen + 2 + 1);
	memcpy(mask, argv[1], slen-4);
	memcpy(&mask[slen-4], "_0.png", 6);
	mask[slen + 2] = 0;

	FILE *f = fopen(argv[1], "rb");

	if(!f) {
		failmsg("Failed to open file");
	}

	png_structp ptr;
	ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!ptr) {
		fclose(f);
		failmsg("Failed to init libpng");
	}

	png_infop info = png_create_info_struct(ptr);
	if(!info)
		goto destroy;

	if(setjmp(png_jmpbuf(ptr)))
		goto destroy;

	png_init_io(ptr, f);
	png_read_info(ptr, info);

	if(png_get_color_type(ptr, info) != PNG_COLOR_TYPE_RGBA || png_get_bit_depth(ptr, info) != 8) {
		fprintf(stderr, "Not yet implemented\n");
		goto destroy;
	}

	png_uint_32 x, y;
	x = png_get_image_width(ptr, info);
	y = png_get_image_height(ptr, info);

	if(x % 16 != 0 || y % 16 != 0) {
		fprintf(stderr, "Linear sizes should be multiple of 16\n");
		goto destroy;
	}

	uint16_t _Alignas(8 * sizeof(uint16_t)) *buf;

	png_bytep u8buf;
	{
	png_bytep *row_pointers = (png_bytep*) alloca(sizeof(png_bytep) * y);
	size_t rb = png_get_rowbytes(ptr, info);
	u8buf = malloc(rb * y);
	for(unsigned int i = 0; i < y; i++)
		row_pointers[i] = &u8buf[rb * i];

	if(!u8buf)
		goto destroy;

	if(setjmp(png_jmpbuf(ptr))) {
		free(u8buf);
		goto destroy;
	}

	png_read_image(ptr, row_pointers);
	
	//SOME MATHIMAGIC

	//First: allocate twice bigger space for u16 storage
	buf = aligned_alloc(8 * sizeof(uint16_t), x * y * 4 * sizeof(uint16_t));

	//Second: premultiply
	for(size_t i = 0; i < (x * y & ~15) ; i++) {
		uint16_t r, g, b, a;
		r = u8buf[i * 4];
		g = u8buf[i * 4 + 1];
		b = u8buf[i * 4 + 2];
		a = u8buf[i * 4 + 3];

		r *= a;
		g *= a;
		b *= a;
//		a *= 255;

		buf[i * 4] = r;
		buf[i * 4 + 1] = g;
		buf[i * 4 + 2] = b;
		buf[i * 4 + 3] = a;
	}//TODO: save premulted;
	}//Close alloca scope
	
	png_destroy_read_struct(&ptr, &info, NULL);
	fclose(f);

	f = NULL;
	info = NULL;
	ptr = NULL;

	for(unsigned int level = 0; level < 4; level++) {
		uint32_t dx = x >> 1;
		uint32_t dy = y >> 1;
		for(uint32_t ly = 0; ly < dy; ly++) {
			for(uint32_t lx = 0; lx < dx; lx++) {
			//Third: mid-4
				//printf("{%u,%u}, %u\n", lx, ly, (ly * x * 4 * 2) + lx * 4 * 2);
				uint16x8_t v00_01;
				{
					uint16x8_t v10_11;
					v00_01 = vld1q_u16(&buf[(ly * x * 4 * 2) + lx * 4 * 2]);
					v10_11 = vld1q_u16(&buf[(ly * x * 4 * 2) + lx * 4 * 2 + (x * 4)]);
					v00_01 = vhaddq_u16(v00_01, v10_11);
				}
				uint16x4_t store;
				store = vhadd_u16(vget_high_u16(v00_01), vget_low_u16(v00_01));
				
				vst1_u16(&buf[(ly * dx * 4) + (lx * 4)], store);
			}
		}
		x = dx;
		y = dy;
	
		//Save to RGBA8(PMA): divide color by 255
//		uint8_t *sbuf = malloc(x * y * 4);
		for(size_t i = 0; i < x * y; i++) {
			uint16_t r, g, b, a;
			r = buf[i * 4];
			g = buf[i * 4 + 1];
			b = buf[i * 4 + 2];
			a = buf[i * 4 + 3];
			
			/*
			r /= 255;
			g /= 255;
			b /= 255;
			a /= 255;
			*/
			
			//Demult for testing purposes
			//a /= 255;
			r /= a;
			g /= a;
			b /= a;
			//Clamp
			if(r > 255)
				r = 255;
			if(g > 255)
				g = 255;
			if(b > 255)
				b = 255;

			u8buf[i * 4] = r;
			u8buf[i * 4 + 1] = g;
			u8buf[i * 4 + 2] = b;
			u8buf[i * 4 + 3] = a;
		}
		{
			mask[slen-3] = '0' + level + 1;
			printf("%s\n", mask);
			f = fopen(mask, "wb");
			if(!f) {
				fprintf(stderr, "Failed to open file \"%s\"", mask);
				goto des;
			}
	
			ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
			if(!ptr) {
				fprintf(stderr, "Failed to create write context\n");
				goto des;
			}

			info = png_create_info_struct(ptr);
			if(!info) {
				png_destroy_write_struct(&ptr, NULL);
				fprintf(stderr, "Failed to create write context\n");
				goto des;
			}

			if(setjmp(png_jmpbuf(ptr))) {
				fprintf(stderr, "Failed to write\n");
				goto des;
			}

			png_init_io(ptr, f);
			png_set_sig_bytes(ptr, 0);

			png_set_IHDR(ptr, info, x, y,
				8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_DEFAULT);

			png_write_info(ptr, info);
			printf("Info done: %u %u\n", x, y);

			png_bytep *row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * y);
			for(uint32_t i = 0; i < y; i++) {
				row_pointers[i] = &u8buf[i * x * 4];
				//printf("%u ", row_pointers[i] - sbuf);
			}
			//printf("\n");
			png_write_image(ptr, row_pointers);
			//png_write_rows(ptr, row_pointers, dy);
			printf("Write done\n");
			free(row_pointers);

			png_write_end(ptr, info);
			printf("End\n");

			fclose(f);
			f = NULL;

			png_destroy_write_struct(&ptr, &info);
			ptr = NULL;
			info = NULL;
		}
	}
	goto des;

	destroy:
	png_destroy_read_struct(&ptr, &info, NULL);

	fclose(f);
	return -2;

	des:
	if(f)
		fclose(f);
	free(u8buf);
	free(buf);
	return 0;
}
