#ifndef AL_BUZZER_H
#define AL_BUZZER_H

/**
 * Plays a single "click" sound.
 */
void al_buzzer_click();

/**
 * Plays a tone at the specified frequency for the specified duration.
 *
 * @param hz The frequency of the tone to play.
 * @param ms The duration of the tone to play.
 */
void al_buzzer_beep(int hz, int ms);

#endif  // AL_BUZZER_H
