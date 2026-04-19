#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"

static void reset_vtab_width_iter(struct sway_container *con, void *data) {
	con->vtab_width = config->vtab_width;
}

static int apply_vtab_width(int current, bool relative, int value) {
	int result = relative ? current + value : value;
	return result < 0 ? 0 : result;
}

struct cmd_results *cmd_vtab_width(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_width", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	// Accept absolute values ("220"), or relative adjustments ("+20", "-20")
	bool relative = argv[0][0] == '+' || argv[0][0] == '-';
	char *inv;
	int value = strtol(argv[0], &inv, 10);
	if (*inv != '\0' || (!relative && value < 0)) {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'vtab_width <px>' or 'vtab_width +<px>'/'vtab_width -<px>'");
	}

	struct sway_container *ctx = config->handler_context.container;
	if (ctx == NULL) {
		// Config context: update global default and propagate to all containers.
		// During reload, skip propagation so per-container overrides survive.
		config->vtab_width = apply_vtab_width(config->vtab_width, relative, value);
		if (!config->reloading) {
			root_for_each_container(reset_vtab_width_iter, NULL);
		}
		arrange_root();
	} else {
		// Runtime: find the nearest vtabbed ancestor and set on it
		struct sway_container *vtab_con = ctx;
		while (vtab_con && vtab_con->pending.layout != L_VTABBED) {
			vtab_con = vtab_con->pending.parent;
		}
		if (vtab_con == NULL) {
			ctx->vtab_width = apply_vtab_width(ctx->vtab_width, relative, value);
		} else {
			vtab_con->vtab_width = apply_vtab_width(vtab_con->vtab_width, relative, value);
			arrange_container(vtab_con);
		}
	}
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
