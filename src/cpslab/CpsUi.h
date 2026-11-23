/* CPS Lab: UI glue (toolbar / command handlers) */

#pragma once

struct MainWindow;
struct WindowTab;

namespace cpslab {

// handles CmdSelectPins / CmdSelectCells / CmdSelectNets
void OnSelectCommand(MainWindow* win, WindowTab* tab, int cmdId);

} // namespace cpslab
