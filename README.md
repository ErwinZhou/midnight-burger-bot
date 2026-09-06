# Midnight Burger Bot

Author: Yuchen Zhou

Design: Build burgers by driving a three-joint robot arm between six ingredient bins and a central assembly plate. Ingredient order matters: correct layers complete an order, while a wrong layer scraps the current stack.

Screen Shot:

![Screen Shot](screenshot.png)

How To Play:

The Step 2 build uses a fixed camera. Press **1–6** to select a bin position (its ingredient changes at runtime), **Enter** to pick, **R** to restart with a fresh order, and **Escape** to quit. No mouse is required. The green order board shows the recipe in numbered assembly order, with the next layer highlighted. There are no bin numbers, selection outlines, or ingredient-name overlays; slots run left to right. Step 3 will move the arm above a selected bin, then lower the gripper when Enter is pressed.

Orders contain 3–10 layers and may repeat fillings. Picks currently place ingredients immediately, then advance supplies after a short feedback pause. A wrong pick clears the stack and restarts the same recipe; completing an order starts a new one. Mechanical-arm movement, conveyor sliding, score, and timer are planned for subsequent steps. The arm is stationary in this build.

Build with `node Maekfile.js -q`. Run logic tests with `node Maekfile.js -q :test-logic`. To check the scene integration, build `node Maekfile.js -q dist/play-mode-test`, then run `./dist/play-mode-test objs/step2.png` (requires an OpenGL-capable desktop session; Windows executable has an `.exe` suffix).

Asset Pipeline:

The editable source is `scenes/burger.blend`. From `scenes/`, running `make` invokes the project's original Blender exporters to generate `dist/burger.pnct` for mesh data and `dist/burger.scene` for transforms, hierarchy, mesh instances, and the camera. Run `python3 scenes/validate-burger.py` from the repository root to check names, colors, hierarchy, work-envelope distances, ingredient alignment, triangle budget, camera count, and chunk structure.

The generated runtime assets are checked into `dist/`, so building or running the game does not require Blender. All Burger Bot geometry and vertex colors were created for this project.

This game was built with [NEST](NEST.md).
