const int ecgPin = 34;

void setup() {
  Serial.begin(115200);
}

void loop() {
  int value = analogRead(ecgPin);
  Serial.println(value);
  delay(10);
}