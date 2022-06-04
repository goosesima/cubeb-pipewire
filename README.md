# Cubeb with experimental PipeWire support (WIP)
## TODO
### cubeb_pipewire.c 
pipewire_init - wip
pipewire_get_backend_id - done
pipewire_get_max_channel_count - wip
pipewire_get_min_latency - wip
pipewire_get_preferred_sample_rate - wip
pipewire_enumerate_devices - wip
pipewire_device_collection_destroy - wip
pipewire_destroy - wip
pipewire_stream_init - input support
pipewire_stream_destroy - input support
pipewire_stream_start - input support
pipewire_stream_stop - input support
pipewire_stream_get_position - wip
pipewire_stream_get_latency - no progress
pipewire_stream_get_latency_input - when input support done
pipewire_stream_set_volume - done
pipewire_stream_set_name - almost done
pipewire_stream_get_current_device - when pipewire_enumerate_devices done
pipewire_stream_device_destroy - no progress
stream_register_device_changed_callback - no progress
register_device_collection_changed - no progress
### Other 
- PipeWire dependencies CMakeLists.txt
- Pass all tests.
- WRAP(x) (when CMakeLists fixed)
- Add pipewire to [wiki](https://github.com/mozilla/cubeb/wiki/Backend-Support).
## Passed tests:
- [ ] test_devices
- [ ] test_resampler
- [x] test_tone
- [x] test_audio
- [ ] test_duplex
- [ ] test_ring_array
- [ ] test_utils
- [ ] cubeb-test
- [ ] test_callback_ret
- [ ] test_latency
- [ ] test_ring_buffer

[![Build Status](https://github.com/mozilla/cubeb/actions/workflows/build.yml/badge.svg)](https://github.com/mozilla/cubeb/actions/workflows/build.yml)

See INSTALL.md for build instructions.

See [Backend Support](https://github.com/mozilla/cubeb/wiki/Backend-Support) in the wiki for the support level of each backend.

Licensed under an ISC-style license.  See LICENSE for details.
