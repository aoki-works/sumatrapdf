/* CPS Lab: UI glue (toolbar / command handlers) */

#pragma once

struct MainWindow;
struct WindowTab;

namespace cpslab {

// handles CmdSelectPins / CmdSelectCells / CmdSelectNets
void OnSelectCommand(MainWindow* win, WindowTab* tab, int cmdId);

// restores the checked state of the Nets / Parts / Pins buttons from the tab's markers
void UpdateToolbarButtons(MainWindow* win);

} // namespace cpslab
