# Fixed-tick test (stage 4: view, world objects, hands) — about 10 minutes

## Start
- Double-click `~/Projects/StalkerGameEngine/play/simtick-test.desktop` (or copy it to the desktop first).
- It loads a test save in Army Warehouses. It uses its own folder, settings and Wine prefix: your GAMMA install and saves are not touched.
- If nothing opens, a pop-up says why (most likely a benchmark of mine is running: try again in a few minutes).

## Switch
- **Numpad 9** = simulation at 30 per second. **Numpad 6** = 60. **Numpad 0** = off (the normal engine).
- It starts at 30. If a numpad key does nothing, open the console (~) and type `sim_tick 30`, `sim_tick 60` or `sim_tick 0`.

## Look at (at 30, at 60, and off to compare)
- **Mouse look:** turn slowly and fast. Smooth as off, or stepping/lag? No test before yours has moved a real mouse on this path, so this is the main question.
- **Hands and weapon:** draw, reload, aim in, sprint, walk up to a wall. Smooth? Anything jumping, detached or late?
- **Other stalkers and mutants walking:** smooth, or stepping? Do their weapons stay in their hands?
- **Shooting:** does the shot go where you aim? Does anything feel late (fire, reload, wheel, buttons)?

## Tell me
- Which feels best: 30, 60 or off? Would you play at 30?
- Anything that looked wrong, with which setting it was on.
- Known and not fixed yet, no need to report: your own body and shadow, tracers, particles and lights on moving things, and NPC animation all still step at the tick rate.
