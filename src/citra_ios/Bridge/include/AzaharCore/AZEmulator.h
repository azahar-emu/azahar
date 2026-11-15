// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

@class AZConfiguration;
@protocol AZFrameGenerating;

NS_ASSUME_NONNULL_BEGIN

/// 3DS buttons. The values match Settings::NativeButton.
typedef NS_ENUM(NSInteger, AZButton) {
    AZButtonA = 0,
    AZButtonB = 1,
    AZButtonX = 2,
    AZButtonY = 3,
    AZButtonUp = 4,
    AZButtonDown = 5,
    AZButtonLeft = 6,
    AZButtonRight = 7,
    AZButtonL = 8,
    AZButtonR = 9,
    AZButtonStart = 10,
    AZButtonSelect = 11,
    AZButtonDebug = 12,
    AZButtonGPIO14 = 13,
    AZButtonZL = 14,
    AZButtonZR = 15,
    AZButtonHome = 16,
    AZButtonPower = 17,
};

/// 3DS analog sticks. The values match Settings::NativeAnalog.
typedef NS_ENUM(NSInteger, AZAnalog) {
    AZAnalogCirclePad = 0,
    AZAnalogCStick = 1,
};

typedef NS_ENUM(NSInteger, AZEmulationState) {
    AZEmulationStateStopped = 0,
    AZEmulationStateRunning = 1,
    AZEmulationStatePaused = 2,
};

/// Error domain of the errors reported by AZEmulator.
extern NSErrorDomain const AZEmulatorErrorDomain;

/// A request of the emulated software to type text (the 3DS software keyboard).
@interface AZKeyboardRequest : NSObject
@property(nonatomic, copy) NSString* hintText;
/// Why the previous input was rejected, shown above the field. Nil for a fresh request.
@property(nonatomic, copy, nullable) NSString* errorMessage;
/// Labels of the buttons from left to right, the last one is the one that confirms the input.
@property(nonatomic, copy) NSArray<NSString*>* buttonTexts;
/// Index of the button that confirms the input, the other ones (cancel, "I forgot") skip checks.
@property(nonatomic) NSInteger confirmButtonIndex;
@property(nonatomic) NSInteger maxLength;
@property(nonatomic) BOOL multiline;
@property(nonatomic) BOOL numeric;
/// Hands the text and the index of the pressed button to the emulated software. Returns nil when
/// the input was accepted (the request is over), otherwise why it was rejected.
@property(nonatomic, copy) NSString* _Nullable (^submit)(NSString* text, NSInteger button);
@end

/// A request of the emulated software to choose a Mii (the 3DS Mii selector).
@interface AZMiiRequest : NSObject
@property(nonatomic, copy) NSString* title;
/// Names of the Miis the player can choose from.
@property(nonatomic, copy) NSArray<NSString*>* names;
@property(nonatomic) NSInteger initialIndex;
@property(nonatomic) BOOL allowsCancel;
/// Hands the choice to the emulated software: the index of a Mii, or -1 to cancel.
@property(nonatomic, copy) void (^submit)(NSInteger index);
@end

/// A save state slot that holds a state of the running title.
@interface AZSaveStateSlot : NSObject
/// 0 is the quick slot, 1 to 10 the numbered ones.
@property(nonatomic, readonly) NSInteger slot;
@property(nonatomic, readonly) NSDate* date;
/// False when the state was made by another build and can't be loaded.
@property(nonatomic, readonly) BOOL isLoadable;
/// A small picture of the game when the state was saved (a JPEG file), nil for states saved
/// before pictures were kept.
@property(nonatomic, readonly, nullable) NSString* thumbnailPath;
@end

/// A save state file on disk, of any game (a title does not have to be running).
@interface AZSaveStateEntry : NSObject
@property(nonatomic, readonly) uint64_t programID;
/// 0 unless the state belongs to a movie recording or playback.
@property(nonatomic, readonly) uint64_t movieID;
@property(nonatomic, readonly) NSInteger slot;
@property(nonatomic, readonly) NSDate* date;
/// False when the state was made by another build and can't be loaded (or the file is damaged).
@property(nonatomic, readonly) BOOL isLoadable;
/// Size of the state and its picture on disk, in bytes.
@property(nonatomic, readonly) unsigned long long fileSize;
@property(nonatomic, readonly, nullable) NSString* thumbnailPath;
@end

/// A cheat of the running title.
@interface AZCheat : NSObject
@property(nonatomic, copy) NSString* name;
/// Gateway codes, one "XXXXXXXX XXXXXXXX" pair per line.
@property(nonatomic, copy) NSString* code;
@property(nonatomic, copy) NSString* comments;
@property(nonatomic) BOOL enabled;
@end

/// Result of loading an amiibo.
typedef NS_ENUM(NSInteger, AZAmiiboStatus) {
    AZAmiiboStatusLoaded = 0,
    AZAmiiboStatusNotRunning,
    /// The game is not asking for an amiibo right now.
    AZAmiiboStatusNotSearching,
    /// An amiibo is already on the reader.
    AZAmiiboStatusTagInUse,
    AZAmiiboStatusFailed,
};

/// Result of installing a CIA.
typedef NS_ENUM(NSInteger, AZInstallStatus) {
    AZInstallStatusSuccess = 0,
    AZInstallStatusFailedToOpen,
    AZInstallStatusNotFound,
    AZInstallStatusAborted,
    AZInstallStatusInvalid,
    AZInstallStatusEncrypted,
    AZInstallStatusUnknown,
};

/// The identity of the emulated console (what its settings app would show).
@interface AZConsoleProfile : NSObject
@property(nonatomic, copy) NSString* username;
@property(nonatomic) NSInteger birthMonth;
@property(nonatomic) NSInteger birthDay;
/// 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish, 6 Simplified Chinese,
/// 7 Korean, 8 Dutch, 9 Portuguese, 10 Russian, 11 Traditional Chinese.
@property(nonatomic) NSInteger language;
@property(nonatomic) NSInteger country;
/// 0 mono, 1 stereo, 2 surround.
@property(nonatomic) NSInteger soundMode;
@property(nonatomic) NSInteger playCoins;
@end

typedef NS_ENUM(NSInteger, AZMovieMode) {
    AZMovieModeNone = 0,
    AZMovieModeRecording,
    AZMovieModePlaying,
    AZMovieModeFinished,
};

/// What the header of a movie file says.
@interface AZMovieInfo : NSObject
@property(nonatomic, readonly) BOOL isValid;
@property(nonatomic, readonly) uint64_t programID;
@property(nonatomic, readonly, copy) NSString* author;
@property(nonatomic, readonly) uint32_t rerecordCount;
@property(nonatomic, readonly) uint64_t inputCount;
@end

typedef NS_ENUM(NSInteger, AZCompressStatus) {
    AZCompressStatusSuccess = 0,
    /// The format cannot be compressed (encrypted, bundled, not a 3DS file).
    AZCompressStatusNotSupported,
    AZCompressStatusAlreadyCompressed,
    AZCompressStatusFailed,
};

/// Owns the emulated 3DS. All methods must be called from the main thread unless stated
/// otherwise; emulation itself runs on a dedicated thread.
@interface AZEmulator : NSObject

@property(class, nonatomic, readonly) AZEmulator* shared;

/// True when the process can generate code at runtime (a debugger enabled JIT).
@property(class, nonatomic, readonly) BOOL isJITAvailable;

/// Whether the device has the Trusted Execution Monitor: JIT memory then has to be prepared by a
/// debugger page by page (see AZJITHelper).
+ (BOOL)deviceNeedsPreparedMemory;

/// True when MetalFX upscaling can be used on this device.
@property(class, nonatomic, readonly) BOOL isUpscalingSupported;

/// Version string of the emulator core.
@property(class, nonatomic, readonly) NSString* versionString;

/// Directory holding the emulator user data (sdmc, nand, config, cache...).
@property(nonatomic, readonly) NSString* userDirectory;

@property(nonatomic, readonly) AZEmulationState state;

/// Speed and frame statistics, safe to read from any thread.
@property(nonatomic, readonly) double emulationSpeed;
@property(nonatomic, readonly) double framesPerSecond;
/// Frames that reach the screen per second: more than framesPerSecond (the frames the game draws)
/// when frame generation adds frames in between.
@property(nonatomic, readonly) double displayFramesPerSecond;
/// Progress (0 to 1) of building the cached shaders while a game boots, negative otherwise.
@property(nonatomic, readonly) double shaderBuildProgress;
/// Average GPU time, time the emulation thread waited for the GPU, and draw calls per frame.
@property(nonatomic, readonly) double gpuMilliseconds;
@property(nonatomic, readonly) double stallMilliseconds;
@property(nonatomic, readonly) double drawsPerFrame;
/// Spread (standard deviation) of the time between frames reaching the display, in ms. Smooth
/// motion needs this to be small, at 60 fps about 0.5 ms or less.
@property(nonatomic, readonly) double paceJitterMilliseconds;

/// Called on the main thread when the emulated software asks for text.
@property(nonatomic, copy, nullable) void (^onKeyboardRequest)(AZKeyboardRequest* request);
/// Called on the main thread when the emulated software asks for a Mii. The emulation waits until
/// the request is submitted.
@property(nonatomic, copy, nullable) void (^onMiiRequest)(AZMiiRequest* request);
/// Called on the main thread with a message for the user that is not an error stopping emulation
/// (for example a failed save state).
@property(nonatomic, copy, nullable) void (^onNotice)(NSString* title, NSString* message);

/// Called on the main thread when emulation stops on its own (shutdown or error). The error is
/// nil on a regular shutdown.
@property(nonatomic, copy, nullable) void (^onStopped)(NSError* _Nullable error);

- (instancetype)init NS_UNAVAILABLE;

/// Prepares the emulator user directory and logging. Must be called once at startup.
- (void)setUpWithUserDirectory:(NSString*)directory NS_SWIFT_NAME(setUp(userDirectory:));

/// Gives the renderer the frame generator that makes the frames between the game's frames.
+ (void)setFrameGenerator:(nullable id<AZFrameGenerating>)generator
    NS_SWIFT_NAME(setFrameGenerator(_:));

/// Turns the emulator log (and the statistics collected for it) on or off. Off, only critical
/// errors are logged.
+ (void)setLoggingEnabled:(BOOL)enabled NS_SWIFT_NAME(setLoggingEnabled(_:));

/// Writes a line from the app into the emulator log (the log file users can share).
+ (void)logMessage:(NSString*)message NS_SWIFT_NAME(log(_:));

/// Fractions (see AZConfiguration.customLayoutLandscape) of the layout used when none was set,
/// for a screen of the given shape.
+ (NSArray<NSNumber*>*)defaultCustomLayoutForSize:(CGSize)size
                                         portrait:(BOOL)portrait
    NS_SWIFT_NAME(defaultCustomLayout(size:portrait:));

/// Replaces the custom layout fractions (8 values, or none for the default) of one orientation
/// without applying the rest of the configuration. Cheap enough to call on every drag update.
- (void)setCustomLayout:(NSArray<NSNumber*>*)fractions
               portrait:(BOOL)portrait NS_SWIFT_NAME(setCustomLayout(_:portrait:));

/// Applies a configuration. Settings that can't change while running take effect on next boot.
- (void)applyConfiguration:(AZConfiguration*)configuration NS_SWIFT_NAME(apply(configuration:));

/// Sets the Metal layer emulation is presented to and its drawable size in pixels.
- (void)setRenderLayer:(nullable CAMetalLayer*)layer NS_SWIFT_NAME(setRenderLayer(_:));
/// Recomputes the screen layout, after the layout settings changed while running.
- (void)refreshLayout;
/// The Metal layer of an external display and its size in pixels, or nil when it is gone. While
/// there is one (and the setting is on) the external display shows the top screen and this device
/// the bottom screen.
- (void)setSecondaryRenderLayer:(nullable CAMetalLayer*)layer
                           size:(CGSize)size NS_SWIFT_NAME(setSecondaryRenderLayer(_:size:));
- (void)setDrawableSize:(CGSize)size
               portrait:(BOOL)portrait NS_SWIFT_NAME(setDrawableSize(_:portrait:));

/// Starts emulating the title at the provided path.
- (BOOL)startWithPath:(NSString*)path error:(NSError**)error NS_SWIFT_NAME(start(path:));
- (void)pause;
- (void)resume;
/// Stops emulation and waits for the emulation thread to exit.
- (void)stop;

// Input, safe to call from any thread.
- (void)setButton:(AZButton)button pressed:(BOOL)pressed NS_SWIFT_NAME(setButton(_:pressed:));
/// Values are in the [-1, 1] range, +Y is up.
- (void)setAnalog:(AZAnalog)analog x:(float)x y:(float)y NS_SWIFT_NAME(setAnalog(_:x:y:));
/// Touch positions are in drawable pixels.
- (void)touchBeganAtPoint:(CGPoint)point NS_SWIFT_NAME(touchBegan(at:));
- (void)touchMovedToPoint:(CGPoint)point NS_SWIFT_NAME(touchMoved(to:));
- (void)touchEnded;
/// The bottom screen in drawable pixels (the area touch positions land on), or an empty
/// rectangle while there is none.
@property(nonatomic, readonly) CGRect bottomScreenFrame;
/// Accelerometer in g and gyroscope in degrees per second, in 3DS axes.
- (void)setMotionAccelerationX:(float)ax
                             y:(float)ay
                             z:(float)az
                         gyroX:(float)gx
                         gyroY:(float)gy
                         gyroZ:(float)gz
    NS_SWIFT_NAME(setMotion(accelX:accelY:accelZ:gyroX:gyroY:gyroZ:));

@end

/// Save states, cheats, the software keyboard and CIA installation.
@interface AZEmulator (Features)

/// Registers the software keyboard and Mii selector applets. Internal, called when a title boots.
- (void)registerApplets;
/// Releases an applet the emulation thread is waiting for so it can stop. Internal.
- (void)cancelPendingApplets;

// Save states of the running title.

/// Slots that hold a state, newest first. Empty when nothing is running.
- (NSArray<AZSaveStateSlot*>*)saveStateSlots;
/// Asks the emulation to save or load a slot. Returns false when that is not possible right now.
/// Called after a save or load was requested: while the game is paused it is still carried out,
/// with the game running (muted) just for as long as that takes.
- (void)beginStateOperation;
- (BOOL)saveStateToSlot:(NSInteger)slot;
- (BOOL)loadStateFromSlot:(NSInteger)slot;
/// Deletes the state (and its picture) in a slot of the running title.
- (BOOL)deleteSaveStateInSlot:(NSInteger)slot;

/// Every save state of every game, newest first. Does not need a running title.
+ (NSArray<AZSaveStateEntry*>*)allSaveStates;
/// Deletes one state and its picture. A state of the running game may be deleted too.
+ (BOOL)deleteSaveStateForProgram:(uint64_t)programID movie:(uint64_t)movieID slot:(NSInteger)slot;

// Cheats of the running title.

- (NSArray<AZCheat*>*)cheats;
- (void)setCheatEnabled:(BOOL)enabled atIndex:(NSInteger)index;
- (void)addCheat:(AZCheat*)cheat;
- (void)replaceCheatAtIndex:(NSInteger)index with:(AZCheat*)cheat;
- (void)removeCheatAtIndex:(NSInteger)index;
/// 1-based numbers of the lines of a code that are not a valid Gateway code line.
+ (NSArray<NSNumber*>*)invalidCheatLinesInCode:(NSString*)code;

/// Tells the cameras which way the app is held, so they deliver upright pictures. Call it when the
/// interface orientation changes.
+ (void)setCameraOrientationLandscapeLeft:(BOOL)landscapeLeft;

// Amiibo.

/// Puts the amiibo file on the virtual NFC reader. Changes the game makes to it are saved to the
/// file, so it should be a copy the app keeps.
- (AZAmiiboStatus)loadAmiiboAtPath:(NSString*)path;
- (void)removeAmiibo;

// Screenshot and speed.

/// Takes a screenshot at the internal resolution and saves it as a PNG in `directory`. The
/// completion block gets the path of the file, or nil when it failed, on the main thread.
- (void)takeScreenshotToDirectory:(NSString*)directory
                       completion:(void (^)(NSString* _Nullable path))completion;
/// Runs the game faster than real time (3x) while enabled.
- (void)setFastForward:(BOOL)enabled;

// Artic: streaming from a real console. Start a title with the path "articbase://<address>"; the
// Artic Setup Tool installs the console's system files with "articinio://<address>" (Old 3DS
// part) and "articinin://<address>" (New 3DS part).

/// Whether the system files of the Old 3DS / the New 3DS setup are installed.
+ (BOOL)areOld3DSSystemFilesInstalled;
+ (BOOL)areNew3DSSystemFilesInstalled;
/// Removes the installed system files of one setup before it is done again.
+ (void)uninstallSystemFilesForOld3DS:(BOOL)old3DS;

// The console's profile, only while no game runs (the system settings live in a save file).

+ (AZConsoleProfile*)readConsoleProfile;
/// Returns NO while a game runs.
+ (BOOL)writeConsoleProfile:(AZConsoleProfile*)profile NS_SWIFT_NAME(writeConsoleProfile(_:));

// Movies: recordings of the controller input of a run, played back later. A recording or a
// playback starts together with the game, so the game has to be (re)started after one is set.

/// The next game that starts records its input into the file.
- (void)recordMovieToPath:(NSString*)path author:(NSString*)author NS_SWIFT_NAME(recordMovie(toPath:author:));
/// The next game that starts plays the movie back.
- (void)playMovieFromPath:(NSString*)path NS_SWIFT_NAME(playMovie(fromPath:));
/// Forgets a recording or playback that was set and did not start yet.
- (void)clearPendingMovie;
@property(nonatomic, readonly) AZMovieMode movieMode;
/// Stops the recording (and saves it) or the playback. Call while the game is paused.
- (void)stopMovie;
/// Writes the recording so far to its file. Call while the game is paused.
- (void)saveMovie;
/// Called on the main thread when a playback reaches its end.
@property(nonatomic, copy, nullable) void (^onMovieFinished)(void);
+ (AZMovieInfo*)infoOfMovieAtPath:(NSString*)path NS_SWIFT_NAME(movieInfo(atPath:));

// Compression of games to the .zcci, .zcxi, .z3dsx and .zcia formats.

/// Compresses a game on a background queue into `directory`, named like the game with the
/// compressed extension. The result is written under a temporary name first, so an interrupted
/// compression never leaves a broken game behind. `progress` and `completion` are called on the
/// main thread, the completion gets the path of the new file when it worked.
+ (void)compressGameAtPath:(NSString*)path
               toDirectory:(NSString*)directory
                  progress:(void (^)(double fraction))progress
                completion:(void (^)(AZCompressStatus status, NSString* _Nullable outputPath))
                               completion;

// CIA installation, safe to call from any thread while no title is running.

/// Installs a CIA (also its compressed form) on a background queue. `progress` and `completion`
/// are called on the main thread.
+ (void)installCIAAtPath:(NSString*)path
                progress:(void (^)(double fraction))progress
              completion:(void (^)(AZInstallStatus status))completion;

@end

NS_ASSUME_NONNULL_END
