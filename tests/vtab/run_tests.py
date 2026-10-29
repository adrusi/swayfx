#!/usr/bin/env python3
"""
Headless tree-shape tests for container moves, vtabbed layouts and
workspace_layout wrapping.

Each configuration starts a nested sway on the wlroots headless backend (no
GPU, no input devices, nothing shown on screen), opens `foot` windows, drives
it over IPC and compares the resulting container tree against the expected
shape. A stress scenario exercises vtab focus/scroll/move/close with
animations enabled and only checks that sway survives.

Build with sanitizers to turn memory bugs into hard failures:

    meson setup build -Db_sanitize=address,undefined
    ninja -C build
    tests/vtab/run_tests.py --sway build/sway/sway

Requires python3 and foot (both provided by the flake's devShell).
"""

import argparse
import glob
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import textwrap
import time

IPC_MAGIC = b"i3-ipc"
IPC_COMMAND = 0
IPC_GET_TREE = 4
IPC_HEADER = struct.Struct("=6sII")

SANITIZER_MARKERS = ("ERROR: AddressSanitizer", "runtime error:",
                     "ERROR: LeakSanitizer")


class TestFailure(Exception):
    pass


class Sway:
    def __init__(self, binary, config, verbose=False):
        self.tmpdir = tempfile.mkdtemp(prefix="sway-test-")
        self.config_path = os.path.join(self.tmpdir, "config")
        self.log_path = os.path.join(self.tmpdir, "sway.log")
        with open(self.config_path, "w") as f:
            f.write(config)

        env = dict(os.environ)
        for var in ("WAYLAND_DISPLAY", "DISPLAY", "SWAYSOCK", "I3SOCK"):
            env.pop(var, None)
        env.update({
            "XDG_RUNTIME_DIR": self.tmpdir,
            "WLR_BACKENDS": "headless",
            "WLR_RENDERER": "pixman",
            "WLR_HEADLESS_OUTPUTS": "1",
            "WLR_LIBINPUT_NO_DEVICES": "1",
            "ASAN_OPTIONS": env.get("ASAN_OPTIONS", "detect_leaks=0"),
            "UBSAN_OPTIONS": env.get("UBSAN_OPTIONS", "print_stacktrace=1"),
        })
        self.env = env
        self.verbose = verbose
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [binary, "-c", self.config_path, "-d"],
            env=env, stdout=self.log, stderr=subprocess.STDOUT)

        self.sock_path = self._wait_for(
            lambda: next(iter(glob.glob(
                os.path.join(self.tmpdir, "sway-ipc.*.sock"))), None),
            "IPC socket")
        self.env["SWAYSOCK"] = self.sock_path
        self.env["WAYLAND_DISPLAY"] = self._wait_for(
            lambda: next((os.path.basename(p) for p in glob.glob(
                os.path.join(self.tmpdir, "wayland-*"))
                if not p.endswith(".lock")), None),
            "Wayland socket")
        self.clients = []

    def _wait_for(self, fn, what, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.check_alive()
            result = fn()
            if result:
                return result
            time.sleep(0.05)
        raise TestFailure(f"timed out waiting for {what}")

    def ipc(self, msg_type, payload=""):
        data = payload.encode()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(self.sock_path)
            s.sendall(IPC_HEADER.pack(IPC_MAGIC, len(data), msg_type) + data)
            header = b""
            while len(header) < IPC_HEADER.size:
                chunk = s.recv(IPC_HEADER.size - len(header))
                if not chunk:
                    raise TestFailure("sway closed the IPC connection")
                header += chunk
            _, length, _ = IPC_HEADER.unpack(header)
            body = b""
            while len(body) < length:
                chunk = s.recv(length - len(body))
                if not chunk:
                    raise TestFailure("sway closed the IPC connection")
                body += chunk
        return json.loads(body)

    def cmd(self, command, check=True):
        self.check_alive()
        if self.verbose:
            print(f"    $ {command}")
        results = self.ipc(IPC_COMMAND, command)
        if check:
            for r in results:
                if not r.get("success"):
                    raise TestFailure(
                        f"command {command!r} failed: {r.get('error')}")
        return results

    def tree(self):
        self.check_alive()
        return self.ipc(IPC_GET_TREE)

    def open(self, app_id):
        """Open a foot window with the given app_id and wait for it to map."""
        self.clients.append(subprocess.Popen(
            ["foot", "--app-id", app_id, "-T", app_id, "sh", "-c",
             "sleep 1000000"],
            env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        self._wait_for(lambda: find_view(self.tree(), app_id), f"{app_id} to map")

    def check_alive(self):
        if self.proc.poll() is not None:
            raise TestFailure(
                f"sway exited with status {self.proc.returncode}\n"
                + self.log_tail())

    def sanitizer_errors(self):
        self.log.flush()
        with open(self.log_path, errors="replace") as f:
            lines = f.read().splitlines()
        for i, line in enumerate(lines):
            if any(m in line for m in SANITIZER_MARKERS):
                return "\n".join(lines[i:i + 40])
        return None

    def log_tail(self, n=40):
        self.log.flush()
        with open(self.log_path, errors="replace") as f:
            return "".join(f.readlines()[-n:])

    def stop(self, keep_tmpdir=False):
        for c in self.clients:
            c.terminate()
        if self.proc.poll() is None:
            try:
                self.cmd("exit", check=False)
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.send_signal(signal.SIGKILL)
                self.proc.wait()
        for c in self.clients:
            try:
                c.wait(timeout=2)
            except subprocess.TimeoutExpired:
                c.kill()
        self.log.close()
        if not keep_tmpdir:
            shutil.rmtree(self.tmpdir, ignore_errors=True)


def find_view(node, app_id):
    if node.get("app_id") == app_id:
        return node
    for child in node.get("nodes", []) + node.get("floating_nodes", []):
        found = find_view(child, app_id)
        if found:
            return found
    return None


def format_node(node, depth, out):
    indent = "  " * depth
    if node.get("pid"):
        out.append(f"{indent}view {node.get('app_id')}")
    else:
        out.append(f"{indent}con [{node['layout']}]")
    for child in node["nodes"] + node["floating_nodes"]:
        format_node(child, depth + 1, out)


def format_workspaces(tree, names):
    """Render the named workspaces as indented text, ignoring ids and focus.
    A workspace that no longer exists renders as "(missing)"."""
    workspaces = {}
    for output in tree["nodes"]:
        for ws in output["nodes"]:
            workspaces[ws["name"]] = ws
    out = []
    for name in names:
        ws = workspaces.get(name)
        if ws is None:
            out.append(f"ws {name} (missing)")
            continue
        out.append(f"ws {name} [{ws['layout']}]")
        for child in ws["nodes"] + ws["floating_nodes"]:
            format_node(child, 1, out)
    return "\n".join(out)


# ── Scenarios ────────────────────────────────────────────────────────────────
#
# Each scenario gets the running sway and the workspace_layout value, performs
# its actions on workspaces of its own, and returns (workspace names, expected
# tree text). Empty workspaces are destroyed once unfocused, so they are
# expected to be missing.


def scenario_move_split_to_empty_workspace(sway, wl):
    """Moving a split container to an empty workspace keeps its structure.
    Regression: with workspace_layout set, each child ended up wrapped in its
    own workspace_layout container."""
    sway.cmd("workspace s1a")
    sway.open("a")
    sway.cmd("splitv")
    sway.open("b")
    sway.cmd("focus parent")
    sway.cmd("move container to workspace s1b")
    sway.cmd("workspace s1a")
    if wl == "default":
        # the source workspace keeps the layout the moved split gave it
        expected = """
            ws s1a [splitv]
            ws s1b [splitv]
              view a
              view b
        """
    else:
        expected = f"""
            ws s1a [splith]
            ws s1b [splith]
              con [{wl}]
                con [splitv]
                  view a
                  view b
        """
    return ["s1a", "s1b"], expected


def scenario_move_workspace_contents(sway, wl):
    """Moving a whole workspace's contents (focus on the workspace itself) to
    an empty workspace reproduces it without extra wrapping."""
    sway.cmd("workspace s2a")
    sway.open("c")
    sway.open("d")
    sway.cmd("focus parent")
    sway.cmd("focus parent")
    sway.cmd("move container to workspace s2b")
    if wl == "default":
        expected = """
            ws s2a [splith]
            ws s2b [splith]
              view c
              view d
        """
    else:
        expected = f"""
            ws s2a [splith]
            ws s2b [splith]
              con [{wl}]
                view c
                view d
        """
    return ["s2a", "s2b"], expected


def scenario_move_nested_container_out(sway, wl):
    """Moving a nested workspace_layout container out to the workspace level
    does not wrap it in a redundant copy of itself."""
    nested = "splitv" if wl == "default" else wl
    sway.cmd("workspace s3")
    sway.open("e")
    sway.open("f")
    sway.cmd("splitv")
    sway.cmd(f"layout {nested}")
    sway.cmd("focus parent")
    sway.cmd("move right")
    if wl == "default":
        expected = """
            ws s3 [splith]
              view e
              con [splitv]
                view f
        """
    else:
        expected = f"""
            ws s3 [splith]
              con [{wl}]
                view e
              con [{wl}]
                view f
        """
    return ["s3"], expected


def scenario_move_view_to_empty_workspace(sway, wl):
    sway.cmd("workspace s4a")
    sway.open("g")
    sway.cmd("move container to workspace s4b")
    if wl == "default":
        expected = """
            ws s4a [splith]
            ws s4b [splith]
              view g
        """
    else:
        expected = f"""
            ws s4a [splith]
            ws s4b [splith]
              con [{wl}]
                view g
        """
    return ["s4a", "s4b"], expected


def scenario_move_view_out_of_container(sway, wl):
    sway.cmd("workspace s5")
    sway.open("h")
    sway.open("i")
    sway.cmd("move right")
    if wl == "default":
        expected = """
            ws s5 [splith]
              view h
              view i
        """
    else:
        expected = f"""
            ws s5 [splith]
              con [{wl}]
                view h
              con [{wl}]
                view i
        """
    return ["s5"], expected


def scenario_vtab_stress(sway, wl):
    """Focus, scroll, move and close within a large vtabbed container. Only
    checks that sway survives and the tree stays sane."""
    sway.cmd("workspace s6a")
    sway.open("t1")
    sway.cmd("layout vtabbed")
    for i in range(2, 11):
        sway.open(f"t{i}")
    for d in ["up"] * 3 + ["down"] * 2 + ["up"] * 5 + ["down"]:
        sway.cmd(f"focus {d}")
    sway.cmd("vtab_scroll down 3")
    sway.cmd("vtab_scroll up 1")
    sway.cmd("vtab_position right")
    sway.cmd("move right")
    sway.cmd("move left")
    sway.cmd("focus parent")
    sway.cmd("move container to workspace s6b")
    sway.cmd("workspace s6b")
    sway.cmd('[app_id="t3"] kill')
    sway.cmd('[app_id="t7"] kill')
    sway.cmd('[app_id="t5"] focus')
    sway.cmd("vtab_position left")
    sway.cmd("workspace s6a")
    time.sleep(0.5)  # let animations and client teardown run
    tree = sway.tree()
    for app_id in ["t1", "t2", "t4", "t5", "t6", "t8", "t9", "t10"]:
        if not find_view(tree, app_id):
            raise TestFailure(f"view {app_id} disappeared")
    return None, None


SCENARIOS = [
    scenario_move_split_to_empty_workspace,
    scenario_move_workspace_contents,
    scenario_move_nested_container_out,
    scenario_move_view_to_empty_workspace,
    scenario_move_view_out_of_container,
    scenario_vtab_stress,
]

WORKSPACE_LAYOUTS = ["vtabbed", "tabbed", "stacking", "default"]
ANIMATION_DURATIONS = [0, 200]


# Decoration styles every configuration also runs under: the defaults, and
# the framed vtab style (tab_frame plus a titlebar_indicator selection rule).
STYLES = {
    "default": "",
    "framed": textwrap.dedent("""\
        tab_frame enable
        titlebar_indicator 3 2
        titlebar_border_thickness 0
        default_border pixel 1
        """),
}


def make_config(wl, anim, style="default"):
    return textwrap.dedent(f"""\
        workspace_layout {wl}
        default_orientation horizontal
        animation_duration_ms {anim}
        corner_radius 10
        gaps inner 10
        shadows on
        blur on
        """) + STYLES[style]


def run_config(args, wl, anim, style):
    label = f"workspace_layout={wl} animation_duration_ms={anim} style={style}"
    print(f"== {label}")
    sway = Sway(args.sway, make_config(wl, anim, style), verbose=args.verbose)
    failures = 0
    try:
        for scenario in SCENARIOS:
            if args.filter and args.filter not in scenario.__name__:
                continue
            name = scenario.__name__.removeprefix("scenario_")
            try:
                names, expected = scenario(sway, wl)
                if names is not None:
                    # stacking reports its layout as "stacked" over IPC
                    expected = textwrap.dedent(expected).strip() \
                        .replace("[stacking]", "[stacked]")
                    actual = format_workspaces(sway.tree(), names)
                    if actual != expected:
                        raise TestFailure(
                            "tree mismatch\n--- expected\n" + expected
                            + "\n--- actual\n" + actual)
                errors = sway.sanitizer_errors()
                if errors:
                    raise TestFailure("sanitizer error\n" + errors)
                print(f"  ok   {name}")
            except TestFailure as e:
                failures += 1
                print(f"  FAIL {name}")
                print(textwrap.indent(str(e), "       "))
                if sway.proc.poll() is not None:
                    break
    finally:
        sway.stop(keep_tmpdir=failures > 0)
    if failures:
        print(f"  log: {sway.log_path}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sway", default="build/sway/sway",
        help="sway binary to test (default: build/sway/sway)")
    parser.add_argument("-k", dest="filter",
        help="only run scenarios whose name contains this string")
    parser.add_argument("-v", "--verbose", action="store_true",
        help="print each IPC command")
    args = parser.parse_args()

    if not os.access(args.sway, os.X_OK):
        sys.exit(f"sway binary not found: {args.sway}")
    if not shutil.which("foot"):
        sys.exit("foot is required to open test windows")

    failures = 0
    for wl in WORKSPACE_LAYOUTS:
        for anim in ANIMATION_DURATIONS:
            for style in STYLES:
                failures += run_config(args, wl, anim, style)
    print(f"\n{failures} failure(s)" if failures else "\nall passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
