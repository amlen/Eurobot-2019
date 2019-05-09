/* Arduino Mega 2560 interrupt pins
   Interrupt number: 0    1    2    3    4    5
   physical pin:     2    3    21   20   19   18

             port mapping
        7  6  5  4  3  2  1  0
       -----------------------
PORTA: 29 28 27 26 25 24 23 22
PORTB: 13 12 11 10 50 51 52 53
PORTC: 30 31 32 33 34 35 36 37
PORTD: 38 -- -- -- 18 19 20 21
PORTE: -- -- 3  2  5  -- 1  0
PORTG: -- -- 4  -- -- 39 40 41
PORTH: -- 8  9  7  6  -- 16 17
PORTJ: -- -- -- -- -- -- 14 15
PORTL: 42 43 44 45 16 47 48 49
*/

#include <FastPID.h>
#include <Arduino.h>
#include <Servo.h>
#include <ros.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Empty.h>
#include <std_msgs/String.h>
#include <geometry_msgs/Vector3.h>

// Macros for easier faster port writing
// Not used in this code anymore, but useful to write to leds
#define SET(x,y) (x|=(1<<y))    // usage: SET(PORTA,7) is the same as digitalWrite(29,HIGH)
#define CLR(x,y) (x&=(~(1<<y))) // usage: CLR(PORTE,1) is the same as digitalWrite(1,LOW)

// Quadrature Encoders
#define c_LeftEncoderPinA 2 // PORTE4
#define c_LeftEncoderPinB 3 // PORTE5
#define c_RightEncoderPinA 19  // PORTD2
#define c_RightEncoderPinB 18 // PORTD3

#define c_LeftLedPin 4 // PORTH6
#define c_RightLedPin 5 // PORTH5

#define c_LeftServoPin 22
#define c_RightServoPin 24

/* Créer un objet Servo pour contrôler le servomoteur */
Servo servo_right;
Servo servo_left;

volatile bool _LeftEncoderASet;
volatile bool _LeftEncoderBSet;
volatile bool _LeftEncoderAPrev = 0;
volatile bool _LeftEncoderBPrev = 0;
volatile long _LeftEncoderTicks = 0;

volatile bool _RightEncoderASet;
volatile bool _RightEncoderBSet;
volatile bool _RightEncoderAPrev = 0;
volatile bool _RightEncoderBPrev = 0;
volatile long _RightEncoderTicks = 0;

volatile  byte c_LeftLedPinState = LOW;
volatile  byte c_RightLedPinState = LOW;

// Init motors
const byte pwm_motorLeft = 9;
const byte dir_motorLeft = 11;
const byte pwm_motorRight = 10;
const byte dir_motorRight = 7;

// declare PWM variables
int vitesse;
int PWM_lin;

int PWM_rot;
int PWM_M_Left;
int PWM_M_Right;
int PWM_M_Max_Left;
int PWM_M_Max_Right;
int ts;
int tss;
volatile float theta;

// set PID constants
float KpLin=0.2, KiLin=0, KdLin=0, Hz=20;
float KpRot=0.25, KiRot=0, KdRot=0;
int output_bits = 9;
bool output_signed = true;

// create PID objects
FastPID diff_PID_lin(KpLin, KiLin, KdLin, Hz, output_bits, output_signed);
FastPID diff_PID_rot(KpRot, KiRot, KdRot, Hz, output_bits, output_signed);
FastPID diff_PID(KpRot, KiRot, KdRot, Hz, output_bits, output_signed);

ros::NodeHandle nh;

// init ros messages
float dist_msg;
float turn_msg;
String arm_msg;
bool stop_condition = false;
std_msgs::Empty feedback_msg;
geometry_msgs::Vector3 encoder_msg;

// define publishers
ros::Publisher pub_encoder("encoder_ticks", &encoder_msg);
ros::Publisher pub_feedback("cmd_feedback", &feedback_msg);

// callback functions are executed each time nh.sipnOnce() is run and there is a new message on the topic
void dist_cb(const std_msgs::Float32& msg) {
    dist_msg = msg.data;
    Linear(dist_msg);
    pub_feedback.publish(&feedback_msg); // publish empty feedback message so we know this action has been completed
}
void turn_cb(const std_msgs::Float32& msg) {
    turn_msg = msg.data;
    Rotation(turn_msg);
    pub_feedback.publish(&feedback_msg);
}
void arm_cb(const std_msgs::String& msg) {
    arm_msg = msg.data;
    if (strcmp(arm_msg.c_str(), "left") == 0)
    {
        activate_left_arm();
        pub_feedback.publish(&feedback_msg);
    }
    else if (strcmp(arm_msg.c_str(), "right") == 0)
    {
        activate_right_arm();
        pub_feedback.publish(&feedback_msg);
    }
}
void stop_cb(const std_msgs::String& msg) {
    if (strcmp(msg.data, "stop") == 0) {
        stop_condition = true;
        stopwheels();
    }
    else
        stop_condition = false;
}

// define listeners
ros::Subscriber<std_msgs::Float32> sub_lin("cmd_lin", &dist_cb );
ros::Subscriber<std_msgs::Float32> sub_rot("cmd_rot", &turn_cb );
ros::Subscriber<std_msgs::String> sub_arm("cmd_arm", &arm_cb );
ros::Subscriber<std_msgs::String> sub_stop("cmd_stop", &stop_cb );

void setup() {
    Serial.begin(57600);

    // init ros
    nh.initNode();
    nh.subscribe(sub_lin);
    nh.subscribe(sub_rot);
    nh.subscribe(sub_arm);
    nh.subscribe(sub_stop);
    nh.advertise(pub_encoder);
    nh.advertise(pub_feedback);

    pinMode(c_LeftEncoderPinA, INPUT);     // sets pin A as input
    digitalWrite(c_LeftEncoderPinA, LOW);  // turn on pullup resistors
    pinMode(c_LeftEncoderPinB, INPUT);     // sets pin B as input
    digitalWrite(c_LeftEncoderPinB, LOW);  // turn on pullup resistors
    attachInterrupt(digitalPinToInterrupt(c_LeftEncoderPinA), HandleLeftMotorInterrupt, CHANGE);

    pinMode(c_RightEncoderPinA, INPUT);     // sets pin A as input
    digitalWrite(c_RightEncoderPinA, LOW);  // turn on pullup resistors
    pinMode(c_RightEncoderPinB, INPUT);     // sets pin B as input
    digitalWrite(c_RightEncoderPinB, LOW);  // turn on pullup resistors
    attachInterrupt(digitalPinToInterrupt(c_RightEncoderPinA), HandleRightMotorInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(c_RightEncoderPinB), HandleRightMotorInterrupt, CHANGE);

    pinMode(c_LeftLedPin, OUTPUT);
    digitalWrite(c_LeftLedPin, LOW);
    pinMode(c_RightLedPin, OUTPUT);
    digitalWrite(c_RightLedPin, LOW);

    pinMode(pwm_motorLeft, OUTPUT);
    pinMode(dir_motorLeft, OUTPUT);
    pinMode(pwm_motorRight, OUTPUT);
    pinMode(dir_motorRight, OUTPUT);

    //Check PID
    if (diff_PID.err()) {
        nh.logerror("There is a configuration error!");
        for (;;) {}
    }

    TCCR2B = TCCR2B & 0b11111000 | 0x01; // sets Arduino Mega's pin 10 and 9 to frequency 31250.

    servo_right.attach(c_RightServoPin);
    servo_left.attach(c_RightServoPin);
}

void loop() {
    // Publish encoder data
    encoder_msg.x = _LeftEncoderTicks;
    encoder_msg.y = _RightEncoderTicks;
    pub_encoder.publish(&encoder_msg);

    nh.spinOnce();

    digitalWrite(c_LeftLedPin, c_LeftLedPinState);
    digitalWrite(c_RightLedPin, c_RightLedPinState);
}

void activate_left_arm() {
    // Fait bouger le bras gauche de 0° à 180°
    servo_left.write(90);
    delay(15);
    for (int position = 110; position >= 0; position--) {
        servo_left.write(position);
        delay(15);
    }
}

void activate_right_arm() {
    servo_right.write(90);
    delay(15);
    for (int position = 130; position >= 40; position--) {
        servo_right.write(position);
        delay(15);
    }
}

void Linear(double setpoint_lindis) {
    double setpoint_rot =0;
    double actposition = (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
    double setpoint_lin= actposition + (setpoint_lindis * 4000 )/(2* 3.14* 0.04) ;
    double error_lin =  setpoint_lin - 0;
    double error_rot =  setpoint_rot -0;
    double feedback_lin = 0;
    double feedback_rot = 0;

    while (abs(error_lin) > 180) {
        nh.spinOnce();
        if (stop_condition == false){
            error_lin = setpoint_lin - (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
            error_rot = setpoint_rot - (double)((_LeftEncoderTicks - _RightEncoderTicks))/2.0;
            feedback_lin = (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
            feedback_rot =(double)((_LeftEncoderTicks - _RightEncoderTicks))/2.0;
            ts = micros();
            PWM_lin = diff_PID_lin.step(setpoint_lin, feedback_lin);
            PWM_rot = diff_PID_rot.step(setpoint_rot, feedback_rot);
            tss = micros();
            PWM_M_Left = PWM_lin + PWM_rot;
            PWM_M_Right =PWM_lin - PWM_rot;

            if(PWM_M_Left > 0) digitalWrite(dir_motorLeft, HIGH);
            else digitalWrite(dir_motorLeft, LOW);
            if(PWM_M_Right > 0) digitalWrite(dir_motorRight, HIGH);
            else digitalWrite(dir_motorRight, LOW);

            if (abs(PWM_M_Left)-abs(PWM_M_Right) > 10){
                if(abs(PWM_M_Left)<abs(PWM_M_Right)){
                    PWM_M_Max_Left = 170;
                    PWM_M_Max_Right = 240;
                }
                else {
                    PWM_M_Max_Left = 240;
                    PWM_M_Max_Right = 170;
                }
            }

            else{
                PWM_M_Max_Left = 170;
                PWM_M_Max_Right = 170;
            }

            if(abs(PWM_M_Left) > PWM_M_Max_Left) PWM_M_Left = PWM_M_Max_Left;
            if(abs(PWM_M_Right) > PWM_M_Max_Right) PWM_M_Right = PWM_M_Max_Right;

            analogWrite(pwm_motorRight, abs(PWM_M_Right));
            analogWrite(pwm_motorLeft, abs(PWM_M_Left));
        } // if()
    } // while()
   stopwheels();
}


void Rotation(double angleConsigne) {
    double actposition = (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
    double distance_rot = (angleConsigne /360)*3.14*2*0.1775;
    double setpoint_rot= actposition + (distance_rot * 4000 )/(2* 3.14* 0.04) ;
    double setpoint_lin = 0;
    double error_lin = setpoint_lin - 0;
    double error_rot = setpoint_rot - 0;
    double feedback_lin = 0;
    double feedback_rot = 0;

    while(abs(error_rot) > 180) {
        error_lin = setpoint_lin - (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
        error_rot = setpoint_rot - (double)((_LeftEncoderTicks - _RightEncoderTicks))/2.0;
        feedback_lin = (double)((_LeftEncoderTicks + _RightEncoderTicks))/2.0;
        feedback_rot =(double)((_LeftEncoderTicks - _RightEncoderTicks))/2.0;
        ts = micros();
        PWM_lin = diff_PID_lin.step(setpoint_lin, feedback_lin);
        PWM_rot = diff_PID_rot.step(setpoint_rot, feedback_rot);
        tss = micros();
        PWM_M_Left = PWM_lin + PWM_rot;
        PWM_M_Right =PWM_lin - PWM_rot;

        if(PWM_M_Left > 0) digitalWrite(dir_motorLeft, HIGH);
        else digitalWrite(dir_motorLeft, LOW);

        if(PWM_M_Right > 0) digitalWrite(dir_motorRight, HIGH);
        else digitalWrite(dir_motorRight, LOW);

        if(abs(PWM_M_Left)-abs(PWM_M_Right) > 10){
            if(abs(PWM_M_Left)<abs(PWM_M_Right)){
                PWM_M_Max_Left = 170;
                PWM_M_Max_Right = 240;
            }
            else {
                PWM_M_Max_Left = 240;
                PWM_M_Max_Right = 170;
            }
        }
        else {
            PWM_M_Max_Left = 170;
            PWM_M_Max_Right = 170;
        }

        if(abs(PWM_M_Left) > PWM_M_Max_Left) PWM_M_Left = PWM_M_Max_Left;
        if(abs(PWM_M_Right) > PWM_M_Max_Right) PWM_M_Right = PWM_M_Max_Right;

        analogWrite(pwm_motorRight, abs(PWM_M_Right));
        analogWrite(pwm_motorLeft, abs(PWM_M_Left));
    }
}

void stopwheels() {
    analogWrite(pwm_motorLeft, 0);
    analogWrite(pwm_motorRight, 0);
}

void HandleLeftMotorInterrupt(){
    _LeftEncoderBSet = ((PINE & B00100000)>>5); //digitalRead(3) PE5
    _LeftEncoderASet = ((PINE & B00010000)>>4); //digitalRead(2) PE4

    _LeftEncoderTicks+=ParseLeftEncoder();

    _LeftEncoderAPrev = _LeftEncoderASet;
    _LeftEncoderBPrev = _LeftEncoderBSet;
}

void HandleRightMotorInterrupt(){
    _RightEncoderBSet = ((PIND & B00000100)>>2); // digitalRead(20) PD2
    _RightEncoderASet = ((PIND & B00001000)>>3); // digitalRead(21) PD3

    _RightEncoderTicks+=ParseRightEncoder();

    _RightEncoderAPrev = _RightEncoderASet;
    _RightEncoderBPrev = _RightEncoderBSet;
}

int ParseLeftEncoder(){
    if(_LeftEncoderAPrev && _LeftEncoderBPrev){
        if(!_LeftEncoderASet && _LeftEncoderBSet) return 1;
        if(_LeftEncoderASet && !_LeftEncoderBSet) return -1;
    }else if(!_LeftEncoderAPrev && _LeftEncoderBPrev){
        if(!_LeftEncoderASet && !_LeftEncoderBSet) return 1;
        if(_LeftEncoderASet && _LeftEncoderBSet) return -1;
    }else if(!_LeftEncoderAPrev && !_LeftEncoderBPrev){
        if(_LeftEncoderASet && !_LeftEncoderBSet) return 1;
        if(!_LeftEncoderASet && _LeftEncoderBSet) return -1;
    }else if(_LeftEncoderAPrev && !_LeftEncoderBPrev){
        if(_LeftEncoderASet && _LeftEncoderBSet) return 1;
        if(!_LeftEncoderASet && !_LeftEncoderBSet) return -1;
    }
    return 0;
}

int ParseRightEncoder(){
    if(_RightEncoderAPrev && _RightEncoderBPrev){
        c_LeftLedPinState = HIGH;
        c_RightLedPinState = HIGH;
        if(!_RightEncoderASet && _RightEncoderBSet) return 1;
        if(_RightEncoderASet && !_RightEncoderBSet) return -1;
    }else if(!_RightEncoderAPrev && _RightEncoderBPrev){
        c_LeftLedPinState = LOW;
        c_RightLedPinState = HIGH;
        if(!_RightEncoderASet && !_RightEncoderBSet) return 1;
        if(_RightEncoderASet && _RightEncoderBSet) return -1;
    }else if(!_RightEncoderAPrev && !_RightEncoderBPrev){
        c_LeftLedPinState = LOW;
        c_RightLedPinState = LOW;
        if(_RightEncoderASet && !_RightEncoderBSet) return 1;
        if(!_RightEncoderASet && _RightEncoderBSet) return -1;
    }else if(_RightEncoderAPrev && !_RightEncoderBPrev){
        c_LeftLedPinState = HIGH;
        c_RightLedPinState = LOW;
        if(_RightEncoderASet && _RightEncoderBSet) return 1;
        if(!_RightEncoderASet && !_RightEncoderBSet) return -1;
    }
    return 0;
}