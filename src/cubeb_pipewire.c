#include "cubeb-internal.h"
#include "cubeb/cubeb.h"
#include "cubeb_mixer.h"
#include "cubeb_strings.h"
#include <assert.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <pthread.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <stdlib.h>

#ifdef DISABLE_LIBPIPEWIRE_DLOPEN
#define WRAP(x) x
#else
#define WRAP(x) (*cubeb_##x)
#define LIBPULSE_API_VISIT(X) X(pa_channel_map_can_balance)

#define MAKE_TYPEDEF(x) static typeof(x) * cubeb_##x;
LIBPIPEWIRE_API_VISIT(MAKE_TYPEDEF);

#undef MAKE_TYPEDEF
#endif

static struct cubeb_ops const pipewire_ops;

struct pw_cubeb_default_sink_info {
  uint32_t channels;
  uint32_t sample_spec_rate;
};

struct cubeb {
  struct cubeb_ops const * ops;
  void * libpipewire;
  struct pw_core * core;
  struct pw_thread_loop * mainloop;
  struct pw_context * context;
  struct pw_cubeb_default_sink_info * pw_cubeb_default_sink_info;
  char * context_name;
  int error;
  cubeb_device_collection_changed_callback output_collection_changed_callback;
  void * output_collection_changed_user_ptr;
  cubeb_device_collection_changed_callback input_collection_changed_callback;
  void * input_collection_changed_user_ptr;
  cubeb_strings * device_ids;
  struct pw_registry * registry;
  struct spa_hook registry_listener;
  cubeb_device_collection * collection;
  uint32_t total_devices;
};

struct cubeb_stream {
  /* Note: Must match cubeb_stream layout in cubeb.c. */
  cubeb * context;
  void * user_ptr;
  /**/
  struct spa_hook stream_listener;
  int shutdown;
  struct spa_audio_info_raw info;
  struct pw_properties * props;
  // pw_stream * output_stream;
  // pw_stream * input_stream;
  // pthread_t thread;
  // pthread_mutex_t mutex;
  struct pw_stream * stream;
  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  int stride;
  // pw_time_event * drain_timer;
  // pw_sample_spec output_sample_spec;
  // pw_sample_spec input_sample_spec;
  // int shutdown;
  float volume;
  cubeb_state state;
  enum spa_audio_format spa_format;
};

static void
registry_event_global(void * userdata, uint32_t id, uint32_t permissions,
                      const char * type, uint32_t version,
                      const struct spa_dict * props)
{
  const char * class = spa_dict_lookup(props, PW_KEY_DEVICE_CLASS);
  if (!class) {
    return;
  }
  if (!(strcmp(class, "Audio/Source") == 0 ||
        strcmp(class, "Audio/Sink") == 0)) {
    return;
  }
  const char * name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  const char * device_id = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
  const char * description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  const char * rate = spa_dict_lookup(props, PW_KEY_AUDIO_RATE);
  const char * channels = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNELS);
  // const char *id = spa_dict_lookup(props, PW_KEY_MEDIA_ID);
  printf("Found audio source: %s (%s)\n", name, description);
  // if () {
  //   printf("Found audio sink: %s (%s)\n", name, description);
  //   printf("class: %s\n", class);
  //   // printf("id: %s\n", id);
  // }
  // if(id == PW_ID_ANY){
  //         // printf("Found def: %s (%s)\n", name, description);
  //         // printf("class: %s\n", class);
  //         // printf("id: %s\n", id);
  // // }
  // printf("id: %d\n", id);
  // printf("PW_ID_ANY: %d\n", PW_ID_ANY);

  // get default device
  // PW_ID_ANY
  // {
  //         return;
  // }
  // if (name)
  // {
  //         printf("\tname: %s\n", name);
  //         printf("\tgroup: %s\n", description);
  //         printf("\tclass: %s\n", class);
  // }
  // if (strcmp(type, "Device") == 0) {
  // }

  cubeb_device_info * device;
  device->device_id = device_id;
  device->friendly_name = name;
  device->group_id = name;
  device->type = strcmp(class, "Audio/Source") == 0 ? CUBEB_DEVICE_TYPE_INPUT
                                                    : CUBEB_DEVICE_TYPE_OUTPUT;
  device->state = CUBEB_DEVICE_STATE_ENABLED;
  device->preferred = false;
  // device.format = SPA_AUDIO_FORMAT_S16;
  // device.default_format = SPA_AUDIO_FORMAT_S16;
  device->max_channels = atoi(channels);
  device->default_rate = atoi(rate);
  device->max_rate = atoi(rate);
  device->min_rate = atoi(rate);
  device->latency_lo = 0;
  device->latency_hi = 0;
  struct cubeb_stream * stm = userdata;
  struct cubeb * ctx = stm->context;
  ctx->total_devices--;
}

pipewire_enumerate_devices(cubeb * ctx, cubeb_device_type type,
                           cubeb_device_collection * collection)
{
  // collection->device = user_data.devinfo;
  collection->count = ctx->total_devices;
  return CUBEB_OK;
}

static void
on_process(void * userdata)
{
  printf("on_process\n");
  size_t size;
  int r;
  long got;
  size_t towrite, read_offset;
  size_t frame_size;
  struct cubeb_stream * stm = userdata;
  struct pw_buffer * b;
  struct spa_buffer * buf;
  int i, c, n_frames;
  size_t nbytes;
  void const * input_data;
  // void * output_data;
  struct spa_data * d;
  b = pw_stream_dequeue_buffer(stm->stream);

  if (b == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }
  buf = b->buffer;
  if (buf->datas[0].data == NULL) {
    return;
  }
  printf("on_process: got buffer\n");
  towrite = nbytes;
  read_offset = 0;
  int num_channels = stm->info.channels;
  if (stm->spa_format == SPA_AUDIO_FORMAT_S16) {
    stm->stride = sizeof(int16_t) * num_channels;
  } else {
    stm->stride = sizeof(int32_t) * num_channels;
  }

  n_frames = buf->datas[0].maxsize / stm->stride;
  got = stm->data_callback(stm, stm->user_ptr,
                           (uint8_t const *)input_data + read_offset,
                           buf->datas[0].data, n_frames);

  // if (input_data) {
  //   read_offset += (size / frame_size) * stm->stride;
  // }
  int sizebuf = n_frames * num_channels;
  if (stm->spa_format == SPA_AUDIO_FORMAT_S16) {
    short * dst = buf->datas[0].data;
    for (i = 0; i < sizebuf; i++) {
      dst[i] *= stm->volume;
    }
  } else {
    float * dst = buf->datas[0].data;
    for (i = 0; i < sizebuf; i++) {
      dst[i] *= stm->volume;
    }
  }
  if (got < 0) {
    stm->shutdown = 1;
    stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_ERROR);
    return;
  }

  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->stride = stm->stride;
  buf->datas[0].chunk->size = n_frames * stm->stride;
  printf("pw_stream_queue_buffer");
  pw_stream_queue_buffer(stm->stream, b);
}

static void
on_control_info()
{
  printf("on_control_info\n");
}

static void
on_param_changed(void * userdata, uint32_t id, const struct spa_pod * param)
{
  printf("on_param_changed\n");

  struct cubeb_stream * stm = userdata;
  // cubeb * ctx = stm->context;

  // return;
  if (param == NULL || id != SPA_PARAM_Format)
    return;

  // set stm->channels
  // struct spa_audio_info_raw audio_info;
  // spa_audio_info_raw_parse(&audio_info, param);
  // stm->info.channels = audio_info.channels;
  // if (spa_format_parse(param, &stm->info.media_type,
  // &stm->info.media_subtype) <
  //     0)
  //   return;

  // const struct spa_pod * params[1];
  // uint8_t buffer[1024];
  // struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  // // int buffer_size = stm->info.rate * stm->info.channels * sizeof(int16_t);
  // int buffer_size = 20;
  // params[0] = spa_pod_builder_add_object(
  //     &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
  //     SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
  //     SPA_PARAM_BUFFERS_size, SPA_POD_Int(buffer_size),
  //     SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stm->stride));

  // pw_stream_update_params(stm->stream, params, 1);
}

static void
on_state_changed(void * userdata, enum pw_stream_state old_state,
                 enum pw_stream_state new_state)
{
  printf("on_state_changed\n");
  if (new_state == PW_STREAM_STATE_ERROR) {
  }
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,          .process = on_process,
    .control_info = on_control_info,   .param_changed = on_param_changed,
    .state_changed = on_state_changed,
};

static void
registry_event_global_remove(void * userdata, uint32_t id, uint32_t permissions,
                             const char * type, uint32_t version,
                             const struct spa_dict * props)
{
  printf("registry_event_global_remove\n");
  // struct cubeb_stream * stm = userdata;
  // struct cubeb * ctx = stm->context;
  // ctx->total_devices--;
}
static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static void new_pipewire_props(cubeb_stream * stm, char const * name){
  stm->props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, NULL, PW_KEY_NODE_NAME, name, PW_KEY_NODE_DESCRIPTION,
      name, PW_KEY_APP_NAME, name, PW_KEY_APP_ID, name, PW_KEY_APP_ICON_NAME,
      name, PW_KEY_NODE_ALWAYS_PROCESS, "true", NULL);
}

int
pipewire_init(cubeb ** context, char const * context_name)
{
  printf("pipewire_init\n");

  void * libpipewire = NULL;
  cubeb * ctx;

  *context = NULL;
  // #ifndef DISABLE_LIBPIPEWIRE_DLOPEN
  //   libpipewire = dlopen("libpipewire-0.3.so.0", RTLD_LAZY);
  //   if (!libpipewire) {
  //     libpipewire = dlopen("libpipewire-0.3.so", RTLD_LAZY);
  //     if (!libpipewire) {
  //       return CUBEB_ERROR;
  //     }
  //   }

  // #define LOAD(x)                                                                \
//   {                                                                            \
//     cubeb_##x = dlsym(libpipewire, #x);                                        \
//     if (!cubeb_##x) {                                                          \
//       dlclose(libpipewire);                                                    \
//       return CUBEB_ERROR;                                                      \
//     }                                                                          \
//   }

  //   LIBPIPEWIRE_API_VISIT(LOAD);
  // #undef LOAD
  // #endif

  ctx = calloc(1, sizeof(*ctx));
  assert(ctx);

  ctx->ops = &pipewire_ops;
  ctx->libpipewire = libpipewire;

  pw_init(&ctx->mainloop, NULL);

  ctx->total_devices = 0;
  ctx->context_name = context_name ? strdup(context_name) : NULL;
  ctx->mainloop = pw_thread_loop_new("ao-pipewire", NULL);
  pw_thread_loop_lock(ctx->mainloop);
  if (ctx->mainloop == NULL) {
    return CUBEB_ERROR;
  }
  if (pw_thread_loop_start(ctx->mainloop) < 0) {
    return CUBEB_ERROR;
  }
  ctx->context =
      pw_context_new(pw_thread_loop_get_loop(ctx->mainloop), NULL, 0);
  ctx->ops = &pipewire_ops;
  ctx->core = pw_context_connect(ctx->context, NULL, 0);
  if (!ctx->core) {
    return CUBEB_ERROR;
  }

  ctx->registry = pw_core_get_registry(ctx->core, PW_VERSION_REGISTRY,
                                       0 /* user_data size */);

  spa_zero(ctx->registry_listener);
  pw_registry_add_listener(ctx->registry, &ctx->registry_listener,
                           &registry_events, NULL);
  *context = ctx;

  printf("pipewire_init done\n");
  return CUBEB_OK;
}

static enum spa_audio_format
spa_format(cubeb_sample_format format)
{
  switch (format) {
  case CUBEB_SAMPLE_S16LE:
    return SPA_AUDIO_FORMAT_S16;
  case CUBEB_SAMPLE_S16BE:
    return SPA_AUDIO_FORMAT_S16;
  case CUBEB_SAMPLE_FLOAT32LE:
    return SPA_AUDIO_FORMAT_F32;
  case CUBEB_SAMPLE_FLOAT32BE:
    return SPA_AUDIO_FORMAT_F32;
  default:
    return -1;
  }
}

static int
pipewire_stream_init(cubeb * context, cubeb_stream ** stream,
                     char const * stream_name, cubeb_devid input_device,
                     cubeb_stream_params * input_stream_params,
                     cubeb_devid output_device,
                     cubeb_stream_params * output_stream_params,
                     unsigned int latency_frames,
                     cubeb_data_callback data_callback,
                     cubeb_state_callback state_callback, void * user_ptr)
{
  printf("pipewire_stream_init\n");
  cubeb_stream * stm;
  (void)stream_name;

  assert(context);

  *stream = NULL;

  stm = calloc(1, sizeof(*stm));
  assert(stm);

  stm->context = context;
  stm->data_callback = data_callback;
  stm->state_callback = state_callback;
  stm->user_ptr = user_ptr;
  stm->volume = 1.0;
  stm->state = -1;

  *stream = stm;

  const struct spa_pod * params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  cubeb * ctx = stm->context;
  // pw_thread_loop_lock(ctx->mainloop);
  new_pipewire_props(stm, stream_name);
  printf("pipewire_stream_init: pw_thread_loop_lock\n");
  stm->stream = pw_stream_new(ctx->core, ctx->context_name, stm->props);
  stm->info.format = stm->spa_format = spa_format(output_stream_params->format);
  stm->info.rate = output_stream_params->rate;
  stm->info.channels = output_stream_params->channels;
  // printf("channels: %d\n", info.channels);
  // info.channels = output_stream_params;
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &stm->info);
  // direction: argc > 1 ? (uint32_t)atoi(argv[1]) : PW_ID_ANY
  pw_stream_connect(stm->stream, PW_DIRECTION_OUTPUT, 0,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);
  pw_stream_add_listener(stm->stream, &stm->stream_listener, &stream_events,
                         stm);

  pw_thread_loop_unlock(ctx->mainloop);

  pw_stream_set_active(stm->stream, false);

  LOG("Cubeb stream (%p) init successful.", *stream);

  return CUBEB_OK;
}

static void
pipewire_stream_destroy(cubeb_stream * stm);

static void
pipewire_stream_destroy(cubeb_stream * stm)
{
  printf("pipewire_stream_destroy\n");
  cubeb * ctx = stm->context;

  if (stm->stream) {
    return;
  }

  pw_thread_loop_lock(ctx->mainloop);

  pw_stream_destroy(stm->stream);
  stm->stream = NULL;

  pw_thread_loop_unlock(ctx->mainloop);

  free(stm);
}

static int
pipewire_stream_start(cubeb_stream * stm)
{
  stm->shutdown = 0;

  printf("pipewire_stream_start\n");

  pw_stream_set_active(stm->stream, true);
  LOG("Cubeb stream (%p) started successfully.", stm);
  return CUBEB_OK;
}

static int
pipewire_get_max_channel_count(cubeb * ctx, uint32_t * max_channels)
{
  (void)ctx;
  assert(ctx && max_channels);
  // if (!ctx->default_sink_info) return CUBEB_ERROR;
  *max_channels = 2;
  // *max_channels = ctx->default_sink_info->channel_map.channels;
  // *max_channels = ctx->pw_cubeb_default_sink_info->max_channels;
  return CUBEB_OK;
}

static char const *
pipewire_get_backend_id(cubeb * ctx)
{
  printf("pipewire_get_backend_id\n");
  return "pipewire";
}

static int
pipewire_get_min_latency(cubeb * ctx, cubeb_stream_params params,
                         uint32_t * latency_frames)
{
  (void)ctx;
  *latency_frames = 1;

  return CUBEB_OK;
}

static void
pipewire_destroy(cubeb * ctx)
{
  printf("pipewire_destroy\n");
  assert(!ctx->input_collection_changed_callback &&
         !ctx->input_collection_changed_user_ptr &&
         !ctx->output_collection_changed_callback &&
         !ctx->output_collection_changed_user_ptr);

  free(ctx->context_name);
  if (ctx->context) {
    pw_context_destroy(ctx->context);
  }
  if (ctx->mainloop) {
    pw_thread_loop_destroy(ctx->mainloop);
  }
  if (ctx->device_ids) {
    cubeb_strings_destroy(ctx->device_ids);
  }
  if (ctx->registry) {
    pw_proxy_destroy((struct pw_proxy *)ctx->registry);
  }
  // if (ctx->core){
  //   pw_core_disconnect(ctx->core);
  // }
  pw_deinit();
  // if (ctx->libpipewire) {
  //   dlclose(ctx->libpipewire);
  // }
  // free(ctx->default_sink_info);
  free(ctx);
}

static int
pipewire_get_preferred_sample_rate(cubeb * ctx, uint32_t * rate)
{
  printf("pipewire_get_preferred_sample_rate\n");
  assert(ctx && rate);
  (void)ctx;

  // if (!ctx->default_sink_info)
  //   return CUBEB_ERROR;

  // // pw_properties_get(props, PW_KEY_NODE_LATENCY) == NULL)
  // char * samplerate = pw_properties_get(ctx->props, PW_KEY_NODE_RATE);
  // printf("samplerate: %s\n", samplerate);
  *rate = 48000;

  return CUBEB_OK;
}

static int
pipewire_stream_stop(cubeb_stream * stm)
{
  printf("pipewire_stream_stop\n");
  pw_stream_set_active(stm->stream, false);
  LOG("Cubeb stream (%p) stopped successfully.", stm);
  return CUBEB_OK;
}

static int
pipewire_stream_get_position(cubeb_stream * stm, uint64_t * position)
{
  // Not sure if it works.
  int r;
  uint64_t bytes;
  printf("pipewire_stream_get_position\n");
  r = pw_stream_get_time(stm->stream, position);
  bytes = *position * stm->info.rate / 1000;
  *position = bytes;

  return CUBEB_OK;
}

static int
pipewire_stream_set_volume(cubeb_stream * stm, float volume)
{
  stm->volume = volume;
  return CUBEB_OK;
}

static int
pipewire_stream_get_current_device(cubeb_stream * stm,
                                   cubeb_device ** const device)
{
  *device = calloc(1, sizeof(cubeb_device));
  if (*device == NULL)
    return CUBEB_ERROR;

  // if (stm->inp) {
  //   const char * name = WRAP(pa_stream_get_device_name)(stm->input_stream);
  //   (*device)->input_name = (name == NULL) ? NULL : strdup(name);
  // }

  // if (stm->output_stream) {
  //   const char * name =
  //   WRAP(pa_stream_get_device_name)(stm->output_stream);
  //   (*device)->output_name = (name == NULL) ? NULL : strdup(name);
  // }

  return CUBEB_OK;
}

static int
pipewire_stream_set_name(cubeb_stream * stm, char const * stream_name)
{
  if (!stm) {
    return CUBEB_ERROR;
  }

  new_pipewire_props(stm, stream_name);
  pw_stream_update_properties(stm->stream, stm->props);
  return CUBEB_OK;
}

static int
pipewire_device_collection_destroy(cubeb * ctx,
                                cubeb_device_collection * collection)
{
  size_t n;

  for (n = 0; n < collection->count; n++) {
    free((void *)collection->device[n].friendly_name);
    free((void *)collection->device[n].vendor_name);
    free((void *)collection->device[n].group_id);
  }

  free(collection->device);
  return CUBEB_OK;
}

static int
pipewire_stream_get_latency(cubeb_stream * stm, uint32_t * latency)
{
  *latency = 0;
  return CUBEB_OK;
}

static int
pipewire_stream_get_latency_input(cubeb_stream * stm, uint32_t * latency){
  *latency = 0;
  return CUBEB_OK;
}

static int
pipewire_stream_device_destroy(cubeb_stream * stream, cubeb_device * device)
{
  (void)stream;
  free(device->input_name);
  free(device->output_name);
  free(device);
  return CUBEB_OK;
}

static struct cubeb_ops const pipewire_ops = {
    .init = pipewire_init,
    .get_backend_id = pipewire_get_backend_id,
    .get_max_channel_count = pipewire_get_max_channel_count,
    .get_min_latency = pipewire_get_min_latency,
    .get_preferred_sample_rate = pipewire_get_preferred_sample_rate,
    .enumerate_devices = pipewire_enumerate_devices,
    .device_collection_destroy = pipewire_device_collection_destroy,
    .destroy = pipewire_destroy,
    .stream_init = pipewire_stream_init,
    .stream_destroy = pipewire_stream_destroy,
    .stream_start = pipewire_stream_start,
    .stream_stop = pipewire_stream_stop,
    .stream_get_position = pipewire_stream_get_position,
    .stream_get_latency = pipewire_stream_get_latency,
    .stream_get_input_latency = pipewire_stream_get_latency_input,
    .stream_set_volume = pipewire_stream_set_volume,
    .stream_set_name = pipewire_stream_set_name,
    .stream_get_current_device = pipewire_stream_get_current_device,
    .stream_device_destroy = pipewire_stream_device_destroy,
    .stream_register_device_changed_callback = NULL,
    .register_device_collection_changed = NULL};
