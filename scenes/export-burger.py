"""Run both course exporters from an interactive Blender session."""

import os
import sys
import contextlib
import traceback


HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "dist"))
BLEND = os.path.join(HERE, "burger.blend") + ":Main"


def run_export(script_name, arguments):
	script_path = os.path.join(HERE, script_name)
	previous_argv = sys.argv
	try:
		sys.argv = ["blender", "--"] + arguments
		namespace = {"__file__": script_path, "__name__": "__main__"}
		exec(compile(open(script_path).read(), script_path, "exec"), namespace)
	finally:
		sys.argv = previous_argv


os.makedirs(DIST, exist_ok=True)
status_path = os.path.join(HERE, "export-burger.log")
with open(status_path, "w") as status, contextlib.redirect_stdout(status), contextlib.redirect_stderr(status):
	try:
		run_export("export-meshes.py", [BLEND, os.path.join(DIST, "burger.pnct")])
		run_export("export-scene.py", [BLEND, os.path.join(DIST, "burger.scene")])
		print("MIDNIGHT_BURGER_BOT_EXPORT_COMPLETE")
	except BaseException:
		traceback.print_exc()
		raise
