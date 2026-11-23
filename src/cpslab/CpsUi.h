/* CPS Lab: UI glue (toolbar / command handlers) */

#pragma once

struct MainWindow;
struct WindowTab;
struct Flags;

namespace cpslab {

// handles CmdSelectPins / CmdSelectCells / CmdSelectNets
void OnSelectCommand(MainWindow* win, WindowTab* tab, int cmdId);

// restores the checked state of the Nets / Parts / Pins buttons from the tab's markers
void UpdateToolbarButtons(MainWindow* win);

// applies CPS Lab command-line flags (DDE service/topic names, document mode, text export)
void InitFromFlags(const Flags& flags);

// mouse callbacks to the user application via DDE (called from Canvas.cpp)
void OnDragStopped(MainWindow* win, int x, int y, bool didDragMouse);
void OnSelectionStopped(MainWindow* win, int x, int y);
void OnWordSelected(MainWindow* win);
void OnImageSelected(MainWindow* win, int x, int y);

} // namespace cpslab
