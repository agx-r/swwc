/* swcbg: main.c
 *
 * Copyright (c) 2026 agx
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "protocol/swc-client-protocol.h"
#include <getopt.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wld/wayland.h>
#include <wld/wld.h>

struct screen {
	struct wl_list link;
	struct swc_screen *swc;
	struct swcbg *swcbg;
	uint32_t id;
	int x, y;
	unsigned width, height;
	struct wl_surface *surface;
	struct swc_background *background;
	struct wld_surface *wld_surface;
};

struct swcbg {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct swc_background_manager *background_manager;
	struct wl_list screens;
	uint32_t color;
	char *image_path;
	int img_width, img_height;
	struct wld_buffer *img_buffer;
	struct wld_context *ctx;
	struct wld_renderer *renderer;
};

static void
load_png(struct swcbg *swcbg, const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "could not open %s\n", path);
		return;
	}

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fclose(fp);
		return;
	}

	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(fp);
		return;
	}

	if (setjmp(png_jmpbuf(png))) {
		fprintf(stderr, "libpng error reading %s\n", path);
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		return;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	int width = png_get_image_width(png, info);
	int height = png_get_image_height(png, info);
	png_byte color_type = png_get_color_type(png, info);
	png_byte bit_depth = png_get_bit_depth(png, info);

	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);

	png_set_bgr(png);
	png_read_update_info(png, info);

	uint32_t *pixels = malloc(width * height * 4);
	png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
	for (int y = 0; y < height; y++)
		row_pointers[y] = (png_bytep)(&pixels[y * width]);

	png_read_image(png, row_pointers);

	swcbg->img_buffer = wld_create_buffer(swcbg->ctx, width, height, WLD_FORMAT_XRGB8888, WLD_FLAG_MAP);
	if (swcbg->img_buffer) {
		if (wld_map(swcbg->img_buffer)) {
			uint8_t *dst = swcbg->img_buffer->map;
			uint8_t *src = (uint8_t *)pixels;
			uint32_t dpitch = swcbg->img_buffer->pitch;
			uint32_t spitch = width * 4;
			for (int i = 0; i < height; ++i) {
				memcpy(dst, src, spitch);
				dst += dpitch;
				src += spitch;
			}
			wld_unmap(swcbg->img_buffer);
			swcbg->img_width = width;
			swcbg->img_height = height;
			fprintf(stderr, "Loaded image: %dx%d (pitch=%u)\n", width, height, dpitch);
		} else {
			fprintf(stderr, "could not map image buffer\n");
			wld_buffer_unreference(swcbg->img_buffer);
			swcbg->img_buffer = NULL;
		}
	} else {
		fprintf(stderr, "could not create %dx%d image buffer\n", width, height);
	}

	free(pixels);
	fclose(fp);
	png_destroy_read_struct(&png, &info, NULL);
	free(row_pointers);
}

static void
screen_geometry(void *data, struct swc_screen *swc_screen,
                int32_t x, int32_t y, uint32_t width,
                uint32_t height)
{
	struct screen *screen = data;
	screen->x = x;
	screen->y = y;
	screen->width = width;
	screen->height = height;
}

static const struct swc_screen_listener screen_listener = {
	.geometry = screen_geometry,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t id, const char *interface,
                       uint32_t version)
{
	struct swcbg *swcbg = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		swcbg->compositor =
		    wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	else if (strcmp(interface, swc_background_manager_interface.name) == 0)
		swcbg->background_manager =
		    wl_registry_bind(registry, id, &swc_background_manager_interface, 1);
	else if (strcmp(interface, swc_screen_interface.name) == 0) {
		struct screen *screen = malloc(sizeof(*screen));
		screen->id = id;
		screen->swcbg = swcbg;
		screen->swc = wl_registry_bind(registry, id, &swc_screen_interface, 1);
		screen->wld_surface = NULL;
		swc_screen_add_listener(screen->swc, &screen_listener, screen);
		wl_list_insert(&swcbg->screens, &screen->link);
	}
}

static void
registry_handle_global_remove(void *data,
                              struct wl_registry *registry,
                              uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

int
main(int argc, char *argv[])
{
	struct swcbg swcbg = { .color = 0, .image_path = NULL, .img_buffer = NULL };
	wl_list_init(&swcbg.screens);

	int c;
	static struct option long_options[] = { { "color", required_argument, 0, 'c' },
		                                { "image", required_argument, 0, 'i' },
		                                { 0, 0, 0, 0 } };

	while ((c = getopt_long(argc, argv, "c:i:", long_options, NULL)) != -1) {
		switch (c) {
		case 'c':
			swcbg.color = strtoul(optarg, NULL, 16) | 0xFF000000;
			break;
		case 'i':
			swcbg.image_path = optarg;
			break;
		}
	}

	swcbg.display = wl_display_connect(NULL);
	if (!swcbg.display)
		return EXIT_FAILURE;

	swcbg.registry = wl_display_get_registry(swcbg.display);
	wl_registry_add_listener(swcbg.registry, &registry_listener, &swcbg);
	wl_display_roundtrip(swcbg.display); // Get globals
	wl_display_roundtrip(swcbg.display); // Get screen geometry

	if (!swcbg.compositor || !swcbg.background_manager) {
		fprintf(stderr, "required protocols not supported\n");
		return EXIT_FAILURE;
	}

	swcbg.ctx = wld_wayland_create_context(swcbg.display, WLD_ANY);
	if (!swcbg.ctx) {
		fprintf(stderr, "could not create wld context\n");
		return EXIT_FAILURE;
	}
	swcbg.renderer = wld_create_renderer(swcbg.ctx);

	if (swcbg.image_path)
		load_png(&swcbg, swcbg.image_path);

	struct screen *screen;
	wl_list_for_each (screen, &swcbg.screens, link) {
		printf("Setting up background for screen %u (%ux%u at %d,%d)\n", screen->id, screen->width, screen->height, screen->x, screen->y);
		screen->surface = wl_compositor_create_surface(swcbg.compositor);
		screen->background = swc_background_manager_get_background(
		    swcbg.background_manager, screen->surface, screen->swc);

		screen->wld_surface = wld_wayland_create_surface(swcbg.ctx, screen->width, screen->height, WLD_FORMAT_XRGB8888, 0, screen->surface);
		if (!screen->wld_surface) {
			fprintf(stderr, "could not create wld surface for screen %u\n", screen->id);
			continue;
		}

		wld_set_target_surface(swcbg.renderer, screen->wld_surface);
		if (swcbg.img_buffer) {
			if (swcbg.img_width != (int)screen->width || swcbg.img_height != (int)screen->height) {
				fprintf(stderr, "error: image resolution %dx%d does not match screen %u resolution %ux%u\n",
				        swcbg.img_width, swcbg.img_height, screen->id, screen->width, screen->height);
				exit(EXIT_FAILURE);
			}
			wld_copy_rectangle(swcbg.renderer, swcbg.img_buffer, 0, 0, 0, 0, screen->width, screen->height);
		} else {
			wld_fill_rectangle(swcbg.renderer, swcbg.color, 0, 0, screen->width, screen->height);
		}

		struct wl_region *opaque_region = wl_compositor_create_region(swcbg.compositor);
		wl_region_add(opaque_region, 0, 0, screen->width, screen->height);
		wl_surface_set_opaque_region(screen->surface, opaque_region);
		wl_region_destroy(opaque_region);

		wl_surface_damage(screen->surface, 0, 0, screen->width, screen->height);
		wld_flush(swcbg.renderer);
		wld_swap(screen->wld_surface);
		wl_surface_commit(screen->surface);
	}

	wl_display_flush(swcbg.display);

	// Stay alive to keep the background surfaces
	while (wl_display_dispatch(swcbg.display) != -1)
		;

	return EXIT_SUCCESS;
}
