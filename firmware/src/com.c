#include <stdlib.h>
#include <string.h>
#include <naos.h>
#include <naos/msg.h>
#include <naos/fs.h>
#include <naos/ble.h>
#include <naos/wifi.h>
#include <naos/mqtt.h>
#include <naos/auth.h>
#include <naos/sys.h>
#include <esp_err.h>

#include <al/sensor.h>
#include <al/store.h>
#include <al/storage.h>

#include "com.h"

#define ENDPOINT 0xA1

// TODO: Only send discovery when the HA MQTT service comes online.

typedef enum {
  COM_CMD_SENSOR_READ = 0x01,
} com_cmd_t;

static bool com_mqtt_ha = false;

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
    default:
      reply = NAOS_MSG_UNKNOWN;
  }

  return reply;
}

static void com_ha_config_sensor(const char *hat, const char *did, const char *fwv, const char *bt, const char *uid,
                                 const char *t, const char *n, const char *uom, const char *dc) {
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
#define SEN_TPL                           \
  ("{"                                    \
   "  \"name\": \"%s\","                  \
   "  \"state_topic\": \"%s/%s\","        \
   "  %s"                                 \
   "  \"device_class\": \"%s\","          \
   "  \"state_class\": \"measurement\","  \
   "  \"unique_id\": \"%s-%s\","          \
   "  \"device\": {"                      \
   "    \"ids\": \"%s\","                 \
   "    \"name\": \"Air Lab\","           \
   "    \"mf\": \"Networked Artifacts\"," \
   "    \"mdl\": \"NA-AL1\","             \
   "    \"sw\": \"%s\","                  \
   "    \"sn\": \"%s\","                  \
   "    \"hw\": \"R4\""                   \
   "  }"                                  \
   "}")
  r = snprintf(buf + 128, 1024, SEN_TPL, n, bt, t, uom_field, dc, did, uid, did, fwv, did);
  if (r < 0 || r >= 1024) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // publish discovery message
  naos_publish_s(buf, buf + 128, 0, false, NAOS_GLOBAL);

  // release buffer
  free(buf);
}

static void com_task() {
  // wait some time
  naos_delay(5000);

  // install custom endpoint
  naos_msg_install((naos_msg_endpoint_t){
      .ref = ENDPOINT,
      .name = "com",
      .handle = com_handle,
  });

  // install filesystem endpoint
  naos_fs_install((naos_fs_config_t){
      .root = AL_STORAGE_ROOT,
  });

  // install authentication endpoint
  naos_auth_install();

  // TODO: Also require bonding for more security?

  // run network stack
  naos_ble_init((naos_ble_config_t){
      .pairing = true,
  });
  naos_wifi_init();
  naos_mqtt_init(1);

  for (;;) {
    // await sample
    al_sample_t sample = al_sensor_next();

    // check connection
    if (naos_status() != NAOS_NETWORKED) {
      continue;
    }

    // publish sample
    naos_publish_d("co2", al_sample_read(sample, AL_SAMPLE_CO2), 0, false, NAOS_LOCAL);
    naos_publish_d("tmp", al_sample_read(sample, AL_SAMPLE_TMP), 0, false, NAOS_LOCAL);
    naos_publish_d("hum", al_sample_read(sample, AL_SAMPLE_HUM), 0, false, NAOS_LOCAL);
    naos_publish_d("voc", al_sample_read(sample, AL_SAMPLE_VOC), 0, false, NAOS_LOCAL);
    naos_publish_d("nox", al_sample_read(sample, AL_SAMPLE_NOX), 0, false, NAOS_LOCAL);
    naos_publish_d("prs", al_sample_read(sample, AL_SAMPLE_PRS), 0, false, NAOS_LOCAL);
  }
}

static naos_param_t com_params[] = {
    {.name = "mqtt-ha", .type = NAOS_BOOL, .sync_b = &com_mqtt_ha},
    {.name = "mqtt-ha-topic", .type = NAOS_STRING, .default_s = "homeassistant"},
};

void com_init() {
  // register params
  for (size_t i = 0; i < sizeof(com_params) / sizeof(naos_param_t); i++) {
    naos_register(&com_params[i]);
  }

  // run tasks
  naos_run("com", 4096, 1, com_task);
  naos_repeat("com-discovery", 10000, com_online);
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
  com_ha_config_sensor(hat, did, av, bt, "al-co2", "co2", "CO2", "ppm", "carbon_dioxide");
  com_ha_config_sensor(hat, did, av, bt, "al-tmp", "tmp", "Temperature", "°C", "temperature");
  com_ha_config_sensor(hat, did, av, bt, "al-hum", "hum", "Humidity", "%", "humidity");
  com_ha_config_sensor(hat, did, av, bt, "al-voc", "voc", "VOC", "", "aqi");
  com_ha_config_sensor(hat, did, av, bt, "al-nox", "nox", "NOx", "", "aqi");
  com_ha_config_sensor(hat, did, av, bt, "al-prs", "prs", "Pressure", "hPa", "atmospheric_pressure");
}
