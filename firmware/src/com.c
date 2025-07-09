#include <string.h>
#include <naos.h>
#include <naos/msg.h>
#include <naos/fs.h>
#include <naos/ble.h>
#include <naos/wifi.h>
#include <naos/mqtt.h>
#include <naos/sys.h>

#include <al/sensor.h>
#include <al/store.h>
#include <al/storage.h>

#define ENDPOINT 0xA1

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
          .data = (uint8_t*)&start,
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
            .data = (uint8_t*)&sample,
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

static void com_task() {
  // wait some time
  naos_delay(5000);

  // install endpoint
  naos_msg_install((naos_msg_endpoint_t){
      .ref = ENDPOINT,
      .name = "com",
      .handle = com_handle,
  });

  // run network
  naos_fs_install((naos_fs_config_t){.root = AL_STORAGE_ROOT});
  naos_ble_init((naos_ble_config_t){});
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
    if (com_mqtt_ha) {
      naos_publish_d("airlab/co2", al_sample_read(sample, AL_SAMPLE_CO2), 0, false, NAOS_GLOBAL);
      naos_publish_d("airlab/tmp", al_sample_read(sample, AL_SAMPLE_TMP), 0, false, NAOS_GLOBAL);
      naos_publish_d("airlab/hum", al_sample_read(sample, AL_SAMPLE_HUM), 0, false, NAOS_GLOBAL);
      naos_publish_d("airlab/voc", al_sample_read(sample, AL_SAMPLE_VOC), 0, false, NAOS_GLOBAL);
      naos_publish_d("airlab/nox", al_sample_read(sample, AL_SAMPLE_NOX), 0, false, NAOS_GLOBAL);
      naos_publish_d("airlab/prs", al_sample_read(sample, AL_SAMPLE_PRS), 0, false, NAOS_GLOBAL);
    } else {
      naos_publish_d("co2", al_sample_read(sample, AL_SAMPLE_CO2), 0, false, NAOS_LOCAL);
      naos_publish_d("tmp", al_sample_read(sample, AL_SAMPLE_TMP), 0, false, NAOS_LOCAL);
      naos_publish_d("hum", al_sample_read(sample, AL_SAMPLE_HUM), 0, false, NAOS_LOCAL);
      naos_publish_d("voc", al_sample_read(sample, AL_SAMPLE_VOC), 0, false, NAOS_LOCAL);
      naos_publish_d("nox", al_sample_read(sample, AL_SAMPLE_NOX), 0, false, NAOS_LOCAL);
      naos_publish_d("prs", al_sample_read(sample, AL_SAMPLE_PRS), 0, false, NAOS_LOCAL);
    }
  }
}

static naos_param_t com_params[] = {
    {.name = "mqtt-ha", .type = NAOS_BOOL, .sync_b = &com_mqtt_ha},
};

void com_online() {
  // check if home assistant is enabled
  if (!com_mqtt_ha) {
    return;
  }

  // prepare discovery messages
  const char* co2 =
      "{"
      "  \"name\": \"CO2\","
      "  \"state_topic\": \"airlab/co2\","
      "  \"unit_of_measurement\": \"ppm\", "
      "  \"device_class\": \"carbon_dioxide\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_co2\","
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";
  const char* tmp =
      "{"
      "  \"name\": \"Temperature\","
      "  \"state_topic\": \"airlab/tmp\","
      "  \"unit_of_measurement\": \"°C\", "
      "  \"device_class\": \"temperature\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_tmp\","
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";
  const char* hum =
      "{"
      "  \"name\": \"Humidity\","
      "  \"state_topic\": \"airlab/hum\","
      "  \"unit_of_measurement\": \"%\","
      "  \"device_class\": \"humidity\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_hum\","
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";
  const char* voc =
      "{"
      "  \"name\": \"VOC\","
      "  \"state_topic\": \"airlab/voc\","
      "  \"unit_of_measurement\": \"\","
      "  \"device_class\": \"aqi\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_voc\","
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";
  const char* nox =
      "{"
      "  \"name\": \"NOx\","
      "  \"state_topic\": \"airlab/nox\","
      "  \"unit_of_measurement\": \"\","
      "  \"device_class\": \"aqi\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_nox\","
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";
  const char* prs =
      "{"
      "  \"name\": \"Pressure\","
      "  \"state_topic\": \"airlab/prs\","
      "  \"unit_of_measurement\": \"hPa\", "
      "  \"device_class\": \"atmospheric_pressure\","
      "  \"state_class\": \"measurement\","
      "  \"unique_id\": \"al_prs\", "
      "  \"device\": {"
      "    \"identifiers\": [\"al_01\"],"
      "    \"name\": \"Air Lab\","
      "    \"manufacturer\": \"Networked Artifacts\","
      "    \"model\": \"R3-2025\""
      "  }"
      "}";

  // publish discovery messages
  naos_publish_s("homeassistant/sensor/al_co2/config", co2, 0, true, NAOS_GLOBAL);
  naos_publish_s("homeassistant/sensor/al_tmp/config", tmp, 0, true, NAOS_GLOBAL);
  naos_publish_s("homeassistant/sensor/al_hum/config", hum, 0, true, NAOS_GLOBAL);
  naos_publish_s("homeassistant/sensor/al_voc/config", voc, 0, true, NAOS_GLOBAL);
  naos_publish_s("homeassistant/sensor/al_nox/config", nox, 0, true, NAOS_GLOBAL);
  naos_publish_s("homeassistant/sensor/al_prs/config", prs, 0, true, NAOS_GLOBAL);
}

void com_init() {
  // register params
  for (size_t i = 0; i < sizeof(com_params) / sizeof(naos_param_t); i++) {
    naos_register(&com_params[i]);
  }

  // run tasks
  naos_run("com", 4096, 1, com_task);
  naos_repeat("com-discovery", 10000, com_online);
}
