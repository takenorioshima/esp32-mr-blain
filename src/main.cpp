#include <MIDI.h>
#include <JC_Button.h>
#include <SSD1306Wire.h>
#include <RotaryEncoder.h>

const int PIN_LED = 13;
const int PIN_START = 5;
const int PIN_RX = 16;
const int PIN_TX = 17;
const int PIN_ENCODER_S1 = 19;
const int PIN_ENCODER_S2 = 18;
const int PIN_PROGRAM_BUTTON = 25;

const int PIN_CV_GATE_A = 32;
const int PIN_CV_GATE_A_CONTROL_POT = 33;

const uint8_t MIDI_CH = 2;

Button startButton(PIN_START);
Button programButton(PIN_PROGRAM_BUTTON);
int programIndex = 0;

const byte PROGRAM_VALUES[] = {8, 9, 10, 11};
const int PROGRAM_COUNT = sizeof(PROGRAM_VALUES) / sizeof(PROGRAM_VALUES[0]);

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

unsigned long ledOnTime = 0;
bool ledState = false;

// CV/Gate
unsigned long gateLengthMs = 0;
unsigned long gateStartTime = 0;
bool isCvGateA = false;

const int patterns[][8] = {
    {1, 1, 1, 1, 1, 1, 1, 1}, // "oooooooo"
    {1, 1, 1, 0, 1, 0, 1, 1}, // "oooxoxoo"
    {1, 1, 0, 1, 1, 0, 1, 0}, // "ooxooxox"
    {1, 1, 0, 1, 0, 0, 1, 1}, // "ooxoxxoo"
    {1, 0, 0, 0, 1, 0, 0, 0}, // "oxxxoxxx"
    {1, 0, 0, 0, 0, 0, 1, 0}  // "oxxxxxox"
};
const int NUM_PATTERNS = sizeof(patterns) / sizeof(patterns[0]);
const int PATTERN_STEPS = 8;
int lastStepTick = -1;
int cvGateStepIndex = 0;
int cvGateCurrentPattern = 0;

// OLED
SSD1306Wire display(0x3c, SDA, SCL);
enum MetronomePositoon
{
  LEFT,
  CENTER,
  RIGHT
};
MetronomePositoon metronomePosition = CENTER;
unsigned int metronomePhase;

bool stateChanged = false;

void drawQuarterNoteCircle()
{
  if (!isPlaying)
  {
    return;
  }
  int tick = clockTickCount % 24;
  if (tick % 24 < 6)
  {
    display.fillCircle(64, 32, 6);
  }
}

void drwawDisplay()
{
  display.clear();

  // BPM
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(94, 16, "BPM");
  display.setFont(ArialMT_Plain_24);
  display.drawString(94, 26, String(bpm));
  display.setFont(ArialMT_Plain_10);

  // CV/Gate
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  String cvGateAText = "CV A";
  cvGateAText += isCvGateA ? "*" : " ";
  if (cvGateCurrentPattern > 0)
  {
    if (cvGateCurrentPattern < NUM_PATTERNS)
    {
      cvGateAText += "(M:" + String(cvGateCurrentPattern) + ")";
    }
    else
    {
      cvGateAText += "(M:R)";
    }
  }
  display.drawString(0, 54, cvGateAText);

  // Current Program
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  int slot = (PROGRAM_VALUES[programIndex] % 4) + 1; // SLOT 1-4
  display.drawString(64, 0, "BANK 2 - SLOT " + String(slot));
  
  // Metronome
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

  drawQuarterNoteCircle();
  display.display();
}

void setup()
{
  MIDIserial.begin(31250, SERIAL_8N1, PIN_RX, PIN_TX);
  midiA.begin(MIDI_CHANNEL_OMNI);

  updateClockInterval();
  midiA.sendProgramChange(8, MIDI_CH);

  startButton.begin();
  programButton.begin();
  
  Serial.begin(115200);
  Serial.println("Start");
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CV_GATE_A, OUTPUT);
  pinMode(PIN_CV_GATE_A_CONTROL_POT, ANALOG);
  analogSetAttenuation(ADC_11db);

  gateLengthMs = 240 / bpm; // 16th note length in ms

  // Initialize OLED display
  display.init();
  display.flipScreenVertically();
  drwawDisplay();
}

void loop()
{
  // Encoder.
  encoder.tick();
  int encoderNewPos = encoder.getPosition() * 0.5;
  if (encoderNewPos != encoderLastPos)
  {
    int delta = encoderNewPos - encoderLastPos;
    bpm += delta;
    bpm = constrain(bpm, 40, 240);
    encoderLastPos = encoderNewPos;
    gateLengthMs = 240 / bpm;
    updateClockInterval();
    stateChanged = true;
  }

  // Start/Stop button
  startButton.read();
  if (startButton.wasReleased())
  {
    Serial.println("Pressed");
    midiA.sendControlChange(54, 2, MIDI_CH);
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
    }
    stateChanged = true;
  }

  // Read CV/Gate Control Pot
  int rawValue = analogRead(PIN_CV_GATE_A_CONTROL_POT);
  cvGateCurrentPattern = map(rawValue, 0, 4095, 0, NUM_PATTERNS);

  if (isPlaying)
  {
    // Send MIDI clock
    sendMidiClock();

    // Send CV/Gate
    int currentStepTick = clockTickCount / 12; // 8th note step
    if (currentStepTick != lastStepTick)
    {
      Serial.print("Step: ");
      Serial.print(currentStepTick);

      lastStepTick = currentStepTick;

      if (cvGateCurrentPattern < NUM_PATTERNS)
      {
        int stepIndex = currentStepTick % PATTERN_STEPS;
        int gateVal = patterns[cvGateCurrentPattern][stepIndex];
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
      isCvGateA = false;
    }

    // Update metronome position
    metronomePhase = ((clockTickCount % 24) / 6);
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

  // Program button
  programButton.read();
  if (programButton.wasReleased())
  {
    byte value = PROGRAM_VALUES[programIndex];
    midiA.sendProgramChange(value, MIDI_CH);
    programIndex++;
    if (programIndex >= PROGRAM_COUNT) {
      programIndex = 0;
    }
    stateChanged = true;
  }

  // Turn off LED
  if (ledState && millis() - ledOnTime >= 50)
  {
    digitalWrite(PIN_LED, LOW);
    ledState = false;
  }

  // Update display if state changed
  if (stateChanged)
  {
    drwawDisplay();
    stateChanged = false;
  }
}