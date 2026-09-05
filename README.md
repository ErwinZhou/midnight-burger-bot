# Midnight Burger Bot

Author: Yuchen Zhou

Design: Build burgers by driving a three-joint robot arm between six ingredient bins and a central assembly plate. Ingredient order matters: correct layers complete an order, while a wrong layer scraps the current stack.

Screen Shot:

![Screen Shot](screenshot.png)

How To Play:

The current asset-integration build displays the complete Burger Bot work cell and animates all three arm joints. Click the window to capture the mouse, move the mouse to rotate the camera, use **W/A/S/D** to move, and press **Escape** to release the mouse.

The planned gameplay controls are **1–6** to select an ingredient bin, **Space** to run the pick-and-place sequence, and **R** to restart. These gameplay controls, recipe logic, stacking, score, and timer are not implemented yet.

Asset Pipeline:

The editable source is `scenes/burger.blend`. From `scenes/`, running `make` invokes the project's original Blender exporters to generate `dist/burger.pnct` for mesh data and `dist/burger.scene` for transforms, hierarchy, mesh instances, and the camera. Run `python3 scenes/validate-burger.py` from the repository root to check names, colors, hierarchy, work-envelope distances, ingredient alignment, triangle budget, camera count, and chunk structure.

The generated runtime assets are checked into `dist/`, so building or running the game does not require Blender. All Burger Bot geometry and vertex colors were created for this project.

This game was built with [NEST](NEST.md).
