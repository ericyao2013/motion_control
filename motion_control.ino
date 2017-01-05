/* Copyright (C) 2012 Kristian Lauszus, TKJ Electronics. All rights reserved.

 This software may be distributed and modified under the terms of the GNU
 General Public License version 2 (GPL2) as published by the Free Software
 Foundation and appearing in the file GPL2.TXT included in the packaging of
 this file. Please note that GPL2 Section 2[b] requires that all works based
 on this software must also be made publicly available under the terms of
 the GPL2 ("Copyleft").

 Contact information
 -------------------

 Kristian Lauszus, TKJ Electronics
 Web      :  http://www.tkjelectronics.com
 e-mail   :  kristianl@tkjelectronics.com
 */

#include <Wire.h>
#include "Kalman.h" // Source: https://github.com/TKJElectronics/KalmanFilter


#define USE_KALMAN_CC 1
#ifdef USE_KALMAN_CC
#include <kalman_filter.h>
#endif

#define USE_ROS
#ifdef USE_ROS
#include <ros.h>
#include <std_msgs/Float64.h>
#endif



#define RESTRICT_PITCH // Comment out to restrict roll to ±90deg instead - please read: http://www.freescale.com/files/sensors/doc/app_note/AN3461.pdf

#define INFINITE (unsigned long)1000000
#define P 5.f
#define D 10.f
#define V_upper_limit 300.0f
//#define ACC_upper_limit 40000.0f
#define ACC_upper_limit 400000.0f

const int array_size = 1;
double sum_kalAngleX, sum_kalAngleY;
double pre_kalAngleX[array_size];
double pre_kalAngleY[array_size];

uint32_t Now = 0;
uint32_t lastUpdate = 0;
float deltat = 0.0f;
float sum = 0.0f;
uint32_t count = 0;
uint32_t sumCount = 0; 
uint32_t delt_t = 0;


Kalman kalmanX; // Create the Kalman instances
Kalman kalmanY;

/* IMU Data */
double accX, accY, accZ;
double gyroX, gyroY, gyroZ;
int16_t tempRaw;

double gyroXangle, gyroYangle; // Angle calculate using the gyro only
double compAngleX, compAngleY; // Calculated angle using a complementary filter
double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter

uint32_t timer;
uint8_t i2cData[14]; // Buffer for I2C data

// TODO: Make calibration routine

//ROS stuff
#ifdef USE_ROS
ros::NodeHandle nh;
std_msgs::Float64 imu_pitch_msg;
std_msgs::Float64 imu_roll_msg;
ros::Publisher imu_pitch_pub("imu_pitch", &imu_pitch_msg);
ros::Publisher imu_roll_pub("imu_roll", &imu_roll_msg);

std_msgs::Float64 acc_pitch_msg;
std_msgs::Float64 acc_roll_msg;
ros::Publisher acc_pitch_pub("acc_pitch", &acc_pitch_msg);
ros::Publisher acc_roll_pub("acc_roll", &acc_roll_msg);
#endif

//stepper motor stuff
uint16_t counter_m[2];
uint16_t period_m[2];
uint8_t dir_m[2];

//motor 0 step: pin32 => PORTC 5
//motor 0 dir:  pin33 => PORTC 4
//motor 2 step: pin30 => PORTC 7
//motor 2 dir:  pin31 => PORTC 6
//motor 1 step: pin34 => PORTC 3
//motor 1 dir:  pin35 => PORTC 2
//motor 3 step: pin36 => PORTC 1
//motor 3 dir:  pin37 => PORTC 0
#define CLR(x,y) (x&=(~(1<<y)))
#define SET(x,y) (x|=(1<<y))

const uint8_t motor_step_0 = 5;
const uint8_t motor_dir_0 = 4;
const uint8_t motor_step_2 = 7;
const uint8_t motor_dir_2 = 6;
const uint8_t motor_step_1 = 3;
const uint8_t motor_dir_1 = 2;
const uint8_t motor_step_3 = 1;
const uint8_t motor_dir_3 = 0;

//PID stuff
float kp_pitch = P;
float kd_pitch = D;
float kp_roll = P;
float kd_roll = D;

float error_pitch = 0.0;
float pre_error_pitch = 0.0;
float pre_pre_error_pitch = 0.0;
float error_roll = 0.0;
float pre_error_roll = 0.0;
float pre_pre_error_roll = 0.0;

double dt = 0.002;

float pid_output;

float velocity_pitch = 0.0;
float velocity_roll = 0.0;
float acc_pitch = 0.0;
float acc_roll = 0.0;

float setpoint_pitch = 0.0;
float setpoint_roll = 0.0;

//kalman filter cc stuff
#ifdef USE_KALMAN_CC
KalmanFilter * kalman_cc_pitch;
KalmanFilter * kalman_cc_roll;
float kalman_cc_P = 1;
float kalman_cc_Q = 0.1;//the bigger the tighter
float kalman_cc_R = 2;//the smaller the tighter

float kalman_cc_pitch_value = 0.0;
float kalman_cc_roll_value = 0.0;

float filtered_error_pitch = 0.0;
float filtered_error_roll = 0.0;

float filtered_pre_error_pitch = 0.0;
float filtered_pre_error_roll = 0.0;

float filtered_pre_pre_error_pitch = 0.0;
float filtered_pre_pre_error_roll = 0.0;
#ifdef USE_ROS
std_msgs::Float64 kalman_cc_imu_pitch_msg;
std_msgs::Float64 kalman_cc_imu_roll_msg;
ros::Publisher kalman_cc_imu_pitch_pub("kalman_cc_imu_pitch", &kalman_cc_imu_pitch_msg);
ros::Publisher kalman_cc_imu_roll_pub("kalman_cc_imu_roll", &kalman_cc_imu_roll_msg);
#endif
#endif

#ifdef USE_KALMAN_CC
float pid_pitch(float pv, float filtered_pv)
{
    error_pitch = setpoint_pitch - pv;
    filtered_error_pitch = setpoint_pitch - filtered_pv;
    pid_output = kp_pitch * error_pitch + kd_pitch * (filtered_error_pitch - filtered_pre_pre_error_pitch) / (2 * dt);
    filtered_pre_pre_error_pitch = filtered_pre_error_pitch;
    filtered_pre_error_pitch = filtered_error_pitch;
    if (fabs(pid_output) > ACC_upper_limit)
    {
      pid_output = pid_output > 0 ? ACC_upper_limit : -ACC_upper_limit;
    }
    return pid_output;
}

float pid_roll(float pv, float filtered_pv)
{
    error_roll = setpoint_roll - pv;
    filtered_error_roll = setpoint_roll - filtered_pv;
    pid_output = kp_roll * error_roll + kd_roll * (filtered_error_roll - filtered_pre_pre_error_roll) / (2 * dt);
    filtered_pre_pre_error_roll = filtered_pre_error_roll;
    filtered_pre_error_roll = filtered_error_roll;
    if (fabs(pid_output) > ACC_upper_limit)
    {
      pid_output = pid_output > 0 ? ACC_upper_limit : -ACC_upper_limit;
    }
    return pid_output;
}
#endif

float pid_pitch(float pv)
{
    error_pitch = setpoint_pitch - pv;
    pid_output = kp_pitch * error_pitch + kd_pitch * (error_pitch - pre_pre_error_pitch) / (2 * dt);
    pre_pre_error_pitch = pre_error_pitch;
    pre_error_pitch = error_pitch;
    if (fabs(pid_output) > ACC_upper_limit)
    {
      pid_output = pid_output > 0 ? ACC_upper_limit : -ACC_upper_limit;
    }
    return pid_output;
}

float pid_roll(float pv)
{
    error_roll = setpoint_roll - pv;
    pid_output = kp_roll * error_roll + kd_roll * (error_roll - pre_pre_error_roll) / (2 * dt);
    pre_pre_error_roll = pre_error_roll;
    pre_error_roll = error_roll;
    if (fabs(pid_output) > ACC_upper_limit)
    {
      pid_output = pid_output > 0 ? ACC_upper_limit : -ACC_upper_limit;
    }
    return pid_output;
}


ISR(TIMER1_COMPA_vect)
{
   for(int i = 0; i < 2; i++)
   {
     counter_m[i] ++;
   }
   //Serial.print(counter_m[0]);
   if(counter_m[0] >= period_m[0])
   {
     counter_m[0] = 0;
     if(dir_m[0] == 0)
     {
       CLR(PORTC, motor_dir_2);
       SET(PORTC, motor_dir_3);
     }
     else
     {
       SET(PORTC, motor_dir_2);
       CLR(PORTC, motor_dir_3);
     }
     SET(PORTC, motor_step_2);
     SET(PORTC, motor_step_3);
     delayMicroseconds(1);
     CLR(PORTC, motor_step_2);
     CLR(PORTC, motor_step_3);
   }
   if(counter_m[1] >= period_m[1])
   {
     counter_m[1] = 0;
     if(dir_m[1] == 0)
     {
       CLR(PORTC, motor_dir_0);
       SET(PORTC, motor_dir_1);
     }
     else
     {
       SET(PORTC, motor_dir_0);
       CLR(PORTC, motor_dir_1);
     }
     SET(PORTC, motor_step_0);
     SET(PORTC, motor_step_1);
     delayMicroseconds(1);
     CLR(PORTC, motor_step_0);
     CLR(PORTC, motor_step_1);
   }
}


void setMotorSpeed(int motor_idx, float tspeed)
{
  float absspeed;
  if (tspeed>0)
  {
     dir_m[motor_idx] = 0;
     absspeed = tspeed; 
  }
  else
  {
    dir_m[motor_idx] = 1;
    absspeed = -tspeed;
  }
  period_m[motor_idx] = (int) (1000 / absspeed);
}

void set_delay_pitch(float acc)
{
  velocity_pitch += acc * dt;
  if (fabs(velocity_pitch) > V_upper_limit)
  {
    velocity_pitch = velocity_pitch > 0 ? V_upper_limit : -V_upper_limit;
  }
  if(fabs(velocity_pitch) > 0.002)
  {
    if (velocity_pitch>0)
    {
      dir_m[0]=0;
    }
    else
    {
      dir_m[0]=1;
    }
    period_m[0]= int(fabs(1000 / velocity_pitch));
  }
  else
  {
    period_m[0] = INFINITE;
  }
}


void set_delay_roll(float acc)
{
  velocity_roll += acc * dt;
  if (fabs(velocity_roll) > V_upper_limit)
  {
    velocity_roll = velocity_roll > 0 ? V_upper_limit : -V_upper_limit;
  }
  if(fabs(velocity_roll) > 0.002)
  {
    if (velocity_roll>0)
    {
      dir_m[1]=1;
    }
    else
    {
      dir_m[1]=0;
    }
    period_m[1]= int(fabs(1000 / velocity_roll));
  }
  else
  {
    period_m[1] = INFINITE;
  }
}


void setup() {
  Serial.begin(115200);
  Wire.begin();
  TWBR = ((F_CPU / 400000L) - 16) / 2; // Set I2C frequency to 400kHz

  i2cData[0] = 3; // Set the sample rate to 1000Hz - 8kHz/(7+1) = 1000Hz
  i2cData[1] = 0x00; // Disable FSYNC and set 260 Hz Acc filtering, 256 Hz Gyro filtering, 8 KHz sampling
  i2cData[2] = 0x00; // Set Gyro Full Scale Range to ±250deg/s
  i2cData[3] = 0x00; // Set Accelerometer Full Scale Range to ±2g
  while (i2cWrite(0x19, i2cData, 4, false)); // Write to all four registers at once
  while (i2cWrite(0x6B, 0x01, true)); // PLL with X axis gyroscope reference and disable sleep mode

  while (i2cRead(0x75, i2cData, 1));
  if (i2cData[0] != 0x68) { // Read "WHO_AM_I" register
    Serial.print(F("Error reading sensor"));
    while (1);
  }

  delay(100); // Wait for sensor to stabilize

  /* Set kalman and gyro starting angle */
  while (i2cRead(0x3B, i2cData, 6));
  accX = (i2cData[0] << 8) | i2cData[1];
  accY = (i2cData[2] << 8) | i2cData[3];
  accZ = (i2cData[4] << 8) | i2cData[5];

#ifdef RESTRICT_PITCH // Eq. 25 and 26
  double roll  = atan2(accY, accZ) * RAD_TO_DEG;
  double pitch = atan(-accX / sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;
#else // Eq. 28 and 29
  double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
  double pitch = atan2(-accX, accZ) * RAD_TO_DEG;
#endif

  kalmanX.setAngle(roll); // Set starting angle
  kalmanY.setAngle(pitch);
  gyroXangle = roll;
  gyroYangle = pitch;
  compAngleX = roll;
  compAngleY = pitch;

  timer = micros();

#ifdef USE_ROS
  //initialize ros
  nh.initNode();
  nh.advertise(imu_pitch_pub);
  nh.advertise(imu_roll_pub);
  nh.advertise(acc_pitch_pub);
  nh.advertise(acc_roll_pub);
#endif

#ifdef USE_KALMAN_CC
kalman_cc_pitch = new KalmanFilter(kalman_cc_P, kalman_cc_Q, kalman_cc_R);
kalman_cc_roll = new KalmanFilter(kalman_cc_P, kalman_cc_Q, kalman_cc_R);
kalman_cc_pitch->init();
kalman_cc_roll->init();
#ifdef USE_ROS
 nh.advertise(kalman_cc_imu_pitch_pub);
 nh.advertise(kalman_cc_imu_roll_pub);
#endif
#endif
  
  //initialize motors
  pinMode(30, OUTPUT);
  pinMode(31, OUTPUT);
  pinMode(32, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(34, OUTPUT);
  pinMode(35, OUTPUT);
  pinMode(36, OUTPUT);
  pinMode(37, OUTPUT);
  
  //initialize timer
  noInterrupts(); // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  OCR1A = 10; // compare match register 16MHz/256/2Hz
  TCCR1B |= (1<<WGM12); // CTC mode
  TCCR1B |= (1<<CS12); // 256 prescaler
  TIMSK1 |= (1<<OCIE1A); // enable timer compare interrupt
  interrupts(); // enable all interrupts
//  setMotorSpeed(0, 100);
//  setMotorSpeed(1, 0.00001);

  for (int i = 0; i < array_size; i++)
  {
    pre_kalAngleX[i] = 0;
    pre_kalAngleY[i] = 0;
  }
}

void loop() {
  /* Update all the values */
  while (i2cRead(0x3B, i2cData, 14));
  accX = ((i2cData[0] << 8) | i2cData[1]);
  accY = ((i2cData[2] << 8) | i2cData[3]);
  accZ = ((i2cData[4] << 8) | i2cData[5]);
  tempRaw = (i2cData[6] << 8) | i2cData[7];
  gyroX = (i2cData[8] << 8) | i2cData[9];
  gyroY = (i2cData[10] << 8) | i2cData[11];
  gyroZ = (i2cData[12] << 8) | i2cData[13];

  dt = (double)(micros() - timer) / 1000000; // Calculate delta time
  timer = micros();

  double gyroXrate = gyroX / 131.0; // Convert to deg/s
  double gyroYrate = gyroY / 131.0; // Convert to deg/s

#ifdef RESTRICT_PITCH // Eq. 25 and 26
  double roll  = atan2(accY, accZ) * RAD_TO_DEG;
  double pitch = atan(-accX / sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;
#else // Eq. 28 and 29
  double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
  double pitch = atan2(-accX, accZ) * RAD_TO_DEG;
#endif



#ifdef RESTRICT_PITCH
  // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
  if ((roll < -90 && kalAngleX > 90) || (roll > 90 && kalAngleX < -90)) {
    kalmanX.setAngle(roll);
    compAngleX = roll;
    kalAngleX = roll;
    gyroXangle = roll;
  } else
    kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter

  if (abs(kalAngleX) > 90)
    gyroYrate = -gyroYrate; // Invert rate, so it fits the restriced accelerometer reading
  kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt);
#else
  // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
  if ((pitch < -90 && kalAngleY > 90) || (pitch > 90 && kalAngleY < -90)) {
    kalmanY.setAngle(pitch);
    compAngleY = pitch;
    kalAngleY = pitch;
    gyroYangle = pitch;
  } else
    kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt); // Calculate the angle using a Kalman filter

  if (abs(kalAngleY) > 90)
    gyroXrate = -gyroXrate; // Invert rate, so it fits the restriced accelerometer reading
  kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter
#endif

  gyroXangle += gyroXrate * dt; // Calculate gyro angle without any filter
  gyroYangle += gyroYrate * dt;
  //gyroXangle += kalmanX.getRate() * dt; // Calculate gyro angle using the unbiased rate
  //gyroYangle += kalmanY.getRate() * dt;

  compAngleX = 0.93 * (compAngleX + gyroXrate * dt) + 0.07 * roll; // Calculate the angle using a Complimentary filter
  compAngleY = 0.93 * (compAngleY + gyroYrate * dt) + 0.07 * pitch;

  // Reset the gyro angle when it has drifted too much
  if (gyroXangle < -180 || gyroXangle > 180)
    gyroXangle = kalAngleX;
  if (gyroYangle < -180 || gyroYangle > 180)
    gyroYangle = kalAngleY;

#ifdef USE_KALMAN_CC
  
  kalman_cc_pitch->update(kalAngleY);
  kalman_cc_pitch_value = kalman_cc_pitch->state();
  
  kalman_cc_roll->update(kalAngleX);
  kalman_cc_roll_value = kalman_cc_roll->state();
  
  acc_pitch = pid_pitch(kalAngleY, kalman_cc_pitch_value);
  acc_roll = pid_roll(-kalAngleX, -kalman_cc_roll_value);
  
  #ifdef USE_ROS
  kalman_cc_imu_pitch_msg.data = kalman_cc_pitch_value;
  kalman_cc_imu_pitch_pub.publish(&kalman_cc_imu_pitch_msg);
  
  kalman_cc_imu_roll_msg.data = kalman_cc_roll_value;
  kalman_cc_imu_roll_pub.publish(&kalman_cc_imu_roll_msg);
  
  #endif
#else
  acc_pitch = pid_pitch(kalAngleY);
  acc_roll = pid_roll(-kalAngleX);
#endif
  
  set_delay_pitch(acc_pitch);
  set_delay_roll(acc_roll);

#ifdef USE_ROS
  imu_pitch_msg.data = kalAngleY;
  imu_roll_msg.data = kalAngleX;
  imu_pitch_pub.publish(&imu_pitch_msg);
  imu_roll_pub.publish(&imu_roll_msg);
  
  acc_pitch_msg.data = acc_pitch/100;
  acc_roll_msg.data = acc_roll/100;
  acc_pitch_pub.publish(&acc_pitch_msg);
  acc_roll_pub.publish(&acc_roll_msg);
  
  nh.spinOnce();
  
#endif
}
