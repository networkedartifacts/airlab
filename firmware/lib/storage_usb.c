#include <string.h>

#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <esp_pm.h>
#include <esp_private/usb_phy.h>
#include <tusb.h>

#include "storage.h"

#define AL_STORAGE_USB_RHPORT 0
#define AL_STORAGE_USB_VBUS_MONITOR_IO GPIO_NUM_18
#define AL_STORAGE_USB_POLL 100

// the USB descriptor and the SCSI inquiry describe the same device to different
// layers, so both are derived from these values; SCSI concatenates vendor and
// product, therefore the product must not repeat the vendor
#define AL_STORAGE_USB_VENDOR "AIRLAB"
#define AL_STORAGE_USB_PRODUCT "Storage"
#define AL_STORAGE_USB_REVISION "1.0"

static bool al_storage_usb_enabled = false;
static bool al_storage_usb_ejected = false;
static bool al_storage_usb_running = false;
static al_storage_eject_t al_storage_eject = NULL;
static usb_phy_handle_t al_storage_usb_phy = NULL;
static naos_signal_t al_storage_usb_signal = NULL;
static esp_pm_lock_handle_t al_storage_usb_pm_lock = NULL;

static tusb_desc_device_t al_storage_usb_dev_desc = {
    .bLength = sizeof(al_storage_usb_dev_desc),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static uint8_t const al_storage_usb_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(0, 4, 0x01, 0x81, 64),
};

static char al_storage_usb_serial[13] = {0};

static char const *al_storage_usb_str_desc[] = {
    (const char[]){0x09, 0x04}, AL_STORAGE_USB_VENDOR, AL_STORAGE_USB_VENDOR " " AL_STORAGE_USB_PRODUCT,
    al_storage_usb_serial,      "External Disk",
};

static uint16_t al_storage_usb_str_buf[32];

static esp_err_t al_storage_usb_install_phy(void) {
  // keep previously installed PHY
  if (al_storage_usb_phy != NULL) {
    return ESP_OK;
  }

  // configure internal USB PHY
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_FULL,
  };
  const usb_phy_otg_io_conf_t otg_io_conf = USB_PHY_SELF_POWERED_DEVICE(AL_STORAGE_USB_VBUS_MONITOR_IO);
  phy_conf.otg_io_conf = &otg_io_conf;

  // create internal PHY
  return usb_new_phy(&phy_conf, &al_storage_usb_phy);
}

static void al_storage_usb_task_main() {
  // initialize TinyUSB device stack
  const tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO,
  };
  if (!tusb_init(AL_STORAGE_USB_RHPORT, &dev_init)) {
    naos_log("al-sto: failed to initialize TinyUSB");
    naos_trigger(al_storage_usb_signal, 1, false);
    return;
  }

  // process USB events until stopped, polling instead of blocking forever, so
  // that the stack is torn down from a known state rather than by deleting the
  // task in the middle of a transfer
  while (al_storage_usb_running) {
    tud_task_ext(AL_STORAGE_USB_POLL, false);
  }

  // signal exit
  naos_trigger(al_storage_usb_signal, 1, false);
}

static void al_storage_external_prepare_usb(al_storage_eject_t eject) {
  // store eject callback and hand ownership to USB
  al_storage_eject = eject;
  al_storage_usb_ejected = false;
  al_storage_usb_enabled = true;
  al_storage_external_ensure();
  al_storage_external_unmount();
}

static void al_storage_external_finish_usb(void) {
  // hand ownership back to the application
  al_storage_usb_enabled = false;
  al_storage_usb_ejected = false;

  // remount file system
  al_storage_external_mount();
}

void al_storage_external_enable_usb(al_storage_eject_t eject) {
  // ignore duplicate enable requests
  if (al_storage_usb_enabled) {
    return;
  }

  // block light sleep while USB is active, as the host cannot talk to a
  // sleeping device; the CPU frequency is pinned anyway, so this lock only
  // needs to gate the sleep itself
  if (al_storage_usb_pm_lock == NULL) {
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "al-usb", &al_storage_usb_pm_lock));
  }
  ESP_ERROR_CHECK(esp_pm_lock_acquire(al_storage_usb_pm_lock));

  // ensure exit signal
  if (al_storage_usb_signal == NULL) {
    al_storage_usb_signal = naos_signal();
  }

  // copy the device ID as serial, as hosts cache device state per vendor,
  // product and serial and would otherwise not tell two devices apart; the
  // value is copied because the parameter store owns the returned pointer
  const char *id = naos_get_s("device-id");
  size_t len = strlen(id);
  if (len > sizeof(al_storage_usb_serial) - 1) {
    len = sizeof(al_storage_usb_serial) - 1;
  }
  memcpy(al_storage_usb_serial, id, len);
  al_storage_usb_serial[len] = 0;

  // log enable
  naos_log("al-sto: USB enabled, serial=%s", al_storage_usb_serial);

  // hand storage to USB and bring up TinyUSB
  al_storage_external_prepare_usb(eject);
  ESP_ERROR_CHECK(al_storage_usb_install_phy());
  al_storage_usb_running = true;
  naos_run("al-sto", 4096, 0, al_storage_usb_task_main);
}

void al_storage_external_disable_usb(void) {
  // ignore duplicate disable requests
  if (!al_storage_usb_enabled) {
    return;
  }

  // log disable, as the ejected flag tells a host eject apart from a user
  // initiated exit
  naos_log("al-sto: USB disabled, ejected=%d", al_storage_usb_ejected);

  // stop TinyUSB task and await its exit
  al_storage_usb_running = false;
  naos_await(al_storage_usb_signal, 1, true, -1);

  // tear down TinyUSB stack
  if (tusb_inited() && !tusb_teardown(AL_STORAGE_USB_RHPORT)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // release USB PHY
  if (al_storage_usb_phy != NULL) {
    ESP_ERROR_CHECK(usb_del_phy(al_storage_usb_phy));
    al_storage_usb_phy = NULL;
  }

  // hand storage back to the application
  al_storage_external_finish_usb();

  // allow light sleep again
  ESP_ERROR_CHECK(esp_pm_lock_release(al_storage_usb_pm_lock));
}

uint32_t tusb_time_millis_api(void) { return (uint32_t)naos_millis(); }

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&al_storage_usb_dev_desc; }

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return al_storage_usb_cfg_desc;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  // index zero returns the language identifier
  uint8_t len = 0;
  if (index == 0) {
    memcpy(&al_storage_usb_str_buf[1], al_storage_usb_str_desc[0], 2);
    len = 1;
  } else {
    if (index >= sizeof(al_storage_usb_str_desc) / sizeof(al_storage_usb_str_desc[0])) {
      return NULL;
    }

    // convert ASCII string to UTF-16
    char const *str = al_storage_usb_str_desc[index];
    while (str[len] != '\0' &&
           len < (uint8_t)(sizeof(al_storage_usb_str_buf) / sizeof(al_storage_usb_str_buf[0]) - 1)) {
      al_storage_usb_str_buf[1 + len] = str[len];
      len++;
    }
  }

  al_storage_usb_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return al_storage_usb_str_buf;
}

// note that these report the bus level configuration state, not whether the
// host has mounted the file system

void tud_mount_cb(void) { naos_log("al-sto: USB configured"); }

void tud_umount_cb(void) { naos_log("al-sto: USB deconfigured"); }

void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }

void tud_resume_cb(void) {}

uint8_t tud_msc_get_maxlun_cb(void) { return 0; }

_Static_assert(sizeof(AL_STORAGE_USB_VENDOR) - 1 <= 8, "USB vendor too long");
_Static_assert(sizeof(AL_STORAGE_USB_PRODUCT) - 1 <= 16, "USB product too long");
_Static_assert(sizeof(AL_STORAGE_USB_REVISION) - 1 <= 4, "USB revision too long");

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;

  // pad fields, as SCSI expects fixed width values that are not terminated
  memset(vendor_id, ' ', 8);
  memset(product_id, ' ', 16);
  memset(product_rev, ' ', 4);

  // copy values
  memcpy(vendor_id, AL_STORAGE_USB_VENDOR, sizeof(AL_STORAGE_USB_VENDOR) - 1);
  memcpy(product_id, AL_STORAGE_USB_PRODUCT, sizeof(AL_STORAGE_USB_PRODUCT) - 1);
  memcpy(product_rev, AL_STORAGE_USB_REVISION, sizeof(AL_STORAGE_USB_REVISION) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  // determine readiness
  if (!al_storage_usb_enabled || al_storage_usb_ejected || !al_storage_external_ready()) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
  }

  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void)lun;

  // determine capacity
  *block_count = al_storage_external_block_count();
  *block_size = AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void)lun;
  (void)power_condition;

  // log request, as this is the primary eject signal on most hosts
  naos_log("al-sto: USB start_stop start=%d eject=%d", start, load_eject);

  // propagate host eject requests to the application
  if (load_eject) {
    al_storage_usb_ejected = !start;
    if (!start && al_storage_eject != NULL) {
      al_storage_eject();
    }
  }

  return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  (void)lun;
  return al_storage_usb_enabled && !al_storage_usb_ejected;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  (void)lun;
  return al_storage_external_read(lba, offset, buffer, bufsize);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void)lun;
  return al_storage_external_write(lba, offset, buffer, bufsize);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void)buffer;
  (void)bufsize;

  // return error
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: unsupported SCSI cmd=0x%02x", scsi_cmd[0]);
  }

  return -1;
}
