# MiniCAD Smoke Test

## P0 Manual Checks

1. Launch the application and verify the main window opens without an error dialog.
2. Press `L`, click two points, and confirm a line is created.
3. Continue clicking to create connected line segments, then right-click to end the current chain.
4. Hover an entity and confirm highlight feedback appears.
5. Click to select a single entity, then `Ctrl`+click to add or remove entities from the selection.
6. Drag left-to-right to test window selection, then drag right-to-left to test crossing selection.
7. Press `Delete` and confirm selected entities are removed.
8. Press `Ctrl+Z` and `Ctrl+Y` to verify undo and redo work after add and delete operations.
9. Press `Ctrl+S` on a new document and verify the save dialog appears.
10. Save a file, modify the scene, press `Ctrl+S` again, and verify it saves back to the same path.
11. Press `Ctrl+Shift+S` and verify Save As writes to a new path.
12. Press `Ctrl+O`, reopen the saved file, and verify entities reload and selection/tool preview are cleared.
13. Use middle mouse drag to pan and mouse wheel to zoom.

## P1 Layer Rules

Temporary layer hotkeys:
`Ctrl+Shift+N` create and activate a layer, `Ctrl+PageUp/PageDown` switch active layer,
`Ctrl+Shift+H` toggle active-layer visibility, `Ctrl+Shift+L` toggle active-layer lock,
`Ctrl+Shift+Delete` delete the active non-default layer.

1. Press `Ctrl+Shift+N`, confirm a new layer becomes active, and verify `Ctrl+Z` removes it and `Ctrl+Y` restores it.
2. Draw entities on the new layer, then use `Ctrl+PageUp/PageDown` to switch layers and verify `Ctrl+Z` / `Ctrl+Y` undo and redo the active-layer change.
3. Press `Ctrl+Shift+H` to hide the active layer and verify its entities are not rendered.
4. While the layer is hidden, verify its entities cannot be hovered, selected, or box-selected, then press `Ctrl+Z` / `Ctrl+Y` to restore and re-hide the layer.
5. Press `Ctrl+Shift+L` to lock the active layer and verify its entities remain visible but cannot be hovered, selected, or deleted.
6. With the layer locked, verify new line creation is rejected with an error instead of creating geometry, then press `Ctrl+Z` / `Ctrl+Y` to unlock and relock it.
7. Press `Ctrl+Shift+Delete` to delete the active non-default layer and verify its entities are reassigned to the default layer while the active layer falls back appropriately.
8. Press `Ctrl+Z` after deletion and verify the deleted layer is restored, reassigned entities return to it, and the previously active layer becomes active again.
