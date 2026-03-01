#include <naos.h>
#include <naos/sys.h>
#include <naos/cpu.h>
#include <naos/ble.h>
#include <naos/auth.h>
#include <esp_err.h>
#include <esp_system.h>
#include <art32/numbers.h>
#include <lvgl.h>
#include <math.h>

#include <al/core.h>
#include <al/accel.h>
#include <al/power.h>
#include <al/clock.h>
#include <al/sensor.h>
#include <al/storage.h>
#include <al/store.h>
#include <al/buzzer.h>
#include <al/led.h>

#include "com.h"
#include "gui.h"
#include "gfx.h"
#include "sig.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "rec.h"
#include "dev.h"
#include "stm.h"
#include "hmi.h"
#include "dat.h"
#include "eng.h"

#define SCR_MSG_TIMEOUT 2000
#define SCR_IDLE_TIMEOUT 30000
#define SCR_ACTION_TIMEOUT 60000
#define SCR_MIN_RESOLUTION 5000
#define SCR_HIST_POINTS 8

#define SCR_MIN(x, y) ((x) < (y) ? (x) : (y))

static stm_action_t scr_action = 0;
DEV_KEEP static uint16_t scr_file = 0;
DEV_KEEP static void* scr_return_timeout = NULL;
DEV_KEEP static void* scr_return_unlock = NULL;

static const char* scr_field_fmt[] = {
    [AL_SAMPLE_CO2] = "%.0f ppm CO2", [AL_SAMPLE_TMP] = "%.1f °C",  [AL_SAMPLE_HUM] = "%.1f %% RH",
    [AL_SAMPLE_VOC] = "%.0f VOC",     [AL_SAMPLE_NOX] = "%.0f NOx", [AL_SAMPLE_PRS] = "%.0f hPa",
};

/* Helpers */

static const char* scr_ms2str(int32_t ms) {
  if (ms > 1000 * 60 * 60) {  // hours
    return lvx_fmt("%dh", ms / 1000 / 60 / 60);
  } else if (ms > 1000 * 60) {  // minutes
    return lvx_fmt("%dm", ms / 1000 / 60);
  } else {  // seconds
    return lvx_fmt("%ds", ms / 1000);
  }
}

static float scr_temp_convert(float celsius) {
  // convert to fahrenheit if setting is enabled
  if (naos_get_b("fahrenheit")) {
    return celsius * 9.0f / 5.0f + 32.0f;
  }

  return celsius;
}

static const char* scr_temp_format() {
  // return appropriate format string
  if (naos_get_b("fahrenheit")) {
    return "%.1f °F";
  }

  return "%.1f °C";
}

static void scr_power_off(bool low_power, bool msg) {
  // set off flag
  hmi_set_flag(HMI_FLAG_OFF);

  // cleanup screen
  gui_cleanup(true);

  // write message
  if (msg) {
    gui_write(low_power ? "Low Battery\n\nCharge via USB-C and press <A>." : "Powered Off\n\nPress <A> to start.",
              true);
  }

  // clear returns
  scr_return_timeout = NULL;
  scr_return_unlock = NULL;

  // power off
  al_power_off();
}

static void scr_launch(const char* file) {
  // write message
  gui_cleanup(false);
  gui_write("Loading plugin...", false);

  // run plugin
  bool ok = eng_run(file, "main");

  // clean screen
  gui_cleanup(false);

  // show message on failure
  if (!ok) {
    gui_message("Failed to run plugin!", SCR_MSG_TIMEOUT);
  }
}

static bool scr_idle_sleep() {
  // read power state
  al_power_state_t power = al_power_get();

  // power off (no return) if battery is low and not charging
  if (power.bat_low && !power.has_usb && !power.charging) {
    scr_power_off(true, true);
  }

  // check BLE and MQTT
  bool has_ble = naos_get_b("ble-prev-sleep") && naos_ble_connections() > 0;
  bool has_mqtt = naos_get_b("mqtt-prev-sleep") && naos_status() == NAOS_NETWORKED;

  // check if powered or connected via BLE/MQTT
  if (power.has_usb || has_ble || has_mqtt) {
    // wait some time
    sig_event_t event = sig_await(SIG_KEYS | SIG_TIMEOUT | SIG_SENSOR | SIG_INTERRUPT | SIG_LAUNCH, 60 * 1000);

    // start engine on launch
    if (event.type == SIG_LAUNCH) {
      // run engine
      scr_launch(event.file);

      return false;
    }

    // handle unlock
    if (event.type & SIG_KEYS) {
      return false;
    }

    return true;
  }

  // determine rate
  al_sensor_rate_t rate = AL_SENSOR_RATE_5S;
  int32_t raw_rate = naos_get_l(rec_running() ? "record-rate" : "sleep-rate");
  if (raw_rate == 30) {
    rate = AL_SENSOR_RATE_30S;
  } else if (raw_rate == 60) {
    rate = AL_SENSOR_RATE_60S;
  }

  // set rate
  al_sensor_set_rate(rate);

  // sleep for one minute (no return)
  al_sleep(true, 60 * 1000);

  return true;
}

/* Translations */

typedef enum {
  SCR_DE,
  SCR_EN,
  SCR_ES,
} scr_lang_t;

static const char* scr_lang_str[] = {
    [SCR_DE] = "Deutsch",
    [SCR_EN] = "English",
    [SCR_ES] = "Español",
};

scr_lang_t scr_lang() {
  // get language
  const char* lang = naos_get_s("language");
  if (strcmp(lang, "en") == 0) {
    return SCR_EN;
  } else if (strcmp(lang, "de") == 0) {
    return SCR_DE;
  } else if (strcmp(lang, "es") == 0) {
    return SCR_ES;
  }
  return SCR_EN;
}

typedef struct {
  const char* yes;
  const char* no;
  const char* on;
  const char* off;
  const char* back;
  const char* next;
  const char* change;
  const char* save;
  const char* cancel;
  const char* execute;
  const char* measurement;
  const char* recording;
  const char* exit__stop;
  const char* exit__back;
  const char* exit__stopped;
  const char* view__not_enough;
  const char* create__full;
  const char* create__info;
  const char* create__start;
  const char* create__import;
  const char* create__importing;
  const char* create__imported;
  const char* delete__confirm;
  const char* delete__delete;
  const char* delete__deleted;
  const char* edit__analyse;
  const char* edit__delete;
  const char* edit__export;
  const char* edit__exporting;
  const char* edit__export_fail;
  const char* edit__export_done;
  const char* explore__empty;
  const char* explore__create;
  const char* explore__select;
  const char* usb__disconnected;
  const char* usb__active;
  const char* usb__eject;
  const char* ble__active;
  const char* reset__confirm;
  const char* reset__reset;
  const char* settings__title;
  const char* settings__about;
  const char* settings__config;
  const char* settings__regulatory;
  const char* settings__introduction;
  const char* settings__off;
  const char* about__device_name;
  const char* about__serial_number;
  const char* about__firmware_version;
  const char* about__internal_storage;
  const char* about__external_storage;
  const char* config__language;
  const char* config__date;
  const char* config__time;
  const char* config__sleep_rate;
  const char* config__record_rate;
  const char* config__long_interval;
  const char* config__temp_unit;
  const char* config__developer;
  const char* config__power_light;
  const char* config__wifi_network;
  const char* config__studio;
  const char* config__ble_prev_sleep;
  const char* config__mqtt_prev_sleep;
  const char* config__ble_pairing;
  const char* config__ble_bonding;
  const char* config__ble_clear;
  const char* config__ble_cleared;
  const char* config__reset;
  const char* menu__no_data;
  const char* intro__hello1;
  const char* intro__hello2;
  const char* intro__watch;
  const char* intro__correct;
  const char* intro__adjust;
  const char* intro__test;
  const char* intro__infos[10];
  const char* intro__end;
  const char* engine__empty;
  const char* engine__run;
} scr_trans_t;

static const scr_trans_t scr_trans_map[] = {
    [SCR_ES] =
        {
            .yes = "Si",
            .no = "No",
            .on = "Encendido",
            .off = "Apagado",
            .back = "Atrás",
            .next = "Próximo",
            .change = "Cambiar",
            .save = "Guardar",
            .cancel = "Cancelar",
            .execute = "Ejecutar",
            .measurement = "Registro %u",
            .recording = "Registro en curso!",
            .exit__stop = "Terminar registro",
            .exit__back = "De vuelta al Laboratorio",
            .exit__stopped = "%s\n terminado!",
            .view__not_enough = "No hay suficientes datos\npara el modo de precisión.",
            .create__full = "Memoria llena!",
            .create__info = "Registro %u\n%d horas disponibles",
            .create__start = "Iniciar",
            .create__import = "Quieres importar los datos\ndel monitoreo actual?",
            .create__importing = "Importando datos...",
            .create__imported = "Operación exitosa!",
            .delete__confirm = "%s\nquieres de verdad eliminarlo?",
            .delete__delete = "Eliminar",
            .delete__deleted = "Registro %d eliminado!",
            .edit__analyse = "Analizar",
            .edit__delete = "Eliminar",
            .edit__export = "Exportar CSV",
            .edit__exporting = "Exportando datos...",
            .edit__export_fail = "Operación fallida!",
            .edit__export_done = "Operación exitosa!",
            .explore__empty = "No hay registros guardados.",
            .explore__create = "Crear nuevo registro",
            .explore__select = "Seleccionar",
            .usb__disconnected = "USB no connectado!",
            .usb__active = "Modo USB activo",
            .usb__eject = "Modo USB desconectado",
            .ble__active = "Bluetooth activo\n\nNombre del dispositivo: %.8s",
            .reset__confirm = "Restablecer Air Lab?",
            .reset__reset = "Air Lab\nrestablecido exitosamente!",
            .settings__title = "Ajustes",
            .settings__about = "Acerca de",
            .settings__config = "Configuración",
            .settings__off = "Apagar",
            .settings__regulatory = "Normativa",
            .settings__introduction = "Introducción",
            .about__device_name = "Nombre del dispositivo",
            .about__serial_number = "Numéro de serie",
            .about__firmware_version = "Versión de FW",
            .about__internal_storage = "Alm. Interno",
            .about__external_storage = "Alm. Externo",
            .config__language = "Idioma",
            .config__date = "Fecha",
            .config__time = "Hora",
            .config__sleep_rate = "Muestreo en reposo",
            .config__record_rate = "Muestreo en registro",
            .config__long_interval = "Intervalo largo plazo",
            .config__temp_unit = "Unidad de temperatura",
            .config__developer = "Modo desarrollador",
            .config__power_light = "Indicador de encendido",
            .config__wifi_network = "Red WiFi",
            .config__studio = "Usa Air Lab Console\npara cambiar este valor.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "Borrar dispositivos BT",
            .config__ble_cleared = "Dispositivos borrados!",
            .config__reset = "Resetear de fábrica",
            .menu__no_data = "Sin Datos",
            .intro__hello1 = "Hola! Yo soy el Profesor Robin,\ndirector científico en el Air Lab.",
            .intro__hello2 = "Creo que me quedé dormido un momento.\nDime, ¿qué hora es?",
            .intro__watch = "Mi reloj dice que son las %d:%02d\ndel %d-%02d-%02d, correcto?",
            .intro__correct = "Correcto!",
            .intro__adjust = "<Corregir>",
            .intro__test = "Gracias! Te gustaría una breve\nexplicación sobre los parámetros\nde la calidad del aire?",
            .intro__infos =
                {
                    "- CO2 -\nEl dióxido de carbono se mide\nen partes por millón (PPM).",
                    "- CO2 -\nEn interiores deberías intentar\nmantenerte por debajo de 1500\nppm ventilando con "
                    "frecuencia.",
                    "- VOC -\nCompuestos orgánicos volátiles\nson emitidos por sustancias\nquímicas como pinturas.",
                    "- VOC -\n100 es el promedio de 24h.\nVariaciones indican\ncambios en el ambiente.",
                    "- NOx -\nLos óxidos de nitrógeno son\ngases generados por combustión\npor ejemplo de autos o "
                    "estufas.",
                    "- NOx -\n1 es el promedio de 24h.\nVariaciones indican\ncambios en el ambiente.",
                    "Eso es todo por ahora.\nAquí puedes aprender "
                    "más:\nnetworkedartifacts.com\n/manuals/airlab/air-parameters",
                },
            .intro__end = "Ahora si, vamos al laboratorio!",
            .engine__empty = "No hay plugins instalados.",
            .engine__run = "Iniciar",
        },
    [SCR_DE] =
        {
            .yes = "Ja",
            .no = "Nein",
            .on = "Ein",
            .off = "Aus",
            .back = "Zurück",
            .next = "Weiter",
            .change = "Ändern",
            .save = "Speichern",
            .cancel = "Abbrechen",
            .execute = "Ausführen",
            .measurement = "Messung %u",
            .recording = "Messung läuft!",
            .exit__stop = "Messung beenden",
            .exit__back = "Zurück zum Labor",
            .exit__stopped = "%s\n beendet!",
            .view__not_enough = "Nicht genug Daten\nfür Präzisionsmodus.",
            .create__full = "Speicher voll!",
            .create__info = "Messung %u\n%d Stunden verfügbar",
            .create__start = "Starten",
            .create__import = "Möchtest du die Live-View\nDaten importieren?",
            .create__importing = "Importiere Daten...",
            .create__imported = "Import erfolgreich!",
            .delete__confirm = "%s\nwirklich löschen?",
            .delete__delete = "Löschen",
            .delete__deleted = "Messung %d gelöscht!",
            .edit__analyse = "Analysieren",
            .edit__delete = "Löschen",
            .edit__export = "CSV Exportieren",
            .edit__exporting = "Exportiere Daten...",
            .edit__export_fail = "Export fehlgeschlagen!",
            .edit__export_done = "Export erfolgreich!",
            .explore__empty = "Keine gespeicherte Messungen.",
            .explore__create = "Neue Messung erstellen",
            .explore__select = "Auswählen",
            .usb__disconnected = "USB nicht angeschlossen!",
            .usb__active = "USB-Modus aktiv",
            .usb__eject = "USB-Modus getrennt",
            .ble__active = "Bluetooth Pairing aktiv\n\nGerätename: %.8s",
            .reset__confirm = "Air Lab zurücksetzen?",
            .reset__reset = "Air Lab\nerfolgreich zurückgesetzt!",
            .settings__title = "Einstellungen",
            .settings__about = "Über",
            .settings__config = "Konfiguration",
            .settings__off = "Ausschalten",
            .settings__regulatory = "Regulatorisches",
            .settings__introduction = "Einführung",
            .about__device_name = "Gerätename",
            .about__serial_number = "Seriennummer",
            .about__firmware_version = "FW Version",
            .about__internal_storage = "Int. Speicher",
            .about__external_storage = "Ext. Speicher",
            .config__language = "Sprache",
            .config__date = "Datum",
            .config__time = "Uhrzeit",
            .config__sleep_rate = "Schlaf Messrate",
            .config__record_rate = "Aufnahme Messrate",
            .config__long_interval = "Langzeit Intervall",
            .config__temp_unit = "Temperatureinheit",
            .config__developer = "Entwicklermodus",
            .config__power_light = "Betriebsanzeige",
            .config__wifi_network = "WiFi Netzwerk",
            .config__studio = "Verwende Air Lab Console\num diesen Wert zu ändern.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "BT Geräte löschen",
            .config__ble_cleared = "Geräte gelöscht!",
            .config__reset = "Zurücksetzen",
            .menu__no_data = "Keine Daten",
            .intro__hello1 = "Hi! Ich bin Professor Robin,\nWissenschaftsleiter am Air Lab.",
            .intro__hello2 = "Da bin ich eben kurz eingenickt.\nSag mal wie spät ist es?",
            .intro__watch = "Meine Uhr zeigt %d:%02d\nam %d-%02d-%02d, richtig?",
            .intro__correct = "Richtig!",
            .intro__adjust = "<Anpassen>",
            .intro__test = "Danke! Brauchst du\neine kurze Auffrischung zu den\nLuftqualitätsparametern?",
            .intro__infos =
                {
                    "- CO2 -\nKohlendioxid wird in Teilen\npro Million (PPM) gemessen.",
                    "- CO2 -\nFür Innenräume solltest du\nversuchen, unter 1500 ppm\nzu bleiben, indem du oft lüftest.",
                    "- VOC -\nFlüchtige organische\nVerbindungen werden von\nChemikalien abgegeben.",
                    "- VOC -\n100 ist der 24h-Durchschnitt.\nAbweichungen zeigen\nVeränderungen im Raum an.",
                    "- NOx -\nStickoxide sind Gase, die bei der\nVerbrennung von Kraftstoffen\n(z. B. in Autos) "
                    "entstehen.",
                    "- NOx -\n1 ist der 24h-Durchschnitt.\nAbweichungen zeigen\nVeränderungen im Raum an.",
                    "Das ist alles für jetzt.\nHier erfährst du "
                    "mehr:\nnetworkedartifacts.com\n/manuals/airlab/air-parameters",
                },
            .intro__end = "Okay, lass uns ins Labor gehen!",
            .engine__empty = "Keine Plugins installiert.",
            .engine__run = "Starten",
        },
    [SCR_EN] =
        {
            .yes = "Yes",
            .no = "No",
            .on = "On",
            .off = "Off",
            .back = "Back",
            .next = "Next",
            .change = "Change",
            .save = "Save",
            .cancel = "Cancel",
            .execute = "Execute",
            .measurement = "Measurement %u",
            .recording = "Measurement running!",
            .exit__stop = "Stop measurement",
            .exit__back = "Go back to Lab",
            .exit__stopped = "%s\n stopped!",
            .view__not_enough = "Not enough data\nfor precision mode.",
            .create__full = "Storage full!",
            .create__info = "Measurement %u\n%d hours available",
            .create__start = "Start",
            .create__import = "Do you want to import\nthe live-view data?",
            .create__importing = "Importing data...",
            .create__imported = "Import successful!",
            .delete__confirm = "Really delete %s?",
            .delete__delete = "Delete",
            .delete__deleted = "Measurement %d deleted!",
            .edit__analyse = "Analyze",
            .edit__delete = "Delete",
            .edit__export = "Export CSV",
            .edit__exporting = "Exporting data...",
            .edit__export_fail = "Export failed!",
            .edit__export_done = "Export done!",
            .explore__empty = "No saved measurements.",
            .explore__create = "Create new measurement",
            .explore__select = "Select",
            .usb__disconnected = "USB not connected!",
            .usb__active = "USB Volume active",
            .usb__eject = "USB Volume ejected",
            .ble__active = "Bluetooth pairing active\n\nDevice name: %.8s",
            .reset__confirm = "Fully reset Air Lab?",
            .reset__reset = "Air Lab\nsuccessfully reset!",
            .settings__title = "Settings",
            .settings__about = "About",
            .settings__config = "Configuration",
            .settings__off = "Power Off",
            .settings__regulatory = "Regulatory",
            .settings__introduction = "Introduction",
            .about__device_name = "Device Name",
            .about__serial_number = "Serial Number",
            .about__firmware_version = "FW Version",
            .about__internal_storage = "Int. Storage",
            .about__external_storage = "Ext. Storage",
            .config__language = "Language",
            .config__date = "Date",
            .config__time = "Time",
            .config__sleep_rate = "Sleep Sample Rate",
            .config__record_rate = "Record Sample Rate",
            .config__long_interval = "Long-term Interval",
            .config__temp_unit = "Temperature Unit",
            .config__developer = "Developer Mode",
            .config__power_light = "Power Light",
            .config__wifi_network = "WiFi Network",
            .config__studio = "Use Air Lab Console\nto change this value.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "Clear BT Devices",
            .config__ble_cleared = "Devices cleared!",
            .config__reset = "Full Reset",
            .menu__no_data = "No Data",
            .intro__hello1 = "Hi! I'm Professor Robin,\nhead of sciences at Air Lab.",
            .intro__hello2 = "I dozed off for a bit...\nCan you tell me the time?",
            .intro__watch = "My watch says its %d:%02d\non the %d-%02d-%02d, right?",
            .intro__correct = "Correct!",
            .intro__adjust = "<Adjust>",
            .intro__test = "Thanks! Now, do you need\na quick refresher on\nair quality parameters?",
            .intro__infos =
                {
                    "- CO2 -\nCarbon dioxide is measured\nin parts per million (PPM).",
                    "- CO2 -\nFor indoor spaces,\ntry to stay below 1500ppm\nby ventilating often.",
                    "- VOC -\nVolatile Organic Compounds\nare emitted from chemicals\nlike paints or cleaning "
                    "products.",
                    "- VOC -\n100 is the average of the\npast 24h. Higher or lower values\nindicate changes in the "
                    "room.",
                    "- NOx -\nNitrogen oxides are gases\ncreated by the combustion\nof fuel (e.g. cars).",
                    "- NOx -\n1 is the average of the\npast 24h. Higher values\nindicate changes in the room.",
                    "That's all for now\nLearn more here:\nnetworkedartifacts.com\n/manuals/airlab/air-parameters",
                },
            .intro__end = "Alright, let's go to the lab!",
            .engine__empty = "No plugins installed.",
            .engine__run = "Run",
        },
};

static const scr_trans_t* scr_trans() {
  // return translation
  return &scr_trans_map[scr_lang()];
}

/* Formatters */

static const char* scr_file_name(dat_file_t* file) {
  // return name
  return lvx_fmt(scr_trans()->measurement, file->head.num);
}

static const char* scr_file_date(dat_file_t* file) {
  // get date
  uint16_t year, month, day;
  al_clock_epoch_date(file->head.start, &year, &month, &day);

  // format date
  return lvx_fmt("%d-%02d-%02d", year, month, day);
}

static const char* scr_file_info(dat_file_t* file) {
  // return info
  return lvx_fmt("%s / %s", scr_file_date(file), scr_ms2str(file->stop));
}

/* Screens */

static void* scr_view();
static void* scr_edit();
static void* scr_explore();
static void* scr_menu();
static void* scr_settings();
static void* scr_develop();

static bool scr_time() {
  // begin draw
  gfx_begin(false, false);

  // add row
  lv_obj_t* row = lv_obj_create(lv_scr_act());
  lv_obj_set_size(row, 200, 100);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

  // prepare wheels
  lvx_wheel_t hour = {.value = 12, .min = 0, .max = 23, .format = "%02d", .fixed = true};
  lvx_wheel_t minute = {.value = 30, .min = 0, .max = 59, .format = "%02d", .fixed = true};

  // assign current time
  uint16_t seconds;
  al_clock_get_time(&hour.value, &minute.value, &seconds);

  // add wheels
  lvx_wheel_create(&hour, row);
  lvx_wheel_create(&minute, row);

  // add button
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // focus first wheel
  lvx_wheel_focus(&hour, true);

  // end draw
  gfx_end(false, false);

  // prepare list
  lvx_wheel_t* wheels[] = {&hour, &minute};
  int cur_wheel = 0;

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // apply wheel events
    if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
      gfx_begin(false, false);
      lvx_wheel_group_update(wheels, 2, event, &cur_wheel);
      gfx_end(false, false);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // handle escape/timeout event
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return false;
    }

    /* handle enter */

    // save time
    al_clock_set_time(hour.value, minute.value, 0);

    return true;
  }
}

static bool scr_date() {
  // begin draw
  gfx_begin(false, false);

  // add row
  lv_obj_t* row = lv_obj_create(lv_scr_act());
  lv_obj_set_size(row, 200, 100);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

  // prepare wheels
  lvx_wheel_t year = {.value = 2023, .min = 2023, .max = 2999, .fixed = true};
  lvx_wheel_t month = {.value = 6, .min = 1, .max = 12, .format = "%02d", .fixed = true};
  lvx_wheel_t day = {.value = 15, .min = 1, .max = 31, .format = "%02d", .fixed = true};

  // assign current date
  al_clock_get_date(&year.value, &month.value, &day.value);

  // add wheels
  lvx_wheel_create(&year, row);
  lvx_wheel_create(&month, row);
  lvx_wheel_create(&day, row);

  // add button
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t off = {
      .title = "B",
      .text = scr_trans()->cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // focus first wheel
  lvx_wheel_focus(&year, true);

  // end draw
  gfx_end(false, false);

  // prepare wheels
  lvx_wheel_t* wheels[] = {&year, &month, &day};
  int cur_wheel = 0;

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // apply wheel events
    if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
      gfx_begin(false, false);
      lvx_wheel_group_update(wheels, 3, event, &cur_wheel);
      gfx_end(false, false);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // return on escape/timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return false;
    }

    /* handle enter */

    // save date
    al_clock_set_date(year.value, month.value, day.value);

    return true;
  }
}

static void* scr_bubbles() {
  // begin draw
  gfx_begin(false, false);

  // add bubble
  lvx_bubble_t bubble = {};
  lvx_bubble_create(&bubble, lv_scr_act());

  // add signs
  lvx_sign_t back = {.title = "B", .text = scr_trans()->cancel, .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t next = {.title = ">", .text = scr_trans()->next, .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // end draw
  gfx_end(true, false);

  // prepare index
  int index = 0;

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // set bubble text
    switch (scr_lang()) {
      case SCR_DE:
        bubble.text = stm_get(index)->text_de;
        break;
      case SCR_EN:
        bubble.text = stm_get(index)->text_en;
        break;
      case SCR_ES:
        bubble.text = stm_get(index)->text_es;
    }

    // update bubble
    lvx_bubble_update(&bubble);

    // end draw
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_ESCAPE | SIG_RIGHT | SIG_LEFT, 0);

    // handle right
    if (event.type == SIG_RIGHT) {
      index++;
      if (index >= stm_num()) {
        index = 0;
      }
      continue;
    } else if (event.type == SIG_LEFT) {
      index--;
      if (index < 0) {
        index = stm_num() - 1;
      }
      continue;
    }

    /* handle escape */

    // cleanup screen
    gui_cleanup(false);

    return scr_develop;
  }
}

static void* scr_info() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get power
    al_power_state_t bat = al_power_get();

    // get date and time
    uint16_t year, month, day, hour, minute, seconds;
    al_clock_get_date(&year, &month, &day);
    al_clock_get_time(&hour, &minute, &seconds);

    // get CPU usage
    float cpu0 = 0, cpu1 = 0;
    naos_cpu_get(&cpu0, &cpu1);

    // prepare text
    const char* text = lvx_fmt("%llds - %.0f%% - P%d - F%d\n%04d-%02d-%02d %02d:%02d:%02d\n%lu kB - %.1f%% - %.1f%%",
                               naos_millis() / 1000, bat.bat_level * 100, bat.has_usb, bat.can_fast, year, month, day,
                               hour, minute, seconds, esp_get_free_heap_size() / 1024, cpu0 * 100, cpu1 * 100);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 1000);

    // loop on timeout
    if (event.type == SIG_TIMEOUT) {
      continue;
    }

    // cleanup
    gui_cleanup(false);

    return scr_develop;
  }
}

static void* scr_sensor() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get last sample and counts
    al_sample_t last = al_store_last();
    int num_short = (int)al_store_count(AL_STORE_SHORT);
    int num_long = (int)al_store_count(AL_STORE_LONG);

    // sample values
    float co2 = al_sample_read(last, AL_SAMPLE_CO2);
    float tmp = al_sample_read(last, AL_SAMPLE_TMP);
    float hum = al_sample_read(last, AL_SAMPLE_HUM);
    float voc = al_sample_read(last, AL_SAMPLE_VOC);
    float nox = al_sample_read(last, AL_SAMPLE_NOX);
    float prs = al_sample_read(last, AL_SAMPLE_PRS);

    // prepare text
    const char* text =
        lvx_fmt("%.0f ppm, %.2f °C, %.0f %%RH\n%.0f VOC, %.0f NOX, %.0f hPa\n\nShort: %d/%d, Long: %d/%d", co2, tmp,
                hum, voc, nox, prs, num_short, AL_STORE_NUM_SHORT, num_long, AL_STORE_NUM_LONG);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 1000);

    // loop on timeout
    if (event.type == SIG_TIMEOUT) {
      continue;
    }

    // cleanup
    gui_cleanup(false);

    return scr_develop;
  }
}

static void* scr_idle() {
  // set timeout return
  scr_return_timeout = scr_idle;

  // begin draw
  gfx_begin(false, false);

  // add status
  lvx_status_t status = {0};
  lvx_status_create(&status, lv_scr_act());

  // add values
  lv_obj_t* time = lv_label_create(lv_scr_act());
  lv_obj_t* co2 = lv_label_create(lv_scr_act());
  lv_obj_t* tmp = lv_label_create(lv_scr_act());
  lv_obj_t* hum = lv_label_create(lv_scr_act());
  lv_obj_t* voc = lv_label_create(lv_scr_act());
  lv_obj_t* nox = lv_label_create(lv_scr_act());
  lv_obj_t* prs = lv_label_create(lv_scr_act());

  // add big values
  lv_obj_t* co2_big = lv_label_create(lv_scr_act());
  lv_obj_t* tmp_big = lv_label_create(lv_scr_act());
  lv_obj_t* hum_big = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(co2_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(tmp_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(hum_big, &fnt_24, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_store_last();

    // await sample, if invalid (after reset)
    if (!al_sample_valid(sample)) {
      sig_await(SIG_SENSOR, 0);
      sample = al_store_last();
    }

    // get accelerometer state
    al_accel_state_t acc = al_accel_get();

    // begin draw
    gfx_begin(false, false);

    // determine vertical
    bool vertical = acc.rotation == 90 || acc.rotation == 270;

    // set display rotation
    lv_disp_set_rotation(NULL, acc.rotation / 90);

    // update status
    lvx_status_update(&status);

    // update values
    lv_label_set_text(time, lvx_fmt("%02d:%02d", hour, minute));
    if (vertical) {
      lv_label_set_text(co2, "ppm CO2");
      lv_label_set_text(tmp, naos_get_b("fahrenheit") ? "° Fahrenheit" : "° Celsius");
      lv_label_set_text(hum, "% RH");
      lv_label_set_text(co2_big, lvx_fmt("%.0f", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp_big, lvx_fmt("%.1f", scr_temp_convert(al_sample_read(sample, AL_SAMPLE_TMP))));
      lv_label_set_text(hum_big, lvx_fmt("%.1f", al_sample_read(sample, AL_SAMPLE_HUM)));
    } else {
      lv_label_set_text(co2, lvx_fmt("%.0f ppm", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp, lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(sample, AL_SAMPLE_TMP))));
      lv_label_set_text(hum, lvx_fmt("%.1f%% RH", al_sample_read(sample, AL_SAMPLE_HUM)));
    }
    lv_label_set_text(voc, lvx_fmt("%.0f VOC", al_sample_read(sample, AL_SAMPLE_VOC)));
    lv_label_set_text(nox, lvx_fmt("%.0f NOX", al_sample_read(sample, AL_SAMPLE_NOX)));
    lv_label_set_text(prs, lvx_fmt("%.0f hPa", al_sample_read(sample, AL_SAMPLE_PRS)));

    // align objects
    if (vertical) {
      lv_obj_align(co2_big, LV_ALIGN_TOP_MID, 0, 20);
      lv_obj_align(co2, LV_ALIGN_TOP_MID, 0, 20 + 27);
      lv_obj_align(tmp_big, LV_ALIGN_TOP_MID, 0, 79);
      lv_obj_align(tmp, LV_ALIGN_TOP_MID, 0, 79 + 27);
      lv_obj_align(hum_big, LV_ALIGN_TOP_MID, 0, 136);
      lv_obj_align(hum, LV_ALIGN_TOP_MID, 0, 136 + 27);
      lv_obj_align(voc, LV_ALIGN_BOTTOM_MID, 0, -85);
      lv_obj_align(nox, LV_ALIGN_BOTTOM_MID, 0, -65);
      lv_obj_align(prs, LV_ALIGN_BOTTOM_MID, 0, -45);
      lv_obj_align(time, LV_ALIGN_BOTTOM_RIGHT, -25, -13);
      lv_obj_align(status.row, LV_ALIGN_BOTTOM_LEFT, 25, -15);
    } else {
      lv_obj_align(status.row, LV_ALIGN_TOP_LEFT, 20, 19);
      lv_obj_align(co2, LV_ALIGN_TOP_LEFT, 19, 53);
      lv_obj_align(tmp, LV_ALIGN_TOP_LEFT, 19, 74);
      lv_obj_align(hum, LV_ALIGN_TOP_LEFT, 19, 95);
      lv_obj_align(time, LV_ALIGN_TOP_LEFT, 148, 21);
      lv_obj_align(voc, LV_ALIGN_TOP_LEFT, 148, 53);
      lv_obj_align(nox, LV_ALIGN_TOP_LEFT, 148, 74);
      lv_obj_align(prs, LV_ALIGN_TOP_LEFT, 148, 95);
      lv_obj_align(co2_big, 0, -100, -100);
      lv_obj_align(tmp_big, 0, -100, -100);
      lv_obj_align(hum_big, 0, -100, -100);
    }

    // end draw
    gfx_end(false, true);

    // sleep until next update
    if (!scr_idle_sleep()) {
      break;
    }
  }

  // cleanup
  gui_cleanup(false);

  return scr_return_unlock;
}

static void* scr_view() {
  // prepare variables
  static int8_t field = 0;
  static bool precision = false;

  // allocate sample buffer
  static al_sample_t* samples = NULL;
  if (samples == NULL) {
    samples = al_calloc(LVX_CHART_SIZE, sizeof(al_sample_t));
  }

  // prepare chart buffer
  static lv_color_t* chart_buffer = NULL;
  if (chart_buffer == NULL) {
    chart_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96));
  }

  // clear memory
  memset(samples, 0, LVX_CHART_SIZE * sizeof(al_sample_t));
  memset(chart_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96));

  // find file, if not live
  dat_file_t* file = NULL;
  if (scr_file != 0) {
    file = dat_find(scr_file, NULL);
    if (file == NULL) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  }

  // check recording
  bool recording = rec_running() && rec_file() == scr_file;

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // add chart
  lv_obj_t* canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(canvas, chart_buffer, 288, 96, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(canvas, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

  // end draw
  gfx_end(true, false);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare source
  al_sample_source_t source = {0};
  if (file == NULL) {
    source = al_store_source();
  } else {
    source = dat_source(scr_file);
  }

  // prepare position
  int32_t position = 0;
  if (!recording) {
    position = source.stop(source.ctx);
  }

  for (;;) {
    // get source info
    size_t source_count = source.count(source.ctx);
    int64_t source_start = source.start(source.ctx);
    int32_t source_stop = source.stop(source.ctx);

    // update recording
    recording = rec_running() && rec_file() == scr_file;

    // adjust position if recording
    if (recording) {
      position = source_stop;
    }

    // calculate resolution
    int32_t resolution = source_stop / LVX_CHART_SIZE;
    if (recording) {
      resolution = SCR_MIN_RESOLUTION;
    } else if (precision) {
      resolution = source_stop / 10 / LVX_CHART_SIZE;
    }
    if (resolution < SCR_MIN_RESOLUTION) {
      resolution = SCR_MIN_RESOLUTION;
    }

    // calculate range
    int32_t start = 0;
    int32_t end = LVX_CHART_SIZE * resolution;
    if (recording) {
      start = position - LVX_CHART_SIZE / 3 * 2 * resolution;
      end = position + LVX_CHART_SIZE / 3 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
    } else if (precision) {
      start = position - LVX_CHART_SIZE / 2 * resolution;
      end = position + LVX_CHART_SIZE / 2 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
      if (end > source_stop) {
        int32_t shift = SCR_MIN(start, end - source_stop);
        end -= shift;
        start -= shift;
      }
    }

    // calculate index
    size_t index = (size_t)roundf(a32_safe_map_f((float)position, (float)start, (float)end, 0, LVX_CHART_SIZE - 1));

    // query samples
    size_t num = 0;
    if (source_count > 0) {
      num = al_sample_query(&source, samples, LVX_CHART_SIZE, start, resolution);
      if (recording) {
        index = num - 1;
      }
    }

    // ensure index is within valid sample range
    if (index >= num) {
      index = num > 0 ? num - 1 : 0;
    }

    // find marks
    uint8_t marks[LVX_CHART_SIZE] = {0};
    if (file != NULL) {
      for (uint8_t i = 0; i < DAT_MARKS; i++) {
        if (file->head.marks[i] > 0) {
          int32_t mark =
              (int32_t)roundf(a32_map_f((float)file->head.marks[i], (float)start, (float)end, 0, LVX_CHART_SIZE - 1));
          if (mark >= 0 && mark <= LVX_CHART_SIZE - 1) {
            marks[(size_t)mark] = i + 1;
          }
        }
      }
    }

    // select current sample
    al_sample_t current = samples[index];

    // parse time
    uint16_t hour;
    uint16_t minute;
    al_clock_epoch_time(source_start + (int64_t)current.off, &hour, &minute, NULL);

    // begin draw
    gfx_begin(false, precision);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (file != NULL) {
      if (recording) {
        bar.mark = file->marks > 0 ? lvx_fmt("(M%d)", file->marks) : "";
      } else {
        bar.mark = marks[index] > 0 ? lvx_fmt("(M%d)", marks[index]) : "";
      }
    }
    if (field == AL_SAMPLE_TMP) {
      bar.value = lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(current, field)));
    } else {
      bar.value = lvx_fmt(scr_field_fmt[field], al_sample_read(current, field));
    }
    lvx_bar_update(&bar);

    // check fahrenheit
    bool fahrenheit = naos_get_b("fahrenheit");

    // prepare range
    float range = 100;  // hum
    if (field == AL_SAMPLE_CO2) {
      range = 3000;
    } else if (field == AL_SAMPLE_TMP) {
      range = fahrenheit ? 120 : 50;
    } else if (field == AL_SAMPLE_VOC) {
      range = 500;
    } else if (field == AL_SAMPLE_NOX) {
      range = 50;
    } else if (field == AL_SAMPLE_PRS) {
      range = 1500;
    }

    // collect values
    float values[LVX_CHART_SIZE] = {0};
    for (size_t i = 0; i < num; i++) {
      al_sample_t sample = samples[i];
      values[i] = al_sample_read(sample, field);
      if (field == AL_SAMPLE_TMP && fahrenheit) {
        values[i] = scr_temp_convert(values[i]);
      }
      if (values[i] > range) {
        range = values[i];
      }
    }

    // draw chart
    lvx_chart_draw((lvx_chart_t){
        .canvas = canvas,
        .range = range,
        .values = values,
        .marks = marks,
        .arrows = precision,
        .offset = source_start,
        .start = start,
        .end = end,
        .stop = source_stop,
        .cursor = !recording,
        .index = (int)index,
    });

    // end draw
    gfx_end(false, false);

    // await event
    sig_type_t filter = SIG_KEYS | SIG_SCROLL;
    if (file == NULL) {
      filter |= SIG_SENSOR;
    } else if (recording) {
      filter |= SIG_APPEND | SIG_STOP;
    }
    sig_event_t event = sig_await(filter, SCR_IDLE_TIMEOUT);

    // handle deadline
    if (event.type & (SIG_SENSOR | SIG_APPEND) && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if (event.type & (SIG_KEYS | SIG_SCROLL)) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // update on append or stop
    if (event.type & (SIG_SENSOR | SIG_APPEND | SIG_STOP)) {
      continue;
    }

    // handle idle timeout
    if (event.type == SIG_TIMEOUT) {
      // cleanup
      gui_cleanup(false);

      // set return
      scr_return_unlock = scr_view;

      return scr_idle;
    }

    // handle escape
    if (event.type == SIG_ESCAPE) {
      // handle precision mode
      if (precision) {
        precision = false;
        continue;
      }

      // cleanup
      gui_cleanup(false);

      // handle recording
      if (recording) {
        // choose option
        int ret = gui_choose(scr_trans()->exit__stop, scr_trans()->exit__back, true, SCR_ACTION_TIMEOUT);
        if (ret == 0) {
          return scr_view;
        }

        // set action, if not set (to not override action set by scr_create)
        if (scr_action == 0) {
          scr_action = STM_FROM_MEASUREMENT;
        }

        // handle stop
        if (ret == 1) {
          // stop recording
          rec_stop();

          // show message
          gui_message(lvx_fmt(scr_trans()->exit__stopped, scr_file_name(file)), SCR_MSG_TIMEOUT);

          // set action
          scr_action = STM_COMP_MEASUREMENT;
        }

        return scr_menu;
      }

      // set action
      scr_action = STM_FROM_ANALYSIS;

      // handle live
      if (scr_file == 0) {
        return scr_menu;
      }

      return scr_edit;
    }

    // handle enter key
    if (event.type == SIG_ENTER) {
      // add mark when recording
      if (recording) {
        rec_mark();
        continue;
      }

      // cancel advanced mode if too less
      if (source_count < LVX_CHART_SIZE) {
        gui_cleanup(false);
        gui_message(scr_trans()->view__not_enough, SCR_MSG_TIMEOUT);
        return scr_view;
      }

      // enter precision mode
      precision = true;

      continue;
    }

    // change mode on up/down
    if (event.type == SIG_UP) {
      field++;
      if (field > AL_SAMPLE_PRS) {
        field = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      field--;
      if (field < 0) {
        field = AL_SAMPLE_PRS;
      }
      continue;
    }

    // change position on left/right/scroll if not recording
    if (!recording) {
      if (event.type == SIG_LEFT) {
        position -= resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_RIGHT) {
        position += resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_SCROLL) {
        position += resolution * (int32_t)event.scroll.fast;
      }
      if (position > source_stop) {
        position = source_stop;
      }
      if (position < 0) {
        position = 0;
      }
    }
  }
}

static void* scr_create() {
  // get free samples
  uint32_t samples = rec_free(true);

  // handle no space
  if (!samples) {
    gui_message(scr_trans()->create__full, SCR_MSG_TIMEOUT);
    return scr_explore;
  }

  // check recording
  if (rec_running()) {
    gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
    return scr_explore;
  }

  // get record rate
  int32_t record_rate = naos_get_l("record-rate");

  // calculate hours at record rate
  int32_t sph = 60 * 60 / record_rate;
  uint32_t hours = samples / sph;

  // confirm creation
  if (!gui_confirm(lvx_fmt(scr_trans()->create__info, dat_next(), hours), scr_trans()->create__start, scr_trans()->back,
                   false, 0)) {
    return scr_explore;
  }

  // confirm import
  bool import = gui_confirm(scr_trans()->create__import, scr_trans()->yes, scr_trans()->no, false, SCR_ACTION_TIMEOUT);

  // determine epoch
  int64_t epoch = al_clock_get_epoch();
  if (import) {
    al_sample_source_t source = al_store_source();
    epoch = source.start(source.ctx);
  }

  // create measurement
  scr_file = dat_create(epoch);

  // confirm and perform data import
  if (import) {
    // set flag
    hmi_set_flag(HMI_FLAG_PROCESS);

    // perform import
    gui_progress_start(scr_trans()->create__importing);
    dat_import(scr_file, 0, gui_progress_update);
    gui_cleanup(false);

    // clear flag
    hmi_clear_flag(HMI_FLAG_PROCESS);

    // write message
    gui_message(scr_trans()->create__imported, SCR_MSG_TIMEOUT);
  }

  // start recording
  rec_start(scr_file);

  // set action
  if (scr_file == 1) {
    scr_action = STM_START_FIRST_MEASUREMENT;
  } else {
    scr_action = STM_START_MEASUREMENT;
  }

  return scr_view;
}

static void* scr_edit() {
  // begin draw
  gfx_begin(false, false);

  // find file
  dat_file_t* file = dat_find(scr_file, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return NULL;
  }

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_file_name(file));
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_label_set_text(info, scr_file_info(file));
  lv_obj_align(info, LV_ALIGN_TOP_LEFT, 5, 26);

  // add signs
  lvx_sign_t analyze = {
      .title = "A",
      .text = scr_trans()->edit__analyse,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t delete = {
      .title = "<",
      .text = scr_trans()->delete__delete,
      .align = LV_ALIGN_BOTTOM_LEFT,
      .offset = -25,
  };
  lvx_sign_t export = {
      .title = ">",
      .text = scr_trans()->edit__export,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -25,
  };
  lvx_sign_create(&analyze, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&delete, lv_scr_act());
  lvx_sign_create(&export, lv_scr_act());

  // end draw
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_LEFT | SIG_RIGHT, SCR_ACTION_TIMEOUT);

    // cleanup
    gui_cleanup(false);

    // handle delete
    if (event.type == SIG_LEFT) {
      // confirm deletion
      const char* msg = lvx_fmt(scr_trans()->delete__confirm, scr_file_name(file));
      if (!gui_confirm(msg, scr_trans()->delete__delete, scr_trans()->back, false, SCR_ACTION_TIMEOUT)) {
        return scr_edit;
      }

      // delete file
      uint16_t num = file->head.num;
      dat_delete(file->head.num);
      gui_message(lvx_fmt(scr_trans()->delete__deleted, num), SCR_MSG_TIMEOUT);

      // set action
      scr_action = STM_DEL_MEASUREMENT;

      return scr_explore;
    }

    // handle export
    if (event.type == SIG_RIGHT) {
      // set flag
      hmi_set_flag(HMI_FLAG_PROCESS);

      // perform export
      gui_progress_start(scr_trans()->edit__exporting);
      bool ok = dat_export(scr_file, gui_progress_update);
      gui_cleanup(false);

      // clear flag
      hmi_clear_flag(HMI_FLAG_PROCESS);

      // export file
      if (!ok) {
        gui_message(scr_trans()->edit__export_fail, SCR_MSG_TIMEOUT);
      } else {
        gui_message(scr_trans()->edit__export_done, SCR_MSG_TIMEOUT);
      }

      return scr_edit;
    }

    // handle event
    switch (event.type) {
      case SIG_ESCAPE:
      case SIG_TIMEOUT:
        return scr_explore;
      case SIG_ENTER:
        return scr_view;
      default:
        ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

static gui_list_item_t scr_explore_cb(int num, void* ctx) {
  // handle create
  if (num == 0) {
    return (gui_list_item_t){
        .title = scr_trans()->explore__create,
        .info = "",
    };
  }

  // get file
  dat_file_t* file = dat_get(num - 1);

  return (gui_list_item_t){
      .title = scr_file_name(file),
      .info = scr_file_info(file),
  };
}

static void* scr_explore() {
  // prepare state
  static int offset = 0;

  // get total
  size_t total = dat_count();

  // ignore last if recording
  if (rec_running()) {
    total--;
  }

  // get start
  int start = 0;
  if (dat_find(scr_file, &start)) {
    start++;
  }

  // show list
  int selected = gui_list((int)total + 1, start, &offset, scr_trans()->explore__select, scr_trans()->back,
                          scr_explore_cb, NULL, SCR_ACTION_TIMEOUT);
  if (selected < 0) {
    return scr_menu;
  }

  // handle create
  if (selected == 0) {
    scr_file = 0;
    return scr_create;
  }

  // set file
  scr_file = dat_get(selected - 1)->head.num;

  return scr_edit;
}

static void* scr_usb() {
  // check recording
  if (rec_running()) {
    // show message
    gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);

    return scr_menu;
  }

  // check connection
  if (!al_power_get().has_usb) {
    // show message
    gui_message(scr_trans()->usb__disconnected, SCR_MSG_TIMEOUT);

    return scr_menu;
  }

  // set modal flag
  hmi_set_flag(HMI_FLAG_MODAL);

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->usb__active);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // enable USB
  dat_enable_usb();

  // await escape
  sig_event_t event = sig_await(SIG_ESCAPE | SIG_EJECT, 0);

  // disable USB
  dat_disable_usb();

  // cleanup
  gui_cleanup(false);

  // clear modal flag
  hmi_clear_flag(HMI_FLAG_MODAL);

  // show message on eject
  if (event.type == SIG_EJECT) {
    gui_message(scr_trans()->usb__eject, SCR_MSG_TIMEOUT);
  }

  return scr_menu;
}

static void* scr_ble() {
  // wait for com to start
  while (!com_started()) {
    naos_delay(100);
  }

  // set modal flag
  hmi_set_flag(HMI_FLAG_MODAL);

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, lvx_fmt(scr_trans()->ble__active, naos_get_s("device-name")));
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // enable pairing
  naos_ble_enable_pairing();

  // await escape
  sig_await(SIG_ESCAPE, 0);

  // disable pairing
  naos_ble_disable_pairing();

  // cleanup
  gui_cleanup(false);

  // clear modal flag
  hmi_clear_flag(HMI_FLAG_MODAL);

  return scr_menu;
}

static gui_list_item_t scr_config_cb(int num, void* ctx) {
  // get translation
  const scr_trans_t* t = scr_trans();

  // handle config items
  switch (num) {
    case 0: {
      return (gui_list_item_t){
          .title = t->config__language,
          .info = scr_lang_str[scr_lang()],
      };
    }
    case 1: {
      // get date
      uint16_t year, month, day;
      al_clock_get_date(&year, &month, &day);

      return (gui_list_item_t){
          .title = t->config__date,
          .info = lvx_fmt("%d-%02d-%02d", year, month, day),
      };
    }
    case 2: {
      // get time
      uint16_t hour, minute, seconds;
      al_clock_get_time(&hour, &minute, &seconds);

      return (gui_list_item_t){
          .title = t->config__time,
          .info = lvx_fmt("%02d:%02d", hour, minute),
      };
    }
    case 3: {
      return (gui_list_item_t){
          .title = t->config__sleep_rate,
          .info = lvx_fmt("%lds", naos_get_l("sleep-rate")),
      };
    }
    case 4: {
      return (gui_list_item_t){
          .title = t->config__record_rate,
          .info = lvx_fmt("%lds", naos_get_l("record-rate")),
      };
    }
    case 5: {
      return (gui_list_item_t){
          .title = t->config__long_interval,
          .info = lvx_fmt("%lds", naos_get_l("long-interval")),
      };
    }
    case 6: {
      return (gui_list_item_t){
          .title = t->config__temp_unit,
          .info = naos_get_b("fahrenheit") ? "°F" : "°C",
      };
    }
    case 7: {
      return (gui_list_item_t){
          .title = t->config__developer,
          .info = naos_get_b("developer") ? t->on : t->off,
      };
    }
    case 8: {
      return (gui_list_item_t){
          .title = t->config__power_light,
          .info = naos_get_b("power-light") ? t->on : t->off,
      };
    }
    case 9: {
      return (gui_list_item_t){
          .title = t->config__wifi_network,
          .info = lvx_truncate(naos_get_s("wifi-ssid"), 20),
      };
    }
    case 10: {
      return (gui_list_item_t){
          .title = "MQTT Broker",
          .info = lvx_truncate(naos_get_s("mqtt-host"), 20),
      };
    }
    case 11: {
      return (gui_list_item_t){
          .title = "Home Assistant",
          .info = naos_get_b("mqtt-ha") ? t->on : t->off,
      };
    }
    case 12: {
      return (gui_list_item_t){
          .title = t->config__ble_prev_sleep,
          .info = naos_get_b("ble-prev-sleep") ? t->on : t->off,
      };
    }
    case 13: {
      return (gui_list_item_t){
          .title = t->config__mqtt_prev_sleep,
          .info = naos_get_b("mqtt-prev-sleep") ? t->on : t->off,
      };
    }
    case 14: {
      return (gui_list_item_t){
          .title = t->config__ble_pairing,
          .info = naos_get_b("ble-pairing") ? t->on : t->off,
      };
    }
    case 15: {
      return (gui_list_item_t){
          .title = t->config__ble_bonding,
          .info = naos_get_b("ble-bonding") ? t->on : t->off,
      };
    }
    case 16: {
      return (gui_list_item_t){
          .title = t->config__ble_clear,
          .info = t->execute,
      };
    }
    case 17: {
      return (gui_list_item_t){
          .title = t->config__reset,
          .info = t->execute,
      };
    }
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
      return (gui_list_item_t){0};
  }
}

static void* scr_config() {
  // prepare state
  static int offset = 0;
  static int selected = 0;

  // get translation
  const scr_trans_t* t = scr_trans();

  for (;;) {
    // select parameter
    int choice = gui_list(18, selected, &offset, t->change, t->back, scr_config_cb, NULL, SCR_ACTION_TIMEOUT);
    if (choice < 0) {
      return scr_settings;
    }

    // store choice
    selected = choice;

    // handle choice
    switch (choice) {
      case 0: {
        // toggle language between en and de
        scr_lang_t lang = scr_lang();
        if (lang == SCR_DE) {
          naos_set_s("language", "en");
        } else if (lang == SCR_EN) {
          naos_set_s("language", "es");
        } else {
          naos_set_s("language", "de");
        }

        // reload screen
        return scr_config;
      }

      case 1: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config;
        }

        // change date
        scr_date();

        break;
      }

      case 2: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config;
        }

        // change time
        scr_time();

        break;
      }

      case 3: {
        // cycle through sensor rates
        int32_t value = naos_get_l("sleep-rate");
        if (value == 5) {
          naos_set_l("sleep-rate", 30);
        } else if (value == 30) {
          naos_set_l("sleep-rate", 60);
        } else {
          naos_set_l("sleep-rate", 5);
        }

        break;
      }

      case 4: {
        // cycle through sensor rates
        int32_t value = naos_get_l("record-rate");
        if (value == 5) {
          naos_set_l("record-rate", 30);
        } else if (value == 30) {
          naos_set_l("record-rate", 60);
        } else {
          naos_set_l("record-rate", 5);
        }

        break;
      }

      case 5: {
        // use wheel to change long interval
        int32_t value = naos_get_l("long-interval");
        if (gui_wheel(t->config__long_interval, &value, 30, 10, 900, t->save, t->cancel, "%lds", SCR_ACTION_TIMEOUT)) {
          naos_set_l("long-interval", value);
        }

        break;
      }

      case 6: {
        // toggle fahrenheit temperature setting
        naos_set_b("fahrenheit", !naos_get_b("fahrenheit"));

        break;
      }

      case 7: {
        // toggle developer mode
        naos_set_b("developer", !naos_get_b("developer"));

        break;
      }

      case 8: {
        // toggle power light
        naos_set_b("power-light", !naos_get_b("power-light"));

        break;
      }

      case 12: {
        // toggle BLE no sleep
        naos_set_b("ble-prev-sleep", !naos_get_b("ble-prev-sleep"));

        break;
      }

      case 13: {
        // toggle MQTT no sleep
        naos_set_b("mqtt-prev-sleep", !naos_get_b("mqtt-prev-sleep"));

        break;
      }

      case 14: {
        // toggle BLE pairing
        bool value = !naos_get_b("ble-pairing");
        naos_set_b("ble-pairing", value);
        if (gui_confirm(lvx_fmt("Pairing: %s\n\nRestart now?", value ? "ON" : "OFF"), scr_trans()->yes, scr_trans()->no,
                        false, SCR_ACTION_TIMEOUT)) {
          esp_restart();
        }

        break;
      }

      case 15: {
        // toggle BLE bonding
        bool value = !naos_get_b("ble-bonding");
        naos_set_b("ble-bonding", value);
        if (gui_confirm(lvx_fmt("Bonding: %s\n\nRestart now?", value ? "ON" : "OFF"), scr_trans()->yes, scr_trans()->no,
                        false, SCR_ACTION_TIMEOUT)) {
          esp_restart();
        }

        break;
      }

      case 16: {
        // clear BLE peers
        naos_ble_peerlist_clear();
        naos_ble_allowlist_clear();
        gui_message(scr_trans()->config__ble_cleared, SCR_MSG_TIMEOUT);

        break;
      }

      case 17: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config;
        }

        // confirm reset
        if (!gui_confirm(scr_trans()->reset__confirm, scr_trans()->yes, scr_trans()->no, true, SCR_ACTION_TIMEOUT)) {
          return scr_config;
        }

        // reset data
        dat_reset();

        // reset settings
        naos_reset();

        // reset BLE lists
        naos_ble_peerlist_clear();
        naos_ble_allowlist_clear();

        // show message
        gui_message(scr_trans()->reset__reset, SCR_MSG_TIMEOUT);

        // restart device
        esp_restart();

        break;
      }

      default:
        // show read-only message for AL Studio managed settings
        gui_message(t->config__studio, SCR_ACTION_TIMEOUT);
    }
  }
}

static void* scr_check() {
  // date
  uint16_t year;
  al_clock_init();
  al_clock_get_date(&year, NULL, NULL);
  if (year < 2026) {
    gui_message("Date check failed!", SCR_MSG_TIMEOUT);
    return scr_develop;
  }

  // interrupt
  int64_t start = naos_millis();
  gui_write("Checking interrupts...", true);
  al_sleep(false, 5 * 1000);
  gui_cleanup(false);
  if (naos_millis() - start < 4 * 1000) {
    gui_message("Interrupt check failed!", SCR_MSG_TIMEOUT);
    return scr_develop;
  }

  // buttons
  gui_write("Press all buttons once...", false);
  sig_type_t pressed = 0;
  while ((pressed & SIG_KEYS) != SIG_KEYS) {
    pressed |= sig_await(SIG_KEYS, 0).type;
  }

  // accel
  gui_cleanup(false);
  gui_write("Rotate the device...", false);
  for (;;) {
    sig_await(SIG_MOTION, 0);
    al_accel_state_t state = al_accel_get();
    if (state.front && state.rotation != 0) {
      break;
    }
  }

  // touch
  gui_cleanup(false);
  gui_write("Scroll the touch strip...", false);
  for (;;) {
    sig_event_t event = sig_await(SIG_SCROLL, 0);
    if (event.scroll.std >= 2 || event.scroll.std <= -2) {
      break;
    }
  }

  // power
  gui_cleanup(false);
  gui_write("Plug in USB power...", false);
  for (;;) {
    sig_await(SIG_POWER, 0);
    if (al_power_get().has_usb) {
      break;
    }
  }

  // buzzer
  gui_cleanup(false);
  gui_write("Listen for the buzzer...", false);
  al_buzzer_beep(4400, 200, true);
  naos_delay(1000);
  al_buzzer_beep(440, 200, true);
  naos_delay(1000);

  // LED
  gui_cleanup(false);
  gui_write("Check the LED colors...", false);
  hmi_set_flag(HMI_FLAG_IGNORE);
  naos_delay(250);
  al_led_set(255, 0, 0);
  naos_delay(1000);
  al_led_set(0, 255, 0);
  naos_delay(1000);
  al_led_set(0, 0, 255);
  naos_delay(1000);
  al_led_set(0, 0, 0);
  hmi_clear_flag(HMI_FLAG_IGNORE);

  // sensors
  for (;;) {
    gui_cleanup(false);
    al_sample_t sample = al_store_last();
    float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
    float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
    float hum = al_sample_read(sample, AL_SAMPLE_HUM);
    float voc = al_sample_read(sample, AL_SAMPLE_VOC);
    gui_write(
        lvx_fmt("Blow on the sensors...\nCO2: %.0f/2500, TMP: %.1f/26\nHUM: %.0f/60, VOC: %.0f/50", co2, tmp, hum, voc),
        false);
    sig_await(SIG_SENSOR, 0);
    al_sample_t state = al_store_last();
    if (state.co2 > 2500 && state.tmp > 25 && state.hum > 60 && state.voc > 50) {
      break;
    }
  }

  // cleanup
  gui_cleanup(false);

  return scr_develop;
}

static gui_list_item_t scr_about_cb(int num, void* ctx) {
  // get translation
  const scr_trans_t* t = scr_trans();

  // handle config items
  switch (num) {
    case 0: {
      return (gui_list_item_t){
          .title = t->about__device_name,
          .info = naos_get_s("device-name"),
      };
    }
    case 1: {
      // get authentication data
      naos_auth_data_t auth = {0};
      naos_auth_describe(&auth);

      return (gui_list_item_t){
          .title = t->about__serial_number,
          .info = lvx_fmt("NA-AL1-R%d/%d", auth.revision, auth.batch),
      };
    }
    case 2: {
      // find "g" in version
      int n = (int)(strchr(naos_config()->app_version, 'g') - naos_config()->app_version) - 1;

      return (gui_list_item_t){
          .title = t->about__firmware_version,
          .info = lvx_fmt("%.*s", n, naos_config()->app_version),
      };
    }
    case 3: {
      // get info
      al_storage_info_t info = al_storage_info(AL_STORAGE_INT);

      return (gui_list_item_t){
          .title = t->about__internal_storage,
          .info = lvx_fmt("%.2f %% of %.1f MB", info.usage * 100.f, (float)info.total / 1024.f / 1024.f),
      };
    }
    case 4: {
      // get info
      al_storage_info_t info = al_storage_info(AL_STORAGE_EXT);

      return (gui_list_item_t){
          .title = t->about__external_storage,
          .info = lvx_fmt("%.2f %% of %.1f MB", info.usage * 100.f, (float)info.total / 1024.f / 1024.f),
      };
    }
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
      return (gui_list_item_t){0};
  }
}

static void* scr_about() {
  // prepare state
  static int offset = 0;
  static int selected = 0;

  // get translation
  const scr_trans_t* t = scr_trans();

  for (;;) {
    // select parameter
    int choice = gui_list(5, selected, &offset, NULL, t->back, scr_about_cb, NULL, SCR_ACTION_TIMEOUT);
    if (choice < 0) {
      return scr_settings;
    }

    // store choice
    selected = choice;
  }
}

static void* scr_settings() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->settings__title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add signs
  lvx_sign_t about = {
      .title = "A",
      .text = scr_trans()->settings__about,
      .align = LV_ALIGN_BOTTOM_LEFT,
      .offset = -25,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t regulatory = {
      .title = "↑",
      .text = scr_trans()->settings__regulatory,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -75,
  };
  lvx_sign_t info = {
      .title = "<",
      .text = scr_trans()->settings__introduction,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -50,
  };
  lvx_sign_t off = {
      .title = ">",
      .text = scr_trans()->settings__config,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -25,
  };
  lvx_sign_t config = {
      .title = "↓",
      .text = scr_trans()->settings__off,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&about, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&regulatory, lv_scr_act());
  lvx_sign_create(&info, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());
  lvx_sign_create(&config, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await event
  sig_event_t event = sig_await(SIG_KEYS, SCR_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  // handle power off
  if (event.type == SIG_DOWN) {
    // check recording
    if (rec_running()) {
      gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
      return scr_settings;
    }

    // turn off
    scr_power_off(false, true);

    return scr_settings;
  }

  // handle regulatory
  if (event.type == SIG_UP) {
    // prepare texts
    const char* texts[] = {
        "Product Information\n\nAir Lab - Portable Air Quality Monitor\nNA-AL1 / Made in Switzerland\n© 2025 Networked "
        "Artifacts Inc.\nhttps://networkedartifacts.com/airlab",
        "EU Regulations\n\nThis product complies with the following directives:\n2014/53/EU (RED)\n2011/65/EU (RoHS)",
        "FCC Statement 1/2\n\nThis device is certified under FCC ID 2BTBB-NA-AL1.\nThis device complies with Part 15 "
        "of the FCC rules.",
        "FCC Statement 2/2\n\nOperation is subject to the following two conditions:\n1. This device may not cause "
        "harmful interference; and\n2. This device must accept any interference received,\nincluding interference that "
        "may cause undesired operation.",
        NULL,
    };

    // show regulatory info
    gui_cycle(true, texts, scr_trans()->next, scr_trans()->back);

    return scr_settings;
  }

  // handle introduction
  if (event.type == SIG_LEFT) {
    // show introduction info
    gui_cycle(false, scr_trans()->intro__infos, scr_trans()->next, scr_trans()->back);

    return scr_settings;
  }

  // handle event
  switch (event.type) {
    case SIG_RIGHT:
      return scr_config;
    case SIG_ENTER:
      return scr_about;
    case SIG_ESCAPE:
    case SIG_TIMEOUT:
      // set action
      scr_action = STM_FROM_SETTINGS;

      return scr_menu;
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
  }

  return scr_settings;
}

static gui_list_item_t scr_engine_cb(int num, void* _) {
  return (gui_list_item_t){
      .title = eng_get(num)->title,
      .info = eng_get(num)->version,
  };
}

static void* scr_engine() {
  // prepare state
  static int selected = 0;
  static int offset = 0;

  // reload engine
  eng_reload();

  // get count
  int count = (int)eng_num();

  // check count
  if (count == 0) {
    gui_message(scr_trans()->engine__empty, SCR_MSG_TIMEOUT);
    return scr_menu;
  }

  for (;;) {
    // select plugin
    selected = gui_list(count, selected, &offset, scr_trans()->engine__run, scr_trans()->back, scr_engine_cb, NULL,
                        SCR_ACTION_TIMEOUT);
    if (selected < 0) {
      return scr_menu;
    }

    // launch plugin
    scr_launch(eng_get(selected)->file);
  }
}

static void* scr_develop() {
  // prepare variables
  static int selected = 0;
  static int offset = 0;

  // prepare labels
  const char* labels[] = {
      "System Info",   "Self Check",   "Shipping Mode", "Sensor Data",  "Sleep Mode", "CPU Reset", "Power Off",
      "Clear Display", "Test Bubbles", "Touch Info",    "Compensation", "Buzzer",     NULL,
  };

  for (;;) {
    // select item
    selected = gui_list_strings(selected, &offset, labels, "Select", "Cancel", SCR_ACTION_TIMEOUT);
    if (selected < 0) {
      return scr_menu;
    }

    // handle system info
    if (selected == 0) {
      return scr_info;
    }

    // handle device check
    if (selected == 1) {
      return scr_check;
    }

    // handle ship mode
    if (selected == 2) {
      // disable developer mode
      naos_set_b("developer", false);

      // begin draw
      gfx_begin(false, false);

      // show image
      lv_obj_t* img = lv_img_create(lv_scr_act());
      lv_img_set_src(img, &img_shipping_mode);
      lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 20);

      // show message
      lv_obj_t* lbl = lv_label_create(lv_scr_act());
      lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
      lv_label_set_text(lbl, "Yo! Plug in a USB-C cable,\nand press <A> to begin.");
      lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
      lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

      // end draw
      gfx_end(false, false);

      // show message
      naos_delay(1000);

      // enable ship mode
      al_power_ship();

      // clean up in case ship mode did not work
      gui_cleanup(false);
    }

    // handle sensor data
    if (selected == 3) {
      return scr_sensor;
    }

    // handle sleep
    if (selected == 4) {
      // determine deep
      bool deep = gui_confirm("Which sleep mode?", "Deep", "Light", false, 0);

      // log sleep
      naos_log("sleeping... (deep=%d)", deep);

      // set return
      scr_return_unlock = scr_develop;

      // write message
      if (deep) {
        gui_write("Deep Sleeping...\nPress <A> to wake up.", true);
      } else {
        gui_write("Light Sleeping...\nPress <A> to wake up.", true);
      }

      // perform sleep
      al_trigger_t trigger = al_sleep(deep, 0);

      // capture enter when unlocked
      if (trigger == AL_BUTTON) {
        sig_await(SIG_ENTER, 1000);
      }

      // clean up
      gui_cleanup(false);

      // log wakeup
      naos_log("woke up!");
    }

    // handle power reset
    if (selected == 5) {
      esp_restart();
    }

    // handle power off
    if (selected == 6) {
      scr_power_off(false, false);
    }

    // handle clear display
    if (selected == 7) {
      gui_cleanup(true);
    }

    // handle bubbles test
    if (selected == 8) {
      return scr_bubbles;
    }

    // handle touch info
    if (selected == 9) {
      // prepare data
      float position = NAN;
      float scroll = 0;
      float scroll_fast = 0;

      for (;;) {
        // update screen
        gui_write(lvx_fmt("Position: %.1f\nScroll: %.1f, %.1f", position, scroll, scroll_fast), false);

        // await event
        sig_event_t event = sig_await(SIG_ESCAPE | SIG_TOUCH | SIG_SCROLL, 0);

        // cleanup
        gui_cleanup(false);

        // handle events
        if (event.type & SIG_ESCAPE) {
          break;
        } else if (event.type & SIG_TOUCH) {
          position = event.position;
        } else if (event.type & SIG_SCROLL) {
          scroll = event.scroll.std;
          scroll_fast = event.scroll.fast;
        }
      }
    }

    // handle compensation
    if (selected == 10) {
      // prepare variables
      al_sensor_rate_t rate = AL_SENSOR_RATE_5S;

      for (;;) {
        // get last sample
        al_sample_t sample = al_store_last();
        float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
        float hum = al_sample_read(sample, AL_SAMPLE_HUM);

        // update screen
        gui_write(lvx_fmt("Rate: %d\nTemp: %.1f\nHum: %.1f", rate, tmp, hum), false);

        // await event
        sig_event_t event = sig_await(SIG_SENSOR | SIG_ESCAPE | SIG_ENTER, 0);

        // cleanup
        gui_cleanup(false);

        // handle events
        if (event.type & SIG_ESCAPE) {
          break;
        } else if (event.type & SIG_ENTER) {
          rate++;
          if (rate > AL_SENSOR_RATE_60S) {
            rate = AL_SENSOR_RATE_5S;
          }
          al_sensor_set_rate(rate);
        }
      }
    }

    // handle buzzer
    if (selected == 11) {
      // begin draw
      gfx_begin(false, false);

      // add row
      lv_obj_t* row = lv_obj_create(lv_scr_act());
      lv_obj_set_size(row, 200, 100);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
      lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

      // prepare wheels
      lvx_wheel_t hertz = {.value = 2000, .min = 0, .step = 50, .max = 8000, .format = "%04d", .fixed = true};
      lvx_wheel_t duration = {.value = 100, .min = 0, .step = 100, .max = 5000, .format = "%04d", .fixed = true};
      lvx_wheel_create(&hertz, row);
      lvx_wheel_create(&duration, row);

      // add button
      lvx_sign_t back = {
          .title = "B",
          .text = "Exit",
          .align = LV_ALIGN_BOTTOM_LEFT,
      };
      lvx_sign_t next = {
          .title = "A",
          .text = "Play",
          .align = LV_ALIGN_BOTTOM_RIGHT,
      };
      lvx_sign_create(&back, lv_scr_act());
      lvx_sign_create(&next, lv_scr_act());

      // focus first wheel
      lvx_wheel_focus(&hertz, true);

      // end draw
      gfx_end(false, false);

      // prepare wheels
      lvx_wheel_t* wheels[] = {&hertz, &duration};
      int cur_wheel = 0;

      for (;;) {
        // await event
        sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, 0);

        // apply wheel events
        if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
          gfx_begin(false, false);
          lvx_wheel_group_update(wheels, 2, event, &cur_wheel);
          gfx_end(false, false);
          continue;
        }

        // handle enter
        if (event.type == SIG_ENTER) {
          // play beep
          al_buzzer_beep(hertz.value, duration.value, false);

          continue;
        }

        // cleanup
        gui_cleanup(false);

        break;
      }
    }
  }
}

static void* scr_menu() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum, voc, nox, prs
  static int8_t opt = 0;   // create, explore, settings, usb, ble, plugins, develop
  static bool fan_alt = false;

  // get settings
  bool developer = naos_get_b("developer");

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // add line
  lv_obj_t* line = lv_obj_create(lv_scr_act());
  lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, -8);
  lv_obj_set_width(line, lv_pct(100));
  lv_obj_set_height(line, 2);
  lv_obj_set_style_border_width(line, 2, LV_PART_MAIN);
  lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_color(line, lv_color_black(), LV_PART_MAIN);

  // add robin
  lv_obj_t* robin = lv_img_create(lv_scr_act());
  lv_img_set_src(robin, &img_robin_standing);
  lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);

  // add lab
  lv_obj_t* lab = lv_img_create(lv_scr_act());
  lv_img_set_src(lab, &img_lab);
  lv_obj_align(lab, LV_ALIGN_BOTTOM_RIGHT, -5, -10);

  // add icon
  lv_obj_t* icon = lv_img_create(lv_scr_act());
  lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 1, -38);

  // add fan
  lv_obj_t* fan = lv_img_create(lv_scr_act());
  lv_obj_align(fan, LV_ALIGN_BOTTOM_RIGHT, -19, -35);

  // add chart
  lv_obj_t* chart = lv_canvas_create(lv_scr_act());
  lv_color_t chart_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(24, 16)] = {0};
  lv_canvas_set_buffer(chart, chart_buffer, 24, 16, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_RIGHT, -87, -53);
  lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);

  // add drain
  lv_obj_t* drain = lv_canvas_create(lv_scr_act());
  lv_color_t drain_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(22, 11)] = {0};
  lv_canvas_set_buffer(drain, drain_buffer, 22, 11, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(drain, LV_ALIGN_BOTTOM_RIGHT, -21, -71);
  lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);

  // add bubble
  lvx_bubble_t bubble = {};
  lvx_bubble_create(&bubble, lv_scr_act());

  // end draw
  gfx_end(true, false);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare flags
  bool urgent = true;
  bool fun = false;

  // prepare statement
  stm_entry_t* statement = NULL;

  // prepare sample source
  al_sample_source_t source = al_store_source();

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_store_last();

    // query sensor
    float values[SCR_HIST_POINTS] = {0};
    float min = 0, max = 0;
    al_sample_pick(&source, (al_sample_field_t)mode, SCR_HIST_POINTS, values, &min, &max);

    // query statement
    if (statement == NULL && (urgent || fun)) {
      statement = stm_query(urgent, scr_action);
    }

    // get power
    al_power_state_t power = al_power_get();

    // power off (no return) if battery is low and not charging
    if (power.bat_low && !power.has_usb && !power.charging) {
      scr_power_off(true, true);
    }

    // begin draw
    gfx_begin(false, false);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (!al_sample_valid(sample)) {
      bar.value = scr_trans()->menu__no_data;
    } else {
      if (mode == AL_SAMPLE_TMP) {
        bar.value = lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(sample, mode)));
      } else {
        bar.value = lvx_fmt(scr_field_fmt[mode], al_sample_read(sample, mode));
      }
    }
    lvx_bar_update(&bar);

    // set icon
    if (opt == 0) {
      lv_img_set_src(icon, rec_running() ? &img_file2 : &img_file1);
    } else if (opt == 1) {
      lv_img_set_src(icon, &img_folder);
    } else if (opt == 2) {
      lv_img_set_src(icon, &img_cog);
    } else if (opt == 3) {
      lv_img_set_src(icon, &img_usb);
    } else if (opt == 4) {
      lv_img_set_src(icon, &img_ble);
    } else if (opt == 5) {
      lv_img_set_src(icon, &img_engine);
    } else if (opt == 6) {
      lv_img_set_src(icon, &img_wrench);
    }

    // set fan
    if (fan_alt) {
      lv_img_set_src(fan, &img_fan2);
    } else {
      lv_img_set_src(fan, &img_fan1);
    }

    // draw chart
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    lv_point_t points[SCR_HIST_POINTS] = {0};
    for (size_t i = 0; i < SCR_HIST_POINTS; i++) {
      points[i].x = (lv_coord_t)a32_safe_map_i((int32_t)i, 0, SCR_HIST_POINTS - 1, 0, 24);
      points[i].y = (lv_coord_t)a32_safe_map_f(values[i], min, max, 14, 2);
    }
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    lv_canvas_draw_line(chart, points, SCR_HIST_POINTS, &line_dsc);

    // draw drain
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = (lv_coord_t)a32_safe_map_f(values[SCR_HIST_POINTS - 1], min, max, 0, 9);
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    lv_canvas_draw_rect(drain, 1, (lv_coord_t)(1 + 9 - drain_height), 20, drain_height, &rect_dsc);

    // set bubble text
    switch (scr_lang()) {
      case SCR_DE:
        bubble.text = statement ? statement->text_de : NULL;
        break;
      case SCR_EN:
        bubble.text = statement ? statement->text_en : NULL;
        break;
      case SCR_ES:
        bubble.text = statement ? statement->text_es : NULL;
        break;
    }

    // update bubble
    lvx_bubble_update(&bubble);

    // update robin
    if (statement) {
      switch (statement->mood) {
        case STM_HAPPY:
          lv_img_set_src(robin, &img_robin_happy);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_COLD:
          lv_img_set_src(robin, &img_robin_cold);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_ANGRY1:
          lv_img_set_src(robin, &img_robin_angry1);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_ANGRY2:
          lv_img_set_src(robin, &img_robin_angry2);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 11, -10);
          break;
        case STM_STANDING:
          lv_img_set_src(robin, &img_robin_standing);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_POINTING:
          lv_img_set_src(robin, &img_robin_pointing);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_WORKING:
          lv_img_set_src(robin, &img_robin_working);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
      }
    } else {
      lv_img_set_src(robin, &img_robin_standing);
      lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    }

    // end draw
    gfx_end(false, false);

    // clear flags
    urgent = false;
    fun = false;

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_INTERRUPT | SIG_LAUNCH | SIG_KEYS, 0);

    // handle deadline
    if (event.type & (SIG_SENSOR | SIG_INTERRUPT) && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if (event.type & (SIG_KEYS | SIG_LAUNCH)) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // clear statement and action on any key
    if (statement != NULL && (event.type & SIG_KEYS)) {
      statement = NULL;
      scr_action = 0;
      continue;
    }

    // enter idle screen on escape
    if (event.type == SIG_ESCAPE) {
      // cleanup
      gui_cleanup(false);

      // set return
      scr_return_unlock = scr_menu;

      return scr_idle;
    }

    // loop on sensor or interrupt
    if (event.type & (SIG_SENSOR | SIG_INTERRUPT)) {
      // cycle fan on sensor value
      if (event.type & SIG_SENSOR) {
        fan_alt = !fan_alt;
      }

      // show fun fact after half of deadline expired
      if (deadline - naos_millis() < SCR_IDLE_TIMEOUT / 2) {
        fun = true;
      }

      continue;
    }

    // start engine on launch
    if (event.type == SIG_LAUNCH) {
      // run engine
      scr_launch(event.file);

      return scr_menu;
    }

    // change mode on up/down
    if (event.type == SIG_UP) {
      mode++;
      if (mode > 5) {
        mode = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 5;
      }
      continue;
    }

    // change opt left/right
    if (event.type == SIG_LEFT) {
      opt--;
      if (opt < 0) {
        opt = developer ? 6 : 5;
      }
      continue;
    } else if (event.type == SIG_RIGHT) {
      opt++;
      if (opt > (developer ? 6 : 5)) {
        opt = 0;
      }
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // clear action
    scr_action = 0;

    // enter idle screen on timeout
    if (event.type == SIG_TIMEOUT) {
      // set return
      scr_return_unlock = scr_menu;

      return scr_idle;
    }

    // handle enter
    if (event.type == SIG_ENTER) {
      switch (opt) {
        case 0:
          scr_file = rec_running() ? rec_file() : 0;
          return scr_view;
        case 1:
          return scr_explore;
        case 2:
          return scr_settings;
        case 3:
          return scr_usb;
        case 4:
          return scr_ble;
        case 5:
          return scr_engine;
        case 6:
          return scr_develop;
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_intro() {
  // skip if developer
  if (naos_get_b("developer")) {
    return scr_menu;
  }

  // wait a bit
  naos_delay(1000);

  // show robin
  gfx_begin(false, false);
  lv_obj_t* img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_robin_standing);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  gfx_end(false, false);
  naos_delay(3000);
  gui_cleanup(false);

  // show messages
  gui_message(scr_trans()->intro__hello1, 5000);
  gui_message(scr_trans()->intro__hello2, 5000);

  for (;;) {
    // format current date/time
    uint16_t year, month, day, hour, minute, seconds;
    al_clock_get_date(&year, &month, &day);
    al_clock_get_time(&hour, &minute, &seconds);
    const char* date_time = lvx_fmt(scr_trans()->intro__watch, hour, minute, year, month, day);

    // confirm date/time
    if (gui_confirm(date_time, scr_trans()->intro__correct, scr_trans()->intro__adjust, false, SCR_ACTION_TIMEOUT)) {
      break;
    }

    // otherwise, update date/time
    if (scr_date()) {
      scr_time();
    }
  }

  // test knowledge
  if (gui_confirm(scr_trans()->intro__test, scr_trans()->yes, scr_trans()->no, false, SCR_ACTION_TIMEOUT)) {
    gui_cycle(false, scr_trans()->intro__infos, scr_trans()->next, scr_trans()->back);
  }

  // show end
  gui_message(scr_trans()->intro__end, 5000);

  // section action
  scr_action = STM_FROM_INTRO;

  return scr_menu;
}

/* Management */

static void* (*scr_handler)();

static void scr_task() {
  // call handlers
  for (;;) {
    void* next = scr_handler();
    scr_handler = next;
  }
}

void scr_run(al_trigger_t trigger) {
  // handle return
  scr_handler = scr_menu;
  if (trigger == AL_RESET) {
    scr_handler = scr_intro;
  } else if (trigger == AL_BUTTON && scr_return_unlock != NULL) {
    scr_handler = scr_return_unlock;
  } else if ((trigger == AL_TIMEOUT || trigger == AL_INTERRUPT) && scr_return_timeout != NULL) {
    scr_handler = scr_return_timeout;
  }

  // clear return handlers
  scr_return_unlock = NULL;
  scr_return_timeout = NULL;

  // run screen task
  naos_run("scr", 8192, 1, scr_task);
}
