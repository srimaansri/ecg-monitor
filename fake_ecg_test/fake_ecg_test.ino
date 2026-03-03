#include <math.h>

const int BAUD_RATE = 115200;
const int ADC_MAX = 4095;
const float ADC_CENTER = 2048.0f;

const float SAMPLE_RATE_HZ = 250.0f;
const float DT = 1.0f / SAMPLE_RATE_HZ;
const uint32_t SAMPLE_PERIOD_US = (uint32_t)(1000000.0f / SAMPLE_RATE_HZ);

const float BASE_HEART_RATE_BPM = 72.0f;
const float RESP_RATE_HZ = 0.23f;

const float MAINS_HZ = 50.0f;
const float MAINS_AMPLITUDE = 7.5f;

uint32_t nextSampleUs = 0;

float simTime = 0.0f;
float beatElapsed = 0.0f;
float rrInterval = 60.0f / BASE_HEART_RATE_BPM;
float respirationPhase = 0.0f;

float noiseLP = 0.0f;
float motionBurstTimer = 0.0f;
float motionBurstLevel = 0.0f;

struct BeatShape {
  float pAmp;
  float pCenter;
  float pWidth;

  float qAmp;
  float qCenter;
  float qWidth;

  float rAmp;
  float rCenter;
  float rWidth;

  float sAmp;
  float sCenter;
  float sWidth;

  float tAmp;
  float tCenter;
  float tWidth;
};

BeatShape shape;

float randUniform(float minValue, float maxValue) {
  return minValue + (maxValue - minValue) * (random(0, 10001) / 10000.0f);
}

float gaussianPulse(float x, float center, float width, float amplitude) {
  float z = (x - center) / width;
  return amplitude * expf(-0.5f * z * z);
}

void randomizeBeatShape(float currentRR) {
  float rrNorm = currentRR;

  shape.pAmp = 70.0f * randUniform(0.85f, 1.20f);
  shape.pCenter = 0.17f * rrNorm + randUniform(-0.004f, 0.004f);
  shape.pWidth = 0.030f * rrNorm * randUniform(0.90f, 1.20f);

  shape.qAmp = -95.0f * randUniform(0.85f, 1.20f);
  shape.qCenter = 0.34f * rrNorm + randUniform(-0.003f, 0.003f);
  shape.qWidth = 0.010f * rrNorm * randUniform(0.80f, 1.20f);

  shape.rAmp = 900.0f * randUniform(0.90f, 1.20f);
  shape.rCenter = 0.37f * rrNorm + randUniform(-0.002f, 0.002f);
  shape.rWidth = 0.008f * rrNorm * randUniform(0.75f, 1.25f);

  shape.sAmp = -220.0f * randUniform(0.85f, 1.25f);
  shape.sCenter = 0.40f * rrNorm + randUniform(-0.003f, 0.003f);
  shape.sWidth = 0.012f * rrNorm * randUniform(0.85f, 1.20f);

  shape.tAmp = 180.0f * randUniform(0.85f, 1.20f);
  shape.tCenter = 0.62f * rrNorm + randUniform(-0.010f, 0.010f);
  shape.tWidth = 0.070f * rrNorm * randUniform(0.85f, 1.20f);
}

float updateRRInterval() {
  float respiratorySinusArrhythmia = 4.5f * sinf(respirationPhase);
  float shortTermJitter = randUniform(-1.8f, 1.8f);
  float bpm = BASE_HEART_RATE_BPM + respiratorySinusArrhythmia + shortTermJitter;

  if (bpm < 50.0f) bpm = 50.0f;
  if (bpm > 110.0f) bpm = 110.0f;

  return 60.0f / bpm;
}

float morphologyECG(float tInBeat) {
  float ecg = 0.0f;
  ecg += gaussianPulse(tInBeat, shape.pCenter, shape.pWidth, shape.pAmp);
  ecg += gaussianPulse(tInBeat, shape.qCenter, shape.qWidth, shape.qAmp);
  ecg += gaussianPulse(tInBeat, shape.rCenter, shape.rWidth, shape.rAmp);
  ecg += gaussianPulse(tInBeat, shape.sCenter, shape.sWidth, shape.sAmp);
  ecg += gaussianPulse(tInBeat, shape.tCenter, shape.tWidth, shape.tAmp);
  return ecg;
}

float realisticNoise() {
  float white = randUniform(-1.0f, 1.0f);
  noiseLP = 0.90f * noiseLP + 0.10f * white;
  float emgLikeNoise = 18.0f * noiseLP;

  motionBurstTimer -= DT;
  if (motionBurstTimer <= 0.0f) {
    if (random(0, 1000) < 3) {
      motionBurstLevel = randUniform(40.0f, 120.0f);
      motionBurstTimer = randUniform(0.08f, 0.25f);
    } else {
      motionBurstLevel = 0.0f;
      motionBurstTimer = randUniform(0.15f, 0.40f);
    }
  }
  float motionArtifact = motionBurstLevel * randUniform(-1.0f, 1.0f);

  return emgLikeNoise + motionArtifact;
}

void setup() {
  Serial.begin(BAUD_RATE);
  randomSeed((uint32_t)(micros() ^ analogRead(34) ^ analogRead(35)));

  rrInterval = 60.0f / BASE_HEART_RATE_BPM;
  randomizeBeatShape(rrInterval);
  nextSampleUs = micros();

  Serial.println("clean realistic");
}

void loop() {
  uint32_t now = micros();
  if ((int32_t)(now - nextSampleUs) < 0) {
    return;
  }
  nextSampleUs += SAMPLE_PERIOD_US;

  beatElapsed += DT;
  simTime += DT;
  respirationPhase += 2.0f * PI * RESP_RATE_HZ * DT;

  if (beatElapsed >= rrInterval) {
    beatElapsed -= rrInterval;
    rrInterval = updateRRInterval();
    randomizeBeatShape(rrInterval);
  }

  float cleanWave = morphologyECG(beatElapsed);

  float baselineWander = 55.0f * sinf(2.0f * PI * 0.28f * simTime)
                       + 20.0f * sinf(2.0f * PI * 0.07f * simTime + 1.1f);
  float mainsHum = MAINS_AMPLITUDE * sinf(2.0f * PI * MAINS_HZ * simTime);
  float noise = realisticNoise();

  float cleanSignal = ADC_CENTER + cleanWave;
  float realisticSignal = cleanSignal + baselineWander + mainsHum + noise;

  if (cleanSignal < 0.0f) cleanSignal = 0.0f;
  if (cleanSignal > ADC_MAX) cleanSignal = ADC_MAX;

  if (realisticSignal < 0.0f) realisticSignal = 0.0f;
  if (realisticSignal > ADC_MAX) realisticSignal = ADC_MAX;

  Serial.print((int)cleanSignal);
  Serial.print(' ');
  Serial.println((int)realisticSignal);
}