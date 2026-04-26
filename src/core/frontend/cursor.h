#ifndef CURSOR_H
#define CURSOR_H
#include <queue>
#include "emu_window.h"
#include <array>
#include <vector>
namespace Frontend {
  class EmuWindow;
}

class Cursor
{
public:
  void update();
  void setRawCursorPos(float x, float y);
  void setDeviceInUse(int device);
  void setEmuWindow(Frontend::EmuWindow* emuWindow);
  void setRotation(int rot);
  void setLayout(int layout);
  int cursorPos[2];
  float normStylusDirection[2];
  bool cursorEnabled = true;
private:
  Frontend::EmuWindow* emuWindow = nullptr;
  float rawCursorPos[2] = {159, 119};
  void touchScreen();
  void release();
  void clamp();
  void updateCursorPos();
  bool wasTouching;
  std::array<float, 4> stylusInput;
  std::array<float, 11> modButtons;
  void circle(int direction); //0 is clockwise, 1 is counter clockwise
  void rub();
  void runMacro();

  std::vector<std::array<float, 2>> rotateVector(std::vector<std::array<float, 2>> input);
  bool inMacro;
  std::deque<std::array<float, 2>> macroPositions;
  int macroFrames;
  bool macroBtnPressed;
  int macroType;
  int justFinishedMacro;
  std::array<float, 2> macroInitPos;
  int rotation;
  int layout;
  int deviceInUse; //0 is Gamepad, 1 is Mouse/Tablet
};

#endif // CURSOR_H