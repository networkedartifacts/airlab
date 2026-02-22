#include <stdlib.h>
#include <string.h>
#include <naos.h>
#include <naos/msg.h>
#include <naos/fs.h>
#include <naos/ble.h>
#include <naos/wifi.h>
#include <naos/mqtt.h>
#include <naos/auth.h>
#include <naos/debug.h>
#include <naos/sys.h>
#include <esp_err.h>

#include <al/sensor.h>
#include <al/store.h>
#include <al/storage.h>
#include <al/clock.h>

#include "com.h"
#include "sig.h"

#define ENDPOINT 0xA1

// TODO: Only send discovery when the HA MQTT service comes online.

typedef enum {
  COM_CMD_SENSOR_READ = 0x1,
  COM_CMD_ENGINE_LAUNCH = 0x2,
  COM_CMD_ENGINE_KILL = 0x3,
  COM_CMD_ENGINE_LOG_START = 0x4,
  COM_CMD_ENGINE_LOG_STOP = 0x5,
  COM_CMD_CONFIG_EPOCH = 0x6,
} com_cmd_t;

static bool com_mqtt_ha = false;
static bool com_did_start = false;
static char *com_plugin_file = NULL;
static char *com_plugin_mode = NULL;
static uint16_t com_log_sessions[16] = {0};

static naos_msg_reply_t com_cmd_sensor_read(naos_msg_t msg) {
  // command structure:
  // SINCE (8)

  // check length
  if (msg.len != 8) {
    return NAOS_MSG_INVALID;
  }

  // get since
  int64_t since;
  memcpy(&since, msg.data, sizeof(since));

  // prepare source
  al_sample_source_t source = al_store_source();

  // get source info
  size_t count = source.count(source.ctx);
  int64_t start = source.start(source.ctx);

  // prepare index
  int index = 0;
  if (since > 0) {
    int32_t offset = (int32_t)(since - start);
    index = al_sample_search(&source, &offset);
  }

  // check index
  if (index < 0) {
    return NAOS_MSG_ACK;
  }

  // send start
  if (!naos_msg_send((naos_msg_t){
          .session = msg.session,
          .endpoint = ENDPOINT,
          .data = (uint8_t *)&start,
          .len = sizeof(start),
      })) {
    return NAOS_MSG_ERROR;
  }

  // send samples
  for (size_t i = index; i < count; i++) {
    // get sample
    al_sample_t sample = {0};
    source.read(source.ctx, &sample, 1, i);

    // send reply
    if (!naos_msg_send((naos_msg_t){
            .session = msg.session,
            .endpoint = ENDPOINT,
            .data = (uint8_t *)&sample,
            .len = sizeof(sample),
        })) {
      return NAOS_MSG_ERROR;
    }
  }

  return NAOS_MSG_ACK;
}

static naos_msg_reply_t com_cmd_signal_launch(naos_msg_t msg) {
  // command structure:
  // FILE (*) [ | 0 | MODE (*) ]

  // check length
  if (msg.len == 0) {
    return NAOS_MSG_INVALID;
  }

  // get file length
  size_t file_len = strnlen((const char *)msg.data, msg.len);
  size_t bin_len = 0;
  if (file_len < msg.len) {
    bin_len = msg.len - (file_len + 1);
  }

  // check file length
  if (file_len == 0) {
    return NAOS_MSG_INVALID;
  }

  // free previous plugin file and mode
  if (com_plugin_file) {
    free(com_plugin_file);
  }
  if (com_plugin_mode) {
    free(com_plugin_mode);
  }

  // copy plugin file
  com_plugin_file = strdup((const char *)msg.data);

  // copy plugin mo
  if (bin_len > 0 && msg.data[file_len + 1] != '\0') {
    com_plugin_mode = strdup((const char *)&msg.data[file_len + 1]);
  } else {
    com_plugin_mode = strdup("main");
  }

  // signal launch
  sig_dispatch((sig_event_t){
      .type = SIG_LAUNCH,
      .plugin.file = com_plugin_file,
      .plugin.mode = com_plugin_mode,
  });

  return NAOS_MSG_ACK;
}

static naos_msg_reply_t com_cmd_signal_kill(naos_msg_t msg) {
  // check length
  if (msg.len != 0) {
    return NAOS_MSG_INVALID;
  }

  // signal kill
  sig_dispatch((sig_event_t){
      .type = SIG_KILL,
  });

  return NAOS_MSG_ACK;
}

static naos_msg_reply_t com_cmd_engine_log_start(naos_msg_t msg) {
  // TODO: Support filter?

  // check if already registered
  for (size_t i = 0; i < sizeof(com_log_sessions) / sizeof(uint16_t); i++) {
    if (com_log_sessions[i] == msg.session) {
      return NAOS_MSG_ACK;
    }
  }

  // find free slot and add session
  for (size_t i = 0; i < sizeof(com_log_sessions) / sizeof(uint16_t); i++) {
    if (com_log_sessions[i] == 0) {
      com_log_sessions[i] = msg.session;
      return NAOS_MSG_ACK;
    }
  }

  return NAOS_MSG_ERROR;
}

static naos_msg_reply_t com_cmd_engine_log_stop(naos_msg_t msg) {
  // TODO: Support filter?

  // find session and remove
  for (size_t i = 0; i < sizeof(com_log_sessions) / sizeof(uint16_t); i++) {
    if (com_log_sessions[i] == msg.session) {
      com_log_sessions[i] = 0;
      return NAOS_MSG_ACK;
    }
  }

  return NAOS_MSG_ERROR;
}

static naos_msg_reply_t com_cmd_config_epoch(naos_msg_t msg) {
  // command structure:
  // EPOCH (8)

  // check length
  if (msg.len != 8) {
    return NAOS_MSG_INVALID;
  }

  // get epoch
  int64_t epoch;
  memcpy(&epoch, msg.data, sizeof(epoch));

  // set epoch
  al_clock_set_epoch(epoch);

  return NAOS_MSG_ACK;
}

static naos_msg_reply_t com_handle(naos_msg_t msg) {
  // message structure:
  // CMD (1) | *

  // check length
  if (msg.len == 0) {
    return NAOS_MSG_INVALID;
  }

  // check lock status
  if (naos_msg_is_locked(msg.session)) {
    return NAOS_MSG_LOCKED;
  }

  // get command
  com_cmd_t cmd = msg.data[0];

  // resize message
  msg.data = &msg.data[1];
  msg.len -= 1;

  // handle command
  naos_msg_reply_t reply;
  switch (cmd) {
    case COM_CMD_SENSOR_READ:
      reply = com_cmd_sensor_read(msg);
      break;
    case COM_CMD_ENGINE_LAUNCH:
      reply = com_cmd_signal_launch(msg);
      break;
    case COM_CMD_ENGINE_KILL:
      reply = com_cmd_signal_kill(msg);
      break;
    case COM_CMD_ENGINE_LOG_START:
      reply = com_cmd_engine_log_start(msg);
      break;
    case COM_CMD_ENGINE_LOG_STOP:
      reply = com_cmd_engine_log_stop(msg);
      break;
    case COM_CMD_CONFIG_EPOCH:
      reply = com_cmd_config_epoch(msg);
      break;
    default:
      reply = NAOS_MSG_UNKNOWN;
  }

  return reply;
}

static void com_cleanup(uint16_t session) {
  // remove from log sessions
  for (size_t i = 0; i < sizeof(com_log_sessions) / sizeof(uint16_t); i++) {
    if (com_log_sessions[i] == session) {
      com_log_sessions[i] = 0;
    }
  }
}

static void com_ha_config_sensor(const char *hat, const char *did, const char *fwv, const char *bt, const char *uid,
                                 const char *t, const char *n, const char *uom, const char *dc, int sdp) {
  // allocate buffer
  void *buf = malloc(128 + 1024);
  if (!buf) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate topic
  int r = snprintf(buf, 128, "%s/sensor/%s/%s/config", hat, did, uid);
  if (r < 0 || r >= 128) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // prepare unit of measurement field
  char uom_field[64] = {0};
  if (strlen(uom) > 0) {
    r = snprintf(uom_field, sizeof(uom_field), "\"unit_of_measurement\": \"%s\",", uom);
    if (r < 0 || r >= sizeof(uom_field)) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  }

  // calculate message
#define SEN_TPL                                 \
  ("{"                                          \
   "  \"name\": \"%s\","                        \
   "  \"state_topic\": \"%s/%s\","              \
   "  %s"                                       \
   "  \"device_class\": \"%s\","                \
   "  \"state_class\": \"measurement\","        \
   "  \"unique_id\": \"%s-%s\","                \
   "  \"suggested_display_precision\": \"%d\"," \
   "  \"device\": {"                            \
   "    \"ids\": \"%s\","                       \
   "    \"name\": \"Air Lab\","                 \
   "    \"mf\": \"Networked Artifacts\","       \
   "    \"mdl\": \"NA-AL1\","                   \
   "    \"sw\": \"%s\","                        \
   "    \"sn\": \"%s\","                        \
   "    \"hw\": \"R4\""                         \
   "  }"                                        \
   "}")
  r = snprintf(buf + 128, 1024, SEN_TPL, n, bt, t, uom_field, dc, did, uid, sdp, did, fwv, did);
  if (r < 0 || r >= 1024) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // publish discovery message
  naos_publish_s(buf, buf + 128, 0, false, NAOS_GLOBAL);

  // release buffer
  free(buf);
}

static void com_pub_sensor(al_sample_t sample, al_sample_field_t field, const char *topic, int res) {
  // format value
  char value[16] = {0};
  snprintf(value, sizeof(value), "%.*f", res, al_sample_read(sample, field));

  // publish value
  naos_publish_s(topic, value, 0, false, NAOS_LOCAL);
}

static void com_task() {
  // wait some time
  naos_delay(2000);

  // run network stack
  naos_ble_init((naos_ble_config_t){
      .pairing = true,
      .bonding = naos_get_b("ble-bonding"),
  });
  naos_wifi_init();
  naos_mqtt_init(1);

  // set flag
  com_did_start = true;

  for (;;) {
    // await sample
    al_sample_t sample = al_sensor_next();

    // check connection
    if (naos_status() != NAOS_NETWORKED) {
      continue;
    }

    // publish sample
    com_pub_sensor(sample, AL_SAMPLE_CO2, "co2", 0);
    com_pub_sensor(sample, AL_SAMPLE_TMP, "tmp", 1);
    com_pub_sensor(sample, AL_SAMPLE_HUM, "hum", 1);
    com_pub_sensor(sample, AL_SAMPLE_VOC, "voc", 0);
    com_pub_sensor(sample, AL_SAMPLE_NOX, "nox", 0);
    com_pub_sensor(sample, AL_SAMPLE_PRS, "prs", 0);
  }
}

static naos_param_t com_params[] = {
    {.name = "mqtt-ha", .type = NAOS_BOOL, .sync_b = &com_mqtt_ha},
    {.name = "mqtt-ha-topic", .type = NAOS_STRING, .default_s = "homeassistant"},
    {.name = "ble-bonding", .type = NAOS_BOOL},
};

void com_init() {
  // register params
  for (size_t i = 0; i < NAOS_COUNT(com_params); i++) {
    naos_register(&com_params[i]);
  }

  // install custom endpoint
  naos_msg_install((naos_msg_endpoint_t){
      .ref = ENDPOINT,
      .name = "com",
      .handle = com_handle,
      .cleanup = com_cleanup,
  });

  // install filesystem endpoint
  naos_fs_install((naos_fs_config_t){
      .root = AL_STORAGE_ROOT,
  });

  // install authentication endpoint
  naos_auth_install();

  // install debug endpoint
  naos_debug_install();

  // run tasks
  naos_run("com", 4096, 1, com_task);
  naos_repeat("com-discovery", 10000, com_online);
}

bool com_started() {
  // return state
  return com_did_start;
}

void com_online() {
  // check if home assistant is enabled
  if (!com_mqtt_ha) {
    return;
  }

  // get information
  const char *hat = naos_get_s("mqtt-ha-topic");
  const char *did = naos_get_s("device-id");
  const char *av = naos_get_s("app-version");
  const char *bt = naos_get_s("base-topic");

  // configure sensors
  com_ha_config_sensor(hat, did, av, bt, "al-co2", "co2", "CO2", "ppm", "carbon_dioxide", 0);
  com_ha_config_sensor(hat, did, av, bt, "al-tmp", "tmp", "Temperature", "°C", "temperature", 1);
  com_ha_config_sensor(hat, did, av, bt, "al-hum", "hum", "Humidity", "%", "humidity", 1);
  com_ha_config_sensor(hat, did, av, bt, "al-voc", "voc", "VOC", "", "aqi", 0);
  com_ha_config_sensor(hat, did, av, bt, "al-nox", "nox", "NOx", "", "aqi", 0);
  com_ha_config_sensor(hat, did, av, bt, "al-prs", "prs", "Pressure", "hPa", "atmospheric_pressure", 0);
}

void com_log(const char *msg, size_t len) {
  // dispatch message to sessions
  for (size_t i = 0; i < sizeof(com_log_sessions) / sizeof(uint16_t); i++) {
    // check session
    if (com_log_sessions[i] == 0) {
      continue;
    }

    // send message
    naos_msg_send((naos_msg_t){
        .session = com_log_sessions[i],
        .endpoint = ENDPOINT,
        .data = (uint8_t *)msg,
        .len = len,
    });
  }
}
