# Map overview assets

Publish each map and its map-specific particle resources in a map-specific
Workshop package. Keep reusable shared resources in the separate shared package.

## Optional server-side overview cache input

The plugin cannot render CS2 geometry or take a screenshot from the server. To
seed its local overview cache, place an overview image and matching world bounds
here before packaging:

```text
assets/maps/<map_name>_overview.png
assets/maps/<map_name>.overview.ini
```

The package builder copies these files to
`addons/cs2-openworld/maps/`. On the first load of that map, the plugin validates
and atomically copies the PNG to
`addons/cs2-openworld/cache/maps/<map_name>_overview.png`. The cached copy is
reused on later loads. PNGs must be no larger than 32 MiB or 8192 pixels per
side; map names may contain only ASCII letters, digits, `_`, and `-`. A
Workshop-style map path such as `workshop/12345/my_map` is normalized to
`workshop_12345_my_map` for the asset filenames.

Metadata is plain UTF-8/ASCII key-value text with all four world-space bounds:

```ini
min_x = -4096
min_y = -4096
max_x = 4096
max_y = 4096
```

The coordinates are Source world XY bounds for the full image extent. Image `u`
coordinates increase with world X; image `v` increases downward, so world max-Y
maps to the top edge. Missing or invalid bounds leave the image cacheable but
prevent accurate world-marker projection.

This cache is server-side only. It does not create a screenshot, compile a
Source 2 texture, download a file to clients, or make an arbitrary PNG visible
in a particle. A graphical particle map still needs a map-specific compiled
VTEX/VPCF resource published and mounted through Workshop. Do not ship map
resources in the plugin package as a substitute for that Workshop package.
