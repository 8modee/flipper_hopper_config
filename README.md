# Hopper Config Editor for Flipper Zero

Toggle Hopper Frequencies and Hopping Presets ON/OFF directly on your Flipper.

## What This Does

- Toggle Hopper_frequency entries ON/OFF
- Toggle Hopping_Preset entries ON/OFF  
- Uses Flipper Format File (FFF) API - compatible with D4C1-Labs ARF
- Visual separation between frequencies and presets
- Saves changes back to setting_user file

## Installation

### Build
```bash
ufbt
```

### Install
Copy `dist/subghz_config_editor.fap` to `/ext/apps/SubGHz/` on your Flipper.

## Usage

1. Launch from **Apps -> SubGHz -> Hopper Config Editor**
2. Use **UP/DOWN** to navigate
3. Use **LEFT/RIGHT** to toggle ON/OFF
4. Press **BACK** to save and exit

## Requirements

- Flipper Zero with D4C1-Labs ARF firmware
- setting_user file at /ext/subghz/assets/setting_user (FFF format)

## File Format

This app reads the FFF format used by D4C1-Labs ARF:
- Hopper_frequency: uint32 values
- Hopping_Preset: string values

## Troubleshooting

- If no items appear, verify your setting_user file exists at the correct path
- Check qFlipper logs for FFF parsing errors
- Ensure you're using D4C1-Labs ARF firmware