# Instruments Implementation Guidelines

### Goal

To implement a Plugin that will provide instruments that can show any data in the nav\_data\_store

Plugin development information is in docs/plugin\_api.md

### UI

Instruments will be displayed either horizontally or vertically as an instrument bar. The instrument bar will be made up of multiple instrument tiles, either stacked horizontally or side by side vertically. 

The instrument bar should have a similar style to the nav\_display\_window window.

There will be a Plugin Settings Menu to configure the instruments (tiles) shown on the bar. That dialog will have a selector for horizontal or vertical, and a list of instruments that can be selected for inclusion, and reordered. The list of instruments will come from an xml file.

There will be an Instruments checkbox item (under plugins heading) on the main menu to turn the instruments display on or off.

The instrument bar will be scalable via the settings dialog. (touch friendly)

### Configuration

* There will be an xml file that the user can use to configure the available instrument tiles.
* The xml file will allow configuration of:
* Instrument name
* Path of data in nav\_data\_store
* Unit of data to display
* Conversion factor to apply to data
* Analog or digital display

  * If analog, the range of the analog gauge
  * If digital, the number of decimals to display

The default xml file should contain analog and digital versions of:

Course over ground

Speed over ground

Speed through water

Apparent Wind Speed

Apparent Wind Direction

### Implementation notes

Plugin will subscribe to nav\_data\_store

All interface elements must be touch friendly

Any scrollable elements should follow same pattern as ais\_target\_list\_dialog













