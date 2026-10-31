# TinyCraft Editable Asset Packs

This directory is the human-readable source for `Tinycraft/assets.tca`.
It follows the Bedrock resource-pack / behavior-pack responsibility split:

- `resource_pack/` contains textures, client entity bindings, geometry, UI,
  sounds, particles, localization, and future visual animations.
- `behavior_pack/` contains blocks, items, entity behavior, spawning, loot,
  recipes, world-generation data, functions, and future scripts.

Run the converter against this directory. The NARM program never parses these
files during gameplay; it loads the generated binary cache instead.

Files use Bedrock-shaped JSON where practical. Fields under `tinycraft:*`
carry fixed runtime IDs or TinyCraft-specific values that Bedrock itself does not
define. Unsupported folders intentionally exist for future implementation.
