# Factory presets

Any `.xml` file here is embedded in the plugin and appears in the factory
bank. `fxme::PresetManager` keeps every embedded `*_xml` resource whose root
tag matches the APVTS state type and ignores the rest, so nothing else needs
registering.

To add one: set the plugin up as you want it, save it as a user preset, then
copy that file out of the user preset directory into this folder and re-run
CMake configure.

    ~/.config/ModalDish/Presets            (Linux)
    ~/Library/Application Support/ModalDish/Presets   (macOS)
    %APPDATA%\ModalDish\Presets            (Windows)

A preset carries the parameters *and* the plate geometry (the `SHAPE` child
folded into the state on save), but not the computed modes — those are
megabytes of mode shapes, and loading a preset recomputes them in the
background instead.
