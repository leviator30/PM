#include <Arduino.h>
#include <avr/interrupt.h>

// VARIABILE GLOBALE
String password = "";
String access_password = "";
bool set_password = false;

// BUZZER
const int buzzer = A3;
volatile bool toggle = false;

ISR(TIMER2_COMPA_vect) {
  toggle = !toggle;
  if (toggle)
    PORTC |= (1 << PC3); // buzzer ON
  else
    PORTC &= ~(1 << PC3); // buzzer OFF
}

// FUNCTIE DE SUNET
void playTone(float freq) {
  if (freq == 0) {
    TIMSK2 &= ~(1 << OCIE2A);
    PORTC &= ~(1 << PC3);
    return;
  }

  int ocr = (int)(1000000 / (freq * 2) / 0.5);
  if (ocr < 255) {
    OCR2A = ocr;
  } else {
    OCR2A = 255;
  }
  TIMSK2 |= (1 << OCIE2A);
}

// BUTON SI RUTINA INTRERUPERE BUTON
volatile bool buttonInterruptTriggered = false;

// MINIM 1S INTRE APASARI
ISR(INT0_vect) {
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();

  if (interrupt_time - last_interrupt_time > 1000) {
    buttonInterruptTriggered = true;
    last_interrupt_time = interrupt_time;
  }
}

// KEYPAD
#include <Keypad.h>
const byte ROWS = 4;
const byte COLS = 4;

byte rowPins[ROWS] = {12, 11, 10, 9}; 
byte colPins[COLS] = {7, 6, 5, 4};

char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// SETARE LCD-I2C
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

// SETARE SERVOS
#include <Servo.h>
Servo servo;
const int servos = 3;

// SETARE HC-SR04 SI RUTINA INTRERUPERE TIMER1
volatile uint16_t start_time = 0;
volatile uint16_t end_time = 0;
volatile bool capture_done = false;

ISR(TIMER1_CAPT_vect) {
  if (!capture_done) {
    if (TCCR1B & (1 << ICES1)) {
      start_time = ICR1;              // Captură pe front RISING
      TCCR1B &= ~(1 << ICES1);        // Comutare pe front FALLING
    } else {
      end_time = ICR1;                // Captură pe front FALLING
      capture_done = true;            // Măsurare completă
      TCCR1B |= (1 << ICES1);         // Comutare înapoi pe RISING
    }
  }
}

/* DELAY PROPRIU */
void delayMicroseconds_custom(uint8_t us) {
  uint8_t ticks_needed = us / 4;
  uint8_t start = TCNT0;

  while ((uint8_t)(TCNT0 - start) < ticks_needed);
}

/* FUNCTIE CITIRE DISTANTA */
float getDistance() {
  capture_done = false;
  TCNT1 = 0;

  PORTB &= ~(1<<PB5);
  delayMicroseconds_custom(2);
  PORTB |= (1<<PB5);
  delayMicroseconds_custom(10);
  PORTB &= ~(1<<PB5);

  // Se asteapta captarea ecoului
  unsigned long timeout = millis();
  while (!capture_done && (millis() - timeout < 100)) {
  }

  if (capture_done) {
    uint16_t pulse_length = end_time - start_time;
    float duration_us = pulse_length * 0.5;
    float distance_cm = duration_us * 0.0343 / 2.0;
    return distance_cm;
  }

  return -1.0;
}

/* FUNCTIE DE DESCHIDERE A USII */
void openDoor() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Acces");
  lcd.setCursor(0, 1);
  lcd.print("garantat!");

  PORTC &= ~(1<<PC0);
  PORTC |= (1<<PC1);

  playTone(1000);
  for(int pos=0; pos<=90; pos+=1) {
    servo.write(pos);
    delay(15);
  }
  playTone(0);

  
  /* SENZOR PERSOANA IN FATA USII */
  delay(1000);
  int distance = getDistance();
  while(distance > 0 && distance<10) {
    Serial.println(distance);
    distance = getDistance();
    delay(1000);
  }

  playTone(1000);
  for(int pos=90; pos>=0; pos-=1) {
    servo.write(pos);
    delay(15);
  }
  playTone(0);

  PORTC |= (1<<PC0);
  PORTC &= ~(1<<PC1);
}

/* FUNCTIE DE RESPINGERE A ACCESULUI */
void accessDenied() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Parola");
  lcd.setCursor(0, 1);
  lcd.print("gresita!");

  playTone(200);
  delay(500);
  playTone(0);
  delay(500);
  playTone(200);
  delay(500);
  playTone(0);
  delay(500);
  playTone(200);
  delay(1000);
  playTone(0);
}

void setup() {
  // put your setup code here, to run once:
  DDRC |= (1<<PC0); // setare pinul A0(14), led rosu
  DDRC |= (1<<PC1); // setare pinul A1(15), led verde
  DDRC |= (1<<PC2); // setare pinul A2(16), led albastru

  /* SETARE BUTON SI REGISTRI INTRERUPERE */
  DDRD &= ~(1<<PD2);
  cli();
  EICRA |= (1<<ISC01);
  EICRA &= ~(1<<ISC00);
  EIMSK |= (1<<INT0);
  sei();

  /* SETARE REGISTRI HC-SR04 SI TIMER1 (NORMAL MODE, PRESCALER 8) */
  DDRB |= (1<<PB5);
  DDRB &= ~(1<<PB0);
  
  TCCR1A = 0;
  TCCR1B = (1 << ICES1) | (1 << CS11);
  TIMSK1 = (1 << ICIE1);

  /* SETARE BUZZER SI REGISTRI */
  DDRC |= (1 << PC3);

  cli();
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;

  OCR2A = 0;
  TCCR2A |= (1 << WGM21);
  TCCR2B |= (1 << CS21);
  sei();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Setati parola 6");
  lcd.setCursor(0, 1);
  lcd.print("cifre:");

  /* SETARE SERVO */
  servo.attach(servos);

  /*
    SETARE INITIALA A PAROLEI
    PAROLA TREBUIE SA FIE DIN 6 CARACTERE
    SE CERE LA FINAL CONFIRMAREA CORECTITUDINII INTRODUCERII PAROLEI
  */
  int cursor = 7;
  char key;
  enum State {
    SET_PASSWORD,
    CONFIRM_PASSWORD,
  };
  State state = SET_PASSWORD;

  /* SEMNALIZARE LED ALBASTRU SETAREA PAROLEI */
  PORTC |= (1<<PC2);

  while(!set_password) {
    key = customKeypad.getKey();

    if(key != NO_KEY) {
      switch(state) {
        case SET_PASSWORD: 
          lcd.setCursor(cursor+password.length(), 1);
          lcd.print(key);

          password += key;

          /* STARE CONFIRMARE PAROLA */
          if(password.length()==6) {
            state = CONFIRM_PASSWORD;
            delay(1000);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Confirmati");
            lcd.setCursor(0, 1);
            lcd.print("(0=nu, 1=da)?");
          }
          break;

        case CONFIRM_PASSWORD:
          /* PAROLA INTRODUSA CORECT*/
          if(key == '1') {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Parola");
            lcd.setCursor(0, 1);
            lcd.print("salvata!");

            set_password = true;

          /* PAROLA INTRODUSA GRESIT, REINTRODUCERE PAROLA*/
          } else if(key == '0') {
            state = SET_PASSWORD;

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Setati o noua");
            lcd.setCursor(0, 1);
            lcd.print("parola:");

            password = "";
          }
          break;
      }
    }
  }

  delay(2500);
  PORTC &= ~(1<<PC2);
  PORTC |= (1<<PC0);
  
  servo.write(0);

  lcd.clear();
}

void loop() {
  int cursor = 8;

  lcd.setCursor(0, 0);
  lcd.print("Introduceti");
  lcd.setCursor(0, 1);
  lcd.print("parola: ");

  /* ASCULTARE INTRODUCERE PAROLA/BUTON */
  char customKey = customKeypad.getKey();
  if(customKey != NO_KEY) {
    lcd.setCursor(cursor+access_password.length(), 1);
    lcd.print(customKey);
    
    access_password += customKey;

    if(access_password.length() == 6) {
      delay(1000);

      if(access_password.equals(password)) {
        openDoor();
      } else {
        accessDenied();
      }

      access_password = "";
      lcd.clear();
    }
  }

  if(buttonInterruptTriggered) {
    buttonInterruptTriggered = false;
    openDoor();
    lcd.clear();
  }
}
