#include <MIDI.h>

/*
 * Unison Midi Teacher
 * Made by Synthicate for INF1510 spring 2015
 * and developed by Kjetil Svalestuen.
 *
 * All comments are written in English.
 */

MIDI_CREATE_DEFAULT_INSTANCE();

// Important constants with descriptions
const int limit = 128;      // Memory limit of melodies
const int maxTasks = 3;     // Number of tasks/recording tracks
const int maxKeys = 10;     // Max simultaneous keys pressed
const int scoreLimit = 66;  // Score needed to unlock next task
const int noteC4 = 262;     // Middle C, used in note frequence formula
const int noteC5 = 523;     // High pitched C
const int noteG5 = 784;     // High pitched G

// Simulate key presses with the buzzer?
const boolean simulateKeys = false;

// Inputs and outputs
int buttonPlay = 12;
int buttonHidden = 8;
int selector = 0;
int speaker = 13;
int ledTask[maxTasks];
int ledScore[maxTasks];
int ledGlobalScore[maxTasks];

// Task managing
int selected = 0; // Current selected task
int recorded = 3; // Number of tasks recorded by admin
int unlocked = 3; // Number of tasks unlocked by user
int locked = false;

// Modes
boolean record = false;
boolean ready = false;
boolean play = false;

// Melody and rhythm
byte melody[maxTasks][limit];
unsigned int rhythm[maxTasks][limit];
unsigned int melodyLength[maxTasks];
unsigned int rhythmLength[maxTasks];
unsigned int melodyIndex = 0;
unsigned int rhythmIndex = 0;

// Metronome related
unsigned int stepCounter = 0;
boolean tick = true;

// Melody resolution and timers
int whole = 2000;
int eighth = whole / 8;
int resolution = eighth;
unsigned long timer = 0;
unsigned long stimer = resolution;

// Caching simultaneous notes when playing
unsigned int currentIndex = 0;
int newNotesCount = 0;
byte newNotes[maxKeys];

// Gamification
int currentScore;
int melodyScore[maxTasks];
int globalScore = 0;

/* Configure pins, variables and MIDI library. */
void setup() {
  Serial.begin(9600);
  
  pinMode(speaker, OUTPUT);
  pinMode(buttonPlay, INPUT);
  pinMode(buttonHidden, INPUT);

  ledTask[0] = 2;
  ledTask[1] = 3;
  ledTask[2] = 4;
  for (int i = 0; i < maxTasks; i++)
    pinMode(ledTask[i], OUTPUT);

  ledScore[0] = 5;
  ledScore[1] = 6;
  ledScore[2] = 7;
  for (int i = 0; i < maxTasks; i++)
    pinMode(ledScore[i], OUTPUT);

  ledGlobalScore[0] = 9;
  ledGlobalScore[1] = 10;
  ledGlobalScore[2] = 11;
  for (int i = 0; i < maxTasks; i++)
    pinMode(ledGlobalScore[i], OUTPUT);

  // MIDI-related
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.begin(MIDI_CHANNEL_OMNI); // Listen to all channels.
}

/* Main program flow
 * MIDI.read() triggers the notehandlers if there are 
 * more than three bytes waiting on the serial channel
 */
void loop() {
  MIDI.read();

  if (!locked)
    selectTask();
  
  int pressed = getPressed();
  if (pressed == 1)
    clickPlay();
  else if (pressed == 2)
    clickRecord();
  else if (pressed == 3)
    resetProgress();
  else if (pressed == 4)
    playMelody();
    
  if (record || play)
    metronome();
}

/* Handle what to do whenever a key is pressed. */
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  if (simulateKeys) playNote(pitch, resolution / 2);
  if (record)
    advanceRecording(pitch);
  else if (ready)
    startPlaying(pitch);
  else if (play)
    advancePlaying(pitch);
}

/* Choose a task based on the selector's position and number of tasks unlocked. 
 * Example: Say you have three tasks unlocked and the selector's analog read
 * returns 500. Selected = 500 / (1024 / 3) = (int) 1.46 = 1. The selector is in
 * the middle position and you have selected task 1 from (0, 1, 2) which
 * returns the second task.  When recording, unlocked tasks should be +1 so that
 * you can either overwrite stored melodies or record the next, free task. */
void selectTask() {

  if (unlocked == 0) {
    if (record) {
      selected = 0;
    } else return;
  } else {
    int availableTasks;
    if (record && recorded < maxTasks)
      availableTasks = recorded + 1;
    else availableTasks = unlocked;
    selected = (analogRead(selector) / (1024 / availableTasks));
  }

  litSelectedTask();
}

void litSelectedTask() {
  for (int i = 0; i < maxTasks; i++) {
    if (i == selected)
      digitalWrite(ledTask[i], HIGH);
    else digitalWrite(ledTask[i], LOW);
  } litScore();
}

/* Handle record click */
void clickRecord() {
  if (play)
    stopPlaying(false);
  if (record)
    stopRecording();
  else startRecording();
}

/* Go into recording mode */
void startRecording() {
  reset();
  record = true;
  tone(speaker, noteC5, 200);
}

/* Go to the next note in the melody array. */
void advanceRecording(byte pitch) {
  
  // First note played. Lock to task and synch rhythm.
  if (melodyIndex == 0) {
    locked = true;
    rhythmIndex = 0;
    stepCounter = 0;
  }

  melody[selected][melodyIndex] = pitch;
  melodyIndex++;
  
  // If recording has exceeded its memory 'limit'
  if (melodyIndex == limit)
    stopRecording();
}

/* Stop the recording. Save the melody on selected task */
void stopRecording() {

  // Case first melody recorded
  if (unlocked == 0)
    unlocked++;

  // Mark task as recorded if you didnt overwrite an existing melody.
  if (recorded < maxTasks && recorded == selected)
    recorded++;

  melodyLength[selected] = melodyIndex; 
  rhythmLength[selected] = melodyIndex;
  tone(speaker,noteC5, 200);

  record = false;
  locked = false;
}

/* Handle play button click */
void clickPlay() {
  if (record)
    stopRecording();
  if (play)
    stopPlaying(false);
  else if (melodyLength[selected] != 0)
    prepareToPlay();
  else tone(speaker, 100, 100);
}

/* Prepare to play selected melody by going into 'ready' mode. When in ready
 * mode, the notehandler will handle the first key press different to
 * synchronize melody with the user. */
void prepareToPlay() {
  ready = true;
  tone(speaker, noteG5, 200);
}

/* Reset counters and go into play mode */
void startPlaying(int firstNotePitch) {
  locked = true;
  ready = false;
  play = true;
  reset();

  advancePlaying(firstNotePitch);
}

/* Move onto the next note in the melody. Store notes played in the
 * current step. */
void advancePlaying(byte pitch) {
  newNotes[newNotesCount++] = pitch;
  melodyIndex++;
}

/* Calculate melodyScore on task. Uses a delay to ensure that
 * the feedback 'beep' is not overlapped by other system sounds. */
void stopPlaying(boolean success) {
  if (success) {
    tone(speaker, noteG5, 500);
    
    // Max score is two points per note; one for pitch and one for rhythm.
    int maxScore = melodyLength[selected] * 2;
    
    currentScore = maxScore;
    if (currentScore < 1) currentScore = 0;
    else {
      double dec = currentScore / (double) maxScore;
      currentScore = dec * 100; 
    }

    if (currentScore > melodyScore[selected]) {
      melodyScore[selected] = currentScore;
      
      litScore();
      calculateGlobalScore();
    }
    
    unlockNextTask();
    
  } else tone(speaker, 100, 500);
  
  delay(1000);
  play = false;
  locked = false;
}

// Lit LED appropriate to melody score.
void litScore() {
  if (melodyScore[selected] < 1) {
    lit(ledScore, -1);
  } else if (melodyScore[selected] < 34) {
    lit(ledScore, 0);
  } else if (melodyScore[selected] < 67) {
    lit(ledScore, 1);
  } else {
    lit(ledScore, 2);
  }
}

/* Find the average melodyScore of all tasks and lit the appropriate LED. */
void calculateGlobalScore() {
  globalScore = 0;
  for (int i = 0; i < maxTasks; i++)
    globalScore += melodyScore[i];
  globalScore /= maxTasks;
  litGlobalScore();
}

// Lit LED appropriate to global score.
void litGlobalScore() {
  if (globalScore < 1) {
    lit(ledGlobalScore, -1);
  } else if (globalScore < 34) {
    lit(ledGlobalScore, 0);
  } else if (globalScore < 67) {
    lit(ledGlobalScore, 1);
  } else {
    lit(ledScore, 2);
  }
}

/* Lit one of three leds */
void lit(int leds[], int high) {
  for (int i = 0; i < maxTasks; i++) {
    if (i == high)
      digitalWrite(leds[i], HIGH);
    digitalWrite(leds[i], LOW);
  }
}

/* Unlock next task if there is one */
void unlockNextTask() {
  if (melodyScore[selected] > scoreLimit)
      unlocked++;
}

/* Reset counters and timers. */
void reset() {
  timer = 0;
  stimer = resolution;
  currentScore = 0;
  melodyIndex = 0;
  rhythmIndex = 0;
  stepCounter = 0;
  currentIndex = 0;
}

/* Metronome 
 * Handle rhythm both when recording and when playing. When recording,
 * match new notes with the current step.
 */
void metronome() {
  long currentTime = millis();
  boolean rhythmStep = (currentTime >= (timer + resolution));
  boolean shiftedStep = (currentTime >= (stimer + (resolution / 2)));
  
  if (rhythmStep) {
    if (record || play) {
      timer = currentTime;
      stimer = currentTime;
    }

    if (tick)
      clickMetronome();
    tick = !tick;
    
  } else if (shiftedStep) {
    if (record) {
      while (rhythmIndex < melodyIndex)
        rhythm[selected][rhythmIndex++] = stepCounter;
    } else if (play) {
      validateNewNotes();
    }
    
    stimer = currentTime;
    stepCounter++;
  }
}

/* Validate if notes pressed since last validation have the correct
 * pitch and rhythm. */
void validateNewNotes() {
  
  // Too many notes?
  if (newNotesCount > (melodyIndex - currentIndex)) {
    currentScore -= ((melodyIndex - currentIndex) - newNotesCount);
  }
  
  // From last checked to last played note
  while (currentIndex < melodyIndex) {
    // Loop from last checked to last played note
    if (rhythm[selected][currentIndex] == stepCounter) {
      currentScore++;
    }

    // Try to find the current note in the set of newly pressed notes.
    boolean rightPitch = false;
    for (int i = 0; i < newNotesCount; i++)
      if (newNotes[i] == melody[selected][currentIndex])
        rightPitch = true;
    
    if (rightPitch)
      currentScore++;
    
    currentIndex++;
  } newNotesCount = 0;

  // If done with melody
  if (melodyIndex == melodyLength[selected])
    stopPlaying(true);
}

/* Blink current task and make a ticking sound. */
void clickMetronome() {
  digitalWrite(ledTask[selected], !digitalRead(ledTask[selected]));
  tone(speaker, noteC5, 2);
}

/* Play the recorded and selected melody in heavenly strings (aka buzzer).
 * Uses delays because this feature is hidden and only for testing purposes. */
void playMelody() {
  int d = resolution;
  melodyIndex = 0;
  
  tone(speaker, noteG5, 100); delay(100);
  tone(speaker, noteC5, 400); delay(1000);
  
  for (int i = 0; i < rhythmLength[selected]; i++) {
    if (rhythm[selected][melodyIndex] == i) {
      int e = d;
      while (rhythm[selected][melodyIndex] == i) {
        playNote(melody[selected][melodyIndex++], d/2);
        delay(d/16);
        e -= (d/16);
      } delay(e);
    } else {
      tone(speaker, noteC5, 2);
      delay(d);
    }
  }
 
  delay(400); tone(speaker, noteC5, 100);
  delay(100); tone(speaker, noteG5, 400);
}

/* Plays a note using 'pitch' as the keyboard note ID. 
 * f(0) = C5, f(n) = f(0) * twelfth-root-of-two**n */
void playNote(byte pitch, int length) {
  tone(speaker, noteC5 * pow((1.05946309436), pitch - 60), length); 
}

/* Returns pressed button. 
 * 1: Play button click
 * 2: Play button long press
 * 3: Hidden button click
 * 4: Hidden button long press 
 */
int getPressed() {
  boolean playPressed = (digitalRead(buttonPlay) == HIGH);
  boolean hiddenPressed = (digitalRead(buttonHidden) == HIGH);

  if (playPressed) {
    int pressed = 1;
    long pressTime = millis();
    while (digitalRead(buttonPlay) == HIGH) {
      if ((millis() - pressTime) > 2000) {
        pressed = 2;
        tone(speaker, noteC5, 100);
      }
    } return pressed;
  }

  if (hiddenPressed) {
    int pressed = 3;
    long pressTime = millis();
      if ((millis() - pressTime) > 2000) {
    while (digitalRead(buttonHidden) == HIGH) {
        pressed = 4;
        tone(speaker, noteC5, 100);
      }
    } return pressed; 
  }
}

/* Delete user progress, but keep recordings. */
void resetProgress() {
  for (int i = 0; i < maxTasks; i++) {
    melodyScore[i] = 0; 
  } calculateGlobalScore();
  
  for (int i = 0; i < 3; i++) {
    tone(speaker, 100, 300);
    delay(600);
  }
}
