#include <esp_random.h>
#include <math.h>

#include <al/sensor.h>
#include <al/store.h>

#include "stm.h"

stm_entry_t stm_entries[] = {
    /* Urgent Actions */
    {
        .urgent = true,
        .action = STM_FROM_INTRO,
        .text_de = "Willkommen im Air Lab! Sieh dich doch einmal um. Berühren ist erlaubt!",
        .text_en = "Welcome to the Air Lab! Please look around. Touching is allowed!",
        .text_es = "Bienvenido al Air Lab! Puedes mirar en calma. Tocar es permitido!",
        .text_fr = "Bienvenue à l'Air Lab! Regarde autour, tu peux toucher!",
        .mood = STM_HAPPY,
    },
    {
        .urgent = true,
        .action = STM_START_FIRST_MEASUREMENT,
        .text_de = "Super, du hast gerade deine erste Messung gestartet!",
        .text_en = "Great, you've just started your first measurement!",
        .text_es = "Genial, acabas de iniciar tu primer registro!",
        .text_fr = "Super, tu viens de lancer ta première mesure!",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_START_MEASUREMENT,
        .text_de = "So, dann lassen wir die Machine arbeiten.",
        .text_en = "So, let's let the machine work.",
        .text_es = "Bueno, dejemos que la máquina haga su trabajo.",
        .text_fr = "Bon, laissons la machine travailler.",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_START_MEASUREMENT,
        .text_de = "Die Maschine läuft, jetzt heisst es abwarten!",
        .text_en = "The machine is running, now it's time to wait!",
        .text_es = "La máquina está en marcha, ahora es solo esperar!",
        .text_fr = "La machine tourne, il n'y a plus qu'à attendre!",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_START_MEASUREMENT,
        .text_de = "Alles klar, Messung gestartet. Die Technik macht den Rest.",
        .text_en = "All set, measurement started. The tech will handle the rest.",
        .text_es = "Bravo, registro iniciado. La tecnología hará el resto.",
        .text_fr = "C'est parti, mesure lancée. La technique fait le reste.",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_FROM_MEASUREMENT,
        .text_de = "Ich bin am rechnen...",
        .text_en = "I'm calculating...",
        .text_es = "Estoy calculando...",
        .text_fr = "Je calcule...",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_FROM_MEASUREMENT,
        .text_de = "Diesen Wert muss ich kurz nachprüfen.",
        .text_en = "I need to double-check this value.",
        .text_es = "Necesito verificar este valor.",
        .text_fr = "Je dois revérifier cette valeur.",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_FROM_MEASUREMENT,
        .text_de = "(RH + 454) x 10^3/0.544 = ?",
        .text_en = "(RH + 454) x 10^3/0.544 = ?",
        .text_es = "(RH + 454) x 10^3/0.544 = ?",
        .text_fr = "(RH + 454) x 10^3/0.544 = ?",
        .mood = STM_WORKING,
    },
    {
        .urgent = true,
        .action = STM_COMP_MEASUREMENT,
        .text_de = "Cool, du hast deine Messung abgeschlossen!",
        .text_en = "Cool, you've completed your measurement!",
        .text_es = "Bravo, has completado tu registro!",
        .text_fr = "Cool, tu as terminé ta mesure!",
        .mood = STM_HAPPY,
    },
    {
        .urgent = true,
        .action = STM_FROM_ANALYSIS,
        .text_de = "Die Analyse ist der Anfang der Erkenntnis.",
        .text_en = "Analysis is the beginning of knowledge.",
        .text_es = "Análisis es la base del conocimiento.",
        .text_fr = "L'analyse est le début de la connaissance.",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_FROM_ANALYSIS,
        .text_de = "Daten ohne Analyse sind wie Bücher ohne Lesen.",
        .text_en = "Data without analysis is like books without reading.",
        .text_es = "Datos sin análisis son como libros sin leer.",
        .text_fr = "Des données sans analyse, c'est comme un livre jamais lu.",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_FROM_ANALYSIS,
        .text_de = "Verstehen heisst, Muster in Zahlen zu sehen.",
        .text_en = "To understand is to see patterns in numbers.",
        .text_es = "Para entender hay que buscar patrones en los números.",
        .text_fr = "Comprendre, c'est voir des motifs dans les chiffres.",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_FROM_ANALYSIS,
        .text_de = "Wer misst, misst manchmal Mist.",
        .text_en = "Measure twice, analyze once.",
        .text_es = "Registra bastante datos, es mejor para el análisis.",
        .text_fr = "Mesure deux fois, analyse une fois.",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_DEL_MEASUREMENT,
        .text_de = "Ein sauberes Labor ist die hälfte der Messung.",
        .text_en = "Maintaining a clean lab is half the measurement.",
        .text_es = "Mantener el laboratorio limpio es la mitad del trabajo.",
        .text_fr = "Un labo propre, c'est déjà la moitié de la mesure.",
        .mood = STM_POINTING,
    },
    {
        .urgent = true,
        .action = STM_DEL_MEASUREMENT,
        .text_de = "Aufräumen gehört genauso dazu wie messen.",
        .text_en = "Tidying up is just as much a part of science as measuring.",
        .text_es = "Ordenar es tan importante como medir.",
        .text_fr = "Ranger fait autant partie de la science que mesurer.",
        .mood = STM_POINTING,
    },
    /* Urgent Conditions */
    {
        .urgent = true,
        .co2_min = 4000,
        .text_de = "Schmuuhhh, mir wird ganz schwindelig...",
        .text_en = "Ughhh, I'm getting dizzy...",
        .text_es = "Ughhh, me estoy mareando...",
        .text_fr = "Ouhla, la tête me tourne...",
        .mood = STM_COLD,
    },
    {
        .urgent = true,
        .co2_min = 3000,
        .text_de = "Mir ist übel!",
        .text_en = "I feel sick!",
        .text_es = "Me siento mal!",
        .text_fr = "Je me sens mal!",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .co2_min = 2000,
        .co2_max = 3000,
        .text_de = "Ich kann hier kaum atmen!",
        .text_en = "I can hardly breathe here!",
        .text_es = "Casi no puedo respirar aquí!",
        .text_fr = "J'arrive à peine à respirer ici!",
        .mood = STM_ANGRY2,
    },
    {
        .urgent = true,
        .co2_min = 1700,
        .co2_max = 2000,
        .text_de = "Uuuu... ich bin müde.",
        .text_en = "Ugh... I'm tired.",
        .text_es = "Ugh... estoy cansado.",
        .text_fr = "Pfff... je suis fatigué.",
        .mood = STM_STANDING,
    },
    {
        .urgent = true,
        .co2_min = 1400,
        .co2_max = 1700,
        .text_de = "Jo, ich kann mich voll nicht konzentrieren.",
        .text_en = "Yo, I can't concentrate at all.",
        .text_es = "Ey, no puedo concentrarme!",
        .text_fr = "Yo, j'arrive pas à me concentrer.",
        .mood = STM_ANGRY2,
    },
    {
        .urgent = true,
        .co2_min = 1000,
        .co2_max = 1400,
        .text_de = "Puh, langsam wird die Luft stickig.",
        .text_en = "Phew, the air is getting stuffy.",
        .text_es = "Uiuiuiii, el aire se está volviendo pesado.",
        .text_fr = "Pfiou, l'air devient étouffant.",
        .mood = STM_STANDING,
    },
    {
        .urgent = true,
        .tmp_max = 10,
        .text_de = "Boa ey, ist das kalt hier!",
        .text_en = "Uff, it's cold in here!",
        .text_es = "Uff, hace frío aquí!",
        .text_fr = "Brrr, qu'est-ce qu'il fait froid ici!",
        .mood = STM_COLD,
    },
    {
        .urgent = true,
        .tmp_min = 25,
        .hum_min = 70,
        .text_de = "Sag mal, sind wir im Regenwald?",
        .text_en = "Hey, are we in the rainforest?",
        .text_es = "Estamos en la selva tropical, o qué?",
        .text_fr = "Dis donc, on est dans la jungle ou quoi?",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .hum_max = 30,
        .text_de = "Ahem, mega trocken hier!",
        .text_en = "Ahem, super dry here!",
        .text_es = "Ahem, súper seco aquí!",
        .text_fr = "Hum, c'est super sec ici!",
        .mood = STM_COLD,
    },
    {
        .urgent = true,
        .tmp_min = 28,
        .hum_max = 30,
        .text_de = "Uff, heiss und trocken hier drin!",
        .text_en = "Ugh, it's hot and dry in here!",
        .text_es = "Uy, hace calor y está seco aquí!",
        .text_fr = "Ouh là, il fait chaud et sec ici!",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .tmp_max = 10,
        .hum_max = 30,
        .text_de = "Brrr... kalt und trocken, meine Haut spannt schon.",
        .text_en = "Brrr... cold and dry, my skin feels tight.",
        .text_es = "Brrr... está frío y seco, mi piel está sufriendo.",
        .text_fr = "Brrr... froid et sec, ma peau tiraille.",
        .mood = STM_COLD,
    },
    {
        .urgent = true,
        .voc_min = 150,
        .voc_max = 250,
        .text_de = "Hmm, hier riecht es etwas streng.",
        .text_en = "Hmm, it's starting to smell a bit strong here.",
        .text_es = "Hmm, aquí empieza a oler un poco fuerte.",
        .text_fr = "Hmm, ça commence à sentir fort ici.",
        .mood = STM_STANDING,
    },
    {
        .urgent = true,
        .voc_min = 250,
        .text_de = "Uff, die Luft ist voller Ausdünstungen!",
        .text_en = "Ugh, the air is full of fumes!",
        .text_es = "Uy, el aire está lleno de vapores!",
        .text_fr = "Pouah, l'air est plein d'émanations!",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .nox_min = 50,
        .nox_max = 150,
        .text_de = "Da sind Abgase in der Luft!",
        .text_en = "There are exhaust fumes in the air!",
        .text_es = "Hay gases de escape en el aire!",
        .text_fr = "Il y a des gaz d'échappement dans l'air!",
        .mood = STM_ANGRY2,
    },
    {
        .urgent = true,
        .nox_min = 150,
        .text_de = "Achtung, zu viele Stickoxide! Lüften empfohlen!",
        .text_en = "Warning, too much NOx! Ventilation recommended!",
        .text_es = "Atención, demasiado NOx! Te recomiendo ventilar!",
        .text_fr = "Attention, trop de NOx! Aération recommandée!",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .pm_min = 25,
        .pm_max = 55,
        .needs_pm = true,
        .text_de = "Hier ist es ganz schön staubig!",
        .text_en = "It's quite dusty in here!",
        .text_es = "Está bastante polvoriento aquí!",
        .text_fr = "C'est drôlement poussiéreux ici!",
        .mood = STM_STANDING,
    },
    {
        .urgent = true,
        .pm_min = 55,
        .needs_pm = true,
        .text_de = "Achtung, sehr viel Feinstaub in der Luft!",
        .text_en = "Warning, a lot of fine dust in the air!",
        .text_es = "Atención, mucho polvo fino en el aire!",
        .text_fr = "Attention, beaucoup de particules fines dans l'air!",
        .mood = STM_ANGRY1,
    },
    {
        .urgent = true,
        .pm_min = 150,
        .needs_pm = true,
        .text_de = "Hust, hust... hier ist es ja richtig rauchig!",
        .text_en = "Cough, cough... it's really smoky in here!",
        .text_es = "Cof, cof... hay mucho humo aquí!",
        .text_fr = "Kof, kof... il y a vraiment de la fumée ici!",
        .mood = STM_COLD,
    },
    /* Good Conditions */
    {
        .urgent = true,
        .co2_min = 600,
        .co2_max = 1000,
        .voc_max = 150,
        .nox_max = 50,
        .text_de = "Alles im grünen Bereich, gute Luftqualität!",
        .text_en = "All good, air quality is fine!",
        .text_es = "Todo bien, la calidad del aire es buena!",
        .text_fr = "Tout est au vert, bonne qualité d'air!",
        .mood = STM_HAPPY,
    },
    {
        .co2_max = 600,
        .voc_max = 110,
        .nox_max = 5,
        .text_de = "Ahhh... Ich liebe frische Luft!",
        .text_en = "Ahhh... I love fresh air!",
        .text_es = "Ahhh... me encanta el aire fresco!",
        .text_fr = "Ahhh... j'adore l'air frais!",
        .mood = STM_HAPPY,
    },
    {
        .co2_max = 600,
        .hum_min = 40,
        .hum_max = 60,
        .text_de = "Die Luft hier ist jetzt richtig nice!",
        .text_en = "The air here is really nice now!",
        .text_es = "El aire aquí está realmente bueno ahora!",
        .text_fr = "L'air est vraiment agréable ici!",
        .mood = STM_HAPPY,
    },
    {
        .voc_max = 150,
        .text_de = "Schön, keine störenden Gerüche in der Luft!",
        .text_en = "Nice, no bothersome smells in the air!",
        .text_es = "Bravo, no hay olores molestos en el aire!",
        .text_fr = "Chouette, pas d'odeurs gênantes dans l'air!",
        .mood = STM_HAPPY,
    },
    {
        .nox_max = 50,
        .text_de = "Kaum Stickoxide, die Luft ist sauber!",
        .text_en = "Hardly any NOx, the air is clean!",
        .text_es = "Casi no hay NOx, el aire está limpio!",
        .text_fr = "Presque pas de NOx, l'air est propre!",
        .mood = STM_HAPPY,
    },
    {
        .pm_max = 10,
        .needs_pm = true,
        .text_de = "Kaum Feinstaub, die Luft ist klar!",
        .text_en = "Hardly any fine dust, the air is clear!",
        .text_es = "Casi no hay partículas finas, el aire está limpio!",
        .text_fr = "Presque pas de particules fines, l'air est limpide!",
        .mood = STM_HAPPY,
    },
    /* Air Facts */
    {
        .text_de = "Warme Luft kann mehr Feuchtigkeit auf- nehmen als kalte Luft.",
        .text_en = "Warm air can absorb more moisture than cold air.",
        .text_es = "Aire caliente puede absorber más humedad que aire frío.",
        .text_fr = "L'air chaud absorbe plus d'humidité que l'air froid.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Pro Tag atmen wir 10'000 bis 20'000 Liter Luft.",
        .text_en = "We breathe 10,000 to 20,000 liters of air per day.",
        .text_es = "Respiramos entre 10,000 y 20,000 litros de aire al día.",
        .text_fr = "On respire 10'000 à 20'000 litres d'air par jour.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Die Luft ist ein Gemisch aus vielen verschiedenen Gasen.",
        .text_en = "The air is a mixture of many different gases.",
        .text_es = "El aire es una mezcla de muchos gases diferentes.",
        .text_fr = "L'air est un mélange de nombreux gaz différents.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "78% der Luft ist Stickstoff.",
        .text_en = "78% of the air is nitrogen.",
        .text_es = "78% del aire es nitrógeno.",
        .text_fr = "L'air contient 78% d'azote.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "21% der Luft ist Sauerstoff.",
        .text_en = "21% of the air is oxygen.",
        .text_es = "21% del aire es oxígeno.",
        .text_fr = "L'air contient 21% d'oxygène.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Achtung! Luftschad- stoffe können dich krank machen!",
        .text_en = "Attention! Air pollutants can make you sick!",
        .text_es = "Atención! Los contaminantes del aire pueden enfermarte!",
        .text_fr = "Attention! Les polluants de l'air peuvent te rendre malade!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "1% der Luft sind ganz viele verschiedene Spurengase.",
        .text_en = "1% of the air are many different trace gases.",
        .text_es = "1% del aire es una mezcla de muchos gases diferentes.",
        .text_fr = "1% de l'air est fait de nombreux gaz à l'état de traces.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Klug ist jener, der Schweres einfach sagt.",
        .text_en = "Wise is the one who says heavy things simply.",
        .text_es = "Sabio es quien dice cosas complejas de forma simple.",
        .text_fr = "Sage est celui qui dit simplement les choses compliquées.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Auch Bakterien und Viren fliegen in der Luft herum!",
        .text_en = "Bacteria and viruses are also flying around in the air!",
        .text_es = "Bacterias y virus también vuelan por el aire!",
        .text_fr = "Des bactéries et des virus flottent aussi dans l'air!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Pro Tag atmest du ca. 2'500 Liter CO2 aus!",
        .text_en = "You exhale about 2,500 liters of CO2 per day!",
        .text_es = "Tú exhalas alrededor de 2,500 litros de CO2 al día!",
        .text_fr = "Tu expires environ 2'500 litres de CO2 par jour!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Ein Kubikmeter CO2 wiegt 1.98 kg.",
        .text_en = "One cubic meter of CO2 weighs 1.98 kg.",
        .text_es = "Un metro cúbico de CO2 pesa 1.98 kg.",
        .text_fr = "Un mètre cube de CO2 pèse 1.98 kg.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Zu trockene Luft kann deine Schleimhäute reizen.",
        .text_en = "Air that's too dry can irritate your mucous membranes.",
        .text_es = "Aire demasiado seco puede irritar tus membranas mucosas.",
        .text_fr = "Un air trop sec peut irriter tes muqueuses.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Im Sommer fühlen sich 24°C oft angenehm an, im Winter eher kühl.",
        .text_en = "In summer, 24°C feels comfortable, but in winter it feels cool.",
        .text_es = "En verano, 24°C está bien, pero en invierno se percibe frío.",
        .text_fr = "En été, 24°C semble agréable, en hiver plutôt frais.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "In Klassenzimmern erreicht CO2 oft über 2000 ppm!",
        .text_en = "In classrooms, CO2 often exceeds 2000 ppm!",
        .text_es = "En aulas de clases, el CO2 a menudo supera los 2000 ppm!",
        .text_fr = "En classe, le CO2 dépasse souvent les 2000 ppm!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "CO2 wird mit photoa- kustischer Spektro- skopie gemessen.",
        .text_en = "CO2 is measured using photoacoustic spectroscopy.",
        .text_es = "CO2 se mide mediante espectroscopía fotoacústica.",
        .text_fr = "Le CO2 se mesure par spectroscopie photoacoustique.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "VOC sind flüchtige organische Verbind- ungen, oft von Farben.",
        .text_en = "VOCs are volatile organic compounds, often from paints.",
        .text_es = "VOCs son compuestos orgánicos volátiles, a menudo de pinturas.",
        .text_fr = "Les VOC sont des composés volatils, issus p.ex. des peintures.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Stickoxide (NOx) stammen oft aus Verbrennung.",
        .text_en = "Nitrogen oxides (NOx) often come from combustion.",
        .text_es = "Los óxidos de nitrógeno (NOx) vienen de la combustión.",
        .text_fr = "Les oxydes d'azote (NOx) proviennent souvent de la combustion.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Der Luftdruck hier liegt bei etwa 1013 hPa auf Meereshöhe.",
        .text_en = "Air pressure is about 1013 hPa at sea level.",
        .text_es = "La presión del aire es de aprox. 1013 hPa al nivel del mar.",
        .text_fr = "La pression de l'air est d'env. 1013 hPa au niveau de la mer.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Gute Belüftung hilft, VOCs und NOx zu reduzieren.",
        .text_en = "Good ventilation helps reduce VOCs and NOx.",
        .text_es = "Buena ventilación ayuda a reducir VOCs y NOx.",
        .text_fr = "Bien aérer aide à réduire les VOC et les NOx.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Neue Möbel können VOCs freisetzen, am besten gut lüften!",
        .text_en = "New furniture can emit VOCs, best to ventilate well!",
        .text_es = "Nuevos muebles pueden emitir VOCs, mejor ventilar bien!",
        .text_fr = "Les meubles neufs peuvent émettre des VOC, pense à aérer!",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "PM2.5-Partikel sind 30-mal dünner als ein menschliches Haar!",
        .text_en = "PM2.5 particles are 30 times thinner than a human hair!",
        .text_es = "Las partículas PM2.5 son 30 veces más finas que un cabello!",
        .text_fr = "Les particules PM2.5 sont 30 fois plus fines qu'un cheveu!",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Feinstaub entsteht beim Kochen, durch Kerzen und im Verkehr.",
        .text_en = "Fine dust comes from cooking, candles and traffic.",
        .text_es = "Las partículas finas vienen de cocinar, velas y el tráfico.",
        .text_fr = "Cuisson, bougies et trafic produisent des particules fines.",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Feinstaub kann tief in die Lunge eindringen.",
        .text_en = "Fine dust can penetrate deep into the lungs.",
        .text_es = "Las partículas finas pueden entrar profundo en los pulmones.",
        .text_fr = "Les particules fines peuvent pénétrer au fond des poumons.",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Feinstaub wird mit gestreutem Laserlicht gemessen.",
        .text_en = "Fine dust is measured with scattered laser light.",
        .text_es = "El polvo fino se mide con luz láser dispersada.",
        .text_fr = "Les particules fines se mesurent à la lumière laser diffusée.",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "PM2.5 sind Teilchen kleiner als 2.5 Mikrometer.",
        .text_en = "PM2.5 means particles smaller than 2.5 micrometers.",
        .text_es = "PM2.5 son partículas más pequeñas que 2.5 micrómetros.",
        .text_fr = "PM2.5 désigne les particules de moins de 2.5 micromètres.",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Regen wäscht den Feinstaub aus der Luft!",
        .text_en = "Rain washes fine dust out of the air!",
        .text_es = "La lluvia lava el material particulado del aire!",
        .text_fr = "La pluie lave les particules fines de l'air!",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Saharastaub fliegt manchmal tausende Kilometer weit!",
        .text_en = "Sahara dust sometimes travels thousands of kilometers!",
        .text_es = "El polvo del Sahara a veces viaja miles de kilómetros!",
        .text_fr = "La poussière du Sahara parcourt parfois des milliers de km!",
        .mood = STM_POINTING,
    },
    {
        .needs_pm = true,
        .text_de = "Auch Pollen und Meersalz schweben als Partikel in der Luft.",
        .text_en = "Pollen and sea salt also float in the air as particles.",
        .text_es = "Polen y sal marina también flotan en el aire como partículas.",
        .text_fr = "Pollen et sel marin flottent aussi en particules dans l'air.",
        .mood = STM_POINTING,
    },
    /* Exercise Prompts */
    {
        .text_de = "Öffne das Fenster und schau, wie sich die Luft verändert!",
        .text_en = "Try opening the window and see how the air changes!",
        .text_es = "Intenta abriendo la ventana y observa los cambios el aire!",
        .text_fr = "Ouvre la fenêtre et regarde comment l'air change!",
        .mood = STM_HAPPY,
    },
    {
        .needs_pm = true,
        .text_de = "Beobachte den Feinstaub-Wert beim Kochen!",
        .text_en = "Watch the fine dust value while cooking!",
        .text_es = "Observa el valor de partículas finas mientras cocinas!",
        .text_fr = "Observe la valeur des particules fines en cuisinant!",
        .mood = STM_HAPPY,
    },
};

int stm_num() {
  // return number of entries
  return sizeof(stm_entries) / sizeof(stm_entry_t);
}

stm_entry_t* stm_get(size_t i) {
  // return entry by index
  return i < stm_num() ? &stm_entries[i] : NULL;
}

stm_entry_t* stm_query(bool urgent, stm_action_t action) {
  // get last sample
  al_sample_t sample = al_store_last();

  // check if ok
  bool ok = al_sample_valid(sample);

  // calculate values
  float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
  float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
  float hum = al_sample_read(sample, AL_SAMPLE_HUM);
  float voc = al_sample_read(sample, AL_SAMPLE_VOC);
  float nox = al_sample_read(sample, AL_SAMPLE_NOX);
  float pm = al_sample_read(sample, AL_SAMPLE_PM);

  // treat PM readings without an installed sensor or with an obstruction as unavailable
  bool has_pm = al_sensor_pm_present();
  bool pm_ok = has_pm && !isnan(pm) && (al_sample_flags(sample, AL_SAMPLE_PM) & AL_SAMPLE_PM_OBSTRUCTED) == 0;

  // de/select and count entries
  int selected = 0;
  for (size_t i = 0; i < stm_num(); i++) {
    // get entry
    stm_entry_t* entry = &stm_entries[i];

    // set selection
    entry->selected = true;

    // check urgency
    if (entry->urgent != urgent) {
      entry->selected = false;
      continue;
    }

    // check action
    if (entry->action != 0 && action != entry->action) {
      entry->selected = false;
      continue;
    }

    // check co2
    if (entry->co2_min != 0 && (!ok || co2 < entry->co2_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->co2_max != 0 && (!ok || co2 > entry->co2_max)) {
      entry->selected = false;
      continue;
    }

    // check temperature
    if (entry->tmp_min != 0 && (!ok || tmp < entry->tmp_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->tmp_max != 0 && (!ok || tmp > entry->tmp_max)) {
      entry->selected = false;
      continue;
    }

    // check humidity
    if (entry->hum_min != 0 && (!ok || hum < entry->hum_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->hum_max != 0 && (!ok || hum > entry->hum_max)) {
      entry->selected = false;
      continue;
    }

    // check VOC (NaN means no reading is available)
    if (entry->voc_min != 0 && (!ok || isnan(voc) || voc < entry->voc_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->voc_max != 0 && (!ok || isnan(voc) || voc > entry->voc_max)) {
      entry->selected = false;
      continue;
    }

    // check NOx (NaN means no reading is available)
    if (entry->nox_min != 0 && (!ok || isnan(nox) || nox < entry->nox_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->nox_max != 0 && (!ok || isnan(nox) || nox > entry->nox_max)) {
      entry->selected = false;
      continue;
    }

    // check PM sensor requirement
    if (entry->needs_pm && !has_pm) {
      entry->selected = false;
      continue;
    }

    // check PM (no sensor, no reading or obstruction means unavailable)
    if (entry->pm_min != 0 && (!ok || !pm_ok || pm < entry->pm_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->pm_max != 0 && (!ok || !pm_ok || pm > entry->pm_max)) {
      entry->selected = false;
      continue;
    }

    // increment if selected
    if (entry->selected) {
      selected++;
    }
  }

  // check selected
  if (selected == 0) {
    return NULL;
  }

  // choose entry randomly
  selected = (int)esp_random() % selected;

  // find and return entry
  for (int i = 0; i < stm_num(); i++) {
    stm_entry_t* entry = &stm_entries[i];
    if (entry->selected) {
      selected--;
      if (selected < 0) {
        return entry;
      }
    }
  }

  return NULL;
}
