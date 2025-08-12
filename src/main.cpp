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

const int PIN_CV_GATE_A = 32;
const int PIN_CV_GATE_B = 33;

const uint8_t MIDI_CH = 2;

Button startButton(PIN_START);

// Rotary encoder
RotaryEncoder encoder(PIN_ENCODER_S1, PIN_ENCODER_S2, RotaryEncoder::LatchMode::TWO03);
int encoderLastPos = encoder.getPosition();

// MIDI
HardwareSerial MIDIserial(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDIserial, midiA);

volatile int bpm = 120;
unsigned long lastClockTime = 0;
int pulseCount = 0;

bool isPlaying = false;

unsigned long ledOnTime = 0;
bool ledState = false;

bool isCvGateA = false;
bool isCvGateB = false;
unsigned long gateLengthMs = 0;
unsigned long gateStartTime = 0;

// OLED
SSD1306Wire display(0x3c, SDA, SCL);
enum MetronomePositoon
{
  LEFT,
  CENTER,
  RIGHT
};
MetronomePositoon metronomePosition = CENTER;
int metronomeCount = 0;
bool stateChanged = false;

void drwawDisplay()
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 0, "DISPLAY SLOT NAME");
  display.setFont(ArialMT_Plain_10);
  display.drawString(94, 16, "BPM");
  display.setFont(ArialMT_Plain_24);
  display.drawString(94, 26, String(bpm));
  display.setFont(ArialMT_Plain_10);
  // display.drawString(64, 54, isPlaying ? "Playing" : "Stopped");

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  String cvGateAText = "GATE A";
  display.drawString(0, 54, isCvGateA ? cvGateAText + "*" : cvGateAText + "");

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

  display.display();
}

void setup()
{
  MIDIserial.begin(31250, SERIAL_8N1, PIN_RX, PIN_TX);
  midiA.begin(MIDI_CHANNEL_OMNI);

  startButton.begin();
  Serial.begin(115200);
  Serial.println("Start");
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CV_GATE_A, OUTPUT);
  pinMode(PIN_CV_GATE_B, OUTPUT);

  gateLengthMs = 240 / bpm; // 16th note length in ms

  // Initialize OLED display
  display.init();
  display.flipScreenVertically();
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
    stateChanged = true;
  }

  startButton.read();
  if (startButton.wasReleased())
  {
    Serial.println("Pressed");
    isPlaying = !isPlaying;
    if (isPlaying)
    {
      midiA.sendRealTime(midi::Start);
      pulseCount = 0;
      metronomeCount = 0;
      lastClockTime = millis();
      Serial.println("MIDI Start");
    }
    else
    {
      midiA.sendRealTime(midi::Stop);
      pulseCount = 0;
      metronomeCount = 0;
      metronomePosition = CENTER;
      lastClockTime = millis();
      Serial.println("MIDI Stop");
    }
    stateChanged = true;
  }

  if (isPlaying)
  {
    unsigned long interval = 60000UL / (bpm * 24);
    if (millis() - lastClockTime >= interval)
    {
      // Send MIDI clock
      lastClockTime += interval;
      midiA.sendRealTime(midi::Clock);

      // Count pulse
      pulseCount++;
      if (pulseCount >= 24)
      {
        pulseCount = 0;
        digitalWrite(PIN_LED, HIGH);
        ledState = true;
        ledOnTime = millis();
        Serial.println("4th note");
      }

      // Output CV/Gate
      if ((pulseCount % 12) == 1)
      {
        // if(random(2)){
        digitalWrite(PIN_CV_GATE_A, HIGH);
        gateStartTime = millis();
        isCvGateA = true;
        // };
      }

      if (isCvGateA && millis() - gateStartTime >= gateLengthMs)
      {
        digitalWrite(PIN_CV_GATE_A, LOW);
        isCvGateA = false;
      }

      // Update metronome position
      metronomeCount++;
      if (metronomeCount <= 12)
      {
        metronomePosition = LEFT;
      }
      else if (metronomeCount <= 24)
      {
        metronomePosition = CENTER;
      }
      else if (metronomeCount <= 36)
      {
        metronomePosition = RIGHT;
      }
      else if (metronomeCount < 48)
      {
        metronomePosition = CENTER;
      }
      else
      {
        metronomeCount = 0;
      }

      stateChanged = true;
      Serial.println(pulseCount);
    }
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