#pragma once

// Diaspora port: flight stick control of the menus. Outside the flight itself the stick moves the mouse cursor, the trigger
// clicks (second button = right click) and the hat presses the arrow keys. FSO_JOY_MENU=0 turns it off.
void joy_menu_update(int gamestate);
