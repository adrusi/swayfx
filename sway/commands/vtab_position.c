#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"

static void reset_vtab_left_iter(struct sway_container *con, void *data) {
	con->vtab_left = config->vtab_left;
}

struct cmd_results *cmd_vtab_position(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_position", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	bool new_left;
	if (strcasecmp(argv[0], "left") == 0) {
		new_left = true;
	} else if (strcasecmp(argv[0], "right") == 0) {
		new_left = false;
	} else {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'vtab_position left|right'");
	}

	struct sway_container *ctx = config->handler_context.container;
	if (ctx == NULL) {
		// Config context: update global default and propagate to all containers
		config->vtab_left = new_left;
		root_for_each_container(reset_vtab_left_iter, NULL);
		arrange_root();
	} else {
		// Runtime: find the nearest vtabbed ancestor and set on it
		struct sway_container *vtab_con = ctx;
		while (vtab_con && vtab_con->pending.layout != L_VTABBED) {
			vtab_con = vtab_con->pending.parent;
		}
		if (vtab_con == NULL) {
			// No vtabbed ancestor — set on context container so it takes
			// effect if/when the layout is switched to vtabbed
			ctx->vtab_left = new_left;
		} else {
			vtab_con->vtab_left = new_left;
			arrange_container(vtab_con);
		}
	}
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
