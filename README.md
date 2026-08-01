# Hopper Config Editor for Flipper Zero

Toggle Hopper Frequencies and Hopper Presets ON/OFF directly on your Flipper.

## What This Does
- Toggle Hopper_frequency: lines ON/OFF
- Toggle Hopping_Preset: lines ON/OFF
- Visual separation between frequencies and presets
- Preserves all other file content

## Installation
```bash
ufbt
cp dist/subghz_config_editor.fap /path/to/flipper/SD Card/ext/apps/SubGHz/
```

## Usage
1. Launch from Apps -> SubGHz -> Hopper Config Editor
2. UP/DOWN to navigate
3. LEFT/RIGHT to toggle ON/OFF
4. BACK to save and exit

## Supported Formats
- Hopper_frequency: 31
- Hopper_frequency: 433920000
- Hopping_Preset: AM650
- Commented: # Hopper_frequency: 433920000
