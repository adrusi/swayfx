#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include "cairo_util.h"
#include "log.h"
#include "sway/vtab_indicator.h"

// ── Cairo buffer boilerplate ──────────────────────────────────────────────────

struct ind_cairo_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cairo;
};

static void ind_buf_destroy(struct wlr_buffer *wlr_buffer) {
	struct ind_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	cairo_surface_destroy(buf->surface);
	cairo_destroy(buf->cairo);
	free(buf);
}

static bool ind_buf_begin_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct ind_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	*data   = cairo_image_surface_get_data(buf->surface);
	*stride = cairo_image_surface_get_stride(buf->surface);
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void ind_buf_end_access(struct wlr_buffer *wlr_buffer) {}

static const struct wlr_buffer_impl ind_buf_impl = {
	.destroy              = ind_buf_destroy,
	.begin_data_ptr_access = ind_buf_begin_access,
	.end_data_ptr_access  = ind_buf_end_access,
};

// ── Rendering ─────────────────────────────────────────────────────────────────

static void render_indicator(struct vtab_indicator *ind) {
	int px = (int)ceil(ind->size * ind->scale);
	if (px < 2) {
		wlr_scene_buffer_set_buffer(ind->node, NULL);
		return;
	}

	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return;
	}

	cairo_t *cr = cairo_create(surface);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

	// Transparent background
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	float *c = ind->color;
	cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]);

	double half = px / 2.0;
	if (ind->up) {
		// ▲: apex at top-centre, base at bottom
		cairo_move_to(cr, half, 0);
		cairo_line_to(cr, px,   px);
		cairo_line_to(cr, 0,    px);
	} else {
		// ▼: base at top, apex at bottom-centre
		cairo_move_to(cr, 0,    0);
		cairo_line_to(cr, px,   0);
		cairo_line_to(cr, half, px);
	}
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_surface_flush(surface);

	struct ind_cairo_buffer *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return;
	}

	wlr_buffer_init(&buf->base, &ind_buf_impl, px, px);
	buf->surface = surface;
	buf->cairo   = cr;

	wlr_scene_buffer_set_buffer(ind->node, &buf->base);
	wlr_buffer_drop(&buf->base);
	// dest_size keeps the node at the compositor-pixel size regardless of scale
	wlr_scene_buffer_set_dest_size(ind->node, ind->size, ind->size);
}

// ── Event handlers ────────────────────────────────────────────────────────────

static void handle_outputs_update(struct wl_listener *listener, void *data) {
	struct vtab_indicator *ind =
		wl_container_of(listener, ind, outputs_update);
	struct wlr_scene_outputs_update_event *event = data;

	float scale = 0;
	for (size_t i = 0; i < event->size; i++) {
		if (event->active[i]->output->scale > scale) {
			scale = event->active[i]->output->scale;
		}
	}
	if (scale == 0) scale = 1.0f;

	if (scale != ind->scale) {
		ind->scale = scale;
		render_indicator(ind);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct vtab_indicator *ind = wl_container_of(listener, ind, destroy);
	wl_list_remove(&ind->outputs_update.link);
	wl_list_remove(&ind->destroy.link);
	free(ind);
}

// ── Public API ────────────────────────────────────────────────────────────────

struct vtab_indicator *vtab_indicator_create(struct wlr_scene_tree *parent,
		bool up, float color[4], int size) {
	struct vtab_indicator *ind = calloc(1, sizeof(*ind));
	if (!ind) return NULL;

	struct wlr_scene_buffer *node = wlr_scene_buffer_create(parent, NULL);
	if (!node) {
		free(ind);
		return NULL;
	}

	ind->node  = node;
	ind->up    = up;
	ind->size  = size;
	ind->scale = 1.0f;
	memcpy(ind->color, color, sizeof(ind->color));

	ind->outputs_update.notify = handle_outputs_update;
	wl_signal_add(&node->events.outputs_update, &ind->outputs_update);

	ind->destroy.notify = handle_destroy;
	wl_signal_add(&node->node.events.destroy, &ind->destroy);

	render_indicator(ind);
	return ind;
}

void vtab_indicator_set(struct vtab_indicator *ind, float color[4], int size) {
	bool color_changed = memcmp(ind->color, color, sizeof(ind->color)) != 0;
	bool size_changed  = ind->size != size;
	if (!color_changed && !size_changed) return;

	memcpy(ind->color, color, sizeof(ind->color));
	ind->size = size;
	render_indicator(ind);
}
