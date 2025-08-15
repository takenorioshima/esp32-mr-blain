#include <MIDI.h>
#include <JC_Button.h>
#include <SSD1306Wire.h>
#include <RotaryEncoder.h>

const byte PIN_LED = 13;
const byte PIN_START = 5;
const byte PIN_RX = 16;
const byte PIN_TX = 17;
const byte PIN_ENCODER_S1 = 19;
const byte PIN_ENCODER_S2 = 18;
const byte PIN_PROGRAM_BUTTON = 25;

const byte PIN_CV_GATE_A = 32;
const byte PIN_CV_GATE_A_CONTROL_POT = 33;

const byte MIDI_CH = 2;
const byte MIDI_PPQN = 24;

Button startButton(PIN_START);
Button programButton(PIN_PROGRAM_BUTTON);
byte programIndex = 0;

const byte PROGRAM_VALUES[] = {8, 9, 10, 11};
const byte PROGRAM_COUNT = sizeof(PROGRAM_VALUES) / sizeof(PROGRAM_VALUES[0]);

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
int cvGateStepIndex = 0;
int cvGateCurrentPattern = 0;

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
  display.setFont(ArialMT_Plain_10);
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
}

void displayCurrentProgram()
{
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  int slot = (PROGRAM_VALUES[programIndex] % 4) + 1; // SLOT 1-4
  display.drawString(64, 0, "BANK 2 - SLOT " + String(slot));
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

void updateCvGate(){
  if (!isPlaying) return;
  int currentStepTick = clockTickCount / 12; // 8th note step
  if (currentStepTick != lastStepTick)
  {
    // Serial.print("Step: ");
    // Serial.println(currentStepTick);

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
    Serial.println("Gate OFF");
    Serial.print("Gate Length: ");
    Serial.print(gateLengthMs);
    Serial.println(" ms");
    isCvGateA = false;
  }
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

  gateLengthMs = 60000 / (bpm * 4); // 16th note length in ms

  // Initialize OLED display
  display.init();
  display.flipScreenVertically();
  drawDisplay();
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
    gateLengthMs = 60000 / (bpm * 4);
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

    updateCvGate();

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

  // Program button
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

  // Turn off LED
  if (ledState && millis() - ledOnTime >= 50)
  {
    digitalWrite(PIN_LED, LOW);
    ledState = false;
  }

  // Update display if state changed
  if (stateChanged)
  {
    drawDisplay();
    stateChanged = false;
  }
}
