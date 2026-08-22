// Translation tables shared between the firmware (scr.c) and the host tests (test/).

typedef enum {
  SCR_DE,
  SCR_EN,
  SCR_ES,
  SCR_FR,
} scr_lang_t;

static const char* scr_lang_str[] = {
    [SCR_DE] = "Deutsch",
    [SCR_EN] = "English",
    [SCR_ES] = "Español",
    [SCR_FR] = "Français",
};

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
  const char* config__general;
  const char* config__measurement;
  const char* config__power;
  const char* config__connectivity;
  const char* config__advanced;
  const char* config__gas_mode;
  const char* config__gas_mode_cont;
  const char* config__gas_mode_duty;
  const char* config__gas_window_hint;
  const char* config__language;
  const char* config__date;
  const char* config__time;
  const char* config__timezone;
  const char* config__main_rate;
  const char* config__sleep_rate;
  const char* config__record_rate;
  const char* config__pm_rate;
  const char* config__display_rate;
  const char* config__gas_window;
  const char* config__gas_grace;
  const char* config__long_interval;
  const char* config__temp_unit;
  const char* config__developer;
  const char* config__power_light;
  const char* config__co2_light;
  const char* config__wifi_network;
  const char* config__studio;
  const char* config__ble_prev_sleep;
  const char* config__mqtt_prev_sleep;
  const char* config__ble_pairing;
  const char* config__ble_bonding;
  const char* config__ble_clear;
  const char* config__ble_cleared;
  const char* config__altitude;
  const char* config__clock_cal;
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
            .yes = "Sí",
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
            .ble__active = "Bluetooth activo\n\nNombre: %.16s",
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
            .config__general = "General",
            .config__measurement = "Medición",
            .config__power = "Energía",
            .config__connectivity = "Conectividad",
            .config__advanced = "Avanzado",
            .config__gas_mode = "Modo sensor de gas",
            .config__gas_mode_cont = "Continuo",
            .config__gas_mode_duty = "Intermitente",
            .config__gas_window_hint = "Activa primero el\nmodo intermitente.",
            .config__language = "Idioma",
            .config__date = "Fecha",
            .config__time = "Hora",
            .config__timezone = "Zona horaria",
            .config__main_rate = "Muestreo activo",
            .config__sleep_rate = "Muestreo en reposo",
            .config__record_rate = "Muestreo en registro",
            .config__pm_rate = "Muestreo de PM",
            .config__display_rate = "Refresco de pantalla",
            .config__gas_window = "Ventana activa de gas",
            .config__gas_grace = "Periodo de gracia gas",
            .config__long_interval = "Intervalo largo plazo",
            .config__temp_unit = "Unidad de temperatura",
            .config__developer = "Modo desarrollador",
            .config__power_light = "Indicador de encendido",
            .config__co2_light = "Indicador de CO2",
            .config__wifi_network = "Red WiFi",
            .config__studio = "Usa Air Lab Console\npara cambiar este valor.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "Borrar dispositivos BT",
            .config__ble_cleared = "Dispositivos borrados!",
            .config__altitude = "Altitud s.n.m.",
            .config__clock_cal = "Calibración del reloj",
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
            .ble__active = "Bluetooth Pairing aktiv\n\nName: %.16s",
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
            .config__general = "Allgemein",
            .config__measurement = "Messung",
            .config__power = "Energie",
            .config__connectivity = "Konnektivität",
            .config__advanced = "Erweitert",
            .config__gas_mode = "Gas Sensormodus",
            .config__gas_mode_cont = "Kontinuierlich",
            .config__gas_mode_duty = "Getaktet",
            .config__gas_window_hint = "Zuerst getakteten\nModus aktivieren.",
            .config__language = "Sprache",
            .config__date = "Datum",
            .config__time = "Uhrzeit",
            .config__timezone = "Zeitzone",
            .config__main_rate = "Wach Messrate",
            .config__sleep_rate = "Schlaf Messrate",
            .config__record_rate = "Aufnahme Messrate",
            .config__pm_rate = "PM Messrate",
            .config__display_rate = "Anzeigeintervall",
            .config__gas_window = "Gas Aktivfenster",
            .config__gas_grace = "Gas Karenzzeit",
            .config__long_interval = "Langzeit Intervall",
            .config__temp_unit = "Temperatureinheit",
            .config__developer = "Entwicklermodus",
            .config__power_light = "Betriebsanzeige",
            .config__co2_light = "CO2 Anzeige",
            .config__wifi_network = "WiFi Netzwerk",
            .config__studio = "Verwende Air Lab Console\num diesen Wert zu ändern.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "BT Geräte löschen",
            .config__ble_cleared = "Geräte gelöscht!",
            .config__altitude = "Höhe ü.M.",
            .config__clock_cal = "Uhrkalibrierung",
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
            .ble__active = "Bluetooth pairing active\n\nName: %.16s",
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
            .config__general = "General",
            .config__measurement = "Measurement",
            .config__power = "Power",
            .config__connectivity = "Connectivity",
            .config__advanced = "Advanced",
            .config__gas_mode = "Gas Sensor Mode",
            .config__gas_mode_cont = "Continuous",
            .config__gas_mode_duty = "Duty Cycled",
            .config__gas_window_hint = "Enable duty cycling first.",
            .config__language = "Language",
            .config__date = "Date",
            .config__time = "Time",
            .config__timezone = "Timezone",
            .config__main_rate = "Awake Sample Rate",
            .config__sleep_rate = "Sleep Sample Rate",
            .config__record_rate = "Record Sample Rate",
            .config__pm_rate = "PM Sample Rate",
            .config__display_rate = "Display Refresh",
            .config__gas_window = "Gas Active Window",
            .config__gas_grace = "Gas Power Grace",
            .config__long_interval = "Long-term Interval",
            .config__temp_unit = "Temperature Unit",
            .config__developer = "Developer Mode",
            .config__power_light = "Power Light",
            .config__co2_light = "CO2 Light",
            .config__wifi_network = "WiFi Network",
            .config__studio = "Use Air Lab Console\nto change this value.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Bluetooth Pairing",
            .config__ble_bonding = "Bluetooth Bonding",
            .config__ble_clear = "Clear BT Devices",
            .config__ble_cleared = "Devices cleared!",
            .config__altitude = "Altitude ASL",
            .config__clock_cal = "Clock Calibration",
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
    [SCR_FR] =
        {
            .yes = "Oui",
            .no = "Non",
            .on = "Activé",
            .off = "Désactivé",
            .back = "Retour",
            .next = "Suivant",
            .change = "Modifier",
            .save = "Enregistrer",
            .cancel = "Annuler",
            .execute = "Exécuter",
            .measurement = "Mesure %u",
            .recording = "Mesure en cours!",
            .exit__stop = "Arrêter la mesure",
            .exit__back = "Retour au labo",
            .exit__stopped = "%s\n arrêtée!",
            .view__not_enough = "Pas assez de données\npour le mode précision.",
            .create__full = "Mémoire pleine!",
            .create__info = "Mesure %u\n%d heures disponibles",
            .create__start = "Démarrer",
            .create__import = "Veux-tu importer les données\ndu suivi en direct?",
            .create__importing = "Importation des données...",
            .create__imported = "Importation réussie!",
            .delete__confirm = "Vraiment supprimer %s?",
            .delete__delete = "Supprimer",
            .delete__deleted = "Mesure %d supprimée!",
            .edit__analyse = "Analyser",
            .edit__delete = "Supprimer",
            .edit__export = "Exporter CSV",
            .edit__exporting = "Exportation des données...",
            .edit__export_fail = "Échec de l'exportation!",
            .edit__export_done = "Exportation terminée!",
            .explore__empty = "Aucune mesure enregistrée.",
            .explore__create = "Créer une nouvelle mesure",
            .explore__select = "Sélectionner",
            .usb__disconnected = "USB non connecté!",
            .usb__active = "Volume USB actif",
            .usb__eject = "Volume USB éjecté",
            .ble__active = "Appairage Bluetooth actif\n\nNom: %.16s",
            .reset__confirm = "Réinitialiser l'Air Lab?",
            .reset__reset = "Air Lab\nréinitialisé avec succès!",
            .settings__title = "Réglages",
            .settings__about = "À propos",
            .settings__config = "Configuration",
            .settings__off = "Éteindre",
            .settings__regulatory = "Réglementation",
            .settings__introduction = "Introduction",
            .about__device_name = "Nom de l'appareil",
            .about__serial_number = "Numéro de série",
            .about__firmware_version = "Version FW",
            .about__internal_storage = "Stockage int.",
            .about__external_storage = "Stockage ext.",
            .config__general = "Général",
            .config__measurement = "Mesure",
            .config__power = "Énergie",
            .config__connectivity = "Connectivité",
            .config__advanced = "Avancé",
            .config__gas_mode = "Mode capteur gaz",
            .config__gas_mode_cont = "Continu",
            .config__gas_mode_duty = "Intermittent",
            .config__gas_window_hint = "Active d'abord le\nmode intermittent.",
            .config__language = "Langue",
            .config__date = "Date",
            .config__time = "Heure",
            .config__timezone = "Fuseau horaire",
            .config__main_rate = "Échant. actif",
            .config__sleep_rate = "Échant. en veille",
            .config__record_rate = "Échant. en mesure",
            .config__pm_rate = "Échant. PM",
            .config__display_rate = "Actualisation écran",
            .config__gas_window = "Fenêtre active gaz",
            .config__gas_grace = "Délai de grâce gaz",
            .config__long_interval = "Intervalle long terme",
            .config__temp_unit = "Unité de température",
            .config__developer = "Mode développeur",
            .config__power_light = "Voyant d'alimentation",
            .config__co2_light = "Voyant CO2",
            .config__wifi_network = "Réseau WiFi",
            .config__studio = "Utilise Air Lab Console\npour changer cette valeur.",
            .config__ble_prev_sleep = "BLE Prevent Sleep",
            .config__mqtt_prev_sleep = "MQTT Prevent Sleep",
            .config__ble_pairing = "Appairage Bluetooth",
            .config__ble_bonding = "Liaison Bluetooth",
            .config__ble_clear = "Effacer appareils BT",
            .config__ble_cleared = "Appareils effacés!",
            .config__altitude = "Altitude s/mer",
            .config__clock_cal = "Étalonnage horloge",
            .config__reset = "Réinit. complète",
            .menu__no_data = "Aucune donnée",
            .intro__hello1 = "Salut! Je suis Professeur Robin,\nchef scientifique de l'Air Lab.",
            .intro__hello2 = "Je me suis assoupi un moment...\nPeux-tu me donner l'heure?",
            .intro__watch = "Ma montre indique %d:%02d\nle %d-%02d-%02d, c'est juste?",
            .intro__correct = "Exact!",
            .intro__adjust = "<Ajuster>",
            .intro__test = "Merci! As-tu besoin d'un\npetit rappel sur les paramètres\nde qualité de l'air?",
            .intro__infos =
                {
                    "- CO2 -\nLe dioxyde de carbone se mesure\nen parties par million (PPM).",
                    "- CO2 -\nEn intérieur, essaie de rester\nsous 1500 ppm en aérant\nrégulièrement.",
                    "- VOC -\nLes composés organiques\nvolatils sont émis par des\nproduits comme les peintures.",
                    "- VOC -\n100 est la moyenne des\ndernières 24h. Des écarts\nindiquent des changements.",
                    "- NOx -\nLes oxydes d'azote sont des\ngaz issus de la combustion\n(p. ex. les voitures).",
                    "- NOx -\n1 est la moyenne des dernières\n24h. Des valeurs plus hautes\nindiquent des changements.",
                    "C'est tout pour le moment!\nPour en savoir plus:"
                    "\nnetworkedartifacts.com\n/manuals/airlab/air-parameters",
                },
            .intro__end = "Allez, direction le labo!",
            .engine__empty = "Aucun plugin installé.",
            .engine__run = "Lancer",
        },
};
