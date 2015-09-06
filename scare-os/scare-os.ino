#include <Wire.h>
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"

#define PIN_SONIC_TRIGGER 3
#define PIN_SONIC_ECHO 2

#define PIN_SND_TRIGGER_WAKE 9
#define PIN_SND_TRIGGER_REGULAR 8
#define PIN_SND_TRIGGER_NEAR 7
#define PIN_SND_TRIGGER_SLEEPING 6
#define PIN_SND_ACTIVE  5

Adafruit_8x8matrix matrix1 = Adafruit_8x8matrix();
Adafruit_8x8matrix matrix2 = Adafruit_8x8matrix();

// define eye ball without pupil
byte eyeBall[8] = {
  B00111100,
  B01111110,
  B11111111,
  B11111111,
  B11111111,
  B11111111,
  B01111110,
  B00111100
};

byte eyePupil = B11100111;

// stores current state of LEDs
byte eyeCurrent[8];
int currentX;
int currentY;


// min and max positions
#define MIN -2
#define MAX  2

// delays
#define DELAY_BLINK 40

// perform an effect every # of loop iterations, 0 to disable
#define EFFECT_ITERATION 4

// ms
#define SOUND_INTERVAL_MS 20000
#define WAKE_DURATION_MS 180000

#define ACTIVATION_DISTANCE_CM 280
#define NEAR_DISTANCE_CM 100

boolean awake = true;
long activatedMillis = 0;
long lastSound = 0;
boolean soundStarted = false;
long currentDistance = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("Time to power up");

  pinMode(PIN_SONIC_TRIGGER, OUTPUT);
  pinMode(PIN_SONIC_ECHO, INPUT);

  pinMode(PIN_SND_TRIGGER_REGULAR, OUTPUT);
  pinMode(PIN_SND_TRIGGER_WAKE, OUTPUT);
  pinMode(PIN_SND_TRIGGER_NEAR, OUTPUT);
  pinMode(PIN_SND_TRIGGER_SLEEPING, OUTPUT);
  pinMode(PIN_SND_ACTIVE, INPUT);

  digitalWrite(PIN_SND_TRIGGER_REGULAR, HIGH);
  digitalWrite(PIN_SND_TRIGGER_WAKE, HIGH);
  digitalWrite(PIN_SND_TRIGGER_NEAR, HIGH);
  digitalWrite(PIN_SND_TRIGGER_SLEEPING, HIGH);

  matrix1.begin(0x70);  // pass in the address
  matrix2.begin(0x71);  // pass in the address

  // LED test
  // vertical line
  byte b = B10000000;
  for (int c = 0; c <= 7; c++)
  {
    for (int r = 0; r <= 7; r++)
    {
      setRow(0, r, b);
      setRow(1, r, b);
    }
    b = b >> 1;
    writeMatrix();
    delay(20);
  }
  // full module
  b = B11111111;
  for (int r = 0; r <= 7; r++)
  {
    setRow(0, r, b);
    setRow(1, r, b);
  }

  writeMatrix();

  // clear both modules
  matrix1.setBrightness(16);
  matrix1.clear();
  matrix2.setBrightness(16);
  matrix2.clear();
  delay(500);

  // random seed
  randomSeed(analogRead(0));

  // center eyes
  displayEyes(0, 0);
  activatedMillis = 0;
  Serial.println("ScareOS ready.");
}

/*
  Arduino loop
*/
void loop()
{

  long awakeDurationMs = getAwakeDurationMs();

  if (awakeDurationMs > WAKE_DURATION_MS) {
    if (awake) {
      awake = false;
      Serial.println("Going to sleep");
      moveEyes(0, -4, 10);
      reduceBrightness();
    }
  }

  if (!awake) {
    slightGlow();
    bgProcessing(random(5, 7) * 500);
    return;
  }

  matrix1.setBrightness(16);
  matrix2.setBrightness(16);

  // move to random position, wait random time
  moveEyes(random(MIN, MAX + 1), random(MIN, MAX + 1), 50);
  bgProcessing(random(5, 7) * 500);

  // blink time?
  if (random(0, 5) == 0) {
    bgProcessing(500);
    blinkEyes();
    bgProcessing(500);
  }

  int effect = random(0, 10);
  switch (effect) {
    case 0: // cross eyes
      crossEyes();
      break;

    case 1: // round spin
      roundSpin(2);
      break;

    case 2: // crazy spin
      crazySpin(2);
      break;

    case 3: // meth eyes
      methEyes();
      break;

    case 4: // lazy eye
      lazyEye();
      break;

    case 5: // crazy blink
      blinkEyes(true, false);
      blinkEyes(false, true);
      break;

    case 6: // glow
      glowEyes(3);
      break;

    default:
      break;
  }

  bgProcessing(1000);
}

long getAwakeDurationMs() {
  return millis() - activatedMillis;
}

/*
Background-processing:
1. get distance if allowed timeToComeBack is greater the duration needed to measure distance
2. Trigger awake sound
3. Trigger sounds if necessary
4. Delay until timeToComeBack is passed by

*/
void bgProcessing(long timeToComeBack) {

  long start = millis();

  if (timeToComeBack > 100) {
    currentDistance = getFilteredDistance();
    long duration = millis() - start;
    Serial.print("Duration: ");
    Serial.print(duration);
    Serial.print("ms/");

    Serial.print("Distance: ");
    Serial.print(currentDistance);
    Serial.println("cm");
  }

  // activate either by distance or randomly
  if (currentDistance < ACTIVATION_DISTANCE_CM) {
    activatedMillis = millis();

    if (!awake) {
      Serial.println("Wakeup now");
      disableSleepSound();
      delay(20);
      triggerSound(PIN_SND_TRIGGER_WAKE);
      awake = true;
    }
  }

  Serial.print("Awake: ");
  Serial.print(awake);
  Serial.print("/Currently awake since ");
  Serial.print(getAwakeDurationMs());
  Serial.println("ms");

  soundProcessing();

  long duration = millis() - start;
  long sleepTimeLeft = timeToComeBack - duration;
  if (sleepTimeLeft > 100) {
    delay(20);
    bgProcessing(sleepTimeLeft - 20);
    return;
  }
  if (sleepTimeLeft > 0) {
    delay(sleepTimeLeft);
  }
}


// Performs a multi-measure of the distance and returns the 3rd highest value.
long getFilteredDistance() {
  int rangevalue[] = { 0, 0, 0, 0, 0};
  int arraysize = 5;

  for (int i = 0; i < arraysize; i++) {
    rangevalue[i] = distance();
    delay(10);
  }

  isort(rangevalue, arraysize);
  int distance = ifilter(rangevalue, arraysize);

  return distance;
}

long distance() {
  long duration, distance;
  digitalWrite(PIN_SONIC_TRIGGER, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_SONIC_TRIGGER, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_SONIC_TRIGGER, LOW);

  duration = pulseIn(PIN_SONIC_ECHO, HIGH);
  distance = (duration / 2) / 29.1;
  return distance;
}

//Sorting function
// sort function (Author: Bill Gentles, Nov. 12, 2010)
void isort(int *a, int n) {
  // *a is an array pointer function
  for (int i = 1; i < n; ++i)
  {
    int j = a[i];
    int k;
    for (k = i - 1; (k >= 0) && (j < a[k]); k--)
    {
      a[k + 1] = a[k];
    }
    a[k + 1] = j;
  }
}

// return the 2nd highest distance
int ifilter(int *x, int n) {
  return x[n - 2];
}


/*
Sound handling
1. Check if sound is playing. If so, return
2. trace timestamp of last sound playing
3. Play sleep sound if system is not "awake"
4. Play whining sound if someone comes to near
5. Play random sounds if system is "awake"
*/
void soundProcessing() {
  
  if (!awake) {
    enableSleepSound();
    return;
  }

  disableSleepSound();
  
  if (isSoundPlaying()) {
    return;
  }

  if (soundStarted) {
    soundStarted = false;
    lastSound = millis();
    return;
  }

  if (currentDistance > 0 && currentDistance < NEAR_DISTANCE_CM) {
    triggerSound(PIN_SND_TRIGGER_NEAR);
    return;
  }

  long soundInterval = millis() - lastSound;
  if (lastSound == 0 || soundInterval > SOUND_INTERVAL_MS) {
    triggerSound(PIN_SND_TRIGGER_REGULAR);
  }
}

void enableSleepSound(){
  digitalWrite(PIN_SND_TRIGGER_SLEEPING, LOW);
}

void disableSleepSound(){
  digitalWrite(PIN_SND_TRIGGER_SLEEPING, HIGH);
  delay(10);
}

void triggerSound(int pin) {
  Serial.print("triggerSound(");
  Serial.print(pin);
  Serial.println(")");

  digitalWrite(pin, LOW);
  delay(200);
  digitalWrite(pin, HIGH);
  delay(100);
  soundStarted = true;
}

/*
 True if the sound is playing.
 */
boolean isSoundPlaying() {
  if (digitalRead(PIN_SND_ACTIVE) == 0) {
    return true;
  }
  return false;
}


/*
  This method blinks both eyes
*/
void blinkEyes()
{
  blinkEyes(true, true);
}

/*
  This method blinks eyes as per provided params
*/
void blinkEyes(boolean blinkLeft, boolean blinkRight)
{
  // blink?
  if (!blinkLeft && !blinkRight)
    return;

  // close eyelids
  for (int i = 0; i <= 3; i++)
  {
    if (blinkLeft)
    {
      setRow(0, i, 0);
      setRow(0, 7 - i, 0);
    }
    if (blinkRight)
    {
      setRow(1, i, 0);
      setRow(1, 7 - i, 0);
    }
    writeMatrix();
    delay(DELAY_BLINK);
  }

  // open eyelids
  for (int i = 3; i >= 0; i--)
  {
    if (blinkLeft)
    {
      setRow(0, i, eyeCurrent[i]);
      setRow(0, 7 - i, eyeCurrent[7 - i]);
    }
    if (blinkRight)
    {
      setRow(1, i, eyeCurrent[i]);
      setRow(1, 7 - i, eyeCurrent[7 - i]);
    }
    writeMatrix();
    delay(DELAY_BLINK);
  }
}

/*
  This methods moves eyes to center position,
  then moves horizontally with wrapping around edges.
*/
void crazySpin(int times)
{
  if (times == 0)
    return;

  moveEyes(0, 0, 50);
  bgProcessing(500);

  byte row = eyePupil;
  for (int t = 0; t < times; t++)
  {
    // spin from center to L
    for (int i = 0; i < 5; i++)
    {
      row = row >> 1;
      row = row | B10000000;
      setRow(0, 3, row);  setRow(1, 3, row);
      setRow(0, 4, row);  setRow(1, 4, row);
      writeMatrix();
      bgProcessing(50);
      if (t == 0)
        bgProcessing((5 - i) * 10); // increase delay on 1st scroll (speed up effect)
    }
    // spin from R to center
    for (int i = 0; i < 5; i++)
    {
      row = row >> 1;
      if (i >= 2)
        row = row | B10000000;
      setRow(0, 3, row);  setRow(1, 3, row);
      setRow(0, 4, row);  setRow(1, 4, row);
      writeMatrix();
      bgProcessing(50);
      if (t == (times - 1))
        bgProcessing((i + 1) * 10); // increase delay on last scroll (slow down effect)
    }
  }
}

/*
  This method crosses eyes
*/
void crossEyes()
{
  moveEyes(0, 0, 50);
  bgProcessing(500);

  byte pupilR = eyePupil;
  byte pupilL = eyePupil;

  // move pupils together
  for (int i = 0; i < 2; i++)
  {
    pupilR = pupilR >> 1;
    pupilR = pupilR | B10000000;
    pupilL = pupilL << 1;
    pupilL = pupilL | B1;

    setRow(0, 3, pupilR); setRow(1, 3, pupilL);
    setRow(0, 4, pupilR); setRow(1, 4, pupilL);

    writeMatrix();
    bgProcessing(100);
  }

  bgProcessing(2000);

  // move pupils back to center
  for (int i = 0; i < 2; i++)
  {
    pupilR = pupilR << 1;
    pupilR = pupilR | B1;
    pupilL = pupilL >> 1;
    pupilL = pupilL | B10000000;

    setRow(0, 3, pupilR); setRow(1, 3, pupilL);
    setRow(0, 4, pupilR); setRow(1, 4, pupilL);
    
    writeMatrix();
    bgProcessing(100);
  }
}

/*
  This method displays eyeball with pupil offset by X, Y values from center position.
  Valid X and Y range is [MIN,MAX]
  Both LED modules will show identical eyes
*/
void displayEyes(int offsetX, int offsetY)
{
  // ensure offsets are  in valid ranges
  offsetX = getValidValue(offsetX);
  offsetY = getValidValue(offsetY);

  // calculate indexes for pupil rows (perform offset Y)
  int row1 = 3 - offsetY;
  int row2 = 4 - offsetY;

  // define pupil row
  byte pupilRow = eyePupil;

  // perform offset X
  // bit shift and fill in new bit with 1
  if (offsetX > 0) {
    for (int i = 1; i <= offsetX; i++)
    {
      pupilRow = pupilRow >> 1;
      pupilRow = pupilRow | B10000000;
    }
  }
  else if (offsetX < 0) {
    for (int i = -1; i >= offsetX; i--)
    {
      pupilRow = pupilRow << 1;
      pupilRow = pupilRow | B1;
    }
  }

  // pupil row cannot have 1s where eyeBall has 0s
  byte pupilRow1 = pupilRow & eyeBall[row1];
  byte pupilRow2 = pupilRow & eyeBall[row2];

  // display on LCD matrix, update to eyeCurrent
  for (int r = 0; r < 8; r++)
  {
    if (r == row1)
    {
      setRow(0, r, pupilRow1);
      setRow(1, r, pupilRow1);
      eyeCurrent[r] = pupilRow1;
    }
    else if (r == row2)
    {
      setRow(0, r, pupilRow2);
      setRow(1, r, pupilRow2);
      eyeCurrent[r] = pupilRow2;
    }
    else
    {
      setRow(0, r, eyeBall[r]);
      setRow(1, r, eyeBall[r]);
      eyeCurrent[r] = eyeBall[r];
    }
  }
  
  writeMatrix();
  // update current X and Y
  currentX = offsetX;
  currentY = offsetY;
}

/*
  This method corrects provided coordinate value
*/
int getValidValue(int value)
{
  if (value > MAX)
    return MAX;
  else if (value < MIN)
    return MIN;
  else
    return value;
}

/*
  This method pulsates eye (changes LED brightness)
*/
void glowEyes(int times)
{
  for (int t = 0; t < times; t++)
  {
    for (int i = 2; i <= 16; i++)
    {
      matrix1.setBrightness(i);
      matrix2.setBrightness(i);
      delay(50);
    }

    bgProcessing(250);

    reduceBrightness();

    bgProcessing(150);
  }

  for (int i = 2; i <= 16; i++)
  {
    matrix1.setBrightness(i);
    matrix2.setBrightness(i);
    delay(50);
  }

  matrix1.setBrightness(16);
  matrix2.setBrightness(16);
}

void reduceBrightness() {
  for (int i = 16; i >= 1; i--) {
    matrix1.setBrightness(i);
    matrix2.setBrightness(i);
    delay(25);
  }
}

/*
  This method pulsates eye  (changes LED brightness)
*/
void slightGlow()
{
  for (int i = 1; i <= 5; i++)
  {
    matrix1.setBrightness(i);
    matrix2.setBrightness(i);
    delay(150);
  }

  bgProcessing(250);

  for (int i = 5; i >= 1; i--) {
    matrix1.setBrightness(i);
    matrix2.setBrightness(i);
    delay(150);
  }
}

/*
  This method moves eyes to center, out and then back to center
*/
void methEyes()
{
  moveEyes(0, 0, 50);
  bgProcessing(500);

  byte pupilR = eyePupil;
  byte pupilL = eyePupil;

  // move pupils out
  for (int i = 0; i < 2; i++)
  {
    pupilR = pupilR << 1;
    pupilR = pupilR | B1;
    pupilL = pupilL >> 1;
    pupilL = pupilL | B10000000;

    setRow(0, 3, pupilR); setRow(1, 3, pupilL);
    setRow(0, 4, pupilR); setRow(1, 4, pupilL);
    writeMatrix();
    bgProcessing(100);
  }

  bgProcessing(2000);

  // move pupils back to center
  for (int i = 0; i < 2; i++)
  {
    pupilR = pupilR >> 1;
    pupilR = pupilR | B10000000;
    pupilL = pupilL << 1;
    pupilL = pupilL | B1;

    setRow(0, 3, pupilR); setRow(1, 3, pupilL);
    setRow(0, 4, pupilR); setRow(1, 4, pupilL);
    writeMatrix();
    bgProcessing(100);
  }
}

/*
  This method moves both eyes from current position to new position
*/
void moveEyes(int newX, int newY, int stepDelay)
{
  // set current position as start position
  int startX = currentX;
  int startY = currentY;

  // fix invalid new X Y values
  newX = getValidValue(newX);
  newY = getValidValue(newY);

  // eval steps
  int stepsX = abs(currentX - newX);
  int stepsY = abs(currentY - newY);

  // need to change at least one position
  if ((stepsX == 0) && (stepsY == 0))
    return;

  // eval direction of movement, # of steps, change per X Y step, perform move
  int dirX = (newX >= currentX) ? 1 : -1;
  int dirY = (newY >= currentY) ? 1 : -1;
  int steps = (stepsX > stepsY) ? stepsX : stepsY;
  int intX, intY;
  float changeX = (float)stepsX / (float)steps;
  float changeY = (float)stepsY / (float)steps;
  for (int i = 1; i <= steps; i++)
  {
    intX = startX + round(changeX * i * dirX);
    intY = startY + round(changeY * i * dirY);
    displayEyes(intX, intY);
    bgProcessing(stepDelay);
  }
}

/*
  This method lowers and raises right pupil only
*/
void lazyEye()
{
  moveEyes(0, 1, 50);
  bgProcessing(500);

  // lower left pupil slowly
  for (int i = 0; i < 3; i++)
  {
    setRow(1, i + 2, eyeBall[i + 2]);
    setRow(1, i + 3, eyeBall[i + 3] & eyePupil);
    setRow(1, i + 4, eyeBall[i + 4] & eyePupil);
    writeMatrix();
    bgProcessing(150);
  }

  bgProcessing(1000);

  // raise left pupil quickly
  for (int i = 0; i < 3; i++)
  {
    setRow(1, 4 - i, eyeBall[4 - i] & eyePupil);
    setRow(1, 5 - i, eyeBall[5 - i] & eyePupil);
    setRow(1, 6 - i, eyeBall[6 - i]);
    writeMatrix();
    bgProcessing(25);
  }
}

/*
  This method spins pupils clockwise
*/
void roundSpin(int times)
{
  if (times == 0)
    return;

  moveEyes(2, 0, 50);
  bgProcessing(500);

  for (int i = 0; i < times; i++)
  {
    displayEyes(2, -1); delay(40); if (i == 0) delay(40);
    displayEyes(1, -2); delay(40); if (i == 0) delay(30);
    displayEyes(0, -2); delay(40); if (i == 0) delay(20);
    displayEyes(-1, -2); delay(40); if (i == 0) delay(10);
    displayEyes(-2, -1); delay(40);
    displayEyes(-2, 0); delay(40);
    displayEyes(-2, 1); delay(40); if (i == (times - 1)) delay(10);
    displayEyes(-1, 2); delay(40); if (i == (times - 1)) delay(20);
    displayEyes(0, 2); delay(40); if (i == (times - 1)) delay(30);
    displayEyes(1, 2); delay(40); if (i == (times - 1)) delay(40);
    displayEyes(2, 1); delay(40); if (i == (times - 1)) bgProcessing(50);
    displayEyes(2, 0); delay(40);
  }
}


void writeMatrix(){
    matrix1.writeDisplay();
    matrix2.writeDisplay();
}

/*
  This method sets values to matrix row
  Performs 180 rotation if needed
*/
void setRow(int addr, int row, byte rowValue)
{

  for (int i = 0; i < 8; i++)
  {
    int val = rowValue & 1 << i;
    if (addr == 0)
      matrix1.drawPixel(row, i, val);
    if (addr == 1)
      matrix2.drawPixel(row, i, val);

  }


  //lc.setRow(addr, row, rowValue);
}


/*
  Reverse bits in byte
  http://www.nrtm.org/index.php/2013/07/25/reverse-bits-in-a-byte/
*/
byte bitswap (byte x)
{
  byte result;

  asm("mov __tmp_reg__, %[in] \n\t"
      "lsl __tmp_reg__  \n\t"   /* shift out high bit to carry */
      "ror %[out] \n\t"  /* rotate carry __tmp_reg__to low bit (eventually) */
      "lsl __tmp_reg__  \n\t"   /* 2 */
      "ror %[out] \n\t"
      "lsl __tmp_reg__  \n\t"   /* 3 */
      "ror %[out] \n\t"
      "lsl __tmp_reg__  \n\t"   /* 4 */
      "ror %[out] \n\t"

      "lsl __tmp_reg__  \n\t"   /* 5 */
      "ror %[out] \n\t"
      "lsl __tmp_reg__  \n\t"   /* 6 */
      "ror %[out] \n\t"
      "lsl __tmp_reg__  \n\t"   /* 7 */
      "ror %[out] \n\t"
      "lsl __tmp_reg__  \n\t"   /* 8 */
      "ror %[out] \n\t"
      : [out] "=r" (result) : [in] "r" (x));
  return (result);
}
