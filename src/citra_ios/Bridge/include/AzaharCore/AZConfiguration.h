// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Screen layouts, matching Settings::LayoutOption (landscape).
typedef NS_ENUM(NSInteger, AZLayoutOption) {
    AZLayoutOptionDefault = 0,
    AZLayoutOptionSingleScreen = 1,
    AZLayoutOptionLargeScreen = 2,
    AZLayoutOptionSideScreen = 3,
    AZLayoutOptionHybridScreen = 5,
    AZLayoutOptionCustom = 6,
};

/// Screen layouts, matching Settings::PortraitLayoutOption.
typedef NS_ENUM(NSInteger, AZPortraitLayoutOption) {
    AZPortraitLayoutOptionTopFullWidth = 0,
    AZPortraitLayoutOptionCustom = 1,
    AZPortraitLayoutOptionOriginal = 2,
};

/// Subset of the emulator settings exposed to the iOS frontend. The app persists these and hands
/// them to the core before starting emulation.
@interface AZConfiguration : NSObject

// CPU
@property(nonatomic) BOOL useCPUJIT;
@property(nonatomic) NSInteger cpuClockPercentage;

// Graphics
@property(nonatomic) BOOL useShaderJIT;
@property(nonatomic) BOOL useHardwareShaders;
@property(nonatomic) BOOL accurateMultiplication;
@property(nonatomic) NSInteger resolutionFactor;
@property(nonatomic) BOOL linearFiltering;
@property(nonatomic) NSInteger textureFilter;
/// 0: off, 1: MetalFX spatial upscaling.
@property(nonatomic) NSInteger upscaling;
/// How far MetalFX scales, in tenths of the size a screen is drawn at (15 is 1.5x). 0: straight
/// to the size on the display.
@property(nonatomic) NSInteger upscaleFactorTenths;
/// Keep the pixels of the screens crisp when they are enlarged.
@property(nonatomic) BOOL sharpPixels;
/// Sharpening of the screens, 0 (off) to 100 percent.
@property(nonatomic) NSInteger sharpness;
/// Sharpening method (0 unsharp mask, 1 contrast adaptive), the filter when a screen is enlarged
/// (0 bilinear, 1 bicubic, 2 Lanczos), LCD grid darkness (0 default, 1-100) and a color
/// adjustment (-100 to 100 each).
@property(nonatomic) NSInteger sharpenMethod;
@property(nonatomic) NSInteger scaleFilter;
@property(nonatomic) NSInteger gridStrength;
@property(nonatomic) NSInteger saturation;
@property(nonatomic) NSInteger contrast;
@property(nonatomic) NSInteger brightness;
@property(nonatomic) BOOL skipDuplicateFrames;
@property(nonatomic) BOOL asyncShaderCompilation;
/// Keep showing the previous frame while a draw waits for its shader (no flicker, a repeated frame).
@property(nonatomic) BOOL holdFramesForShaders;
/// Frame generation: 0 off, 1 for games below 40 FPS, 2 for every game.
@property(nonatomic) NSInteger frameGeneration;
/// Size the in-between frames are made at: 0 full, 1 half, 2 three eighths, 3 a quarter.
@property(nonatomic) NSInteger frameGenerationSize;
/// Make frames for the top screen only.
@property(nonatomic) BOOL frameGenerationTopOnly;
/// Frame generation pauses while its frames come too late (even at the smallest adaptive size)
/// and comes back when the scene gets lighter.
@property(nonatomic) BOOL frameGenerationPause;
/// One more frame kept waiting before frames are shown: steadier, one refresh more latency.
@property(nonatomic) BOOL frameGenerationBuffer;
/// Show the frames of frame generation from a display link at the display's refreshes (iOS 17).
@property(nonatomic) BOOL frameGenerationDisplayPaced;
/// Frame generation with MetalFX's frame interpolator instead of VideoToolbox's.
@property(nonatomic) BOOL frameGenerationMetalFX;
/// Highest refresh rate of the display (UIScreen.maximumFramesPerSecond).
@property(nonatomic) NSInteger displayRefreshRate;
/// Milliseconds a draw waits for its shader to build before it is skipped (0 never waits).
@property(nonatomic) NSInteger shaderWaitMs;
/// Fragment lighting in half precision with fast normalization (applies to the next game).
@property(nonatomic) BOOL fastLighting;
/// The lighting lookup tables are read with the GPU's texture filtering (one sample a lookup).
@property(nonatomic) BOOL fastLightingLookups;
/// Texture combiners in half precision (applies to the next game).
@property(nonatomic) BOOL fastColor;
/// The emulator log is written (off: critical errors only).
@property(nonatomic) BOOL loggingEnabled;
/// Fast versions of the math functions in fragment shaders (rsqrt, normalize, pow, ...), without
/// the assumptions about NaN and infinity that Fast Shader Math makes (iOS 18).
@property(nonatomic) BOOL fastMathFunctions;
/// The vertex color goes from the vertex to the fragment shader in half precision.
@property(nonatomic) BOOL halfInterpolants;
/// Metal does not track the hazards of the stream buffers (less CPU time per draw).
@property(nonatomic) BOOL untrackedBuffers;
/// Draw hardware vertex shader draws natively so shared vertices are shaded once.
@property(nonatomic) BOOL vertexReuse;
/// Fast math functions in the hardware vertex shaders (iOS 18).
@property(nonatomic) BOOL vertexFastFunctions;
/// Full fast math in the hardware vertex shaders. Turns the accurate multiplication off.
@property(nonatomic) BOOL vertexFastMath;
/// Keep the textures of dropped surfaces and reuse them (fewer texture allocations).
@property(nonatomic) BOOL texturePool;
/// Depth mapped in the vertex shader so the GPU's hidden surface removal works (next game).
@property(nonatomic) BOOL hiddenSurfaceRemoval;
/// Depth/stencil-only draws without a fragment function, the scissor test as the GPU's scissor
/// rectangle and no-op blending dropped. Off draws the way it was done before.
@property(nonatomic) BOOL drawOptimizations;
/// MSAA anti-aliasing of the game's 3D: 0 off, 1 2x, 2 4x, 3 8x (as far as the device supports).
@property(nonatomic) NSInteger antiAliasing;
/// Wait each frame for the GPU to finish the previous one (the emulation stays at most a frame
/// ahead). Off lets the emulation run further ahead of the GPU.
@property(nonatomic) BOOL waitForGPU;
/// GPU timestamps of every render pass for the log (diagnostic, costs GPU time).
@property(nonatomic) BOOL gpuPassTiming;
/// Writes the most used shaders of the game to Neroli/shader_dump (diagnostic).
@property(nonatomic) BOOL dumpShaders;
/// GPU memory compression of textures: 0 off, 1 lossless, 2 lossy (next game).
@property(nonatomic) NSInteger textureCompression;
/// Small depth reads by the game get the previous frame's values instead of waiting (next game).
@property(nonatomic) BOOL fastDepthReadback;
/// Holds each frame on screen for the time of a game frame so frames are evenly spaced.
@property(nonatomic) BOOL framePacing;
/// Frames go to the screen through a display link, and the emulation makes each one just after a
/// refresh of the display, so they stay in step with it (replaces the timeline of framePacing).
@property(nonatomic) BOOL displaySync;
/// With displaySync, every new frame of the game is shown for two of the 3DS's refreshes: a steady
/// 30 FPS for games that run at 30 but hand over their frames unevenly.
@property(nonatomic) BOOL frameCap30;
/// With frameCap30, the game itself also runs at 30: its 3D work never finishes within one
/// refresh, so games that adapt their frame rate do not make frames the cap would throw away.
@property(nonatomic) BOOL frameCapHoldsGame;
/// Milliseconds of delay the frame pacing adds so a slow frame still meets its slot (0 for none).
@property(nonatomic) NSInteger pacingBufferMs;
@property(nonatomic) BOOL fastShaderMath;
/// Settings::StereoRenderOption: 0 off, 1 side by side, 3 anaglyph, 4 interlaced, 5 reverse.
@property(nonatomic) NSInteger stereoMode;
/// Device cameras behind the 3DS cameras: 0 none, 1 back camera, 2 front camera.
@property(nonatomic) NSInteger outerCamera;
@property(nonatomic) NSInteger innerCamera;
/// Strength of the 3D effect in percent.
@property(nonatomic) NSInteger stereoDepth;
@property(nonatomic) BOOL swapEyes3D;
@property(nonatomic) NSInteger anisotropy;
@property(nonatomic) BOOL diskShaderCache;

// Layout
@property(nonatomic) AZLayoutOption layoutOption;
@property(nonatomic) AZPortraitLayoutOption portraitLayoutOption;
@property(nonatomic) BOOL swapScreens;
@property(nonatomic) BOOL uprightScreens;
/// Pixels between the screens of the layouts that have a gap.
@property(nonatomic) NSInteger screenGap;
/// How many times larger the large screen is than the small one (1 to 16).
@property(nonatomic) float largeScreenProportion;
/// Settings::SmallScreenPosition: 0 top right, 1 middle right, 2 bottom right, 3 top left,
/// 4 middle left, 5 bottom left, 6 above the large screen, 7 below it.
@property(nonatomic) NSInteger smallScreenPosition;
/// Rectangles of the custom layouts as fractions of the screen: top x, y, width, height, then the
/// same for the bottom screen. Empty to use the default.
@property(nonatomic, copy) NSArray<NSNumber*>* customLayoutLandscape;
@property(nonatomic, copy) NSArray<NSNumber*>* customLayoutPortrait;
/// Opacity of the second screen of the custom landscape layout, in percent.
@property(nonatomic) NSInteger customSecondLayerOpacity;

/// Show the top screen on an external display (AirPlay, HDMI) and the touch screen on the device.
@property(nonatomic) BOOL useExternalDisplay;
/// What the external display shows: 0 the top screen with this device keeping the bottom screen,
/// otherwise a Settings::SecondaryDisplayLayout (1 top, 2 bottom, 3 side by side, 4 the screen
/// this device does not show, 5 original, 6 hybrid, 7 large screen) while this device keeps its
/// own layout.
@property(nonatomic) NSInteger secondaryDisplayLayout;
/// Do not draw the right eye while 3D is rendered (Settings::disable_right_eye_render).
@property(nonatomic) BOOL disableRightEyeRender;
/// Scale the screens by whole multiples only (the default aspect ratio).
@property(nonatomic) BOOL integerScaling;
/// Load custom textures in the background (the game does not wait for them).
@property(nonatomic) BOOL asyncCustomTextures;
/// Run file and install operations in a fixed order and time, which netplay and movies need.
@property(nonatomic) BOOL deterministicAsync;
/// The DSP reports plugged in headphones to the game.
@property(nonatomic) BOOL simulateHeadphones;
/// Games see the GPU finish its work after the time the real one would take, so games that adapt
/// their frame rate to the GPU (30 FPS in heavy scenes) behave as on a console.
@property(nonatomic) BOOL simulateGPUTimings;
/// How slow the simulated GPU is, in percent of the estimate (which is twice as fast as a console:
/// 200 is about a console). Only used with simulateGPUTimings.
@property(nonatomic) NSInteger gpuTimingPercent;
/// The least time a command list takes to finish, in milliseconds, 0 for none (simulateGPUTimings).
@property(nonatomic) NSInteger gpuTimingMinMs;

// Textures
@property(nonatomic) BOOL customTextures;
@property(nonatomic) BOOL preloadTextures;
@property(nonatomic) BOOL dumpTextures;

// Audio
@property(nonatomic) BOOL audioStretching;
/// Plays the audio at full speed while the emulation runs slower (can desync the audio).
@property(nonatomic) BOOL realtimeAudio;
/// Feeds the device microphone to the emulated console's microphone.
@property(nonatomic) BOOL useMicrophone;
@property(nonatomic) float volume;

// Game hacks
/// Use the built-in list of games that need a workaround (on by default).
@property(nonatomic) BOOL gameHacksEnabled;
/// What the game does about each hack, by key (rightEye, accurateMul, shaderFixup,
/// delayTextureCopy, regionFromSecure, spoofFriendCode, onlineLLE): 1 off, 2 on. A hack that is
/// not in the dictionary follows the built-in list.
@property(nonatomic, copy) NSDictionary<NSString*, NSNumber*>* gameHackOverrides;

// Audio and system modules
/// 0 HLE (built in), 1 LLE (the console's real DSP), 2 LLE on its own thread.
@property(nonatomic) NSInteger audioEmulation;
/// Run the console's real system applets instead of the built-in ones.
@property(nonatomic) BOOL lleApplets;
/// 0 as the game wants, 1 nearest neighbor, 2 linear.
@property(nonatomic) NSInteger textureSampling;
/// The eye shown when 3D is off: 0 left, 1 right.
@property(nonatomic) NSInteger monoEye;

// Post-processing
/// 0 none, 1 sharpen, 2 LCD grid, 3 scanlines.
@property(nonatomic) NSInteger postEffect;
/// 0 Dubois, 1 gray, 2 half color, 3 color.
@property(nonatomic) NSInteger anaglyphMode;
/// The name of a Metal shader file in Neroli/shaders/post (without .metal), or empty.
@property(nonatomic, copy) NSString* postShader;

// Storage
/// Keep the console's SD card on the device (on by default).
@property(nonatomic) BOOL useVirtualSD;
/// Store installed CIAs compressed.
@property(nonatomic) BOOL compressCIAInstalls;
/// A folder for the nand and the virtual SD card (as the subfolders nand and sdmc) instead of the
/// Azahar folder, or nil.
@property(nonatomic, copy, nullable) NSString* customStoragePath;

// Extras
@property(nonatomic) BOOL fixedClock;
@property(nonatomic) NSInteger fixedTime;
@property(nonatomic) BOOL fixedBattery;
@property(nonatomic) NSInteger batteryLevel;
@property(nonatomic) BOOL batteryCharging;
@property(nonatomic) NSInteger stepsPerHour;
@property(nonatomic) BOOL regionFreePatch;
@property(nonatomic) BOOL pluginLoader;
@property(nonatomic) BOOL onlineModules;
@property(nonatomic) NSInteger aspectRatio;
@property(nonatomic) BOOL topStretch;
@property(nonatomic) BOOL bottomStretch;
@property(nonatomic) NSInteger topPaddingH;
@property(nonatomic) NSInteger topPaddingV;
@property(nonatomic) NSInteger bottomPaddingH;
@property(nonatomic) NSInteger bottomPaddingV;
@property(nonatomic) NSInteger cardboardSize;
@property(nonatomic) NSInteger cardboardXShift;
@property(nonatomic) NSInteger cardboardYShift;
@property(nonatomic) NSInteger fastForwardSpeed;
@property(nonatomic) BOOL fastForwardHold;

// System
@property(nonatomic) BOOL isNew3DS;
/// -1 = auto select, 0 = Japan, 1 = USA, 2 = Europe, 3 = Australia, 4 = China, 5 = Korea,
/// 6 = Taiwan
@property(nonatomic) NSInteger region;
@property(nonatomic) BOOL useFrameLimit;
@property(nonatomic) NSInteger frameLimit;
/// At a speed limit of 100% the emulation runs at the display's 60 Hz instead of the console's
/// 59.83 Hz (0.3% faster), so frames and refreshes stay in step. Otherwise a refresh is repeated
/// every few seconds, which shows as a hitch.
@property(nonatomic) BOOL syncSpeedToDisplay;

/// Default configuration.
+ (instancetype)defaultConfiguration NS_SWIFT_NAME(makeDefault());

@end

NS_ASSUME_NONNULL_END
