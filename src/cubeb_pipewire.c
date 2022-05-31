#include "cubeb-internal.h"
#include "cubeb/cubeb.h"
#include "cubeb_mixer.h"
#include "cubeb_strings.h"
#include <assert.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <pthread.h>
#include <spa/param/audio/format-utils.h>
#include <stdlib.h>

#ifdef DISABLE_LIBPIPEWIRE_DLOPEN
#define WRAP(x) x
#else
#define WRAP(x) (*cubeb_##x)
#define LIBPULSE_API_VISIT(X)                                                  \
  X(pa_channel_map_can_balance)                                                \

#define MAKE_TYPEDEF(x) static typeof(x) * cubeb_##x;
LIBPIPEWIRE_API_VISIT(MAKE_TYPEDEF);

#undef MAKE_TYPEDEF
#endif

#define M_PI_M2 (M_PI + M_PI)

#define DEFAULT_RATE 16000
#define DEFAULT_CHANNELS 2
#define DEFAULT_VOLUME 0.7

static struct cubeb_ops const pipewire_ops;

struct cubeb {
  struct cubeb_ops const * ops;
  void * libpipewire;
  struct pw_core * core;
  struct pw_thread_loop * mainloop;
  struct pw_context * context;
  struct cubeb_default_sink_info * default_sink_info;
  char * context_name;
  int error;
  cubeb_device_collection_changed_callback output_collection_changed_callback;
  void * output_collection_changed_user_ptr;
  cubeb_device_collection_changed_callback input_collection_changed_callback;
  void * input_collection_changed_user_ptr;
  cubeb_strings * device_ids;
  struct pw_properties * props;
};

struct cubeb_stream {
  /* Note: Must match cubeb_stream layout in cubeb.c. */
  cubeb * context;
  void * user_ptr;
  double accumulator;
  struct spa_hook stream_listener;
  int shutdown;

  /**/
  // pw_stream * output_stream;
  // pw_stream * input_stream;
  // pthread_t thread;
  // pthread_mutex_t mutex;
  struct pw_stream * stream;
  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  // pw_time_event * drain_timer;
  // pw_sample_spec output_sample_spec;
  // pw_sample_spec input_sample_spec;
  // int shutdown;
  float volume;
  cubeb_state state;
};

static void
on_process(void * userdata)
{
//   void * buffer;
//   long got;

// //   for (int c = 0; c < num_channels; ++c) {
// //     float freq = get_frequency(c);
// //     float phase_inc = 2.0 * M_PI * freq / sample_rate;
// //     for (long n = 0; n < nframes; ++n) {
// //       audiobuffer[n * num_channels + c] =
// //           ConvertSample<T>(sin(phase[c]) * VOLUME);
// //       phase[c] += phase_inc;
// //     }
// //   }
// // }
//   got = stm->data_callback(stm, stm->user_ptr, NULL, buffer, n_frames * stride);
//   if (got < 0) {
//     return;
//   }

//   // print double buffer
//   // for (i = 0; i < sizeof(buffer); i++) {
//   // double vali = buffer;
//   // printf(buffer);


//   buf = b->buffer;
//   if ((dst = buf->datas[0].data) == NULL)
//     return;

//   stride = sizeof(int16_t) * DEFAULT_CHANNELS;
//   n_frames = buf->datas[0].maxsize / stride;


//   for (i = 0; i < n_frames; i++) {
//     // stm->accumulator += M_PI_M2 * 440 / DEFAULT_RATE;
//     // if (stm->accumulator >= M_PI_M2)
//     //   stm->accumulator -= M_PI_M2;
//     // val = rand();
//     // val = sin(stm->accumulator) * DEFAULT_VOLUME * 16767.f;
//     // val = ((float *)buffer)[i];
//     val = 1000;
//     printf("Hello world\n");
//     // printf(buffer[i].to_string().c_str());
//     for (c = 0; c < DEFAULT_CHANNELS; c++)
//       *dst++ = val;
//   }

  void * buffer;
  size_t size;
  int r;
  long got;
  size_t towrite, read_offset;
  size_t frame_size;
  struct cubeb_stream * stm = userdata;
  struct pw_buffer * b;
  struct spa_buffer * buf;
  int i, c, n_frames, stride;
  int16_t *dst, val;
  size_t nbytes;

  if ((b = pw_stream_dequeue_buffer(stm->stream)) == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }

  buf = b->buffer;
  if ((dst = buf->datas[0].data) == NULL)
    return;

  stride = sizeof(int16_t) * DEFAULT_CHANNELS;
  n_frames = buf->datas[0].maxsize / stride;
  frame_size = sizeof(int16_t) * DEFAULT_CHANNELS;
  // assert(nbytes % frame_size == 0);

  towrite = nbytes;
  read_offset = 0;
  // while (towrite) {
  //   printf("Hello world\n");
  //   size = towrite;
  //   // r = WRAP(pa_stream_begin_write)(s, &buffer, &size);
  //   // Note: this has failed running under rr on occassion - needs
  //   // investigation.
  //   assert(r == 0);
  //   assert(size > 0);
  //   assert(size % frame_size == 0);

  //   LOGV("Trigger user callback with output buffer size=%zd, read_offset=%zd",
  //        size, read_offset);
  //   got = stm->data_callback(stm, stm->user_ptr,
  //                            /*(uint8_t const *)input_data + read_offset*/ NULL, buffer,
  //                            size / frame_size);
  //   if (got < 0) {
  //     stm->shutdown = 1;
  //     stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_ERROR);
  //     return;
  //   }

  for (i = 0; i < n_frames; i++) {
    stm->accumulator += M_PI_M2 * 440 / DEFAULT_RATE;
    if (stm->accumulator >= M_PI_M2)
      stm->accumulator -= M_PI_M2;
    val = sin(stm->accumulator) * DEFAULT_VOLUME * 16767.f;
    for (c = 0; c < DEFAULT_CHANNELS; c++)
    *dst++ = val;
  }

    // If more iterations move offset of read buffer
    // if (input_data) {
    //   size_t in_frame_size = WRAP(pa_frame_size)(&stm->input_sample_spec);
    //   read_offset += (size / frame_size) * in_frame_size;
    // }

    // if (stm->volume != PULSE_NO_GAIN) {
    //   uint32_t samples = size * stm->output_sample_spec.channels / frame_size;

    //   if (stm->output_sample_spec.format == PA_SAMPLE_S16BE ||
    //       stm->output_sample_spec.format == PA_SAMPLE_S16LE) {
    //     short * b = buffer;
    //     for (uint32_t i = 0; i < samples; i++) {
    //       b[i] *= stm->volume;
    //     }
    //   } else {
    //     float * b = buffer;
    //     for (uint32_t i = 0; i < samples; i++) {
    //       b[i] *= stm->volume;
    //     }
    //   }
    // }

    // r = WRAP(pa_stream_write)(s, buffer, got * frame_size, NULL, 0,
    //                           PA_SEEK_RELATIVE);

    // assert(r == 0);

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = n_frames * stride;

    pw_stream_queue_buffer(stm->stream, b);
    // if ((size_t)got < size / frame_size) {
    //   pa_usec_t latency = 0;
    //   r = WRAP(pa_stream_get_latency)(s, &latency, NULL);
    //   if (r == -PA_ERR_NODATA) {
    //     /* this needs a better guess. */
    //     latency = 100 * PA_USEC_PER_MSEC;
    //   }
    //   assert(r == 0 || r == -PA_ERR_NODATA);
    //   /* pa_stream_drain is useless, see PA bug# 866. this is a workaround. */
    //   /* arbitrary safety margin: double the current latency. */
    //   assert(!stm->drain_timer);
    //   stm->drain_timer = WRAP(pa_context_rttime_new)(
    //       stm->context->context, WRAP(pa_rtclock_now)() + 2 * latency,
    //       stream_drain_callback, stm);
    //   stm->shutdown = 1;
    //   return;
    // }

  //   towrite -= size;
  // }

  // assert(towrite == 0);
}
/* [on_process] */

static void on_control_info()
{
  
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    .control_info = on_control_info
};

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

  ctx->context_name = context_name ? strdup(context_name) : NULL;
  ctx->mainloop = pw_thread_loop_new("ao-pipewire", NULL);
  pw_thread_loop_lock(ctx->mainloop);
  if(ctx->mainloop == NULL) {
    return CUBEB_ERROR;
  }
  if(pw_thread_loop_start(ctx->mainloop) < 0) {
    return CUBEB_ERROR;
  }
  ctx->context = pw_context_new(pw_thread_loop_get_loop(ctx->mainloop), NULL, 0);
  ctx->ops = &pipewire_ops;
  ctx->core = pw_context_connect(ctx->context, NULL, 0);
  ctx->props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "Movie", PW_KEY_NODE_NAME, ctx->context_name,
      PW_KEY_NODE_DESCRIPTION, ctx->context_name, PW_KEY_APP_NAME,
      ctx->context_name, PW_KEY_APP_ID, ctx->context_name, PW_KEY_APP_ICON_NAME,
      ctx->context_name, PW_KEY_NODE_ALWAYS_PROCESS, "true", NULL);
  if(!ctx->core) {
    return CUBEB_ERROR;
  }
  // pw_main_loop_destroy(ctx->mainloop);
  *context = ctx;

  printf("pipewire_init done\n");
  return CUBEB_OK;
}

// static int
// create_pa_stream(cubeb_stream * stm, pa_stream ** pa_stm,
//                  cubeb_stream_params * stream_params, char const * stream_name)
// {
//   assert(stm && stream_params);
//   assert(&stm->input_stream == pa_stm ||
//          (&stm->output_stream == pa_stm &&
//           (stream_params->layout == CUBEB_LAYOUT_UNDEFINED ||
//            (stream_params->layout != CUBEB_LAYOUT_UNDEFINED &&
//             cubeb_channel_layout_nb_channels(stream_params->layout) ==
//                 stream_params->channels))));
//   if (stream_params->prefs & CUBEB_STREAM_PREF_LOOPBACK) {
//     return CUBEB_ERROR_NOT_SUPPORTED;
//   }
//   *pa_stm = NULL;
//   pa_sample_spec ss;
//   ss.format = to_pulse_format(stream_params->format);
//   if (ss.format == PA_SAMPLE_INVALID)
//     return CUBEB_ERROR_INVALID_FORMAT;
//   ss.rate = stream_params->rate;
//   if (stream_params->channels > UINT8_MAX)
//     return CUBEB_ERROR_INVALID_FORMAT;
//   ss.channels = (uint8_t)stream_params->channels;

//   if (stream_params->layout == CUBEB_LAYOUT_UNDEFINED) {
//     pa_channel_map cm;
//     if (stream_params->channels <= 8 &&
//         !WRAP(pa_channel_map_init_auto)(&cm, stream_params->channels,
//                                         PA_CHANNEL_MAP_DEFAULT)) {
//       LOG("Layout undefined and PulseAudio's default layout has not been "
//           "configured, guess one.");
//       layout_to_channel_map(
//           pulse_default_layout_for_channels(stream_params->channels), &cm);
//       *pa_stm =
//           WRAP(pa_stream_new)(stm->context->context, stream_name, &ss, &cm);
//     } else {
//       LOG("Layout undefined, PulseAudio will use its default.");
//       *pa_stm =
//           WRAP(pa_stream_new)(stm->context->context, stream_name, &ss, NULL);
//     }
//   } else {
//     pa_channel_map cm;
//     layout_to_channel_map(stream_params->layout, &cm);
//     *pa_stm = WRAP(pa_stream_new)(stm->context->context, stream_name, &ss, &cm);
//   }
//   return (*pa_stm == NULL) ? CUBEB_ERROR : CUBEB_OK;
// }

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
  const struct spa_pod * params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  // pa_buffer_attr battr;

  int r;

  assert(context);

  // If the connection failed for some reason, try to reconnect
  // if (context->error == 1 && pulse_context_init(context) != 0) {
  //   return CUBEB_ERROR;
  // }


  *stream = NULL;

  // pthread_mutex_lock(&ctx->mutex);
  // if (ctx->active_streams >= CUBEB_STREAM_MAX) {
  //   pthread_mutex_unlock(&ctx->mutex);
  //   return CUBEB_ERROR;
  // }
  // ctx->active_streams += 1;
  // pthread_mutex_unlock(&ctx->mutex);

  stm = calloc(1, sizeof(*stm));
  assert(stm);

  stm->context = context;
  stm->data_callback = data_callback;
  stm->state_callback = state_callback;
  stm->user_ptr = user_ptr;
  stm->volume = 1.0;
  stm->state = -1;

  // cubeb_stream * stm = u;

  stm->stream = pw_stream_new(context->core, "cubeb", context->props);
  // stm->stream = pw_stream_new_simple(
  //     pw_main_loop_get_loop(context->mainloop), "audio-src",
  //      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
  //                                     PW_KEY_MEDIA_CATEGORY, "Playback",
  //                                     PW_KEY_MEDIA_ROLE, "Music", NULL),
  //     &stream_events, &stm);

  params[0] = spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_S16,
                               .channels = DEFAULT_CHANNELS,
                               .rate = DEFAULT_RATE));
  // direction: argc > 1 ? (uint32_t)atoi(argv[1]) : PW_ID_ANY
  pw_stream_connect(stm->stream, PW_DIRECTION_OUTPUT, 0,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);
  pw_stream_add_listener(stm->stream, &stm->stream_listener, &stream_events,
                         stm);
  pw_thread_loop_unlock(context->mainloop);
  // assert(stm->shutdown == 0);

  // r = pthread_mutex_init(&stm->mutex, NULL);
  // assert(r == 0);

  // r = pthread_cond_init(&stm->cond, NULL);
  // assert(r == 0);

  // WRAP(pa_threaded_mainloop_lock)(stm->context->mainloop);
  // if (output_stream_params) {
  //   r = create_pa_stream(stm, &stm->output_stream, output_stream_params,
  //                        stream_name);
  //   if (r != CUBEB_OK) {
  //     // WRAP(pa_threaded_mainloop_unlock)(stm->context->mainloop);
  //     pipewire_stream_destroy(stm);
  //     return r;
  //   }

  //   stm->output_sample_spec =
  //       *(WRAP(pa_stream_get_sample_spec)(stm->output_stream));

  //   WRAP(pa_stream_set_state_callback)
  //   (stm->output_stream, stream_state_callback, stm);
  //   WRAP(pa_stream_set_write_callback)
  //   (stm->output_stream, stream_write_callback, stm);

  //   battr = set_buffering_attribute(latency_frames, &stm->output_sample_spec);
  //   WRAP(pa_stream_connect_playback)
  //   (stm->output_stream, (char const *)output_device, &battr,
  //    PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING |
  //        PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY,
  //    NULL, NULL);
  // }

  // // Set up input stream
  // if (input_stream_params) {
  //   r = create_pa_stream(stm, &stm->input_stream, input_stream_params,
  //                        stream_name);
  //   if (r != CUBEB_OK) {
  //     WRAP(pa_threaded_mainloop_unlock)(stm->context->mainloop);
  //     pipewire_stream_destroy(stm);
  //     return r;
  //   }

  //   stm->input_sample_spec =
  //       *(WRAP(pa_stream_get_sample_spec)(stm->input_stream));

  //   WRAP(pa_stream_set_state_callback)
  //   (stm->input_stream, stream_state_callback, stm);
  //   WRAP(pa_stream_set_read_callback)
  //   (stm->input_stream, stream_read_callback, stm);

  //   battr = set_buffering_attribute(latency_frames, &stm->input_sample_spec);
  //   WRAP(pa_stream_connect_record)
  //   (stm->input_stream, (char const *)input_device, &battr,
  //    PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING |
  //        PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY);
  // }

  // r = wait_until_stream_ready(stm);
  // if (r == 0) {
  //   /* force a timing update now, otherwise timing info does not become valid
  //      until some point after initialization has completed. */
  //   r = stream_update_timing_info(stm);
  // }

  // WRAP(pa_threaded_mainloop_unlock)(stm->context->mainloop);

  // if (r != 0) {
  //   pipewire_stream_destroy(stm);
  //   return CUBEB_ERROR;
  // }

  // if (g_cubeb_log_level) {
  //   if (output_stream_params) {
  //     const pa_buffer_attr * output_att;
  //     output_att = WRAP(pa_stream_get_buffer_attr)(stm->output_stream);
  //     LOG("Output buffer attributes maxlength %u, tlength %u, prebuf %u, "
  //         "minreq %u, fragsize %u",
  //         output_att->maxlength, output_att->tlength, output_att->prebuf,
  //         output_att->minreq, output_att->fragsize);
  //   }

  //   if (input_stream_params) {
  //     const pa_buffer_attr * input_att;
  //     input_att = WRAP(pa_stream_get_buffer_attr)(stm->input_stream);
  //     LOG("Input buffer attributes maxlength %u, tlength %u, prebuf %u, minreq "
  //         "%u, fragsize %u",
  //         input_att->maxlength, input_att->tlength, input_att->prebuf,
  //         input_att->minreq, input_att->fragsize);
  //   }
  // }

  *stream = stm;
  
  LOG("Cubeb stream (%p) init successful.", *stream);

  return CUBEB_OK;
}

static void
pipewire_stream_destroy(cubeb_stream * stm);

static void
pipewire_stream_destroy(cubeb_stream * stm)
{
  printf("pipewire_stream_destroy\n");
  int r;
  cubeb * ctx;

  // assert(stm && (stm->state == INACTIVE || stm->state == ERROR ||
  //                stm->state == DRAINING));

  ctx = stm->context;
  // TODO
  pw_stream_destroy(stm);

  // if (stm->other_stream) {
  //   stm->other_stream->other_stream = NULL; // to stop infinite recursion
  //   alsa_stream_destroy(stm->other_stream);
  // }

  // pthread_mutex_lock(&stm->mutex);
  // if (stm->pcm) {
  //   if (stm->state == DRAINING) {
  //     WRAP(snd_pcm_drain)(stm->pcm);
  //   }
  //   alsa_locked_pcm_close(stm->pcm);
  //   stm->pcm = NULL;
  // }
  // free(stm->saved_fds);
  // pthread_mutex_unlock(&stm->mutex);
  // pthread_mutex_destroy(&stm->mutex);

  // r = pthread_cond_destroy(&stm->cond);
  // assert(r == 0);

  // alsa_unregister_stream(stm);

  // pthread_mutex_lock(&ctx->mutex);
  // assert(ctx->active_streams >= 1);
  // ctx->active_streams -= 1;
  // pthread_mutex_unlock(&ctx->mutex);

  // free(stm->buffer);

  free(stm);
}

static int
pipewire_stream_start(cubeb_stream * stm)
{
  stm->shutdown = 0;
  
  printf("pipewire_stream_start\n");

  LOG("Cubeb stream (%p) started successfully.", stm);
  return CUBEB_OK;
}

static int
pipewire_get_max_channel_count(cubeb * ctx, uint32_t * max_channels)
{
  (void)ctx;
  assert(ctx && max_channels);
  // if (!ctx->default_sink_info) return CUBEB_ERROR;

  // *max_channels = ctx->default_sink_info->channel_map.channels;
  *max_channels = 2;
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
  *latency_frames = 2048;

  return CUBEB_OK;
}

static void
pipewire_destroy(cubeb * ctx)
{
  printf("pipewire_destroy\n");
  // assert(!ctx->input_collection_changed_callback &&
  //        !ctx->input_collection_changed_user_ptr &&
  //        !ctx->output_collection_changed_callback &&
  //        !ctx->output_collection_changed_user_ptr);
  // free(ctx->context_name);
  // if (ctx->context) {
  //   pulse_context_destroy(ctx);
  // }

  // if (ctx->mainloop) {
  //   WRAP(pa_threaded_mainloop_stop)(ctx->mainloop);
  //   WRAP(pa_threaded_mainloop_free)(ctx->mainloop);
  // }

  // if (ctx->device_ids) {
  //   cubeb_strings_destroy(ctx->device_ids);
  // }

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

  if (!ctx->default_sink_info)
    return CUBEB_ERROR;

  *rate = 48000;

  return CUBEB_OK;
}

static int
pipewire_stream_stop(cubeb_stream * stm)
{
  printf("pipewire_stream_stop\n");
  // WRAP(pa_threaded_mainloop_lock)(stm->context->mainloop);
  stm->shutdown = 1;
  // // If draining is taking place wait to finish
  // while (stm->drain_timer) {
  //   WRAP(pa_threaded_mainloop_wait)(stm->context->mainloop);
  // }
  // WRAP(pa_threaded_mainloop_unlock)(stm->context->mainloop);

  // stream_cork(stm, CORK | NOTIFY);
  LOG("Cubeb stream (%p) stopped successfully.", stm);
  return CUBEB_OK;
}

static int
pipewire_stream_get_position(cubeb_stream * stm, uint64_t * position)
{
  printf("pipewire_stream_get_position\n");
  // int r, in_thread;
  // pa_usec_t r_usec;
  // uint64_t bytes;

  // if (!stm || !stm->output_stream) {
  //   return CUBEB_ERROR;
  // }

  // in_thread = WRAP(pa_threaded_mainloop_in_thread)(stm->context->mainloop);

  // if (!in_thread) {
  //   WRAP(pa_threaded_mainloop_lock)(stm->context->mainloop);
  // }
  // r = WRAP(pa_stream_get_time)(stm->output_stream, &r_usec);
  // if (!in_thread) {
  //   WRAP(pa_threaded_mainloop_unlock)(stm->context->mainloop);
  // }

  // if (r != 0) {
  //   return CUBEB_ERROR;
  // }

  // bytes = WRAP(pa_usec_to_bytes)(r_usec, &stm->output_sample_spec);
  // *position = bytes / WRAP(pa_frame_size)(&stm->output_sample_spec);

  return CUBEB_OK;
}

static int
pipewire_stream_set_volume(cubeb_stream * stm, float volume)
{
  cubeb * ctx;

  ctx = stm->context;
  stm->volume = volume;
  return CUBEB_OK;
}

static struct cubeb_ops const pipewire_ops = {
    .init = pipewire_init,
    .get_backend_id = pipewire_get_backend_id,
    .get_max_channel_count = pipewire_get_max_channel_count,
    .get_min_latency = pipewire_get_min_latency,
    .get_preferred_sample_rate = pipewire_get_preferred_sample_rate,
    .enumerate_devices = NULL,
    .device_collection_destroy = NULL,
    .destroy = pipewire_destroy,
    .stream_init = pipewire_stream_init,
    .stream_destroy = pipewire_stream_destroy,
    .stream_start = pipewire_stream_start,
    .stream_stop = pipewire_stream_stop,
    .stream_get_position = pipewire_stream_get_position,
    .stream_get_latency = NULL,
    .stream_get_input_latency = NULL,
    .stream_set_volume = pipewire_stream_set_volume,
    .stream_set_name = NULL,
    .stream_get_current_device = NULL,
    .stream_device_destroy = NULL,
    .stream_register_device_changed_callback = NULL,
    .register_device_collection_changed = NULL};
