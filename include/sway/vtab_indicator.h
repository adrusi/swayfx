#ifndef _SWAY_VTAB_INDICATOR_H
#define _SWAY_VTAB_INDICATOR_H

#include <stdbool.h>
#include <scenefx/types/wlr_scene.h>
#include <wayland-server-core.h>

/**
 * A small filled-triangle overlay for vtabbed scroll overflow indicators.
 * Rendered via Cairo — not font-dependent, sized and centered precisely.
 * Automatically re-renders at the correct DPI when the output scale changes.
 *
 * The pointer is owned by the scene graph and freed automatically when the
 * scene node is destroyed. Do not call free() on it directly.
 */
struct vtab_indicator {
	struct wlr_scene_buffer *node;
	bool up;        // true = apex at top (▲), false = apex at bottom (▼)
	float color[4];
	int size;       // bounding-box side length in compositor pixels
	float scale;    // current output scale
	struct wl_listener outputs_update;
	struct wl_listener destroy;
};

/**
 * Create a triangle indicator as a child of parent.
 * Returns NULL on allocation failure.
 */
struct vtab_indicator *vtab_indicator_create(struct wlr_scene_tree *parent,
		bool up, float color[4], int size);

/**
 * Update the indicator's color and/or size, re-rendering if either changed.
 */
void vtab_indicator_set(struct vtab_indicator *ind, float color[4], int size);

#endif
