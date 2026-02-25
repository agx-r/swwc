/* swcshot: swcshot.c
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wld/wayland.h>
#include <wld/wld.h>

struct screen {
	struct wl_list link;
	struct swc_screen *swc;
	int x, y;
	unsigned width, height;
};

struct swcshot {
	struct wl_display *display;
	struct wl_registry *registry;
	struct swc_screenshot_manager *screenshot_manager;
	struct wl_shm *shm;
	struct wl_list screens;
	bool done;
};

static void
screen_geometry(void *data, struct swc_screen *swc_screen,
                int32_t x, int32_t y, uint32_t width, uint32_t height)
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
                       uint32_t id, const char *interface, uint32_t version)
{
	struct swcshot *swcshot = data;

	if (strcmp(interface, swc_screenshot_manager_interface.name) == 0)
		swcshot->screenshot_manager = wl_registry_bind(registry, id, &swc_screenshot_manager_interface, 1);
	else if (strcmp(interface, swc_screen_interface.name) == 0) {
		struct screen *screen = malloc(sizeof(*screen));
		screen->swc = wl_registry_bind(registry, id, &swc_screen_interface, 1);
		swc_screen_add_listener(screen->swc, &screen_listener, screen);
		wl_list_insert(&swcshot->screens, &screen->link);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		swcshot->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static void
screenshot_done(void *data, struct swc_screenshot_manager *manager)
{
	struct swcshot *swcshot = data;
	swcshot->done = true;
}

static const struct swc_screenshot_manager_listener screenshot_listener = {
	.done = screenshot_done,
};

int
main(int argc, char *argv[])
{
	struct swcshot swcshot = { 0 };
	wl_list_init(&swcshot.screens);

	swcshot.display = wl_display_connect(NULL);
	if (!swcshot.display)
		return EXIT_FAILURE;

	swcshot.registry = wl_display_get_registry(swcshot.display);
	wl_registry_add_listener(swcshot.registry, &registry_listener, &swcshot);
	wl_display_roundtrip(swcshot.display);
	wl_display_roundtrip(swcshot.display);

	if (!swcshot.screenshot_manager || wl_list_empty(&swcshot.screens)) {
		fprintf(stderr, "screenshot manager or screens not found\n");
		return EXIT_FAILURE;
	}

	swc_screenshot_manager_add_listener(swcshot.screenshot_manager, &screenshot_listener, &swcshot);

	struct screen *screen = wl_container_of(swcshot.screens.next, screen, link);
	struct wld_context *ctx = wld_wayland_create_context(swcshot.display, WLD_SHM, WLD_NONE);
	if (!ctx) {
		fprintf(stderr, "could not create wld SHM context\n");
		return EXIT_FAILURE;
	}
	struct wld_buffer *buffer = wld_create_buffer(ctx, screen->width, screen->height, WLD_FORMAT_XRGB8888, WLD_FLAG_MAP);

	if (!buffer) {
		fprintf(stderr, "could not create wld buffer\n");
		return EXIT_FAILURE;
	}

	union wld_object object;
	if (!wld_export(buffer, WLD_WAYLAND_OBJECT_BUFFER, &object)) {
		fprintf(stderr, "could not export buffer to wayland\n");
		return EXIT_FAILURE;
	}

	struct wl_buffer *wl_buffer = object.ptr;

	swc_screenshot_manager_capture(swcshot.screenshot_manager, screen->swc, wl_buffer);

	while (!swcshot.done && wl_display_dispatch(swcshot.display) != -1)
		;

	if (swcshot.done) {
		if (wld_map(buffer)) {
			FILE *fp = fopen("screenshot.ppm", "wb");
			if (fp) {
				fprintf(fp, "P6\n%u %u\n255\n", screen->width, screen->height);
				uint8_t *pixels = buffer->map;
				for (unsigned y = 0; y < screen->height; ++y) {
					uint32_t *row = (uint32_t *)(pixels + y * buffer->pitch);
					for (unsigned x = 0; x < screen->width; ++x) {
						uint32_t pixel = row[x];
						uint8_t rgb[3] = { (pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff };
						fwrite(rgb, 1, 3, fp);
					}
				}
				fclose(fp);
				printf("Saved screenshot to screenshot.ppm\n");
			} else {
				perror("fopen");
			}
			wld_unmap(buffer);
		} else {
			fprintf(stderr, "could not map buffer\n");
		}
	}

	return EXIT_SUCCESS;
}
