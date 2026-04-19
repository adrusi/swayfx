#ifndef _SWAY_ICON_NODE_H
#define _SWAY_ICON_NODE_H
#include <cairo.h>
#include <scenefx/types/wlr_scene.h>

/*
 * A scene-graph node that renders a Cairo ARGB32 surface scaled to a target
 * logical height (preserving aspect ratio).  DPI scaling is handled
 * automatically via the wlr_scene_buffer outputs_update listener.
 *
 * The caller owns the source surface.  Before freeing the source, call
 * sway_icon_node_set(node, NULL, height) to clear the reference.
 */
struct sway_icon_node {
	int width;   /* logical display width (aspect-correct) */
	int height;  /* logical display height (== target_height) */
	struct wlr_scene_node *node;
};

struct sway_icon_node *sway_icon_node_create(struct wlr_scene_tree *parent,
		cairo_surface_t *src, int target_height);

void sway_icon_node_set(struct sway_icon_node *node,
		cairo_surface_t *src, int target_height);

#endif
