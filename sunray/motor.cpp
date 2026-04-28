// Ardumower Sunray 
// Copyright (c) 2013-2020 by Alexander Grau, Grau GmbH
// Licensed GPLv3 for open source use
// or Grau GmbH Commercial License for commercial use (http://grauonline.de/cms2/?page_id=153)

#include "motor.h"
#include "config.h"
#include "helper.h"
#include "robot.h"
#include "Arduino.h"


void Motor::begin() {
	pwmMax = MOTOR_PID_LIMIT;
 
  #ifdef MAX_MOW_PWM
    if (MAX_MOW_PWM <= 255) {
      pwmMaxMow = MAX_MOW_PWM;
    }
    else pwmMaxMow = 255;
  #else 
    pwmMaxMow = 255;
  #endif
  
  pwmSpeedOffset = 1.0;
  
  mowHeightMillimeter = 50;

  //ticksPerRevolution = 1060/2;
  ticksPerRevolution = TICKS_PER_REVOLUTION;
	wheelBaseCm = WHEEL_BASE_CM;    // wheel-to-wheel distance (cm) 36
  wheelDiameter = WHEEL_DIAMETER; // wheel diameter (mm)
  ticksPerCm         = ((float)ticksPerRevolution) / (((float)wheelDiameter)/10.0) / 3.1415;    // computes encoder ticks per cm (do not change)  

  motorLeftPID.Kp       = MOTOR_PID_KP;  // 2.0;  
  motorLeftPID.Ki       = MOTOR_PID_KI;  // 0.03; 
  motorLeftPID.Kd       = MOTOR_PID_KD;  // 0.03;
  motorLeftPID.reset(); 
  motorRightPID.Kp       = motorLeftPID.Kp;
  motorRightPID.Ki       = motorLeftPID.Ki;
  motorRightPID.Kd       = motorLeftPID.Kd;
  motorRightPID.reset();		 

  motorLeftLpf.Tf = MOTOR_PID_LP;
  motorLeftLpf.reset();
  motorRightLpf.Tf = MOTOR_PID_LP;  
  motorRightLpf.reset();

  robotPitch = 0;
  #ifdef MOTOR_DRIVER_BRUSHLESS
    motorLeftSwapDir = true;
  #else
    motorLeftSwapDir = false;  
  #endif
  motorRightSwapDir = false;
  
  // apply optional custom motor direction swapping 
  #ifdef MOTOR_LEFT_SWAP_DIRECTION
    motorLeftSwapDir = !motorLeftSwapDir;
  #endif
  #ifdef MOTOR_RIGHT_SWAP_DIRECTION
    motorRightSwapDir = !motorRightSwapDir;
  #endif

  motorError = false;
  recoverMotorFault = false;
  recoverMotorFaultCounter = 0;
  nextRecoverMotorFaultTime = 0;
  enableMowMotor = ENABLE_MOW_MOTOR; //Default: true
  tractionMotorsEnabled = true;
  
  motorLeftOverload = false;
  motorRightOverload = false;
  motorMowOverload = false; 
  
  odometryError = false;  
  
  motorLeftStallStartTime = 0;
  motorRightStallStartTime = 0;

  motorLeftSense = 0;
  motorRightSense = 0;
  motorMowSense = 0;  
  motorLeftSenseLP = 0;
  motorRightSenseLP = 0;
  motorMowSenseLP = 0;  
  motorsSenseLP = 0;

  activateLinearSpeedRamp = USE_LINEAR_SPEED_RAMP;
  linearSpeedSet = 0;
  angularSpeedSet = 0;
  motorLeftRpmSet = 0;
  motorRightRpmSet = 0;
  motorMowPWMSet = 0;
  motorMowForwardSet = true;
  toggleMowDir = MOW_TOGGLE_DIR;

  lastControlTime = 0;
  nextSenseTime = 0;
  motorLeftTicks =0;  
  motorRightTicks =0;
  motorMowTicks = 0;
  motorLeftTicksZero=0;
  motorRightTicksZero=0;
  motorLeftPWMCurr =0;    
  motorRightPWMCurr=0; 
  motorMowPWMCurr = 0;
  motorLeftPWMCurrLP = 0;
  motorRightPWMCurrLP=0;   
  motorMowPWMCurrLP = 0;
  
  motorLeftRpmCurr=0;
  motorRightRpmCurr=0;
  motorMowRpmCurr=0;  
  motorLeftRpmLast = 0;
  motorRightRpmLast = 0;
  motorLeftRpmCurrLP = 0;
  motorRightRpmCurrLP = 0;
  motorMowRpmCurrLP = 0;
  
  setLinearAngularSpeedTimeoutActive = false;  
  setLinearAngularSpeedTimeout = 0;
  motorMowSpinUpTime = 0;

  motorRecoveryState = false;
}

void Motor::setMowMaxPwm( int val ){
  CONSOLE.print("Motor::setMowMaxPwm ");
  CONSOLE.println(val);
  pwmMaxMow = val;
}

void Motor::setMowHeightMillimeter( int val )
{
  CONSOLE.print("Motor::setMowHeightMillimeter ");
  CONSOLE.println(val);
  mowHeightMillimeter = val;
  motorDriver.setMowHeight(mowHeightMillimeter);
}

void Motor::speedPWM ( int pwmLeft, int pwmRight, int pwmMow )
{
  //Correct Motor Direction
  if (motorLeftSwapDir) pwmLeft *= -1;
  if (motorRightSwapDir) pwmRight *= -1;

  #ifdef MOTOR_MOW_SWAP_DIRECTION
    pwmMow *= -1;
  #endif

  // ensure pwm is lower than Max
  pwmLeft = min(pwmMax, max(-pwmMax, pwmLeft));
  pwmRight = min(pwmMax, max(-pwmMax, pwmRight));  
  pwmMow = min(pwmMaxMow, max(-pwmMaxMow, pwmMow)); 
  
  bool releaseBrakes = false;  
  if (releaseBrakesWhenZero){
    if ((pwmLeft == 0) && (pwmRight == 0)){
      if (millis() > motorReleaseBrakesTime) releaseBrakes = true;
    } else {
      motorReleaseBrakesTime = millis() + 2000;
    }
  }    
  pwmMowOut = pwmMow;
  motorDriver.setMotorPwm(pwmLeft, pwmRight, pwmMow, releaseBrakes);
}

// linear: m/s
// angular: rad/s
// -------unicycle model equations----------
//      L: wheel-to-wheel distance
//     VR: right speed (m/s)
//     VL: left speed  (m/s)
//  omega: rotation speed (rad/s)
//      V     = (VR + VL) / 2       =>  VR = V + omega * L/2
//      omega = (VR - VL) / L       =>  VL = V - omega * L/2
void Motor::setLinearAngularSpeed(float linear, float angular, bool useLinearRamp){
   setLinearAngularSpeedTimeout = millis() + 1000;
   setLinearAngularSpeedTimeoutActive = true;
   if ((activateLinearSpeedRamp) && (useLinearRamp)) {
     linearSpeedSet = 0.9 * linearSpeedSet + 0.1 * linear;
   } else {
     linearSpeedSet = linear;
   }
   angularSpeedSet = angular;   
   float rspeed = linearSpeedSet + angularSpeedSet * (wheelBaseCm /100.0 /2);          
   float lspeed = linearSpeedSet - angularSpeedSet * (wheelBaseCm /100.0 /2);          
   
   // Minimum wheel speed guarantee: prevent either wheel from sitting in
   // the dead zone (0 .. MIN_WHEEL_SPEED) where there's not enough torque
   // to actually turn a heavy chassis like Alfred.
   //
   // Dead-zone guard for Alfred's nose-heavy chassis (ADR-004):
   // Below MIN_WHEEL_SPEED the motors can't overcome static friction, so
   // the inner wheel stalls and only the outer wheel drives — a one-wheel
   // turn that digs into soft ground and triggers stall detection.
   //
   // Strategy:
   //   Forward/reverse tracking (linear != 0):
   //     Clamp the inner wheel to MIN_WHEEL_SPEED in the direction of
   //     travel.  The turn is slightly wider than requested but the path
   //     tracker corrects on the next cycle.  No counter-rotation — that
   //     was too aggressive and caused oscillation + stall cascades.
   //
   //   Rotation in place (linear ≈ 0):
   //     Counter-rotate both wheels at ±MIN_WHEEL_SPEED so the heavy
   //     nose actually pivots.
   //
   #ifdef MIN_WHEEL_SPEED
   // Docking (approach to station) is excluded so the contact alignment
   // stays precise.
   //
   // Undocking is INCLUDED again - see batman 2026-04-28 10:15.
   // PR #21 had excluded it on the assumption that "upstream (no clamp at
   // all) handles undock correctly", but on batman the right wheel was
   // commanded at -1.3 RPM (~ 0.014 m/s, deep in the MS4931 dead zone) for
   // the entire 4-minute reverse undock - left wheel ran free at 17 RPM,
   // chassis stalled 50 cm out of the dock.  The clamp must engage during
   // undock; the original PR #21 motivation (sign-flip jitter at the
   // NEAR_ZERO_EPS=0.015 boundary) is solved by the wider stateless
   // SIGN_BAND below.  Background on why the clamp exists at all and why
   // we cannot simply remove it for normal mowing: see note
   // 2026-04-19-rwheel-stall-min-wheel-speed-clamp.md.
   if (!maps.isDocking()) {
     float origL = lspeed;
     float origR = rspeed;
     bool adjusted = false;
     if (fabs(linearSpeedSet) > 0.01f) {
       // Forward/reverse tracking: clamp the inner wheel MAGNITUDE to
       // MIN_WHEEL_SPEED.  The sign must be STABLE - basing it on the raw
       // inner-wheel speed is unsafe because near-zero rspeed/lspeed
       // flicker around 0 with PID noise.
       //
       // Stateless wide-band rule.  Inside |v_inner| < SIGN_BAND, clamp
       // in the overall travel direction (linearSign is noise-free and
       // matches the driver intent: move the chassis fwd/rev while the
       // outer wheel steers).  Outside the band, follow geometric sign
       // so a legitimately negative inner speed during a sharp turn
       // (|rspeed| ~ 0.075 m/s) is preserved.
       //
       // SIGN_BAND = 0.040 m/s sits between PID noise (~+/-0.005) and
       // MIN_WHEEL_SPEED (0.050) with comfortable margin on both sides,
       // so neither boundary causes per-cycle sign flips.
       //
       // Why stateless: the previous latched-sign hysteresis (PR #25,
       // commit 218e0fc) used `static` per-wheel sign latches.  That
       // state survived across operation transitions (idle->undock,
       // undock->mow, mow->dock-retry).  A latched +1 from the last
       // forward maneuver could be reused at the start of the next
       // reverse maneuver, producing exactly the wrong-direction kick
       // we are trying to prevent.  The wider band makes hysteresis
       // unnecessary - at any single moment, the geometric sign of v
       // outside the band is stable (no flip every cycle), and inside
       // the band linearSign is the right answer.
       //
       // History of failed attempts (regressions kept biting because
       // each fix only addressed one operating regime):
       //   * PR #6  (98a24ab, reverted ba4e4f3) - clamp magnitude only;
       //     flipped legitimate negative inner speeds during MOW.
       //   * PR #10 (a782720, reverted in #12)  - sign-preserving clamp;
       //     same flip in the near-zero band.
       //   * PR #15 (2940988) - branch at NEAR_ZERO_EPS=0.015; PID jitter
       //     across that boundary flipped sign at 10 Hz during undock.
       //   * PR #21 (3323fcf) - disabled clamp during undock; today's
       //     stall (right wheel stuck at -1.3 RPM in dead zone).
       //   * PR #25 (218e0fc) - latched-sign hysteresis; correct mid-MOW
       //     but state goes stale across op transitions.
       const float SIGN_BAND = 0.040f;  // < MIN_WHEEL_SPEED (0.050), > PID noise (~0.005)
       const float linearSign = (linearSpeedSet < 0) ? -1.0f : 1.0f;
       auto pickSign = [&](float v) {
         return (fabs(v) < SIGN_BAND) ? linearSign : ((v < 0) ? -1.0f : 1.0f);
       };
       if (fabs(lspeed) < MIN_WHEEL_SPEED && fabs(rspeed) >= MIN_WHEEL_SPEED) {
         lspeed = pickSign(lspeed) * MIN_WHEEL_SPEED;
         adjusted = true;
       }
       if (fabs(rspeed) < MIN_WHEEL_SPEED && fabs(lspeed) >= MIN_WHEEL_SPEED) {
         rspeed = pickSign(rspeed) * MIN_WHEEL_SPEED;
         adjusted = true;
       }
     } else {
       // Rotation in place (linear ≈ 0): counter-rotate for heavy chassis
       if (lspeed >= 0 && lspeed < MIN_WHEEL_SPEED && rspeed >= 0 && rspeed < MIN_WHEEL_SPEED
           && fabs(angularSpeedSet) > 0.01) {
         if (angularSpeedSet > 0) { // turning left
           rspeed = MIN_WHEEL_SPEED;
           lspeed = -MIN_WHEEL_SPEED;
         } else { // turning right
           lspeed = MIN_WHEEL_SPEED;
           rspeed = -MIN_WHEEL_SPEED;
         }
         adjusted = true;
       }
     }
     if (adjusted) {
       static unsigned long lastLogTime = 0;
       if (millis() - lastLogTime > 2000) {
         lastLogTime = millis();
         CONSOLE.print("MIN_WHEEL_SPEED adj: lin=");
         CONSOLE.print(linearSpeedSet, 3);
         CONSOLE.print(" ang=");
         CONSOLE.print(angularSpeedSet, 3);
         CONSOLE.print(" L:");
         CONSOLE.print(origL, 3);
         CONSOLE.print("->");
         CONSOLE.print(lspeed, 3);
         CONSOLE.print(" R:");
         CONSOLE.print(origR, 3);
         CONSOLE.print("->");
         CONSOLE.println(rspeed, 3);
       }
     }
   }
   #endif

   // RPM = V / (2*PI*r) * 60
   motorRightRpmSet =  rspeed / (PI*(((float)wheelDiameter)/1000.0)) * 60.0;   
   motorLeftRpmSet = lspeed / (PI*(((float)wheelDiameter)/1000.0)) * 60.0;

   // ----- STEER logging (Phase 1 of steering-analysis-2026-04) -----
   //
   // One line every 100 ms while the wheels are commanded to move.  Captures
   // enough state to classify each steering event as dead-zone stall, load
   // stall, traction slip, or healthy turn (see §6.1 of the analysis doc):
   //
   //   v,  w     = commanded linear / angular speed (input to unicycle)
   //   rpmLset   = left-wheel set-point after any dead-zone adjustment
   //   rpmL      = actual left-wheel RPM (encoder, low-pass filtered)
   //   pwmL, iL  = actuator effort (PWM + motor current)
   //   lat       = Stanley cross-track error in metres
   //   wFus,wImu = fused and IMU-only yaw rate in rad/s (ground truth)
   //   wEnc      = yaw rate derived from wheel encoders
   //
   // The difference (wEnc - wImu) directly reveals traction slip: if the
   // wheels say we're rotating but the IMU disagrees, the contact patch
   // is sliding.  Disable via `#undef STEER_LOG` in config.h.
   #ifdef STEER_LOG
   static unsigned long lastSteerLogTime = 0;
   if ((fabs(motorLeftRpmSet) + fabs(motorRightRpmSet) > 0.5f)
       && (millis() - lastSteerLogTime > 100)) {
     lastSteerLogTime = millis();
     CONSOLE.print("STEER:");
     CONSOLE.print(" v=");       CONSOLE.print(linearSpeedSet, 3);
     CONSOLE.print(" w=");       CONSOLE.print(angularSpeedSet, 3);
     CONSOLE.print(" rpmLset="); CONSOLE.print(motorLeftRpmSet, 1);
     CONSOLE.print(" rpmRset="); CONSOLE.print(motorRightRpmSet, 1);
     CONSOLE.print(" rpmL=");    CONSOLE.print(motorLeftRpmCurrLP, 1);
     CONSOLE.print(" rpmR=");    CONSOLE.print(motorRightRpmCurrLP, 1);
     CONSOLE.print(" pwmL=");    CONSOLE.print(motorLeftPWMCurr);
     CONSOLE.print(" pwmR=");    CONSOLE.print(motorRightPWMCurr);
     CONSOLE.print(" iL=");      CONSOLE.print(motorLeftSenseLP, 2);
     CONSOLE.print(" iR=");      CONSOLE.print(motorRightSenseLP, 2);
     CONSOLE.print(" lat=");     CONSOLE.print(stateEstimator.lateralError, 3);
     CONSOLE.print(" wFus=");    CONSOLE.print(stateEstimator.stateDeltaSpeedLP, 3);
     CONSOLE.print(" wImu=");    CONSOLE.print(stateEstimator.stateDeltaSpeedIMU, 3);
     CONSOLE.print(" wEnc=");    CONSOLE.print(stateEstimator.stateDeltaSpeedWheels, 3);
     CONSOLE.println();
   }
   #endif
}


void Motor::enableTractionMotors(bool enable){
  if (enable == tractionMotorsEnabled) return;
  if (enable)
    CONSOLE.println("traction motors enabled");
  else 
    CONSOLE.println("traction motors disabled");
  tractionMotorsEnabled = enable;
}

void Motor::setReleaseBrakesWhenZero(bool release){
  if (release == releaseBrakesWhenZero) return;
  if (release){
    motorReleaseBrakesTime = millis() + 2000;
    CONSOLE.println("traction motors will release brakes when zero (may only work on owlPlatform)");
  } else { 
    CONSOLE.println("traction motors will not release brakes when zero");
  }
  releaseBrakesWhenZero = release;
}


void Motor::setMowState(bool switchOn){
  //CONSOLE.print("Motor::setMowState ");
  //CONSOLE.println(switchOn);
  if ((enableMowMotor) && (switchOn)){
    if (abs(motorMowPWMSet) > 0) return; // mowing motor already switch ON
    CONSOLE.println("Motor::setMowState ON");
    motorMowSpinUpTime = millis();
    if (toggleMowDir){
      // toggle mowing motor direction each mow motor start
      motorMowForwardSet = !motorMowForwardSet;
      if (motorMowForwardSet) motorMowPWMSet = 255;  
        else motorMowPWMSet = -255;  
    }  else  {      
      motorMowPWMSet = 255;  
    }
  } else {
    if (abs(motorMowPWMSet) < 0.01) return; // mowing motor already switch OFF    
    CONSOLE.println("Motor::setMowState OFF");
    motorMowPWMSet = 0;  
    motorMowPWMCurr = 0;
  }

   pwmSpeedOffset = 1.0; // reset Mow SpeedOffset
}


void Motor::stopImmediately(bool includeMowerMotor){
  //CONSOLE.println("Motor::stopImmediately");
  linearSpeedSet = 0;
  angularSpeedSet = 0;
  motorRightRpmSet = 0;
  motorLeftRpmSet = 0;      
  motorLeftPWMCurr = 0;
  motorRightPWMCurr = 0;   
  if (includeMowerMotor) {
    motorMowPWMSet = 0;
    motorMowPWMCurr = 0;    
  }
  speedPWM(motorLeftPWMCurr, motorRightPWMCurr, motorMowPWMCurr);
  // reset PID
  motorLeftPID.reset();
  motorRightPID.reset();
  motorLeftLpf.reset();
  motorRightLpf.reset();
  // reset unread encoder ticks
  int ticksLeft=0;
  int ticksRight=0;
  int ticksMow=0;
  motorDriver.getMotorEncoderTicks(ticksLeft, ticksRight, ticksMow);        
}


void Motor::run() {
  if (millis() < lastControlTime + 50) return;
  
  if (setLinearAngularSpeedTimeoutActive){
    if (millis() > setLinearAngularSpeedTimeout){
      //CONSOLE.println("Motor::run - LinearAngularSpeedTimeout");
      setLinearAngularSpeedTimeoutActive = false;
      motorLeftRpmSet = 0;
      motorRightRpmSet = 0;
    }
  }
    
  sense();        

  // if motor driver indicates a fault signal, try a recovery   
  // if motor driver uses too much current, try a recovery     
  // if there is some error (odometry, too low current, rpm fault), try a recovery 
  if (!recoverMotorFault) {
    bool someFault = ( (checkFault()) || (checkCurrentTooHighError()) || (checkMowRpmFault()) 
                         || (checkOdometryError()) || (checkCurrentTooLowError()) || (checkMotorStall()) );
    if (someFault){
      stopImmediately(true);
      recoverMotorFault = true;
      nextRecoverMotorFaultTime = millis() + 1000;                  
      motorRecoveryState = true;
    } 
  } 

  // try to recover from a motor driver fault signal by resetting the motor driver fault
  // if it fails, indicate a motor error to the robot control (so it can try an obstacle avoidance)  
  if (nextRecoverMotorFaultTime != 0){
    if (millis() > nextRecoverMotorFaultTime){
      if (recoverMotorFault){
        nextRecoverMotorFaultTime = millis() + 10000;
        recoverMotorFaultCounter++;                                               
        CONSOLE.print("motor fault recover counter ");
        CONSOLE.println(recoverMotorFaultCounter);
        motorDriver.resetMotorFaults();
        recoverMotorFault = false;  
        // issue #22: during dock, a stuck wheel should escalate within ~20s not 60s
        // so DockOp::onMotorError can attempt retry-dock before the battery wastes
        // more current on a stalled motor.
        int faultThreshold = (stateEstimator.stateOp == OP_DOCK) ? 3 : 10;
        if (recoverMotorFaultCounter >= faultThreshold){ // too many successive motor faults
          //stopImmediately();
          CONSOLE.println("ERROR: motor recovery failed");
          recoverMotorFaultCounter = 0;
          motorError = true;
        }
      } else {
        CONSOLE.println("resetting recoverMotorFaultCounter");
        recoverMotorFaultCounter = 0;
        nextRecoverMotorFaultTime = 0;
        motorRecoveryState = false;
      }        
    }
  }
  
  int ticksLeft;
  int ticksRight;
  int ticksMow;
  motorDriver.getMotorEncoderTicks(ticksLeft, ticksRight, ticksMow);  
  
  if (motorLeftPWMCurr < 0) ticksLeft *= -1;
  if (motorRightPWMCurr < 0) ticksRight *= -1;
  if (motorMowPWMCurr < 0) ticksMow *= -1;
  motorLeftTicks += ticksLeft;
  motorRightTicks += ticksRight;
  motorMowTicks += ticksMow;
  //CONSOLE.println(motorMowTicks);

  unsigned long currTime = millis();
  float deltaControlTimeSec =  ((float)(currTime - lastControlTime)) / 1000.0;
  lastControlTime = currTime;

  // calculate speed via tick count
  // 2000 ticksPerRevolution: @ 30 rpm  => 0.5 rps => 1000 ticksPerSec
  // 20 ticksPerRevolution: @ 30 rpm => 0.5 rps => 10 ticksPerSec
  motorLeftRpmCurr = 60.0 * ( ((float)ticksLeft) / ((float)ticksPerRevolution) ) / deltaControlTimeSec;
  motorRightRpmCurr = 60.0 * ( ((float)ticksRight) / ((float)ticksPerRevolution) ) / deltaControlTimeSec;
  motorMowRpmCurr = 60.0 * ( ((float)ticksMow) / ((float)6.0) ) / deltaControlTimeSec; // assuming 6 ticks per revolution
  float lp = 0.9; // 0.995
  motorLeftRpmCurrLP = lp * motorLeftRpmCurrLP + (1.0-lp) * motorLeftRpmCurr;
  motorRightRpmCurrLP = lp * motorRightRpmCurrLP + (1.0-lp) * motorRightRpmCurr;
  motorMowRpmCurrLP = lp * motorMowRpmCurrLP + (1.0-lp) * motorMowRpmCurr;
  
  if (ticksLeft == 0) {
    motorLeftTicksZero++;
    if (motorLeftTicksZero > 2) motorLeftRpmCurr = 0;
  } else motorLeftTicksZero = 0;

  if (ticksRight == 0) {
    motorRightTicksZero++;
    if (motorRightTicksZero > 2) motorRightRpmCurr = 0;
  } else motorRightTicksZero = 0;

  // --- one-wheel turn detection ---
  // Detect when one wheel is commanded to turn but the other is near zero,
  // or when one wheel is commanded but not actually moving (stalled).
  // Uses low-pass filtered RPM to avoid false triggers from encoder noise.
  // Also requires the commanded wheel's PWM to exceed STALL_DETECT_PWM_MIN
  // so we don't flag events during early ramp-up or coast phases where
  // PWM has not yet engaged the motor.
  // Rate-limited to once per second to avoid log spam.
  #ifdef MIN_WHEEL_SPEED
  {
    static unsigned long lastOneWheelLogTime = 0;
    if (millis() > lastOneWheelLogTime + 1000) {
      float absSetL = fabs(motorLeftRpmSet);
      float absSetR = fabs(motorRightRpmSet);
      float absCurL = fabs(motorLeftRpmCurrLP);
      float absCurR = fabs(motorRightRpmCurrLP);
      int absPwmL = abs(motorLeftPWMCurr);
      int absPwmR = abs(motorRightPWMCurr);
      #ifndef STALL_DETECT_PWM_MIN
        #define STALL_DETECT_PWM_MIN 40
      #endif
      float rpmThreshold = 3.0; // RPM below this = effectively stopped
      float setThreshold = 5.0; // only flag if commanded RPM is above this
      bool detected = false;
      // case 1: both wheels commanded, but one is stalled
      if (absSetL > setThreshold && absSetR > setThreshold) {
        if (absCurL < rpmThreshold && absCurR > setThreshold
            && absPwmL >= STALL_DETECT_PWM_MIN) {
          CONSOLE.print("ONE-WHEEL: L stalled  setL=");
          CONSOLE.print(motorLeftRpmSet, 1);
          CONSOLE.print(" curL=");
          CONSOLE.print(motorLeftRpmCurrLP, 1);
          CONSOLE.print(" setR=");
          CONSOLE.print(motorRightRpmSet, 1);
          CONSOLE.print(" curR=");
          CONSOLE.print(motorRightRpmCurrLP, 1);
          CONSOLE.print(" pwmL=");
          CONSOLE.println(motorLeftPWMCurr);
          detected = true;
        }
        if (absCurR < rpmThreshold && absCurL > setThreshold
            && absPwmR >= STALL_DETECT_PWM_MIN) {
          CONSOLE.print("ONE-WHEEL: R stalled  setL=");
          CONSOLE.print(motorLeftRpmSet, 1);
          CONSOLE.print(" curL=");
          CONSOLE.print(motorLeftRpmCurrLP, 1);
          CONSOLE.print(" setR=");
          CONSOLE.print(motorRightRpmSet, 1);
          CONSOLE.print(" curR=");
          CONSOLE.print(motorRightRpmCurrLP, 1);
          CONSOLE.print(" pwmR=");
          CONSOLE.println(motorRightPWMCurr);
          detected = true;
        }
      }
      // case 2: only one wheel commanded (set RPM near zero for one side)
      if (absSetL < rpmThreshold && absSetR > setThreshold
          && absPwmR >= STALL_DETECT_PWM_MIN) {
        CONSOLE.print("ONE-WHEEL: L cmd=0  setL=");
        CONSOLE.print(motorLeftRpmSet, 1);
        CONSOLE.print(" setR=");
        CONSOLE.print(motorRightRpmSet, 1);
        CONSOLE.print(" lin=");
        CONSOLE.print(linearSpeedSet, 3);
        CONSOLE.print(" ang=");
        CONSOLE.println(angularSpeedSet, 3);
        detected = true;
      }
      if (absSetR < rpmThreshold && absSetL > setThreshold
          && absPwmL >= STALL_DETECT_PWM_MIN) {
        CONSOLE.print("ONE-WHEEL: R cmd=0  setL=");
        CONSOLE.print(motorLeftRpmSet, 1);
        CONSOLE.print(" setR=");
        CONSOLE.print(motorRightRpmSet, 1);
        CONSOLE.print(" lin=");
        CONSOLE.print(linearSpeedSet, 3);
        CONSOLE.print(" ang=");
        CONSOLE.println(angularSpeedSet, 3);
        detected = true;
      }
      if (detected) lastOneWheelLogTime = millis();
    }
  }
  #endif

  // speed controller
  control();    
  motorLeftRpmLast = motorLeftRpmCurr;
  motorRightRpmLast = motorRightRpmCurr;
}  


// check if motor current too high
bool Motor::checkCurrentTooHighError(){
  bool motorLeftFault = (motorLeftSense > MOTOR_FAULT_CURRENT);
  bool motorRightFault = (motorRightSense > MOTOR_FAULT_CURRENT);
  bool motorMowFault = (motorMowSense > MOW_FAULT_CURRENT);
  if (motorLeftFault || motorRightFault || motorMowFault){
    CONSOLE.print("ERROR motor current too high: ");
    CONSOLE.print("  current=");
    CONSOLE.print(motorLeftSense);
    CONSOLE.print(",");
    CONSOLE.print(motorRightSense);
    CONSOLE.print(",");
    CONSOLE.println(motorMowSense);
    return true;
  } 
  return false; 
}


// check if motor current too low
bool Motor::checkCurrentTooLowError(){
  //CONSOLE.print(motorRightPWMCurr);
  //CONSOLE.print(",");
  //CONSOLE.println(motorRightSenseLP);
  if  (    ( (abs(motorMowPWMCurr) > 100) && (abs(motorMowPWMCurrLP) > 100) && (motorMowSenseLP < MOW_TOO_LOW_CURRENT)) 
        ||  ( (abs(motorLeftPWMCurr) > 100) && (abs(motorLeftPWMCurrLP) > 100) && (motorLeftSenseLP < MOTOR_TOO_LOW_CURRENT))    
        ||  ( (abs(motorRightPWMCurr) > 100) && (abs(motorRightPWMCurrLP) > 100) && (motorRightSenseLP < MOTOR_TOO_LOW_CURRENT))  ){        
    // at least one motor is not consuming current      
    // first try reovery, then indicate a motor error to the robot control (so it can try an obstacle avoidance)    
    CONSOLE.print("ERROR: motor current too low: pwm (left,right,mow)=");
    CONSOLE.print(motorLeftPWMCurr);
    CONSOLE.print(",");
    CONSOLE.print(motorRightPWMCurr);
    CONSOLE.print(",");
    CONSOLE.print(motorMowPWMCurr);
    CONSOLE.print("  average current amps (left,right,mow)=");
    CONSOLE.print(motorLeftSenseLP);
    CONSOLE.print(",");
    CONSOLE.print(motorRightSenseLP);
    CONSOLE.print(",");
    CONSOLE.println(motorMowSenseLP);
    return true;
  }
  return false;
}


// check motor driver (signal) faults
bool Motor::checkFault() {
  bool fault = false;
  bool leftFault = false;
  bool rightFault = false;
  bool mowFault = false;
  if (ENABLE_FAULT_DETECTION){    
    motorDriver.getMotorFaults(leftFault, rightFault, mowFault);
  }
  if (leftFault) {
    CONSOLE.println("Error: motor driver left signaled fault");
    fault = true;
  }
  if  (rightFault) {
    CONSOLE.println("Error: motor driver right signaled fault"); 
    fault = true;
  }
  if (mowFault) {
    CONSOLE.println("Error: motor driver mow signaled fault");
    fault = true;
  }
  return fault;
}


// check odometry errors
bool Motor::checkOdometryError() {
  if (ENABLE_ODOMETRY_ERROR_DETECTION){
    if  (   ( (abs(motorLeftPWMCurr) > 100) && (abs(motorLeftPWMCurrLP) > 100) && (abs(motorLeftRpmCurrLP) < 0.001))    
        ||  ( (abs(motorRightPWMCurr) > 100) && (abs(motorRightPWMCurrLP) > 100) && (abs(motorRightRpmCurrLP) < 0.001))  )
    {               
      // odometry error
      CONSOLE.print("ERROR: odometry error - rpm too low (left, right)=");
      CONSOLE.print(motorLeftRpmCurrLP);
      CONSOLE.print(",");
      CONSOLE.println(motorRightRpmCurrLP);     
      return true;        
    }
  }
  return false;
}


// Per-wheel stall detection (Alfred fork).
// Trips when a drive wheel is commanded to drive (|pwm| >= STALL_PWM_THRESHOLD)
// but barely rotates (|rpm| < STALL_RPM_THRESHOLD) while drawing current
// (|sense| >= STALL_CURRENT_THRESHOLD, rules out mere disconnection) for
// STALL_DURATION_MS continuously.  Complements checkOdometryError() which
// is too slow / too strict (PWM > 100, rpmLP < 0.001) for Alfred's weight
// distribution where a blocked wheel stalls at moderate PWM.
bool Motor::checkMotorStall() {
#ifdef ENABLE_MOTOR_STALL_DETECTION
  unsigned long now = millis();

  // left wheel
  bool leftCandidate = (abs(motorLeftPWMCurr) >= STALL_PWM_THRESHOLD)
                    && (fabs(motorLeftRpmCurr) < STALL_RPM_THRESHOLD)
                    && (fabs(motorLeftSense)   >= STALL_CURRENT_THRESHOLD);
  if (leftCandidate) {
    if (motorLeftStallStartTime == 0) motorLeftStallStartTime = now;
    if (now - motorLeftStallStartTime >= STALL_DURATION_MS) {
      CONSOLE.print("ERROR: motor stall detected: wheel=L pwm=");
      CONSOLE.print(motorLeftPWMCurr);
      CONSOLE.print(" rpm=");
      CONSOLE.print(motorLeftRpmCurr, 2);
      CONSOLE.print(" current=");
      CONSOLE.print(motorLeftSense, 2);
      CONSOLE.print("A duration=");
      CONSOLE.print((unsigned long)(now - motorLeftStallStartTime));
      CONSOLE.println("ms");
      motorLeftStallStartTime = 0;
      motorRightStallStartTime = 0;
      return true;
    }
  } else {
    motorLeftStallStartTime = 0;
  }

  // right wheel
  bool rightCandidate = (abs(motorRightPWMCurr) >= STALL_PWM_THRESHOLD)
                     && (fabs(motorRightRpmCurr) < STALL_RPM_THRESHOLD)
                     && (fabs(motorRightSense)   >= STALL_CURRENT_THRESHOLD);
  if (rightCandidate) {
    if (motorRightStallStartTime == 0) motorRightStallStartTime = now;
    if (now - motorRightStallStartTime >= STALL_DURATION_MS) {
      CONSOLE.print("ERROR: motor stall detected: wheel=R pwm=");
      CONSOLE.print(motorRightPWMCurr);
      CONSOLE.print(" rpm=");
      CONSOLE.print(motorRightRpmCurr, 2);
      CONSOLE.print(" current=");
      CONSOLE.print(motorRightSense, 2);
      CONSOLE.print("A duration=");
      CONSOLE.print((unsigned long)(now - motorRightStallStartTime));
      CONSOLE.println("ms");
      motorLeftStallStartTime = 0;
      motorRightStallStartTime = 0;
      return true;
    }
  } else {
    motorRightStallStartTime = 0;
  }
#endif
  return false;
}


// check motor overload
void Motor::checkOverload(){
  motorLeftOverload = (motorLeftSenseLP > MOTOR_OVERLOAD_CURRENT);
  motorRightOverload = (motorRightSenseLP > MOTOR_OVERLOAD_CURRENT);
  motorMowOverload = (motorMowSenseLP > MOW_OVERLOAD_CURRENT);
  if (motorLeftOverload || motorRightOverload || motorMowOverload){
    if (motorOverloadDuration == 0){
      CONSOLE.print("ERROR motor overload (average current too high) - duration=");
      CONSOLE.print(motorOverloadDuration);
      CONSOLE.print("  avg current amps (left,right,mow)=");
      CONSOLE.print(motorLeftSenseLP);
      CONSOLE.print(",");
      CONSOLE.print(motorRightSenseLP);
      CONSOLE.print(",");
      CONSOLE.println(motorMowSenseLP);
    }
    motorOverloadDuration += 20;     
  } else {
    motorOverloadDuration = 0;
  }
}


// check mow rpm fault
bool Motor::checkMowRpmFault(){
  //CONSOLE.print(motorMowPWMCurr);
  //CONSOLE.print(",");
  //CONSOLE.print(motorMowPWMCurrLP);  
  //CONSOLE.print(",");
  //CONSOLE.println(motorMowRpmCurrLP);
  if (ENABLE_RPM_FAULT_DETECTION){
    if  ( (abs(motorMowPWMCurr) > 100) && (abs(motorMowPWMCurrLP) > 100) && (abs(motorMowRpmCurrLP) < 10.0)) {        
      CONSOLE.print("ERROR: mow motor, average rpm too low: pwm=");
      CONSOLE.print(motorMowPWMCurr);
      CONSOLE.print("  pwmLP=");      
      CONSOLE.print(motorMowPWMCurrLP);      
      CONSOLE.print("  rpmLP=");
      CONSOLE.print(motorMowRpmCurrLP);
      CONSOLE.println("  (NOTE: choose ENABLE_RPM_FAULT_DETECTION=false in config.h, if your mowing motor has no rpm sensor!)");
      return true;
    }
  }  
  return false;
}

// measure motor currents
void Motor::sense(){
  if (millis() < nextSenseTime) return;
  nextSenseTime = millis() + 20;
  motorDriver.getMotorCurrent(motorLeftSense, motorRightSense, motorMowSense);
  float lp = 0.995; // 0.9
  motorRightSenseLP = lp * motorRightSenseLP + (1.0-lp) * motorRightSense;
  motorLeftSenseLP = lp * motorLeftSenseLP + (1.0-lp) * motorLeftSense;
  motorMowSenseLP = lp * motorMowSenseLP + (1.0-lp) * motorMowSense; 
  motorsSenseLP = motorRightSenseLP + motorLeftSenseLP + motorMowSenseLP;
  motorRightPWMCurrLP = lp * motorRightPWMCurrLP + (1.0-lp) * ((float)motorRightPWMCurr);
  motorLeftPWMCurrLP = lp * motorLeftPWMCurrLP + (1.0-lp) * ((float)motorLeftPWMCurr);
  lp = 0.99;
  motorMowPWMCurrLP = lp * motorMowPWMCurrLP + (1.0-lp) * ((float)motorMowPWMCurr); 
 
  // compute normalized current (normalized to 1g gravity)
  //float leftAcc = (motorLeftRpmCurr - motorLeftRpmLast) / deltaControlTimeSec;
  //float rightAcc = (motorRightRpmCurr - motorRightRpmLast) / deltaControlTimeSec;
  float cosPitch = cos(robotPitch); 
	float pitchfactor;
  float robotMass = 1.0;
	// left wheel friction
	if (  ((motorLeftPWMCurr >= 0) && (robotPitch <= 0)) || ((motorLeftPWMCurr < 0) && (robotPitch >= 0)) )
		pitchfactor = cosPitch; // decrease by angle
	else 
		pitchfactor = 2.0-cosPitch;  // increase by angle
	motorLeftSenseLPNorm = abs(motorLeftSenseLP) * robotMass * pitchfactor;  
	// right wheel friction
	if (  ((motorRightPWMCurr >= 0) && (robotPitch <= 0)) || ((motorRightPWMCurr < 0) && (robotPitch >= 0)) )
		pitchfactor = cosPitch;  // decrease by angle
	else 
		pitchfactor = 2.0-cosPitch; // increase by angle
  motorRightSenseLPNorm = abs(motorRightSenseLP) * robotMass * pitchfactor; 

  checkOverload();  
}


void Motor::control(){  
    
  //########################  Calculate PWM for left driving motor ############################

  motorLeftPID.TaMax = 0.1;
  motorLeftPID.x = motorLeftLpf(motorLeftRpmCurr);  
  motorLeftPID.w  = motorLeftRpmSet;
  motorLeftPID.y_min = -pwmMax;
  motorLeftPID.y_max = pwmMax;
  motorLeftPID.max_output = pwmMax;
  motorLeftPID.output_ramp = MOTOR_PID_RAMP;
  //CONSOLE.print(motorLeftPID.x);
  //CONSOLE.print(",");
  //CONSOLE.print(motorLeftPID.w);
  //CONSOLE.println();
  motorLeftPID.compute();
  motorLeftPWMCurr = motorLeftPWMCurr + motorLeftPID.y;
  if (motorLeftRpmSet >= 0) motorLeftPWMCurr = min( max(0, (int)motorLeftPWMCurr), pwmMax); // 0.. pwmMax
  if (motorLeftRpmSet < 0) motorLeftPWMCurr = max(-pwmMax, min(0, (int)motorLeftPWMCurr));  // -pwmMax..0

  //########################  Calculate PWM for right driving motor ############################
  
  motorRightPID.TaMax = 0.1;
  motorRightPID.x = motorRightLpf(motorRightRpmCurr);
  motorRightPID.w = motorRightRpmSet;
  motorRightPID.y_min = -pwmMax;
  motorRightPID.y_max = pwmMax;
  motorRightPID.max_output = pwmMax;
  motorRightPID.output_ramp = MOTOR_PID_RAMP;
  motorRightPID.compute();
  motorRightPWMCurr = motorRightPWMCurr + motorRightPID.y;
  if (motorRightRpmSet >= 0) motorRightPWMCurr = min( max(0, (int)motorRightPWMCurr), pwmMax);  // 0.. pwmMax
  if (motorRightRpmSet < 0) motorRightPWMCurr = max(-pwmMax, min(0, (int)motorRightPWMCurr));   // -pwmMax..0  

  if ((abs(motorLeftRpmSet) < 0.01) && (motorLeftPWMCurr < 30)) motorLeftPWMCurr = 0;
  if ((abs(motorRightRpmSet) < 0.01) && (motorRightPWMCurr < 30)) motorRightPWMCurr = 0;

  //########################  Print Motor Parameter to LOG ############################
  
//  CONSOLE.print("rpm set=");
//  CONSOLE.print(tempMotorLeftRpmSet);
//  CONSOLE.print(",");
//  CONSOLE.print(tempMotorRightRpmSet);
//  CONSOLE.print("   curr=");
//  CONSOLE.print(motorLeftRpmCurr);
//  CONSOLE.print(",");
//  CONSOLE.print(motorRightRpmCurr);
//  CONSOLE.print(",");
//  CONSOLE.print("   PwmOffset=");
//  CONSOLE.println(tempPwmSpeedOffset);

  //########################  Calculate PWM for mowing motor ############################
  
  motorMowPWMCurr = 0.99 * motorMowPWMCurr + 0.01 * motorMowPWMSet;

  //########################  set PWM for all motors ############################

  if (!tractionMotorsEnabled){
    //CONSOLE.println("!tractionMotorsEnabled");
    motorLeftPWMCurr = motorRightPWMCurr = 0;
  }

  speedPWM(motorLeftPWMCurr, motorRightPWMCurr, motorMowPWMCurr);
  
  /*if ((motorLeftPWMCurr != 0) || (motorRightPWMCurr != 0)){
    CONSOLE.print("PID curr=");
    CONSOLE.print(motorLeftRpmCurr);
    CONSOLE.print(",");  
    CONSOLE.print(motorRightRpmCurr);
    CONSOLE.print(" set=");    
    CONSOLE.print(motorLeftRpmSet);
    CONSOLE.print(",");  
    CONSOLE.print(motorRightRpmSet);
    CONSOLE.print(" PWM:");
    CONSOLE.print(motorLeftPWMCurr);
    CONSOLE.print(",");
    CONSOLE.print(motorRightPWMCurr);
    CONSOLE.print(",");
    CONSOLE.println(motorMowPWMCurr);  
  }*/
}


void Motor::dumpOdoTicks(int seconds){
  int ticksLeft=0;
  int ticksRight=0;
  int ticksMow=0;
  motorDriver.getMotorEncoderTicks(ticksLeft, ticksRight, ticksMow);  
  motorLeftTicks += ticksLeft;
  motorRightTicks += ticksRight;
  motorMowTicks += ticksMow;
  CONSOLE.print("t=");
  CONSOLE.print(seconds);
  CONSOLE.print("  ticks Left=");
  CONSOLE.print(motorLeftTicks);  
  CONSOLE.print("  Right=");
  CONSOLE.print(motorRightTicks);             
  CONSOLE.print("  current Left=");
  CONSOLE.print(motorLeftSense);
  CONSOLE.print("  Right=");
  CONSOLE.print(motorRightSense);
  CONSOLE.println();               
}


void Motor::test(){
  CONSOLE.println("motor test - 10 revolutions");
  motorLeftTicks = 0;  
  motorRightTicks = 0;  
  unsigned long nextInfoTime = 0;
  int seconds = 0;
  int pwmLeft = ODO_TEST_PWM_SPEED;
  int pwmRight = ODO_TEST_PWM_SPEED; 
  bool slowdown = true;
  unsigned long stopTicks = ticksPerRevolution * 10;
  unsigned long nextControlTime = 0;
  while (motorLeftTicks < stopTicks || motorRightTicks < stopTicks){
    if (millis() > nextControlTime){
      nextControlTime = millis() + 20;
      if ((slowdown) && ((motorLeftTicks + ticksPerRevolution  > stopTicks)||(motorRightTicks + ticksPerRevolution > stopTicks))){  //Letzte halbe drehung verlangsamen
        pwmLeft = pwmRight = 20;
        slowdown = false;
      }    
      if (millis() > nextInfoTime){      
        nextInfoTime = millis() + 1000;            
        dumpOdoTicks(seconds);
        seconds++;      
      }    
      if(motorLeftTicks >= stopTicks)
      {
        pwmLeft = 0;
      }  
      if(motorRightTicks >= stopTicks)
      {
        pwmRight = 0;      
      }
      
      speedPWM(pwmLeft, pwmRight, 0);
      sense();
      //delay(50);         
      watchdogReset();     
      robotDriver.run();
    }
  }  
  speedPWM(0, 0, 0);
  CONSOLE.println("motor test done - please ignore any IMU/GPS errors");
}


void Motor::plot(){
  CONSOLE.println("motor plot (left,right,mow) - NOTE: Start Arduino IDE Tools->Serial Plotter (CTRL+SHIFT+L)");
  delay(5000);
  CONSOLE.println("pwmLeft,pwmRight,pwmMow,ticksLeft,ticksRight,ticksMow");
  motorLeftTicks = 0;  
  motorRightTicks = 0;  
  motorMowTicks = 0;
  int pwmLeft = 0;
  int pwmRight = 0; 
  int pwmMow = 0;
  int cycles = 0;
  int acceleration = 1;
  bool forward = true;
  unsigned long nextPlotTime = 0;
  unsigned long stopTime = millis() + 1 * 60 * 1000;
  unsigned long nextControlTime = 0;

  while (millis() < stopTime){   // 60 seconds...
    if (millis() > nextControlTime){
      nextControlTime = millis() + 20; 

      int ticksLeft=0;
      int ticksRight=0;
      int ticksMow=0;
      motorDriver.getMotorEncoderTicks(ticksLeft, ticksRight, ticksMow);  
      motorLeftTicks += ticksLeft;
      motorRightTicks += ticksRight;
      motorMowTicks += ticksMow;

      if (millis() > nextPlotTime){ 
        nextPlotTime = millis() + 100;
        CONSOLE.print(300+pwmLeft);
        CONSOLE.print(",");  
        CONSOLE.print(300+pwmRight);
        CONSOLE.print(",");
        CONSOLE.print(pwmMow);
        CONSOLE.print(",");        
        CONSOLE.print(300+motorLeftTicks);    
        CONSOLE.print(",");
        CONSOLE.print(300+motorRightTicks);
        CONSOLE.print(",");
        CONSOLE.print(motorMowTicks);        
        CONSOLE.println();
        motorLeftTicks = 0;
        motorRightTicks = 0;
        motorMowTicks = 0;      
      }

      speedPWM(pwmLeft, pwmRight, pwmMow);
      if (pwmLeft >= 255){
        forward = false;
        cycles++; 
      }      
      if (pwmLeft <= -255){
        forward = true;
        cycles++;               
      } 
      if ((cycles == 2) && (pwmLeft >= 0)) {
        if (acceleration == 1) acceleration = 20;
          else acceleration = 1;
        cycles = 0;
      }         
      if (forward){
        pwmLeft += acceleration;
        pwmRight += acceleration;
        pwmMow += acceleration;
      } else {
        pwmLeft -= acceleration;
        pwmRight -= acceleration;
        pwmMow -= acceleration;
      }
      pwmLeft = min(255, max(-255, pwmLeft));
      pwmRight = min(255, max(-255, pwmRight));          
      pwmMow = min(255, max(-255, pwmMow));                
    }  
    //sense();
    //delay(10);
    watchdogReset();     
    robotDriver.run(); 
  }
  speedPWM(0, 0, 0);
  CONSOLE.println("motor plot done - please ignore any IMU/GPS errors");
}
