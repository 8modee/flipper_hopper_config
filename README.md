# Hopper Config Editor for Flipper Zero

Toggle Hopper Frequencies and Hopping Presets ON/OFF directly on your Flipper.

## What This Does

- Toggle `Hopper_frequency` entries ON/OFF
- Toggle `Hopping_Preset` entries ON/OFF
- Also supports `Frequency:` and `Preset:` format for compatibility
- Preserves ALL other lines in your setting_user file (comments, custom presets, etc.)
- Visual separation between frequencies and presets
- Saves changes back to setting_user file
- Works with D4C1-Labs ARF firmware

## Installation

### Prerequisites
- Flipper Zero with D4C1-Labs ARF firmware
- `setting_user` file at `/ext/subghz/assets/setting_user` (FFF or text format)

### Build
```bash
# Install ufbt if you haven't already
python3 -m pip install ufbt

# Build the app
cd /path/to/this/repo
ufbt
```

The compiled `.fap` file will be in the `dist/` folder.

### Install
Copy `dist/subghz_config_editor.fap` to `/ext/apps/SubGHz/` on your Flipper's SD card.

## Usage

1. Launch from **Apps -> SubGHz -> Hopper Config Editor**
2. Use **UP/DOWN** to navigate through frequencies and presets
3. Use **LEFT/RIGHT** to toggle ON/OFF
4. Press **BACK** to save and exit

## File Format

This app reads and preserves the format used by D4C1-Labs ARF:
- `Hopper_frequency: 433920000` - enabled frequency
- `# Hopper_frequency: 433920000` - disabled frequency
- `Hopping_Preset: AM650` - enabled preset
- `# Hopping_Preset: AM650` - disabled preset

It also supports:
- `Frequency: 433920000` / `# Frequency: 433920000`
- `Preset: AM650` / `# Preset: AM650`

All other lines (comments, custom presets, etc.) are preserved as-is.

## Troubleshooting

- **No items appear**: Verify your `setting_user` file exists at `/ext/subghz/assets/setting_user`
- **Blank entries**: Your file might use a different format. The app supports `Hopper_frequency:`, `Hopping_Preset:`, `Frequency:`, and `Preset:` prefixes
- **Icon missing**: Ensure `icon.png` is in the same directory as `application.fam` and `subghz_config_editor.c`

## Project Structure

```
flipper_hopper_config/
├── application.fam      # App manifest
├── icon.png             # App icon (10x10 PNG)
├── subghz_config_editor.c  # Main source code
├── setting_user         # Example config file (for reference)
└── README.md            # This file
```

## Adding Custom Frequencies/Presets

Simply edit your `setting_user` file on your PC:
1. Add new lines with `Hopper_frequency: 123456789` or `Hopping_Preset: MyCustomPreset`
2. Use `#` at the start of a line to disable it
3. Save the file back to your Flipper
4. The app will automatically pick up the new entries on next launch

No recompilation needed!
