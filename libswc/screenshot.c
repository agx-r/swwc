/* swc: libswc/screenshot.c
 *
 * Copyright (c) 2026 Michael Forney
 * Copyright (c) 2026      agx
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

#include "screenshot.h"
#include "compositor.h"
#include "internal.h"
#include "screen.h"
#include "wayland_buffer.h"

#include "swc-server-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <wld/wld.h>

static void
capture(struct wl_client *client, struct wl_resource *resource,
        struct wl_resource *screen_resource, struct wl_resource *buffer_resource)
{
	struct screen *screen = wl_resource_get_user_data(screen_resource);
	struct wld_buffer *buffer = wayland_buffer_get(buffer_resource);

	if (!buffer) {
		wl_resource_post_error(resource, 0, "invalid buffer");
		return;
	}

	compositor_render_screen(screen, buffer);
	swc_screenshot_manager_send_done(resource);
}

static const struct swc_screenshot_manager_interface screenshot_manager_impl = {
	.capture = capture,
};

static void
bind_screenshot_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &swc_screenshot_manager_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &screenshot_manager_impl, NULL, NULL);
}

struct wl_global *
screenshot_manager_create(struct wl_display *display)
{
	return wl_global_create(display, &swc_screenshot_manager_interface, 1, NULL, &bind_screenshot_manager);
}
