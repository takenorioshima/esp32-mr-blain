#include <MIDI.h>
#include <JC_Button.h>
#include <SSD1306Wire.h>
#include <RotaryEncoder.h>
#include <jled.h>

const byte PIN_LED_BPM = 13;
const byte PIN_START = 5;
const byte PIN_RX = 16;
const byte PIN_TX = 17;
const byte PIN_ENCODER_S1 = 19;
const byte PIN_ENCODER_S2 = 18;
const byte PIN_PROGRAM_BUTTON = 25;
const byte PIN_PROGRAM_SAVE_BUTTON = 26;

const byte PIN_CV_GATE_A = 32;
const byte PIN_CV_GATE_B = 2; // FIXME: Use internal blue LED to debug CV/Gate B output
const byte PIN_POT_A = 34;
const byte PIN_POT_B = 35;

JLed ledBpm = JLed(PIN_LED_BPM);
bool isBreathing = true;

const byte MIDI_CH = 2;
const byte MIDI_PPQN = 24;

Button startButton(PIN_START);
Button programButton(PIN_PROGRAM_BUTTON);
Button programSaveButton(PIN_PROGRAM_SAVE_BUTTON);

byte programIndex = 0;
const byte PROGRAM_VALUES[] = {8, 9, 10, 11};
const byte PROGRAM_COUNT = sizeof(PROGRAM_VALUES) / sizeof(PROGRAM_VALUES[0]);
int lastProgramChangeSentMs = 0;
int displayUpdateIntervalMS = 1000;
bool isProgramChangeSent = false;

int potAValue = 0;
int potBValue = 0;

const unsigned long analogReadInterval = 100; // ms
unsigned long lastAnalogReadMs = 0;

// CV/Gate B - Divisions
struct Division
{
  const char *label;
  int ticks;
};

const Division DIVISIONS[] = {
    {"1/16", 6},
    {"1/8", 12},
    {"1/4", 24},
    {"3/8", 36},
    {"1/2", 48},
    {"1/1", 96},
    {"2/1", 192},
    {"4/1", 384}};
const int NUM_DIVISIONS = sizeof(DIVISIONS) / sizeof(DIVISIONS[0]);
int currentDivision = 0;
int currentDivisionIndex = 0;

// Rotary encoder
RotaryEncoder encoder(PIN_ENCODER_S1, PIN_ENCODER_S2, RotaryEncoder::LatchMode::TWO03);
int encoderLastPos = encoder.getPosition();

// MIDI
HardwareSerial MIDIserial(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDIserial, midiA);

bool isPlaying = false;

volatile int bpm = 120;
unsigned long clockIntervalMicros;
unsigned long lastClockMicros = 0;
int clockTickCount = 0;

void updateClockInterval()
{
  clockIntervalMicros = (60.0 * 1000000.0) / (bpm * 24.0);
}

void sendMidiClock()
{
  unsigned long now = micros();
  if (now - lastClockMicros >= clockIntervalMicros)
  {
    lastClockMicros += clockIntervalMicros;
    midiA.sendRealTime(midi::Clock);
    clockTickCount++;
  }
}

// CV/Gate
unsigned long gateLengthMs = 0;
unsigned long gateStartTime = 0;
bool isCvGateA = false;
bool isCvGateB = false;

const byte patterns[][8] = {
    {1, 1, 1, 1, 1, 1, 1, 1}, // "oooooooo"
    {1, 1, 1, 0, 1, 0, 1, 1}, // "oooxoxoo"
    {1, 1, 0, 1, 1, 0, 1, 0}, // "ooxooxox"
    {1, 1, 0, 1, 0, 0, 1, 1}, // "ooxoxxoo"
    {1, 0, 0, 0, 1, 0, 0, 0}, // "oxxxoxxx"
    {1, 0, 0, 0, 0, 0, 1, 0}  // "oxxxxxox"
};
const byte NUM_PATTERNS = sizeof(patterns) / sizeof(patterns[0]);
const byte PATTERN_STEPS = 8;
int lastStepTick = -1;
int stepIndex = 0;
int currentPattern = 0;

// OLED
SSD1306Wire display(0x3c, SDA, SCL);
enum MetronomePositon
{
  LEFT,
  CENTER,
  RIGHT
};
MetronomePositon metronomePosition = CENTER;
unsigned int metronomePhase;

bool stateChanged = false;

void drawQuarterNoteCircle()
{
  if (!isPlaying)
  {
    return;
  }
  byte tick = clockTickCount % MIDI_PPQN;
  if (tick % MIDI_PPQN < 6)
  {
    display.fillCircle(64, 32, 6);
  }
}

void drawMetronome()
{
  int offsetX = -30;
  display.drawLine(63 + offsetX, 16, 47 + offsetX, 48);
  display.drawLine(64 + offsetX, 16, 80 + offsetX, 48);
  display.drawLine(48 + offsetX, 49, 79 + offsetX, 49);

  if (metronomePosition == LEFT)
  {
    display.drawLine(74 + offsetX, 22, 64 + offsetX, 43);
    display.drawLine(75 + offsetX, 22, 65 + offsetX, 43);
  }
  else if (metronomePosition == CENTER)
  {
    display.drawLine(63 + offsetX, 18, 63 + offsetX, 40);
    display.drawLine(64 + offsetX, 18, 64 + offsetX, 40);
  }
  else if (metronomePosition == RIGHT)
  {
    display.drawLine(54 + offsetX, 22, 64 + offsetX, 43);
    display.drawLine(53 + offsetX, 22, 63 + offsetX, 43);
  }

  display.fillCircle(64 + offsetX, 42, 3);
}

void displayBpm()
{
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(94, 16, "BPM");
  display.setFont(ArialMT_Plain_24);
  display.drawString(94, 26, String(bpm));
}

void displayCvGateStatus()
{
  // CV/Gate A
  if (isCvGateA)
  {
    display.fillCircle(5, 60, 2);
  }
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  String cvGateAText = "CV A";
  if (currentPattern > 0)
  {
    if (currentPattern < NUM_PATTERNS)
    {
      cvGateAText += "(P" + String(currentPattern) + ")";
    }
    else
    {
      cvGateAText += "(RND)";
    }
  }
  display.drawString(8, 54, cvGateAText);

  // CV/Gate B
  if (isCvGateB)
  {
    display.fillCircle(119, 60, 2);
  }
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  String cvGateBText = "CV B";
  cvGateBText += " (" + String(DIVISIONS[currentDivisionIndex].label) + ")";
  display.drawString(114, 54,cvGateBText);
}

void displayCurrentProgram()
{
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  int slot = (PROGRAM_VALUES[programIndex] % 4) + 1; // SLOT 1-4
  display.drawString(64, 0, "BANK 2 - SLOT " + String(slot));
  if (isProgramChangeSent)
  {
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, ">>");
  }
}

void drawDisplay()
{
  display.clear();
  displayBpm();
  displayCvGateStatus();
  displayCurrentProgram();
  drawMetronome();
  drawQuarterNoteCircle();
  display.display();
}

void updateDisplay()
{
  if (stateChanged)
  {
    drawDisplay();
    stateChanged = false;
  }
}

void updateCvGateA()
{
  // CV/Gaate A - Pattern based gate
  int currentStepTick = clockTickCount / 12; // 8th note step
  if (currentStepTick != lastStepTick)
  {
    lastStepTick = currentStepTick;

    if (currentPattern < NUM_PATTERNS)
    {
      int stepIndex = currentStepTick % PATTERN_STEPS;
      int gateVal = patterns[currentPattern][stepIndex];
      Serial.print(gateVal ? 'o' : 'x');

      if (gateVal)
      {
        digitalWrite(PIN_CV_GATE_A, HIGH);
        gateStartTime = millis();
        isCvGateA = true;
      }
    }
    else
    {
      if (random(10) == 0)
      {
        digitalWrite(PIN_CV_GATE_A, HIGH);
        gateStartTime = millis();
        isCvGateA = true;
      }
    }
  }
  if (isCvGateA && millis() - gateStartTime >= gateLengthMs)
  {
    digitalWrite(PIN_CV_GATE_A, LOW);
    Serial.println("Gate OFF");
    Serial.print("Gate Length: ");
    Serial.print(gateLengthMs);
    Serial.println(" ms");
    isCvGateA = false;
  }
}

void updateCvGateB()
{
  // CV/Gate B - Division based gate
  int divisionTicks = currentDivision;
  int halfCycle = divisionTicks / 2;

  if ((clockTickCount % divisionTicks) < halfCycle)
  {
    digitalWrite(PIN_CV_GATE_B, HIGH);
    isCvGateB = true;
  }
  else
  {
    digitalWrite(PIN_CV_GATE_B, LOW);
    isCvGateB = false;
  }
}

void updateStartButton()
{
  startButton.read();
  if (startButton.wasPressed())
  {
    Serial.println("Pressed");

    // Set note division to 1/4.
    // Ref: https://blooper.chasebliss.com/midi/docs/midi-manual.pdf
    midiA.sendControlChange(54, 3, MIDI_CH);

    isPlaying = !isPlaying;
    if (isPlaying)
    {
      midiA.sendRealTime(midi::Start);
      lastClockMicros = micros();
      clockTickCount = 0;
      Serial.println("MIDI Start");
    }
    else
    {
      midiA.sendRealTime(midi::Stop);
      metronomePosition = CENTER;
      Serial.println("MIDI Stop");

      // CV/Gate off
      digitalWrite(PIN_CV_GATE_A, LOW);
      isCvGateA = false;

      digitalWrite(PIN_CV_GATE_B, LOW);
      isCvGateB = false;
    }
    stateChanged = true;
  }
}

void updateEncoder()
{
  encoder.tick();
  int encoderNewPos = encoder.getPosition() * 0.5;
  if (encoderNewPos != encoderLastPos)
  {
    int delta = encoderNewPos - encoderLastPos;
    bpm += delta;
    bpm = constrain(bpm, 40, 240);
    encoderLastPos = encoderNewPos;
    gateLengthMs = 60000 / (bpm * 4);
    updateClockInterval();
    stateChanged = true;
  }
}

void updateCvGatePots()
{
  // Read CV/Gate Control Pot
  if (millis() - lastAnalogReadMs >= analogReadInterval)
  {
    // Read pot A - Set CV/Gate pattern
    potAValue = analogRead(PIN_POT_A);
    currentPattern = map(potAValue, 0, 4095, 0, NUM_PATTERNS);

    // Read pot B - Set divisions
    potBValue = analogRead(PIN_POT_B);
    currentDivisionIndex = map(potBValue, 0, 4095, 0, NUM_DIVISIONS - 1);
    currentDivision = DIVISIONS[currentDivisionIndex].ticks;
    stateChanged = true;

    lastAnalogReadMs = millis();
    stateChanged = true;
  }
}

void updateProgramButtons()
{
  // Program change button
  programButton.read();
  if (programButton.wasReleased())
  {
    byte value = PROGRAM_VALUES[programIndex];
    midiA.sendProgramChange(value, MIDI_CH);
    programIndex++;
    if (programIndex >= PROGRAM_COUNT)
    {
      programIndex = 0;
    }
    stateChanged = true;
  }

  // Program save button
  programSaveButton.read();
  if (programSaveButton.wasReleased())
  {
    byte value = PROGRAM_VALUES[programIndex];
    midiA.sendProgramChange(value, MIDI_CH);
    isProgramChangeSent = true;
    lastProgramChangeSentMs = millis();
    stateChanged = true;
  }
  if (isProgramChangeSent && millis() - lastProgramChangeSentMs >= displayUpdateIntervalMS)
  {
    isProgramChangeSent = false;
    stateChanged = true;
  }
}

void updateMetronome()
{
  // Update metronome position
  metronomePhase = ((clockTickCount % MIDI_PPQN) / 6);

  switch (metronomePhase)
  {
  case 0:
    metronomePosition = LEFT;
    break;
  case 1:
    metronomePosition = CENTER;
    break;
  case 2:
    metronomePosition = RIGHT;
    break;
  case 3:
    metronomePosition = CENTER;
    break;
  }

  stateChanged = true;
}

void updateBpmLed()
{
  ledBpm.Update();
  if (isPlaying)
  {
    isBreathing = false;
    if (clockTickCount % 24 == 0)
    {
      Serial.print("led update");  
      ledBpm.On();
    }
    else
    {
      ledBpm.Off();
    }
  }
  else if(!isBreathing)
  {
    ledBpm.Breathe(3000).DelayAfter(1000).Forever();
    isBreathing = true;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start");

  MIDIserial.begin(31250, SERIAL_8N1, PIN_RX, PIN_TX);
  midiA.begin(MIDI_CHANNEL_OMNI);

  updateClockInterval();
  midiA.sendProgramChange(8, MIDI_CH); // Set initial program - Bank 2, Slot 1

  startButton.begin();
  programButton.begin();
  programSaveButton.begin();

  pinMode(PIN_CV_GATE_A, OUTPUT);
  pinMode(PIN_CV_GATE_B, OUTPUT);

  pinMode(PIN_POT_A, ANALOG);
  pinMode(PIN_POT_B, ANALOG);
  analogSetAttenuation(ADC_11db);

  gateLengthMs = 60000 / (bpm * 4); // 16th note length in ms

  currentDivisionIndex = 1; // Start with 1/8 note

  ledBpm.Breathe(3000).DelayAfter(1000).Forever();

  // Initialize OLED display
  display.init();
  display.flipScreenVertically();
  drawDisplay();
}

void loop()
{
  updateEncoder();
  updateStartButton();
  updateCvGatePots();
  updateProgramButtons();

  if (isPlaying)
  {
    sendMidiClock();
    updateCvGateA();
    updateCvGateB();
    updateMetronome();
  }

  updateBpmLed();
  updateDisplay();
}
