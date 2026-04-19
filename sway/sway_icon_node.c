#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include "log.h"
#include "sway/sway_icon_node.h"

// ── Cairo buffer boilerplate ──────────────────────────────────────────────────

struct icon_cairo_buffer {
	struct wlr_buffer  base;
	cairo_surface_t   *surface;
	cairo_t           *cairo;
};

static void icb_destroy(struct wlr_buffer *wlr_buffer) {
	struct icon_cairo_buffer *b = wl_container_of(wlr_buffer, b, base);
	cairo_surface_destroy(b->surface);
	cairo_destroy(b->cairo);
	free(b);
}

static bool icb_begin_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride) {
	struct icon_cairo_buffer *b = wl_container_of(wlr_buffer, b, base);
	*data   = cairo_image_surface_get_data(b->surface);
	*stride = cairo_image_surface_get_stride(b->surface);
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void icb_end_access(struct wlr_buffer *wlr_buffer) {}

static const struct wlr_buffer_impl icb_impl = {
	.destroy               = icb_destroy,
	.begin_data_ptr_access = icb_begin_access,
	.end_data_ptr_access   = icb_end_access,
};

// ── Internal struct ───────────────────────────────────────────────────────────

struct icon_buffer {
	struct wlr_scene_buffer *buffer_node;
	struct sway_icon_node    props;
	cairo_surface_t         *src;           /* borrowed, NOT owned */
	int                      target_height; /* logical pixels */
	float                    scale;
	struct wl_listener       outputs_update;
	struct wl_listener       destroy;
};

// ── Rendering ─────────────────────────────────────────────────────────────────

static void render_icon(struct icon_buffer *ib) {
	if (!ib->src || ib->target_height <= 0 || ib->scale <= 0.0f) {
		wlr_scene_buffer_set_buffer(ib->buffer_node, NULL);
		ib->props.width  = 0;
		ib->props.height = 0;
		wlr_scene_buffer_set_dest_size(ib->buffer_node, 0, 0);
		return;
	}

	int src_w = cairo_image_surface_get_width(ib->src);
	int src_h = cairo_image_surface_get_height(ib->src);
	if (src_w <= 0 || src_h <= 0) {
		wlr_scene_buffer_set_buffer(ib->buffer_node, NULL);
		return;
	}

	int dst_h = (int)ceil(ib->target_height * ib->scale);
	int dst_w = (int)ceil((double)src_w * dst_h / src_h);
	if (dst_w < 1) dst_w = 1;
	if (dst_h < 1) dst_h = 1;

	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dst_w, dst_h);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return;
	}

	cairo_t *cr = cairo_create(surface);
	cairo_scale(cr, (double)dst_w / src_w, (double)dst_h / src_h);
	cairo_set_source_surface(cr, ib->src, 0, 0);
	// Use OVER so the icon's own alpha channel is respected
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_paint(cr);
	cairo_surface_flush(surface);

	struct icon_cairo_buffer *cbuf = calloc(1, sizeof(*cbuf));
	if (!cbuf) {
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return;
	}

	wlr_buffer_init(&cbuf->base, &icb_impl, dst_w, dst_h);
	cbuf->surface = surface;
	cbuf->cairo   = cr;

	wlr_scene_buffer_set_buffer(ib->buffer_node, &cbuf->base);
	wlr_buffer_drop(&cbuf->base);

	int logical_w = (int)round((double)dst_w / ib->scale);
	ib->props.width  = logical_w;
	ib->props.height = ib->target_height;
	wlr_scene_buffer_set_dest_size(ib->buffer_node, logical_w, ib->target_height);
}

// ── Event handlers ────────────────────────────────────────────────────────────

static void handle_outputs_update(struct wl_listener *listener, void *data) {
	struct icon_buffer *ib = wl_container_of(listener, ib, outputs_update);
	struct wlr_scene_outputs_update_event *event = data;

	float scale = 0;
	for (size_t i = 0; i < event->size; i++) {
		if (event->active[i]->output->scale > scale) {
			scale = event->active[i]->output->scale;
		}
	}
	if (scale == 0) scale = 1.0f;

	if (scale != ib->scale) {
		ib->scale = scale;
		render_icon(ib);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct icon_buffer *ib = wl_container_of(listener, ib, destroy);
	wl_list_remove(&ib->outputs_update.link);
	wl_list_remove(&ib->destroy.link);
	free(ib);
}

// ── Public API ────────────────────────────────────────────────────────────────

struct sway_icon_node *sway_icon_node_create(struct wlr_scene_tree *parent,
		cairo_surface_t *src, int target_height) {
	struct icon_buffer *ib = calloc(1, sizeof(*ib));
	if (!ib) return NULL;

	struct wlr_scene_buffer *node = wlr_scene_buffer_create(parent, NULL);
	if (!node) {
		free(ib);
		return NULL;
	}

	ib->buffer_node   = node;
	ib->props.node    = &node->node;
	ib->src           = src;
	ib->target_height = target_height;
	ib->scale         = 1.0f;

	ib->outputs_update.notify = handle_outputs_update;
	wl_signal_add(&node->events.outputs_update, &ib->outputs_update);

	ib->destroy.notify = handle_destroy;
	wl_signal_add(&node->node.events.destroy, &ib->destroy);

	render_icon(ib);
	return &ib->props;
}

void sway_icon_node_set(struct sway_icon_node *node,
		cairo_surface_t *src, int target_height) {
	struct icon_buffer *ib = wl_container_of(node, ib, props);
	if (ib->src == src && ib->target_height == target_height) return;
	ib->src           = src;
	ib->target_height = target_height;
	render_icon(ib);
}
