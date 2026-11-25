# Vendored MAVLink headers

Generated C headers from https://github.com/mavlink/c_library_v2, pinned to
the commit recorded in `PIN`. Only the `ardupilotmega` dialect and the
dialects it includes (`common`, `standard`, `minimal`, `csAirLink`,
`cubepilot`, `icarous`, `loweheiser`, `uAvionix`) are kept, together with the
top-level helper headers. The generated `testsuite.h` files are dropped.

SkyRelay code includes these only through `skyrelay/mav/mavlink.hpp`.

## Updating the copy

```bash
git clone https://github.com/mavlink/c_library_v2 /tmp/c_library_v2
cd /tmp/c_library_v2 && git checkout <sha>
D=<repo>/third_party/mavlink
rm -rf "$D"/*/ "$D"/*.h
cp *.h "$D"/
for d in ardupilotmega common csAirLink cubepilot icarous loweheiser uAvionix minimal standard; do
  cp -r "$d" "$D"/
done
find "$D" -name testsuite.h -delete
```

Then update `PIN` with the new commit and date, and rebuild. If the
`ardupilotmega` dialect gains a new `#include`, copy that dialect too.
