#include "cursor.h"
#include <cmath>
#include <algorithm>
#include "common/logging/log.h"
#include "common/logging/types.h"
#include "core\hle\service\hid\hid.h"


void Cursor::update(){
  if (emuWindow != nullptr){
    stylusInput = Service::HID::Module::getStylusInputs();
    modButtons = Service::HID::Module::getModButtons();
    setRotation();
    if (deviceInUse == 0){
      if (!Service::HID::Module::cstickEnabled){
        if (inMacro){
          runMacro();
        } else {
          // LOG_INFO(Core, "Stylus X: {:.2f}, Stylus Y: {:.2f}, Stylus Mod: {}, Stylus Touch: {}", stylusInput[0], stylusInput[1], stylusInput[2], stylusInput[3]);
          // Reset the cursor position if macro was just played
          if (justFinishedMacro > 0){
            justFinishedMacro = 0;
            rawCursorPos[0] = macroInitPos[0];
            rawCursorPos[1] = macroInitPos[1];
          }

          // Macros
          if (modButtons[0]){
            circle(0);
            return;
          } else if (modButtons[1]){
            rub();
            return;
          } else if (modButtons[2]){
            if (!macroBtnPressed){
              // Add macro
              return;
            }
          } else if (modButtons[3]){
            circle(1);
            return;
          } else {
            if (macroBtnPressed){
              macroBtnPressed = false;
            }
          }

          normStylusDirection[0] = stylusInput[0];
          normStylusDirection[1] = stylusInput[1];

          int maxSpeed = 50;
          float multiplier = 0.5f * pow(4.0f, maxSpeed / 100.0f); // 0 is 0.5x speed, 100 is 2.0x speed.
          float heightSpeed = (240.0f / 33.0f) * multiplier;
          bool stylusModPressed = stylusInput[2];
          float responsecurve = 175.0f / 100.0f;
          float speedupratio = 400.0f / 100.0f;
          float joystickScaled[2] = {0.0f};
          float radialLength = std::sqrt((normStylusDirection[0] * normStylusDirection[0]) + (normStylusDirection[1] * normStylusDirection[1]));
          float finalLength;
          float curvedLength;
          if (radialLength > 0) {
              // Get X and Y as a relation to the radial length
              float rComponents[2];
              rComponents[0] = normStylusDirection[0]/radialLength;
              rComponents[1] = normStylusDirection[1]/radialLength;
              // Apply response curve and output
              curvedLength = std::pow(radialLength, responsecurve);
              finalLength = stylusModPressed ? curvedLength * speedupratio : curvedLength;
              joystickScaled[0] = rComponents[0] * finalLength;
              joystickScaled[1] = rComponents[1] * finalLength;
          }
          // The code below sets the cursor position to the position of the joystick (absolute). Needs to be readjusted for standalone melonDS
          // _joystickCursorPosition = vec2((NDS_SCREEN_WIDTH/2.0f)+(std::min<float>(1.0,(normStylusDirection[0]/0.7071))*(NDS_SCREEN_WIDTH/2.0f)), (NDS_SCREEN_HEIGHT/2.0f)+(std::min<float>(1.0,(normStylusDirection[1]/0.7071))*(NDS_SCREEN_HEIGHT/2.0f)));

          float tempX = joystickScaled[0];
          float tempY = joystickScaled[1];

          switch (rotation)
          {
              case 1: // 90°
                  joystickScaled[0] =  tempY;
                  joystickScaled[1] = -tempX;
                  break;
              case 2: // 180°
                  joystickScaled[0] = -tempX;
                  joystickScaled[1] = -tempY;
                  break;
              case 3: // 270°
                  joystickScaled[0] = -tempY;
                  joystickScaled[1] =  tempX;
                  break;
              default:
                  break;
          }

          rawCursorPos[0] += joystickScaled[0]*heightSpeed;
          rawCursorPos[1] += joystickScaled[1]*heightSpeed;

          // Clamp to region and ready position information for touchscreen
          clamp();
          updateCursorPos();

          // Handle stylus touch button presses
          if (stylusInput[3]){
            touchScreen();
            wasTouching = true;
          } else if (wasTouching && !stylusInput[3]){
            release();
            wasTouching = false;
          }
        }
      }
    } else {
      //Update cursor based on mouse position
      clamp();
      updateCursorPos();

      // Handle stylus touch button presses
      if (stylusInput[3]){
        touchScreen();
        wasTouching = true;
      } else if (wasTouching && !stylusInput[3]){
        release();
        wasTouching = false;
      }

      setDeviceInUse(0);
    }
  }
}

void Cursor::setDeviceInUse(int device){
  deviceInUse = device;
}

void Cursor::setRawCursorPos(float x, float y){
  rawCursorPos[0] = x;
  rawCursorPos[1] = y;
}

void Cursor::clamp(){
  rawCursorPos[0] = std::clamp(rawCursorPos[0], 0.0f, 319.0f);
  rawCursorPos[1] = std::clamp(rawCursorPos[1], 0.0f, 239.0f);
}

void Cursor::touchScreen(){
  emuWindow->TouchDirectlyPressed(cursorPos[0], cursorPos[1]);
}

void Cursor::release(){
  emuWindow->TouchReleased();
}
void Cursor::setEmuWindow(Frontend::EmuWindow* emuWindow){
  this->emuWindow = emuWindow;
}

void Cursor::updateCursorPos(){
  cursorPos[0] = std::floor(rawCursorPos[0]);
  cursorPos[1] = std::floor(rawCursorPos[1]);
}

void Cursor::circle(int direction){
  macroBtnPressed = true;
  inMacro = true;
  wasTouching = true;
  macroType = 1;
  float radius = 240.0f/4.0f;
  if (justFinishedMacro != 1){ // Set the original position if just starting
    macroInitPos = {rawCursorPos[0],  rawCursorPos[1]};
  }


  std::vector<std::array<float, 2>> offsetArray;
  if (direction == 0){
    offsetArray.push_back({(0.0f*radius),      (-1.0f*radius)});
    offsetArray.push_back({(0.7071f*radius),   (-0.7071f*radius)});
    offsetArray.push_back({(1.0f*radius),      (0.0f*radius)});
    offsetArray.push_back({(0.7071f*radius),   (0.7071f*radius)});
    offsetArray.push_back({(0.0f*radius),      (1.0f*radius)});
    offsetArray.push_back({(-0.7071f*radius),  (0.7071f*radius)});
    offsetArray.push_back({(-1.0f*radius),     (0.0f*radius)});
    offsetArray.push_back({(-0.7071f*radius),  (-0.7071f*radius)});
  } else {
    offsetArray.push_back({(0.0f*radius),      (-1.0f*radius)});
    offsetArray.push_back({(-0.7071f*radius),  (-0.7071f*radius)});
    offsetArray.push_back({(-1.0f*radius),     (0.0f*radius)});
    offsetArray.push_back({(-0.7071f*radius),  (0.7071f*radius)});
    offsetArray.push_back({(0.0f*radius),      (1.0f*radius)});
    offsetArray.push_back({(0.7071f*radius),   (0.7071f*radius)});
    offsetArray.push_back({(1.0f*radius),      (0.0f*radius)});
    offsetArray.push_back({(0.7071f*radius),   (-0.7071f*radius)});
  }
  offsetArray = rotateVector(offsetArray);

  for (int i = 0; i < offsetArray.size(); i++){
      macroPositions.push_back({rawCursorPos[0]+offsetArray[i][0], rawCursorPos[1]+offsetArray[i][1]});
  }
  macroFrames = macroPositions.size();
  runMacro();
}

void Cursor::rub(){
  macroBtnPressed = true;
  inMacro = true;
  wasTouching = true;
  macroType = 2;
  float radius = 240.0f/6.0f;
  if (justFinishedMacro != 2){ // Set the original position if just starting
    macroInitPos = {rawCursorPos[0],  rawCursorPos[1]};
  }
  std::vector<std::array<float, 2>> offsetArray;
  offsetArray.push_back({(0.0f*radius),   0});
  offsetArray.push_back({(0.5f*radius),   0});
  offsetArray.push_back({(1.0f*radius),   0});
  offsetArray.push_back({(0.5f*radius),   0});
  offsetArray.push_back({(0.0f*radius),   0});
  offsetArray.push_back({(-0.5f*radius),  0});
  offsetArray.push_back({(-1.0f*radius),  0});
  offsetArray.push_back({(-0.5f*radius),  0});
  offsetArray = rotateVector(offsetArray);

  for (int i = 0; i < offsetArray.size(); i++){
      macroPositions.push_back({rawCursorPos[0]+offsetArray[i][0], rawCursorPos[1]+offsetArray[i][1]});
  }
  macroFrames = macroPositions.size();
  runMacro();
}


void Cursor::runMacro(){
    rawCursorPos[0] = macroPositions.front()[0];
    rawCursorPos[1] = macroPositions.front()[1];
    macroPositions.pop_front();
    clamp();
    updateCursorPos();
    touchScreen();
    macroFrames--;
    if (macroFrames == 0){
      macroPositions.clear();
      inMacro = false;
      justFinishedMacro = macroType;
    }
}

void Cursor::setRotation(){
  if (emuWindow->GetFramebufferLayout().is_portrait){
    rotation = 3;
  } else {
    rotation = 0;
  }
}

std::vector<std::array<float, 2>> Cursor::rotateVector(std::vector<std::array<float, 2>> input){
  for (auto& currArray : input){
      float tempX = currArray[0];
      float tempY = currArray[1];
      switch (rotation)
      {
          case 1: // 90°
              currArray[0] =  tempY;
              currArray[1] = -tempX;
              break;
          case 2: // 180°
              currArray[0] = -tempX;
              currArray[1] = -tempY;
              break;
          case 3: // 270°
              currArray[0] = -tempY;
              currArray[1] =  tempX;
              break;
          default:
              break;
    }
  }
  return input;
}
