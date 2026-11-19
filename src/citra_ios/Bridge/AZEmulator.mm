// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <AzaharCore/AZConfiguration.h>
#import <AzaharCore/AZEmulator.h>
#import <AzaharCore/AZFrameGenerator.h>
#import <AzaharCore/AZMultiplayer.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <pthread.h>
#include <pthread/qos.h>
#include <unistd.h>

#include "audio_core/input_details.h"
#include "citra_ios/Bridge/camera_ios.h"
#include "common/hacks/hack_manager.h"
#include "citra_ios/Bridge/movie_state.h"
#include "core/movie.h"
#include "citra_ios/Bridge/mic_ios.h"
#include "citra_ios/Bridge/emu_window_ios.h"
#include "citra_ios/Bridge/input_ios.h"
#include "common/apple_jit.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/frame_cost.h"
#if defined(__aarch64__)
namespace Dynarmic {
/// Defined by externals/patches/dynarmic-ios-jit-timing.patch.
extern void (*g_block_compile_hook)(std::uint64_t nanoseconds);
extern void (*g_cache_clear_hook)(int full, std::uint64_t used, std::uint64_t size);
} // namespace Dynarmic
#endif
#include "core/core.h"
#include "core/core_timing.h"
#include "core/frontend/applets/default_applets.h"
#include "core/hle/service/service.h"
#include "core/loader/loader.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_metal/renderer_metal.h"

NSErrorDomain const AZEmulatorErrorDomain = @"org.azahar-emu.AzaharCore";

namespace {

NSString* DescribeResult(Core::System::ResultStatus result, const std::string& details) {
    using ResultStatus = Core::System::ResultStatus;
    switch (result) {
    case ResultStatus::ErrorGetLoader:
    case ResultStatus::ErrorLoader:
        return @"The title could not be loaded. Make sure it is a valid, decrypted 3DS title.";
    case ResultStatus::ErrorLoader_ErrorEncrypted:
        return @"The title is encrypted. Please dump your titles from your own console "
               @"in a decrypted format.";
    case ResultStatus::ErrorLoader_ErrorInvalidFormat:
        return @"The title format is not supported.";
    case ResultStatus::ErrorLoader_ErrorGbaTitle:
        return @"GBA Virtual Console titles are not supported.";
    case ResultStatus::ErrorSystemMode:
        return @"Could not determine the system mode of the title.";
    case ResultStatus::ErrorSystemFiles:
        return @"Required system files are missing. Please set up the system files from "
               @"your own console.";
    case ResultStatus::ErrorN3DSApplication:
        return @"This title requires New 3DS mode, enable it in the settings.";
    case ResultStatus::ErrorCoreExceptionRaised:
        return @"The emulated CPU raised an exception.";
    case ResultStatus::ErrorShaderCacheCrash:
        return @"The emulator crashed while building the shader cache last time.";
    default:
        break;
    }
    if (!details.empty()) {
        return [NSString stringWithUTF8String:details.c_str()];
    }
    return [NSString
        stringWithFormat:@"Emulation failed with error %u.", static_cast<unsigned>(result)];
}

/// Paths that make the core stream a title from a real console running the Artic server.
bool IsArticPath(const std::string& path) {
    return path.starts_with("articbase://") || path.starts_with("articinio://") ||
           path.starts_with("articinin://");
}

NSString* FindEntryWithLength(NSString* directory, NSUInteger length) {
    NSArray<NSString*>* contents =
        [[NSFileManager defaultManager] contentsOfDirectoryAtPath:directory error:nil];
    for (NSString* name in contents) {
        if (name.length == length) {
            return [directory stringByAppendingPathComponent:name];
        }
    }
    return nil;
}

/// Looks for the TXM firmware image in the preboot volume.
bool HasTXMFirmware() {
    NSString* const firmware = @"usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4";
    NSString* boot = FindEntryWithLength(@"/System/Volumes/Preboot", 36);
    NSString* image =
        boot ? FindEntryWithLength([boot stringByAppendingPathComponent:@"boot"], 96) : nil;
    if (!image) {
        image = FindEntryWithLength(@"/private/preboot", 96);
    }
    if (!image) {
        return false;
    }
    return access([image stringByAppendingPathComponent:firmware].fileSystemRepresentation, F_OK) ==
           0;
}

/// Parses the chip family and generation from the GPU name, for example "Apple A18 Pro GPU".
bool GetChip(unichar* series, NSInteger* number) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        return false;
    }
    NSString* name = device.name.uppercaseString;
    NSRegularExpression* regex =
        [NSRegularExpression regularExpressionWithPattern:@"APPLE\\s+([MA])(\\d+)"
                                                  options:0
                                                    error:nil];
    NSTextCheckingResult* match = [regex firstMatchInString:name
                                                    options:0
                                                      range:NSMakeRange(0, name.length)];
    if (!match) {
        return false;
    }
    *series = [name characterAtIndex:[match rangeAtIndex:1].location];
    *number = [[name substringWithRange:[match rangeAtIndex:2]] integerValue];
    return true;
}

/**
 * Returns true if the device has the Trusted Execution Monitor. With TXM, executable memory has to
 * be mapped by the debugger (StikDebug universal JIT script), mprotect alone does not work.
 * Same detection as MeloCafe.
 */
bool DeviceHasTXM() {
    if (NSProcessInfo.processInfo.isiOSAppOnMac) {
        return false;
    }
    unichar series = 0;
    NSInteger number = 0;
    const bool has_chip = GetChip(&series, &number);
    if (@available(iOS 27, *)) {
        // Everything newer than the A12 has TXM on iOS 27.
        return !(has_chip && series == 'A') || number > 12;
    }
    const bool firmware = HasTXMFirmware();
    if (@available(iOS 26.6, *)) {
        if (!firmware) {
            return has_chip && (series == 'M' ? number >= 2 : number >= 15);
        }
    }
    return firmware;
}

/// Short version of an exception report for an alert: the thread, process and exception type at
/// the top plus the line with SP, LR and PC. The stack dump and all other registers are left out.
NSString* SummarizeException(const std::string& report) {
    NSString* text = [NSString stringWithUTF8String:report.c_str()];
    if (text == nil) {
        return @"";
    }
    const NSRange registers = [text rangeOfString:@"Registers:"];
    NSString* head =
        registers.location == NSNotFound ? text : [text substringToIndex:registers.location];
    head = [head stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    for (NSString* line in [text componentsSeparatedByString:@"\n"]) {
        if ([line containsString:@"PC    0x"]) {
            NSString* trimmed =
                [line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            return [NSString stringWithFormat:@"%@\n\n%@", head, trimmed];
        }
    }
    return head;
}

/// Error for a failed emulation run. For an exception in the emulated program the complete report
/// is attached (AZEmulatorErrorReport) and saved next to the log, because the log stops growing
/// once it is full and would no longer contain it.
NSError* MakeResultError(Core::System::ResultStatus result, const std::string& details) {
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    NSString* description = DescribeResult(result, details);
    if (result == Core::System::ResultStatus::ErrorCoreExceptionRaised && !details.empty()) {
        description =
            [NSString stringWithFormat:@"%@\n\n%@", description, SummarizeException(details)];
        if (NSString* report = [NSString stringWithUTF8String:details.c_str()]) {
            info[@"AZEmulatorErrorReport"] = report;
        }
        FileUtil::WriteStringToFile(
            true, FileUtil::GetUserPath(FileUtil::UserPath::LogDir) + "crash_report.txt", details);
    }
    info[NSLocalizedDescriptionKey] = description;
    return [NSError errorWithDomain:AZEmulatorErrorDomain
                               code:static_cast<NSInteger>(result)
                           userInfo:info];
}

NSError* MakeError(NSInteger code, NSString* description) {
    return [NSError errorWithDomain:AZEmulatorErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : description}];
}

/// The custom layout used before the user sets one: landscape has the top screen on the left and
/// the bottom screen on the right, portrait stacks them. As fractions of the screen.
std::array<float, 8> DefaultCustomLayout(bool portrait, double width, double height) {
    // The screens are 400x240 and 320x240, which is 5:3 and 4:3.
    double top_w, top_h, top_x, top_y, bottom_w, bottom_h, bottom_x, bottom_y;
    if (portrait) {
        top_w = width;
        top_h = top_w * 0.6;
        top_x = 0.0;
        top_y = height * 0.05;
        bottom_w = width * 0.8;
        bottom_h = bottom_w * 0.75;
        bottom_x = (width - bottom_w) / 2.0;
        bottom_y = std::min(top_y + top_h + height * 0.03, height - bottom_h);
    } else {
        top_h = height * 0.72;
        top_w = top_h * 5.0 / 3.0;
        if (top_w > width * 0.62) {
            top_w = width * 0.62;
            top_h = top_w * 0.6;
        }
        top_x = width * 0.01;
        top_y = (height - top_h) / 2.0;
        bottom_w = std::min(width * 0.34, height * 0.6 * 4.0 / 3.0);
        bottom_h = bottom_w * 0.75;
        bottom_x = width - bottom_w - width * 0.01;
        bottom_y = (height - bottom_h) / 2.0;
    }
    const auto fraction = [](double value, double extent) {
        return static_cast<float>(extent > 0 ? value / extent : 0.0);
    };
    return {fraction(top_x, width),    fraction(top_y, height),    fraction(top_w, width),
            fraction(top_h, height),   fraction(bottom_x, width),  fraction(bottom_y, height),
            fraction(bottom_w, width), fraction(bottom_h, height)};
}

} // Anonymous namespace

namespace {
/// The thermal state of the device, which says whether the GPU is being slowed down.
const char* ThermalStateName() {
    switch (NSProcessInfo.processInfo.thermalState) {
    case NSProcessInfoThermalStateNominal:
        return "cool";
    case NSProcessInfoThermalStateFair:
        return "warm (fair)";
    case NSProcessInfoThermalStateSerious:
        return "hot (serious, throttled)";
    case NSProcessInfoThermalStateCritical:
        return "very hot (critical, throttled)";
    }
    return "unknown";
}
} // namespace

@implementation AZEmulator {
    std::unique_ptr<EmuWindow_iOS> _window;
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _stopRequested;
    std::atomic<bool> _pauseRequested;
    /// A save or load state was requested: it is carried out even while paused (see runLoop).
    std::atomic<bool> _stateOperation;
    std::atomic<AZEmulationState> _state;
    std::atomic<double> _speed;
    std::atomic<double> _fps;
    std::atomic<double> _displayFps;
    std::atomic<double> _shaderProgress;
    std::atomic<double> _gpuMs;
    std::atomic<double> _stallMs;
    std::atomic<double> _drawsPerFrame;
    std::atomic<double> _paceJitterMs;

    // Pending surface state, applied on the emulation thread.
    std::mutex _surfaceMutex;
    // The external display (guarded by _surfaceMutex).
    std::unique_ptr<EmuWindow_iOS> _secondaryWindow;
    CAMetalLayer* _secondaryLayer;
    CGSize _secondarySize;
    bool _externalDisplayEnabled;
    Settings::LayoutOption _userLayoutOption;
    bool _userSwapScreens;
    NSInteger _secondaryLayoutMode;
    // Fractions of the custom layouts, see AZConfiguration (guarded by _surfaceMutex).
    std::array<float, 8> _customLandscape;
    std::array<float, 8> _customPortrait;
    bool _hasCustomLandscape;
    bool _hasCustomPortrait;
    CAMetalLayer* _layer;
    CGSize _drawableSize;
    BOOL _portrait;
    std::atomic<bool> _surfaceDirty;
    // Settings changed while a game runs, applied to the core on the emulation thread.
    std::atomic<bool> _settingsDirty;

    NSString* _userDirectory;
    BOOL _gameHacksEnabled;
    NSDictionary<NSString*, NSNumber*>* _gameHackOverrides;
}

+ (void)logMessage:(NSString*)message {
    LOG_INFO(Frontend, "{}", message.UTF8String ?: "");
}

+ (void)setFrameGenerator:(id<AZFrameGenerating>)generator {
    if (generator == nil) {
        Metal::SetFrameGeneratorHooks({});
        return;
    }
    Metal::FrameGeneratorHooks hooks;
    hooks.prepare = [generator](void* device, u32 width, u32 height) {
        return [generator prepareWithDevice:(__bridge id<MTLDevice>)device
                                      width:width
                                     height:height];
    };
    hooks.texture = [generator](u32 slot, u32 plane) -> void* {
        return (__bridge void*)[generator textureForSlot:slot plane:plane];
    };
    hooks.interpolate = [generator](u32 previous, u32 current, u32 destination,
                                    std::function<void(bool)> done) {
        return [generator interpolateFromSlot:previous
                                       toSlot:current
                                     intoSlot:destination
                                   completion:^(BOOL made) {
                                     done(made);
                                   }] == YES;
    };
    Metal::SetFrameGeneratorHooks(std::move(hooks));
}

+ (AZEmulator*)shared {
    static AZEmulator* instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      instance = [[AZEmulator alloc] initPrivate];
    });
    return instance;
}

+ (BOOL)isUpscalingSupported {
    static BOOL supported = NO;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      supported = Metal::IsUpscalingSupported();
    });
    return supported;
}

+ (BOOL)deviceNeedsPreparedMemory {
#if TARGET_OS_SIMULATOR
    return NO;
#else
    return DeviceHasTXM();
#endif
}

+ (BOOL)isJITAvailable {
#if TARGET_OS_SIMULATOR
    return YES;
#else
    static bool has_txm = false;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      has_txm = DeviceHasTXM();
      LOG_INFO(Frontend, "Trusted Execution Monitor: {}", has_txm ? "yes" : "no");
    });
    // Retried on every call, so a debugger attached after launch still enables the JIT.
    return Common::AppleJIT::Initialize(has_txm) != Common::AppleJIT::Mode::Unavailable;
#endif
}

+ (NSString*)versionString {
    return [NSString stringWithUTF8String:Common::g_build_fullname];
}

- (instancetype)initPrivate {
    if ((self = [super init])) {
        _stopRequested = false;
        _pauseRequested = false;
        _stateOperation = false;
        _state = AZEmulationStateStopped;
        _speed = 0.0;
        _fps = 0.0;
        _displayFps = 0.0;
        _shaderProgress = -1.0;
        _gpuMs = 0.0;
        _stallMs = 0.0;
        _drawsPerFrame = 0.0;
        _paceJitterMs = 0.0;
        _surfaceDirty = false;
        _settingsDirty = false;
        _drawableSize = CGSizeZero;
        _userDirectory = @"";
    }
    return self;
}

- (NSString*)userDirectory {
    return _userDirectory;
}

- (AZEmulationState)state {
    return _state.load();
}

- (double)emulationSpeed {
    return _speed.load();
}

- (double)framesPerSecond {
    return _fps.load();
}

- (double)displayFramesPerSecond {
    return _displayFps.load();
}

- (double)shaderBuildProgress {
    return _shaderProgress.load();
}

- (double)gpuMilliseconds {
    return _gpuMs.load();
}

- (double)stallMilliseconds {
    return _stallMs.load();
}

- (double)paceJitterMilliseconds {
    return _paceJitterMs.load();
}

- (double)drawsPerFrame {
    return _drawsPerFrame.load();
}

- (void)setUpWithUserDirectory:(NSString*)directory {
    NSString* path =
        [directory hasSuffix:@"/"] ? directory : [directory stringByAppendingString:@"/"];
    _userDirectory = path;
    FileUtil::SetUserPath(std::string{path.UTF8String});
    for (const auto user_path :
         {FileUtil::UserPath::ConfigDir, FileUtil::UserPath::CacheDir, FileUtil::UserPath::SDMCDir,
          FileUtil::UserPath::NANDDir, FileUtil::UserPath::SysDataDir, FileUtil::UserPath::LogDir,
          FileUtil::UserPath::ShaderDir, FileUtil::UserPath::StatesDir,
          FileUtil::UserPath::CheatsDir, FileUtil::UserPath::LoadDir,
          FileUtil::UserPath::DumpDir}) {
        FileUtil::CreateFullPath(FileUtil::GetUserPath(user_path));
    }

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(false);
    Common::Log::Start();

    IOSInput::Init();
    IOSInput::BindInputProfile();

    // The core looks up every service module in lle_modules (Service::AttemptLLE uses .at()), the
    // other frontends fill it from their config. Use the HLE implementations of all modules.
    for (const auto& service_module : Service::service_module_map) {
        Settings::values.lle_modules.emplace(service_module.name, false);
    }
    // The cameras are off until the settings say otherwise (see applyConfiguration).
    Settings::values.camera_name.fill("blank");
    Camera::RegisterIOSCameraFactory();
    AudioCore::RegisterIOSMicrophone();

    // The native Metal renderer is the only hardware renderer on iOS.
    Settings::values.graphics_api = Settings::GraphicsAPI::Metal;

    LOG_INFO(Frontend, "Azahar {} for iOS, user directory: {}", Common::g_build_fullname,
             path.UTF8String);
    // Ask for executable memory as early as possible: on TXM devices the debugger provides it and
    // is detached afterwards.
    const bool jit = AZEmulator.isJITAvailable;
    LOG_INFO(Frontend, "JIT is {} ({})", jit ? "available" : "not available",
             Common::AppleJIT::GetModeName(Common::AppleJIT::GetMode()));
}

+ (void)setLoggingEnabled:(BOOL)enabled {
    // Off: only critical errors are logged, and the statistics for the log are not collected.
    Common::Log::Filter filter;
    filter.ParseFilterString(enabled ? "*:Info" : "*:Critical");
    Common::Log::SetGlobalFilter(filter);
    Metal::SetStatisticsEnabled(enabled);
}

- (void)applyConfiguration:(AZConfiguration*)configuration {
    const bool jit = AZEmulator.isJITAvailable;
    auto& values = Settings::values;

    // Without a debugger attached, iOS does not allow mapping executable memory.
    [AZEmulator setLoggingEnabled:configuration.loggingEnabled];
    values.use_cpu_jit = configuration.useCPUJIT && jit;
    values.use_shader_jit = configuration.useShaderJIT && jit;
    values.cpu_clock_percentage = static_cast<s32>(configuration.cpuClockPercentage);

    values.resolution_factor = static_cast<u32>(configuration.resolutionFactor);
    values.filter_mode = configuration.linearFiltering;
    values.texture_filter = static_cast<Settings::TextureFilter>(configuration.textureFilter);
    // Frame generation needs to know which frames are new, so it skips the repeated ones.
    values.use_skip_duplicate_frames =
        configuration.skipDuplicateFrames || configuration.frameGeneration > 0;
    Metal::QualityOptions quality;
    quality.fast_math = configuration.fastShaderMath;
    quality.fast_lighting = configuration.fastLighting;
    quality.fast_color = configuration.fastColor;
    quality.fast_functions = configuration.fastMathFunctions;
    quality.half_interpolants = configuration.halfInterpolants;
    quality.untracked_buffers = configuration.untrackedBuffers;
    quality.vertex_reuse = configuration.vertexReuse;
    quality.vertex_fast_functions = configuration.vertexFastFunctions;
    quality.vertex_fast_math = configuration.vertexFastMath;
    quality.texture_pool = configuration.texturePool;
    quality.vertex_depth = configuration.hiddenSurfaceRemoval;
    quality.draw_optimizations = configuration.drawOptimizations;
    quality.gpu_pass_timing = configuration.gpuPassTiming;
    quality.texture_compression = static_cast<Metal::TextureCompression>(
        std::clamp<NSInteger>(configuration.textureCompression, 0, 2));
    quality.async_depth_readback = configuration.fastDepthReadback;
    quality.hold_frames_for_shaders = configuration.holdFramesForShaders;
    quality.frame_generation = static_cast<int>(std::clamp<NSInteger>(configuration.frameGeneration, 0, 2));
    quality.display_hz = static_cast<int>(configuration.displayRefreshRate);
    quality.frame_generation_size = static_cast<int>(configuration.frameGenerationSize);
    quality.frame_generation_top_only = configuration.frameGenerationTopOnly;
    quality.frame_generation_pause = configuration.frameGenerationPause;
    // With display sync the display link owns the layer, so frame generation goes through it too.
    quality.display_sync = configuration.displaySync;
    quality.frame_cap = configuration.displaySync && configuration.frameCap30 ? 2 : 0;
    quality.frame_generation_display_paced =
        configuration.frameGenerationDisplayPaced || configuration.displaySync;
    // The emulation wakes up for each frame just after a refresh of the display (display sync).
    Core::System::GetInstance().frame_limiter.SetDeadlineSnap(
        configuration.displaySync ? &Metal::SnapDeadlineToDisplay : nullptr);
    quality.frame_generation_metalfx = configuration.frameGenerationMetalFX;
    quality.shader_wait_us = static_cast<u32>(std::clamp<NSInteger>(configuration.shaderWaitMs, 0, 16)) * 1000;
    quality.max_anisotropy = static_cast<int>(configuration.anisotropy);
    Metal::SetQualityOptions(quality);
    // Camera slots: 0 outer right, 1 inner, 2 outer left.
    const auto set_camera = [&](std::size_t slot, NSInteger choice) {
        values.camera_name[slot] = choice == 0 ? "blank" : Camera::IOS_CAMERA_NAME;
        values.camera_config[slot] = choice == 2 ? "front" : "back";
        values.camera_flip[slot] = 0;
    };
    set_camera(0, configuration.outerCamera);
    set_camera(1, configuration.innerCamera);
    set_camera(2, configuration.outerCamera);
    values.render_3d = static_cast<Settings::StereoRenderOption>(configuration.stereoMode);
    values.render_3d_which_display = configuration.stereoMode == 0
                                         ? Settings::StereoWhichDisplay::None
                                         : Settings::StereoWhichDisplay::PrimaryOnly;
    values.factor_3d = static_cast<u32>(std::clamp<NSInteger>(configuration.stereoDepth, 0, 100));
    values.swap_eyes_3d = configuration.swapEyes3D;
    values.cardboard_screen_size = static_cast<u32>(std::clamp<NSInteger>(configuration.cardboardSize, 30, 100));
    values.cardboard_x_shift = static_cast<s32>(std::clamp<NSInteger>(configuration.cardboardXShift, -100, 100));
    values.cardboard_y_shift = static_cast<s32>(std::clamp<NSInteger>(configuration.cardboardYShift, -100, 100));
    values.aspect_ratio = static_cast<Settings::AspectRatio>(std::clamp<NSInteger>(configuration.aspectRatio, 0, 5));
    values.screen_top_stretch = configuration.topStretch;
    values.screen_bottom_stretch = configuration.bottomStretch;
    values.screen_top_leftright_padding = static_cast<u16>(std::clamp<NSInteger>(configuration.topPaddingH, 0, 2000));
    values.screen_top_topbottom_padding = static_cast<u16>(std::clamp<NSInteger>(configuration.topPaddingV, 0, 2000));
    values.screen_bottom_leftright_padding = static_cast<u16>(std::clamp<NSInteger>(configuration.bottomPaddingH, 0, 2000));
    values.screen_bottom_topbottom_padding = static_cast<u16>(std::clamp<NSInteger>(configuration.bottomPaddingV, 0, 2000));
    values.turbo_limit = static_cast<double>(std::max<NSInteger>(configuration.fastForwardSpeed, 0));
    values.init_clock = configuration.fixedClock ? Settings::InitClock::FixedTime
                                                 : Settings::InitClock::SystemTime;
    values.init_time = static_cast<u64>(std::max<NSInteger>(configuration.fixedTime, 0));
    values.battery_state_source = configuration.fixedBattery ? Settings::BatteryLevelSource::Fixed
                                                             : Settings::BatteryLevelSource::System;
    values.battery_level = static_cast<u8>(std::clamp<NSInteger>(configuration.batteryLevel, 0, 4));
    values.battery_charging = configuration.batteryCharging;
    values.steps_per_hour = static_cast<u16>(std::clamp<NSInteger>(configuration.stepsPerHour, 0, 65535));
    values.apply_region_free_patch = configuration.regionFreePatch;
    Metal::SetPostProcessing(static_cast<int>(std::clamp<NSInteger>(configuration.postEffect, 0, 3)),
                             static_cast<int>(std::clamp<NSInteger>(configuration.anaglyphMode, 0, 3)));
    Metal::SetCustomPostShader(
        configuration.postShader.length > 0
            ? std::string{_userDirectory.UTF8String} + "shaders/post/" +
                  std::string{configuration.postShader.UTF8String} + ".metal"
            : std::string{});
    _gameHacksEnabled = configuration.gameHacksEnabled;
    _gameHackOverrides = [configuration.gameHackOverrides copy] ?: @{};
    values.audio_emulation = static_cast<Settings::AudioEmulation>(
        std::clamp<NSInteger>(configuration.audioEmulation, 0, 2));
    values.lle_applets = configuration.lleApplets;
    values.texture_sampling = static_cast<Settings::TextureSampling>(
        std::clamp<NSInteger>(configuration.textureSampling, 0, 2));
    values.mono_render_option = configuration.monoEye == 1 ? Settings::MonoRenderOption::RightEye
                                                           : Settings::MonoRenderOption::LeftEye;
    values.use_virtual_sd = configuration.useVirtualSD;
    values.compress_cia_installs = configuration.compressCIAInstalls;
    {
        // The data folders: a folder the user chose (as nand/ and sdmc/ in it), or the defaults
        // inside the Azahar folder. Only changed while no game runs.
        if (_state.load() == AZEmulationStateStopped) {
            std::string nand = std::string{_userDirectory.UTF8String} + "nand/";
            std::string sdmc = std::string{_userDirectory.UTF8String} + "sdmc/";
            values.use_custom_storage = false;
            if (configuration.customStoragePath.length > 0) {
                const std::string base = std::string{configuration.customStoragePath.UTF8String} + "/";
                nand = base + "nand/";
                sdmc = base + "sdmc/";
                values.use_custom_storage = true;
            }
            FileUtil::CreateFullPath(nand);
            FileUtil::CreateFullPath(sdmc);
            FileUtil::UpdateUserPath(FileUtil::UserPath::NANDDir, nand);
            FileUtil::UpdateUserPath(FileUtil::UserPath::SDMCDir, sdmc);
        }
    }
    values.plugin_loader_enabled = configuration.pluginLoader;
    values.enable_required_online_lle_modules = configuration.onlineModules;
    values.async_shader_compilation = configuration.asyncShaderCompilation;
    Metal::SetFramePacing(configuration.framePacing);
    Metal::SetPacingBuffer(static_cast<int>(configuration.pacingBufferMs));
    // The device microphone, or the static noise the console got before it was available.
    values.input_type = configuration.useMicrophone ? AudioCore::InputType::Custom
                                                    : AudioCore::InputType::Static;
    values.use_disk_shader_cache = configuration.diskShaderCache;
    Metal::SetUpscalerMode(configuration.upscaling == 1 ? Metal::UpscalerMode::Spatial
                                                        : Metal::UpscalerMode::Off);
    Metal::SetUpscaleFactor(static_cast<float>(configuration.upscaleFactorTenths) / 10.0f);
    Metal::SetSharpness(configuration.sharpPixels, static_cast<int>(configuration.sharpness));
    Metal::SetScreenStyle(static_cast<int>(configuration.sharpenMethod),
                          static_cast<int>(configuration.scaleFilter),
                          static_cast<int>(configuration.gridStrength),
                          static_cast<int>(configuration.saturation),
                          static_cast<int>(configuration.contrast),
                          static_cast<int>(configuration.brightness));

    _externalDisplayEnabled = configuration.useExternalDisplay;
    _userLayoutOption = static_cast<Settings::LayoutOption>(configuration.layoutOption);
    values.layout_option = _userLayoutOption;
    values.portrait_layout_option =
        static_cast<Settings::PortraitLayoutOption>(configuration.portraitLayoutOption);
    _userSwapScreens = configuration.swapScreens;
    values.swap_screen = _userSwapScreens;
    values.upright_screen = configuration.uprightScreens;
    values.screen_gap = static_cast<int>(configuration.screenGap);
    values.large_screen_proportion = configuration.largeScreenProportion;
    values.small_screen_position =
        static_cast<Settings::SmallScreenPosition>(configuration.smallScreenPosition);
    values.custom_second_layer_opacity =
        static_cast<u16>(std::clamp<NSInteger>(configuration.customSecondLayerOpacity, 0, 100));
    {
        std::scoped_lock lock{_surfaceMutex};
        const auto read = [](NSArray<NSNumber*>* source, std::array<float, 8>& target) {
            if (source.count != 8) {
                return false;
            }
            for (NSUInteger i = 0; i < 8; i++) {
                target[i] = source[i].floatValue;
            }
            return true;
        };
        _hasCustomLandscape = read(configuration.customLayoutLandscape, _customLandscape);
        _hasCustomPortrait = read(configuration.customLayoutPortrait, _customPortrait);
    }
    values.custom_textures = configuration.customTextures;
    values.async_custom_loading = configuration.asyncCustomTextures;
    values.disable_right_eye_render = configuration.disableRightEyeRender;
    values.use_integer_scaling = configuration.integerScaling;
    values.deterministic_async_operations = configuration.deterministicAsync;
    values.simulate_headphones_plugged = configuration.simulateHeadphones;
    values.simulate_3ds_gpu_timings = configuration.simulateGPUTimings;
    Pica::g_gpu_timing_percent.store(static_cast<u32>(std::clamp<NSInteger>(configuration.gpuTimingPercent, 50, 800)));
    // Minimum GPU Time belongs to the GPU simulation. The 30 FPS frame cap can also hold the game
    // itself at 30: the 3D work then never finishes within the refresh it started in (one refresh
    // is 16.71 ms), so a game that adapts its frame rate does not make frames that the cap would
    // throw away.
    u32 gpu_floor_us = configuration.simulateGPUTimings
                           ? static_cast<u32>(std::clamp<NSInteger>(configuration.gpuTimingMinMs, 0, 600)) * 100
                           : 0;
    const bool capHoldsGame =
        configuration.displaySync && configuration.frameCap30 && configuration.frameCapHoldsGame;
    if (capHoldsGame) {
        gpu_floor_us = std::max<u32>(gpu_floor_us, 17000);
    }
    Pica::g_gpu_timing_min_us.store(gpu_floor_us);
    if (configuration.simulateGPUTimings) {
        LOG_INFO(Frontend, "Simulating the GPU time: strength {}%, at least {:.1f} ms",
                 Pica::g_gpu_timing_percent.load(), Pica::g_gpu_timing_min_us.load() / 1000.0);
    }
    if (capHoldsGame) {
        LOG_INFO(Frontend, "The 30 FPS frame cap holds the game at 30: its 3D work takes at least "
                           "{:.1f} ms from the vertical blank",
                 gpu_floor_us / 1000.0);
    }
    _secondaryLayoutMode = configuration.secondaryDisplayLayout;
    values.preload_textures = configuration.preloadTextures;
    values.dump_textures = configuration.dumpTextures;

    values.enable_audio_stretching = configuration.audioStretching;
    values.enable_realtime_audio = configuration.realtimeAudio;
    values.volume = configuration.volume;

    values.is_new_3ds = configuration.isNew3DS;
    values.region_value = static_cast<s32>(configuration.region);
    values.frame_limit =
        configuration.useFrameLimit ? static_cast<double>(configuration.frameLimit) : 0.0;
    if (configuration.syncSpeedToDisplay && configuration.useFrameLimit &&
        configuration.frameLimit == 100) {
        // The console refreshes at 59.83 Hz, the display at 60 (or 120). Run at the display's
        // rate: 0.3% faster, which no one notices, and no refresh is repeated now and then.
        values.frame_limit = 100.0 * 60.0 / SCREEN_REFRESH_RATE;
    }

    values.graphics_api = Settings::GraphicsAPI::Metal;
    // Vertex shaders run on the GPU unless disabled; draws the Metal backend cannot translate
    // (geometry shaders, untranslatable programs) still fall back to the CPU per draw.
    values.use_hw_shader = configuration.useHardwareShaders;
    // Fast math in the vertex shaders assumes no NaN or infinity, which the accurate
    // multiplication needs, so it is off then.
    values.shaders_accurate_mul = configuration.accurateMultiplication && !configuration.vertexFastMath;

    if (_state.load() != AZEmulationStateStopped) {
        // ApplySettings rebuilds the input devices, the audio sink and the cameras and redoes
        // the screen layout. It must run on the emulation thread, between frames: from this
        // (main) thread it tore those down while the game was using them, which crashed now and
        // then (for example when the swap screens button changed the layout).
        _settingsDirty = true;
        _surfaceDirty = true;
    }
}

/// With an external display the screens go to separate windows: the external display shows the
/// top screen, this device the bottom screen (the layout hands the top screen to the primary
/// window unless the screens are swapped).
- (void)applyLayoutSettingsForExternal:(BOOL)external {
    auto& values = Settings::values;
    if (external && _secondaryLayoutMode == 0) {
        values.layout_option = Settings::LayoutOption::SeparateWindows;
        values.swap_screen = !_userSwapScreens;
    } else if (external) {
        // This device keeps its layout, the display shows the chosen secondary layout.
        values.secondary_display_layout = static_cast<Settings::SecondaryDisplayLayout>(
            std::clamp<NSInteger>(_secondaryLayoutMode, 1, 7));
        values.layout_option = _userLayoutOption;
        values.swap_screen = _userSwapScreens;
    } else {
        values.layout_option = _userLayoutOption;
        values.swap_screen = _userSwapScreens;
    }
}

- (void)setSecondaryRenderLayer:(CAMetalLayer*)layer size:(CGSize)size {
    {
        std::scoped_lock lock{_surfaceMutex};
        _secondaryLayer = layer;
        _secondarySize = size;
    }
    _surfaceDirty = true;
}

- (void)setRenderLayer:(CAMetalLayer*)layer {
    std::scoped_lock lock{_surfaceMutex};
    _layer = layer;
    _surfaceDirty = true;
}

+ (NSArray<NSNumber*>*)defaultCustomLayoutForSize:(CGSize)size portrait:(BOOL)portrait {
    const auto layout = DefaultCustomLayout(portrait, size.width, size.height);
    NSMutableArray<NSNumber*>* result = [NSMutableArray array];
    for (const float value : layout) {
        [result addObject:@(value)];
    }
    return result;
}

- (void)refreshLayout {
    _surfaceDirty = true;
}

- (void)setCustomLayout:(NSArray<NSNumber*>*)fractions portrait:(BOOL)portrait {
    {
        std::scoped_lock lock{_surfaceMutex};
        auto& target = portrait ? _customPortrait : _customLandscape;
        bool& has = portrait ? _hasCustomPortrait : _hasCustomLandscape;
        has = fractions.count == 8;
        if (has) {
            for (NSUInteger i = 0; i < 8; i++) {
                target[i] = fractions[i].floatValue;
            }
        }
    }
    _surfaceDirty = true;
}

- (void)setDrawableSize:(CGSize)size portrait:(BOOL)portrait {
    std::scoped_lock lock{_surfaceMutex};
    _drawableSize = size;
    _portrait = portrait;
    _surfaceDirty = true;
}

- (void)applyPendingSurfaceChanges {
    if (!_surfaceDirty.exchange(false) || !_window) {
        return;
    }
    CAMetalLayer* layer;
    CGSize size;
    BOOL portrait;
    {
        std::scoped_lock lock{_surfaceMutex};
        layer = _layer;
        size = _drawableSize;
        portrait = _portrait;
    }
    // The custom layouts are kept as fractions and turned into pixels for the current size.
    {
        std::array<float, 8> rects;
        {
            std::scoped_lock lock{_surfaceMutex};
            rects = portrait ? (_hasCustomPortrait
                                    ? _customPortrait
                                    : DefaultCustomLayout(true, size.width, size.height))
                             : (_hasCustomLandscape
                                    ? _customLandscape
                                    : DefaultCustomLayout(false, size.width, size.height));
        }
        const auto pixels = [&](float fraction, double extent) {
            return static_cast<u16>(std::clamp(std::lround(fraction * extent), 0L, 65535L));
        };
        auto& values = Settings::values;
        if (portrait) {
            values.custom_portrait_top_x = pixels(rects[0], size.width);
            values.custom_portrait_top_y = pixels(rects[1], size.height);
            values.custom_portrait_top_width = pixels(rects[2], size.width);
            values.custom_portrait_top_height = pixels(rects[3], size.height);
            values.custom_portrait_bottom_x = pixels(rects[4], size.width);
            values.custom_portrait_bottom_y = pixels(rects[5], size.height);
            values.custom_portrait_bottom_width = pixels(rects[6], size.width);
            values.custom_portrait_bottom_height = pixels(rects[7], size.height);
        } else {
            values.custom_top_x = pixels(rects[0], size.width);
            values.custom_top_y = pixels(rects[1], size.height);
            values.custom_top_width = pixels(rects[2], size.width);
            values.custom_top_height = pixels(rects[3], size.height);
            values.custom_bottom_x = pixels(rects[4], size.width);
            values.custom_bottom_y = pixels(rects[5], size.height);
            values.custom_bottom_width = pixels(rects[6], size.width);
            values.custom_bottom_height = pixels(rects[7], size.height);
        }
    }
    CAMetalLayer* secondary_layer;
    CGSize secondary_size;
    {
        std::scoped_lock lock{_surfaceMutex};
        secondary_layer = _secondaryLayer;
        secondary_size = _secondarySize;
    }
    const bool external = _externalDisplayEnabled && secondary_layer != nil &&
                          secondary_size.width > 0 && secondary_size.height > 0;
    [self applyLayoutSettingsForExternal:external];

    _window->OnSurfaceChanged((__bridge void*)layer, static_cast<unsigned>(size.width),
                              static_cast<unsigned>(size.height), portrait);
    if (_secondaryWindow) {
        _secondaryWindow->OnSurfaceChanged(
            external ? (__bridge void*)secondary_layer : nullptr,
            static_cast<unsigned>(std::max(secondary_size.width, 1.0)),
            static_cast<unsigned>(std::max(secondary_size.height, 1.0)), false);
        if (_secondaryWindow->TakeSurfaceChanged()) {
            auto& system = Core::System::GetInstance();
            if (system.IsPoweredOn()) {
                system.GPU().Renderer().NotifySurfaceChanged(true);
            }
        }
    }
    if (_window->TakeSurfaceChanged()) {
        auto& system = Core::System::GetInstance();
        if (system.IsPoweredOn()) {
            system.GPU().Renderer().NotifySurfaceChanged(false);
        }
    }
}

- (BOOL)startWithPath:(NSString*)path error:(NSError**)error {
    if (_state.load() != AZEmulationStateStopped || _thread.joinable()) {
        if (error) {
            *error = MakeError(1, @"Emulation is already running.");
        }
        return NO;
    }
    if (!IsArticPath(path.UTF8String) &&
        ![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        if (error) {
            *error = MakeError(2, @"The selected file does not exist.");
        }
        return NO;
    }

    CAMetalLayer* layer;
    CGSize size;
    BOOL portrait;
    {
        std::scoped_lock lock{_surfaceMutex};
        layer = _layer;
        size = _drawableSize;
        portrait = _portrait;
    }
    _window =
        std::make_unique<EmuWindow_iOS>((__bridge void*)layer, static_cast<unsigned>(size.width),
                                        static_cast<unsigned>(size.height), portrait);
    {
        // The window of the external display exists for the whole run, headless while none is
        // connected, so one can be plugged in at any time.
        std::scoped_lock lock{_surfaceMutex};
        const bool connected = _externalDisplayEnabled && _secondaryLayer != nil &&
                               _secondarySize.width > 0 && _secondarySize.height > 0;
        _secondaryWindow = std::make_unique<EmuWindow_iOS>(
            connected ? (__bridge void*)_secondaryLayer : nullptr,
            static_cast<unsigned>(std::max(_secondarySize.width, 1.0)),
            static_cast<unsigned>(std::max(_secondarySize.height, 1.0)), false, true);
    }
    // The first pass of the emulation loop turns the custom layout fractions into pixels for this
    // size and applies the layout overrides (external display), so the first frame uses them.
    _surfaceDirty = true;
    _stopRequested = false;
    _pauseRequested = false;
    _state = AZEmulationStateRunning;

    const std::string filepath{path.UTF8String};
    _thread = std::thread([self, filepath] { [self emulationThread:filepath]; });
    return YES;
}

- (void)emulationThread:(std::string)filepath {
    pthread_setname_np("EmuThread");
    // Without this iOS may run the thread on an efficiency core. Emulation is the most latency
    // sensitive work the app does.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    // An exception escaping this thread would terminate the app. Report it as an emulation error
    // instead so the user gets a message and the log records what happened.
    NSError* error = nil;
    try {
        error = [self runEmulation:filepath];
    } catch (const std::exception& e) {
        LOG_CRITICAL(Frontend, "Unhandled exception during emulation: {}", e.what());
        error = MakeError(-1, [NSString stringWithFormat:@"Emulation failed: %s", e.what()]);
    } catch (...) {
        LOG_CRITICAL(Frontend, "Unhandled unknown exception during emulation");
        error = MakeError(-1, @"Emulation failed with an unknown error.");
    }
    [self finishWithError:error];
}

/// Sets up the per game workarounds of the core for the title that starts: the built-in list (or
/// none of its game workarounds) and what the user decided for this game.
- (void)applyGameHacksForProgram:(u64)program_id {
    using namespace Common::Hacks;
    static std::multimap<HackType, HackEntry> defaults;
    static bool captured = false;
    if (!captured) {
        defaults = hack_manager.entries;
        captured = true;
    }
    hack_manager.entries = defaults;

    // Only the workarounds for games go; the ones for the system titles (applets, the eShop) stay.
    if (!_gameHacksEnabled) {
        for (const auto type : {HackType::RIGHT_EYE_DISABLE, HackType::ACCURATE_MULTIPLICATION,
                                HackType::REQUIRES_SHADER_FIXUP,
                                HackType::DELAY_TEXTURE_COPY_COMPLETION,
                                HackType::SPOOF_FRIEND_CODE_SEED}) {
            hack_manager.entries.erase(type);
        }
    }

    static NSDictionary<NSString*, NSNumber*>* const keys = @{
        @"rightEye" : @(static_cast<int>(HackType::RIGHT_EYE_DISABLE)),
        @"accurateMul" : @(static_cast<int>(HackType::ACCURATE_MULTIPLICATION)),
        @"shaderFixup" : @(static_cast<int>(HackType::REQUIRES_SHADER_FIXUP)),
        @"delayTextureCopy" : @(static_cast<int>(HackType::DELAY_TEXTURE_COPY_COMPLETION)),
        @"regionFromSecure" : @(static_cast<int>(HackType::REGION_FROM_SECURE)),
        @"spoofFriendCode" : @(static_cast<int>(HackType::SPOOF_FRIEND_CODE_SEED)),
        @"onlineLLE" : @(static_cast<int>(HackType::ONLINE_LLE_REQUIRED)),
    };
    for (NSString* key in _gameHackOverrides) {
        NSNumber* type_value = keys[key];
        const int choice = _gameHackOverrides[key].intValue;
        if (type_value == nil || (choice != 1 && choice != 2)) {
            continue;
        }
        const auto type = static_cast<HackType>(type_value.intValue);
        // Take the game out of the entries that name it, then give it one of its own.
        auto range = hack_manager.entries.equal_range(type);
        for (auto it = range.first; it != range.second; ++it) {
            auto& ids = it->second.affected_title_ids;
            ids.erase(std::remove(ids.begin(), ids.end(), program_id), ids.end());
        }
        HackAllowMode mode = HackAllowMode::DISALLOW;
        if (choice == 2) {
            // On: the right eye hack is an optimization that is allowed, the others are forced.
            mode = type == HackType::RIGHT_EYE_DISABLE ? HackAllowMode::ALLOW : HackAllowMode::FORCE;
        }
        HackEntry entry;
        entry.mode = mode;
        entry.affected_title_ids = {program_id};
        hack_manager.entries.emplace(type, std::move(entry));
    }
}

/// Boots the title and runs the emulation loop. Returns the error that stopped emulation, if any.
- (NSError*)runEmulation:(const std::string&)filepath {
    Core::System& system = Core::System::GetInstance();

    u64 program_id{};
    FileUtil::SetCurrentRomPath(filepath);
    auto app_loader = Loader::GetLoader(filepath);
    if (!app_loader && IsArticPath(filepath)) {
        return MakeError(3, @"Could not connect to the Artic server. Check that Artic Base or the "
                            @"Artic Setup Tool is running on the 3DS, that the address is right, "
                            @"that both devices are on the same network and that this app may "
                            @"use the local network (iOS Settings).");
    }
    if (app_loader) {
        app_loader->ReadProgramId(program_id);
        system.RegisterAppLoaderEarly(app_loader);
    }

    system.ApplySettings();
    Settings::LogSettings();
    Frontend::RegisterDefaultApplets(system);
    [self registerApplets];
    IOSInput::BindInputProfile();

    [self applyGameHacksForProgram:program_id];

    // A movie set before the start records or plays along with this run (the clock is set up
    // before the game loads, the movie itself starts once it has).
    int movie_mode = 0;
    std::string movie_path;
    std::string movie_author;
    {
        auto& pending = GetPendingMovie();
        std::scoped_lock lock{pending.mutex};
        movie_mode = pending.mode;
        movie_path = pending.path;
        movie_author = pending.author;
        pending.mode = 0;
    }
    auto& movie = system.Movie();
    movie.SetPlaybackCompletionCallback([self] {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (self.onMovieFinished) {
              self.onMovieFinished();
          }
        });
    });
    if (movie_mode == 1) {
        movie.PrepareForRecording();
    } else if (movie_mode == 2) {
        movie.PrepareForPlayback(movie_path);
    }

    const Core::System::ResultStatus load_result = system.Load(*_window, filepath, _secondaryWindow.get());
    if (load_result != Core::System::ResultStatus::Success) {
        return MakeResultError(load_result, system.GetStatusDetails());
    }
    if (movie_mode == 1) {
        movie.StartRecording(movie_path, movie_author);
    } else if (movie_mode == 2) {
        movie.StartPlayback(movie_path);
    }

    system.GPU().ApplyPerProgramSettings(program_id);
    // Builds the shaders cached by earlier runs of this game, reporting the progress to the UI.
    system.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
        _stopRequested, [self](VideoCore::LoadCallbackStage stage, std::size_t value,
                               std::size_t total, const std::string&) {
            if (stage == VideoCore::LoadCallbackStage::Build && total > 0) {
                self->_shaderProgress = static_cast<double>(value) / static_cast<double>(total);
            } else if (stage == VideoCore::LoadCallbackStage::Complete) {
                self->_shaderProgress = -1.0;
            }
        });
    _shaderProgress = -1.0;
    system.RegisterCoreLoopThreadId();
    Common::FrameCost::MarkEmulationThread();
#if defined(__aarch64__)
    // The JIT reports how long compiling each new block of game code takes (a patch of dynarmic).
    Dynarmic::g_block_compile_hook = [](std::uint64_t nanoseconds) {
        if (Common::FrameCost::t_emulation_thread) {
            Common::FrameCost::Add(Common::FrameCost::Kind::JitCompile, nanoseconds);
        }
    };
    // A code cache thrown away means everything it held is compiled again as it runs, which
    // shows as stutter: logged so it can be told whether the caches are big enough.
    Dynarmic::g_cache_clear_hook = [](int full, std::uint64_t used, std::uint64_t size) {
        if (full) {
            LOG_WARNING(Frontend, "JIT: a code cache was full ({} of {} MiB), all its compiled "
                                  "code is thrown away and compiled again as it runs",
                        used >> 20, size >> 20);
        } else {
            LOG_INFO(Frontend, "JIT: a code cache was cleared as a whole on request");
        }
    };
#endif

    NSError* error = nil;
    // The performance counters only update when they are collected, which resets them. Collect
    // them once per second of wall time and publish the result.
    using StatsClock = std::chrono::steady_clock;
    auto last_stats_time = StatsClock::now();
    while (!_stopRequested) {
        if (_settingsDirty.exchange(false)) {
            system.ApplySettings();
        }
        [self applyPendingSurfaceChanges];

        if (_stateOperation && !system.IsSaveStateOperationPending()) {
            _stateOperation = false;
        }
        if (_pauseRequested) {
            _state = AZEmulationStatePaused;
            const float volume = Settings::values.volume.GetValue();
            Settings::values.volume = 0;
            for (;;) {
                {
                    std::unique_lock lock{_mutex};
                    _cv.wait(lock, [self] {
                        return !_pauseRequested || _stopRequested || _stateOperation;
                    });
                }
                if (_stopRequested || !_pauseRequested) {
                    break;
                }
                // A save or load state asked for while paused: the game runs (muted) just long
                // enough for it to be carried out, then stays paused. Doing it while the game
                // was already running again, with the menu closing and the screens changing at
                // the same time, was seen to lock up.
                const auto started = StatsClock::now();
                while (!_stopRequested && system.IsSaveStateOperationPending() &&
                       StatsClock::now() - started < std::chrono::seconds(8)) {
                    const auto step = system.RunLoop();
                    if (step == Core::System::ResultStatus::ShutdownRequested) {
                        _stopRequested = true;
                        break;
                    }
                    if (step == Core::System::ResultStatus::ErrorSavestate ||
                        step == Core::System::ResultStatus::ErrorSavestateBuildMismatch) {
                        NSString* message =
                            step == Core::System::ResultStatus::ErrorSavestateBuildMismatch
                                ? [NSString stringWithFormat:@"This state was saved by another "
                                                             @"version of Azahar (%s) and can't "
                                                             @"be loaded.",
                                                             system.GetStatusDetails().c_str()]
                                : [NSString stringWithFormat:@"The state operation failed: %s",
                                                             system.GetStatusDetails().c_str()];
                        dispatch_async(dispatch_get_main_queue(), ^{
                          if (self.onNotice) {
                              self.onNotice(@"Save State", message);
                          }
                        });
                        break;
                    }
                    if (step != Core::System::ResultStatus::Success) {
                        break;
                    }
                }
                _stateOperation = false;
            }
            Settings::values.volume = volume;
            if (_stopRequested) {
                break;
            }
            _state = AZEmulationStateRunning;
            // Do not count the time spent paused in the speed.
            [[maybe_unused]] const auto discarded = system.GetAndResetPerfStats();
            last_stats_time = StatsClock::now();
        }

        const auto result = system.RunLoop();
        if (result == Core::System::ResultStatus::ShutdownRequested) {
            break;
        }
        if (result == Core::System::ResultStatus::ErrorSavestate ||
            result == Core::System::ResultStatus::ErrorSavestateBuildMismatch) {
            // A failed save state must not end the game, tell the user and keep running.
            NSString* message =
                result == Core::System::ResultStatus::ErrorSavestateBuildMismatch
                    ? [NSString stringWithFormat:@"This state was saved by another version of "
                                                 @"Azahar (%s) and can't be loaded.",
                                                 system.GetStatusDetails().c_str()]
                    : [NSString stringWithFormat:@"The state operation failed: %s",
                                                 system.GetStatusDetails().c_str()];
            dispatch_async(dispatch_get_main_queue(), ^{
              if (self.onNotice) {
                  self.onNotice(@"Save State", message);
              }
            });
            continue;
        }
        if (result != Core::System::ResultStatus::Success) {
            error = MakeResultError(result, system.GetStatusDetails());
            break;
        }

        const auto now = StatsClock::now();
        if (now - last_stats_time >= std::chrono::seconds(1)) {
            const double window = std::chrono::duration<double>(now - last_stats_time).count();
            last_stats_time = now;
            const auto stats = system.GetAndResetPerfStats();
            _speed = stats.emulation_speed * 100.0;
            _fps = stats.game_fps;
            const auto gpu = Metal::TakeFrameStats();
            // What reaches the screen, which is more than the game draws with frame generation.
            _displayFps = window > 0.0 ? gpu.presented / window : 0.0;
            if (Metal::StatisticsEnabled()) {
                for (const std::string& line : Metal::TakeReadbackSummary(4)) {
                    LOG_INFO(Frontend, "Readback {}", line);
                }
                for (const std::string& line : Metal::TakeRenderPassSummary(16)) {
                    LOG_INFO(Frontend, "Render passes (per second): {}", line);
                }
                for (const std::string& line : Metal::TakePassTimingSummary(10)) {
                    LOG_INFO(Frontend, "GPU time {}", line);
                }
            }
            if (gpu.frames > 0) {
                _gpuMs = gpu.gpu_ms;
                _stallMs = gpu.stall_ms;
                _drawsPerFrame = gpu.draws;
                _paceJitterMs = gpu.present_jitter_ms;
                LOG_INFO(Frontend,
                         "Speed {:.0f}%, {:.1f} fps: GPU {:.1f} ms (presenting {:.1f}), CPU waited "
                         "{:.1f} ms in {:.1f} stalls, {:.0f} draws, {:.1f} skipped, {:.1f} render passes, "
                         "{:.1f} blit passes, {:.1f} feedback copies, drawable wait {:.1f} ms "
                         "per frame; frames on screen every {:.2f} ms (spread {:.2f} ms), {:.0f} late, "
                         "emulation thread CPU {:.1f} ms per frame (worst {:.1f} ms), CPU JIT {}, shader JIT {}, "
                         "device {}{}",
                         _speed.load(), _fps.load(), gpu.gpu_ms, gpu.present_ms, gpu.stall_ms,
                         gpu.stalls,
                         gpu.draws, gpu.skipped, gpu.render_passes, gpu.blit_passes,
                         gpu.feedback_copies, gpu.drawable_ms, gpu.present_interval_ms,
                         gpu.present_jitter_ms, gpu.present_late, gpu.cpu_ms, gpu.cpu_max_ms,
                         Settings::values.use_cpu_jit.GetValue() ? "on" : "OFF (slow)",
                         Settings::values.use_shader_jit.GetValue() ? "on" : "off",
                         ThermalStateName(),
                         NSProcessInfo.processInfo.lowPowerModeEnabled ? ", low power mode" : "");
                if (gpu.fg_made > 0 || gpu.fg_skipped > 0) {
                    LOG_INFO(Frontend,
                             "Frame generation: {:.0f} frames made ({:.1f} ms each), {:.0f} game "
                             "frames dropped because the previous ones were still on their way",
                             gpu.fg_made, gpu.fg_ms, gpu.fg_skipped);
                }
            }
        }
    }
    return error;
}

- (void)finishWithError:(NSError*)error {
    Settings::ResetTemporaryFrameLimit();
    // A room needs the emulated console, leave it before that goes away.
    [[AZMultiplayer shared] leave];
    // Saves a recording that is still going.
    Core::System::GetInstance().Movie().Shutdown();
    Core::System::GetInstance().Shutdown();
    _speed = 0.0;
    _fps = 0.0;
    _displayFps = 0.0;
    _state = AZEmulationStateStopped;

    const bool requested = _stopRequested.load();
    dispatch_async(dispatch_get_main_queue(), ^{
      if (self->_thread.joinable() && !requested) {
          self->_thread.join();
      }
      self->_window.reset();
      {
          std::scoped_lock lock{self->_surfaceMutex};
          self->_secondaryWindow.reset();
      }
      if (!requested && self.onStopped) {
          self.onStopped(error);
      }
    });
}

- (void)beginStateOperation {
    {
        std::scoped_lock lock{_mutex};
        _stateOperation = true;
    }
    _cv.notify_all();
}

- (void)pause {
    if (_state.load() == AZEmulationStateRunning) {
        _pauseRequested = true;
    }
}

- (void)resume {
    {
        std::scoped_lock lock{_mutex};
        _pauseRequested = false;
    }
    _cv.notify_all();
}

- (void)stop {
    {
        std::scoped_lock lock{_mutex};
        _stopRequested = true;
        _pauseRequested = false;
    }
    _cv.notify_all();
    // The emulation thread may be waiting for a Mii to be chosen.
    [self cancelPendingApplets];
    if (_thread.joinable()) {
        _thread.join();
    }
    _window.reset();
    {
        std::scoped_lock lock{_surfaceMutex};
        _secondaryWindow.reset();
    }
}

- (void)setButton:(AZButton)button pressed:(BOOL)pressed {
    IOSInput::SetButton(static_cast<int>(button), pressed);
}

- (void)setAnalog:(AZAnalog)analog x:(float)x y:(float)y {
    IOSInput::SetAnalog(static_cast<int>(analog), x, y);
}

- (CGRect)bottomScreenFrame {
    if (!_window) {
        return CGRectZero;
    }
    const auto layout = _window->GetFramebufferLayout();
    const auto& screen = layout.bottom_screen;
    if (screen.GetWidth() == 0 || screen.GetHeight() == 0) {
        return CGRectZero;
    }
    return CGRectMake(screen.left, screen.top, screen.GetWidth(), screen.GetHeight());
}

- (void)touchBeganAtPoint:(CGPoint)point {
    if (_window) {
        _window->OnTouchPressed(static_cast<unsigned>(std::max(point.x, 0.0)),
                                static_cast<unsigned>(std::max(point.y, 0.0)));
    }
}

- (void)touchMovedToPoint:(CGPoint)point {
    if (_window) {
        _window->OnTouchMoved(static_cast<unsigned>(std::max(point.x, 0.0)),
                              static_cast<unsigned>(std::max(point.y, 0.0)));
    }
}

- (void)touchEnded {
    if (_window) {
        _window->OnTouchReleased();
    }
}

- (void)setMotionAccelerationX:(float)ax
                             y:(float)ay
                             z:(float)az
                         gyroX:(float)gx
                         gyroY:(float)gy
                         gyroZ:(float)gz {
    IOSInput::SetMotion(ax, ay, az, gx, gy, gz);
}

@end
