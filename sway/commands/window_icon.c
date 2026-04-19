#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "sway/commands.h"
#include "sway/tree/view.h"
#include "stringop.h"

// ── Base64 decoder ────────────────────────────────────────────────────────────

static int b64_char(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static uint8_t *base64_decode(const char *src, size_t *out_len) {
	size_t slen = strlen(src);
	uint8_t *out = malloc(slen * 3 / 4 + 4);
	if (!out) return NULL;

	size_t oi = 0;
	unsigned int buf = 0;
	int bits = 0;

	for (size_t i = 0; i < slen; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == '=') break;
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
		int v = b64_char(c);
		if (v < 0) continue;
		buf = (buf << 6) | (unsigned int)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[oi++] = (uint8_t)((buf >> bits) & 0xff);
		}
	}

	*out_len = oi;
	return out;
}

// ── In-memory PNG read via Cairo stream ──────────────────────────────────────

struct mem_read_ctx {
	const uint8_t *data;
	size_t         len;
	size_t         pos;
};

static cairo_status_t mem_read(void *closure,
		unsigned char *data, unsigned int length) {
	struct mem_read_ctx *ctx = closure;
	if (ctx->pos + length > ctx->len) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(data, ctx->data + ctx->pos, length);
	ctx->pos += length;
	return CAIRO_STATUS_SUCCESS;
}

// ── Command ───────────────────────────────────────────────────────────────────

struct cmd_results *cmd_window_icon(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "window_icon", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	if (!con || !con->view) {
		return cmd_results_new(CMD_INVALID,
				"window_icon requires a view container");
	}

	// argv may be split on spaces inside the base64; rejoin
	char *b64 = join_args(argv, argc);

	size_t png_len = 0;
	uint8_t *png_data = base64_decode(b64, &png_len);
	free(b64);

	if (!png_data || png_len == 0) {
		free(png_data);
		return cmd_results_new(CMD_FAILURE, "Failed to decode base64 data");
	}

	struct mem_read_ctx ctx = { .data = png_data, .len = png_len, .pos = 0 };
	cairo_surface_t *surface =
		cairo_image_surface_create_from_png_stream(mem_read, &ctx);
	free(png_data);

	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		if (surface) cairo_surface_destroy(surface);
		return cmd_results_new(CMD_FAILURE, "Failed to decode PNG image");
	}

	view_set_icon(con->view, surface);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
