/* swc: libswc/background.c
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

#include "background.h"
#include "compositor.h"
#include "internal.h"
#include "screen.h"
#include "surface.h"
#include "swc-server-protocol.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

struct background {
	struct wl_resource *resource;
	struct compositor_view *view;
	struct wl_listener surface_destroy_listener;
};

static void
destroy_background(struct wl_resource *resource)
{
	struct background *background = wl_resource_get_user_data(resource);

	compositor_view_destroy(background->view);
	free(background);
}

static const struct swc_background_interface background_impl = {
	.destroy = destroy_resource,
};

static void
handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct background *background = wl_container_of(listener, background, surface_destroy_listener);
	wl_resource_destroy(background->resource);
}

static void
get_background(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *screen_resource)
{
	struct surface *surface = wl_resource_get_user_data(surface_resource);
	struct screen *screen = wl_resource_get_user_data(screen_resource);
	struct background *background;

	background = malloc(sizeof(*background));

	if (!background)
		goto error0;

	background->resource = wl_resource_create(client, &swc_background_interface, 1, id);

	if (!background->resource)
		goto error1;

	if (!(background->view = compositor_create_view(surface)))
		goto error2;

	background->view->background = true;
	view_set_position(&background->view->base, screen->base.geometry.x, screen->base.geometry.y);
	view_set_size(&background->view->base, screen->base.geometry.width, screen->base.geometry.height);
	compositor_view_show(background->view);

	wl_resource_set_implementation(background->resource, &background_impl, background, &destroy_background);
	background->surface_destroy_listener.notify = &handle_surface_destroy;
	wl_resource_add_destroy_listener(surface->resource, &background->surface_destroy_listener);

	return;

error2:
	wl_resource_destroy(background->resource);
error1:
	free(background);
error0:
	wl_client_post_no_memory(client);
}

static const struct swc_background_manager_interface background_manager_impl = {
	.get_background = get_background,
};

static void
bind_background_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &swc_background_manager_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &background_manager_impl, NULL, NULL);
}

struct wl_global *
background_manager_create(struct wl_display *display)
{
	return wl_global_create(display, &swc_background_manager_interface, 1, NULL, &bind_background_manager);
}
