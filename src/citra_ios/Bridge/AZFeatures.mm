// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// The parts of the emulator the user interacts with while a title runs or is installed: the
// software keyboard, save states, cheats and CIA installation.

#import <AzaharCore/AZEmulator.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "citra_ios/Bridge/camera_ios.h"
#include "citra_ios/Bridge/movie_state.h"
#include "core/movie.h"
#include "common/logging/log.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/cheats/gateway_cheat.h"
#include "core/core.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "common/zstd_compression.h"
#include "core/frontend/applets/mii_selector.h"
#include "core/frontend/applets/swkbd.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/ptm/ptm.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/sm/sm.h"
#include "common/settings.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

#import <ImageIO/ImageIO.h>
#include "core/loader/loader.h"
#include "core/movie.h"
#include "core/savestate.h"
#include "core/system_titles.h"

@implementation AZKeyboardRequest
@end

@implementation AZMiiRequest
@end

@implementation AZSaveStateSlot {
    NSInteger _slot;
    NSDate* _date;
    BOOL _loadable;
    NSString* _thumbnailPath;
}

- (instancetype)initWithSlot:(NSInteger)slot
                        date:(NSDate*)date
                    loadable:(BOOL)loadable
               thumbnailPath:(NSString*)thumbnailPath {
    if ((self = [super init])) {
        _slot = slot;
        _date = date;
        _loadable = loadable;
        _thumbnailPath = thumbnailPath;
    }
    return self;
}

- (NSString*)thumbnailPath {
    return _thumbnailPath;
}

- (NSInteger)slot {
    return _slot;
}

- (NSDate*)date {
    return _date;
}

- (BOOL)isLoadable {
    return _loadable;
}

@end

@implementation AZSaveStateEntry {
    uint64_t _programID;
    uint64_t _movieID;
    NSInteger _slot;
    NSDate* _date;
    BOOL _loadable;
    unsigned long long _fileSize;
    NSString* _thumbnailPath;
}

- (instancetype)initWithProgramID:(uint64_t)programID
                          movieID:(uint64_t)movieID
                             slot:(NSInteger)slot
                             date:(NSDate*)date
                         loadable:(BOOL)loadable
                         fileSize:(unsigned long long)fileSize
                    thumbnailPath:(NSString*)thumbnailPath {
    if ((self = [super init])) {
        _programID = programID;
        _movieID = movieID;
        _slot = slot;
        _date = date;
        _loadable = loadable;
        _fileSize = fileSize;
        _thumbnailPath = thumbnailPath;
    }
    return self;
}

- (uint64_t)programID {
    return _programID;
}
- (uint64_t)movieID {
    return _movieID;
}
- (NSInteger)slot {
    return _slot;
}
- (NSDate*)date {
    return _date;
}
- (BOOL)isLoadable {
    return _loadable;
}
- (unsigned long long)fileSize {
    return _fileSize;
}
- (NSString*)thumbnailPath {
    return _thumbnailPath;
}

@end

@implementation AZCheat
@end

namespace {

NSString* ToNSString(const std::string& string) {
    NSString* result = [NSString stringWithUTF8String:string.c_str()];
    return result != nil ? result : @"";
}

const char* ValidationMessage(Frontend::ValidationError error) {
    using Frontend::ValidationError;
    switch (error) {
    case ValidationError::MaxDigitsExceeded:
        return "Too many digits.";
    case ValidationError::AtSignNotAllowed:
        return "The @ sign can't be used here.";
    case ValidationError::PercentNotAllowed:
        return "The % sign can't be used here.";
    case ValidationError::BackslashNotAllowed:
        return "The \\ sign can't be used here.";
    case ValidationError::ProfanityNotAllowed:
        return "That text is not allowed.";
    case ValidationError::FixedLengthRequired:
        return "The text must have exactly the requested length.";
    case ValidationError::MaxLengthExceeded:
        return "The text is too long.";
    case ValidationError::BlankInputNotAllowed:
        return "The text can't be blank.";
    case ValidationError::EmptyInputNotAllowed:
        return "Enter some text.";
    case ValidationError::ButtonOutOfRange:
    case ValidationError::CallbackFailed:
    default:
        return "That input is not accepted.";
    }
}

/// The software keyboard applet. The prompt is shown by the app on its UI thread, which hands the
/// result back with Submit.
class IOSKeyboard final : public Frontend::SoftwareKeyboard {
public:
    using RequestHandler = std::function<void(const Frontend::KeyboardConfig&, const std::string&)>;
    using NoticeHandler = std::function<void(const std::string&)>;

    IOSKeyboard(RequestHandler on_request_, NoticeHandler on_notice_)
        : on_request{std::move(on_request_)}, on_notice{std::move(on_notice_)} {}

    void Execute(const Frontend::KeyboardConfig& config_) override {
        SoftwareKeyboard::Execute(config_);
        std::string error;
        {
            std::scoped_lock lock{mutex};
            error = std::move(pending_error);
            pending_error.clear();
            ++error_generation;
        }
        on_request(config, error);
    }

    void ShowError(const std::string& error) override {
        u32 generation;
        {
            std::scoped_lock lock{mutex};
            pending_error = error;
            generation = ++error_generation;
        }
        // Usually the applet asks for the input again right away and the message is shown with
        // that prompt. Otherwise (the input was refused for good) show it on its own.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
                       dispatch_get_main_queue(), ^{
                         std::string message;
                         {
                             std::scoped_lock lock{mutex};
                             if (error_generation != generation || pending_error.empty()) {
                                 return;
                             }
                             message = std::move(pending_error);
                             pending_error.clear();
                         }
                         on_notice(message);
                       });
    }

    /// Returns why the input was rejected, or an empty string when it was accepted.
    std::string Submit(const std::string& text, u8 button) {
        // Only the button that confirms the input has to pass the checks.
        if (button == static_cast<u8>(config.button_config)) {
            const auto filter_error = ValidateFilters(text);
            if (filter_error != Frontend::ValidationError::None) {
                return ValidationMessage(filter_error);
            }
        }
        const auto error = Finalize(text, button);
        return error == Frontend::ValidationError::None ? std::string{} : ValidationMessage(error);
    }

private:
    RequestHandler on_request;
    NoticeHandler on_notice;
    std::mutex mutex;
    std::string pending_error;
    u32 error_generation{};
};

/// The Mii selector applet. The HLE applet reads the choice right after Setup returns, so Setup
/// blocks the emulation thread until the app submits it (like the Qt frontend's modal dialog).
class IOSMiiSelector final : public Frontend::MiiSelector {
public:
    using RequestHandler = std::function<void(const Frontend::MiiSelectorConfig&,
                                              const std::vector<std::string>&)>;

    explicit IOSMiiSelector(RequestHandler on_request_) : on_request{std::move(on_request_)} {}

    void Setup(const Frontend::MiiSelectorConfig& config_) override {
        MiiSelector::Setup(config_);
        const std::vector<Mii::MiiData> miis = Frontend::LoadMiis();
        if (miis.empty()) {
            // Nothing to choose from: the console's default Mii, like the default applet.
            Finalize(0, HLE::Applets::MiiSelector::GetStandardMiiResult().selected_mii_data);
            return;
        }

        std::vector<std::string> names;
        for (const Mii::MiiData& mii : miis) {
            std::u16string name{mii.mii_name.data(), mii.mii_name.size()};
            name.resize(name.find(u'\0') == std::u16string::npos ? name.size() : name.find(u'\0'));
            names.push_back(Common::UTF16ToUTF8(name));
        }
        {
            std::scoped_lock lock{mutex};
            selected = -1;
            done = false;
        }
        on_request(config, names);
        int index;
        {
            std::unique_lock lock{mutex};
            condition.wait(lock, [this] { return done; });
            index = selected;
        }
        if (index >= 0 && static_cast<std::size_t>(index) < miis.size()) {
            Finalize(0, miis[static_cast<std::size_t>(index)]);
        } else {
            Finalize(1, Mii::MiiData{});
        }
    }

    /// Called with the chosen index, or -1 to cancel.
    void Select(int index) {
        {
            std::scoped_lock lock{mutex};
            if (done) {
                return;
            }
            selected = index;
            done = true;
        }
        condition.notify_all();
    }

private:
    RequestHandler on_request;
    std::mutex mutex;
    std::condition_variable condition;
    int selected{-1};
    bool done{true};
};

std::shared_ptr<IOSMiiSelector> g_mii_selector;

/// The keyboard of the current run. Replaced for every title that starts.
std::shared_ptr<IOSKeyboard> g_keyboard;

AZInstallStatus ToInstallStatus(Service::AM::InstallStatus status) {
    using Service::AM::InstallStatus;
    switch (status) {
    case InstallStatus::Success:
        return AZInstallStatusSuccess;
    case InstallStatus::ErrorFailedToOpenFile:
        return AZInstallStatusFailedToOpen;
    case InstallStatus::ErrorFileNotFound:
        return AZInstallStatusNotFound;
    case InstallStatus::ErrorAborted:
        return AZInstallStatusAborted;
    case InstallStatus::ErrorInvalid:
        return AZInstallStatusInvalid;
    case InstallStatus::ErrorEncrypted:
        return AZInstallStatusEncrypted;
    default:
        return AZInstallStatusUnknown;
    }
}

/// Program ID of the running title, 0 when nothing runs.
u64 RunningProgramId() {
    Core::System& system = Core::System::GetInstance();
    u64 program_id{};
    if (!system.IsPoweredOn() ||
        system.GetAppLoader().ReadProgramId(program_id) != Loader::ResultStatus::Success) {
        return 0;
    }
    return program_id;
}

void SaveCheats() {
    const u64 program_id = RunningProgramId();
    if (program_id != 0) {
        Core::System::GetInstance().CheatEngine().SaveCheatFile(program_id);
    }
}

std::shared_ptr<Cheats::CheatBase> MakeCheat(AZCheat* cheat) {
    auto result = std::make_shared<Cheats::GatewayCheat>(
        cheat.name.UTF8String ?: "", cheat.code.UTF8String ?: "", cheat.comments.UTF8String ?: "");
    result->SetEnabled(cheat.enabled);
    return result;
}

} // Anonymous namespace

@interface AZMovieInfo ()
- (instancetype)initWithPath:(NSString*)path;
@end

@implementation AZEmulator (Features)

// MARK: Software keyboard

- (void)cancelPendingApplets {
    if (g_mii_selector) {
        g_mii_selector->Select(-1);
    }
}

- (void)registerApplets {
    [self registerMiiSelectorApplet];
    [self registerKeyboardApplet];
}

- (void)registerMiiSelectorApplet {
    __weak AZEmulator* weak_self = self;
    auto selector = std::make_shared<IOSMiiSelector>(
        [weak_self](const Frontend::MiiSelectorConfig& config,
                    const std::vector<std::string>& names) {
            AZEmulator* emulator = weak_self;
            if (emulator == nil) {
                g_mii_selector->Select(-1);
                return;
            }
            AZMiiRequest* request = [[AZMiiRequest alloc] init];
            request.title = ToNSString(config.title);
            NSMutableArray<NSString*>* list = [NSMutableArray array];
            for (const auto& name : names) {
                [list addObject:ToNSString(name)];
            }
            request.names = list;
            request.initialIndex = config.initially_selected_mii_index < names.size()
                                       ? static_cast<NSInteger>(config.initially_selected_mii_index)
                                       : 0;
            request.allowsCancel = config.enable_cancel_button;
            std::shared_ptr<IOSMiiSelector> keep = g_mii_selector;
            request.submit = ^(NSInteger index) {
              keep->Select(static_cast<int>(index));
            };
            dispatch_async(dispatch_get_main_queue(), ^{
              if (emulator.onMiiRequest) {
                  emulator.onMiiRequest(request);
              } else {
                  request.submit(request.initialIndex);
              }
            });
        });
    g_mii_selector = selector;
    Core::System::GetInstance().RegisterMiiSelector(selector);
}

- (void)registerKeyboardApplet {
    __weak AZEmulator* weak_self = self;
    auto keyboard = std::make_shared<IOSKeyboard>(
        [weak_self](const Frontend::KeyboardConfig& config, const std::string& error) {
            AZEmulator* emulator = weak_self;
            if (emulator == nil) {
                return;
            }
            AZKeyboardRequest* request = [[AZKeyboardRequest alloc] init];
            request.hintText = ToNSString(config.hint_text);
            request.errorMessage = error.empty() ? nil : ToNSString(error);
            const bool numeric = config.type == Frontend::KeyboardType::NumPad;
            request.numeric = numeric;
            request.multiline = config.multiline_mode;
            request.maxLength = numeric ? config.max_digits : config.max_text_length;

            // Button labels from the application when it provides them, defaults otherwise.
            std::vector<std::string> labels;
            switch (config.button_config) {
            case Frontend::ButtonConfig::Dual:
                labels = {Frontend::SWKBD_BUTTON_CANCEL, Frontend::SWKBD_BUTTON_OKAY};
                break;
            case Frontend::ButtonConfig::Triple:
                labels = {Frontend::SWKBD_BUTTON_CANCEL, Frontend::SWKBD_BUTTON_FORGOT,
                          Frontend::SWKBD_BUTTON_OKAY};
                break;
            default:
                labels = {Frontend::SWKBD_BUTTON_OKAY};
                break;
            }
            for (std::size_t i = 0; i < labels.size() && i < config.button_text.size(); i++) {
                if (!config.button_text[i].empty()) {
                    labels[i] = config.button_text[i];
                }
            }
            NSMutableArray<NSString*>* texts = [NSMutableArray array];
            for (const auto& label : labels) {
                [texts addObject:ToNSString(label)];
            }
            request.buttonTexts = texts;
            request.confirmButtonIndex = static_cast<NSInteger>(labels.size()) - 1;

            // The shared pointer to the keyboard is kept alive by the request.
            std::shared_ptr<IOSKeyboard> keep = g_keyboard;
            request.submit = ^NSString*(NSString* text, NSInteger button) {
              const std::string error = keep->Submit(text.UTF8String ?: "", static_cast<u8>(button));
              return error.empty() ? nil : ToNSString(error);
            };
            dispatch_async(dispatch_get_main_queue(), ^{
              if (emulator.onKeyboardRequest) {
                  emulator.onKeyboardRequest(request);
              } else {
                  // Nobody can show it: answer with an empty input so the game does not wait.
                  request.submit(@"", request.confirmButtonIndex);
              }
            });
        },
        [weak_self](const std::string& message) {
            AZEmulator* emulator = weak_self;
            if (emulator != nil && emulator.onNotice) {
                emulator.onNotice(@"Keyboard", ToNSString(message));
            }
        });
    g_keyboard = keyboard;
    Core::System::GetInstance().RegisterSoftwareKeyboard(keyboard);
}

// MARK: Save states

namespace {
/// Where the picture of a save state is kept, next to the state itself.
std::string StateThumbnailPath(u64 program_id, u64 movie_id, u32 slot) {
    const std::string directory = FileUtil::GetUserPath(FileUtil::UserPath::StatesDir);
    if (movie_id != 0) {
        return fmt::format("{}{:016X}.movie{:016X}.{:02d}.jpg", directory, program_id, movie_id,
                           slot);
    }
    return fmt::format("{}{:016X}.{:02d}.jpg", directory, program_id, slot);
}

/// Where a state itself is kept (the same name the core gives it, see core/savestate.cpp).
std::string StatePath(u64 program_id, u64 movie_id, u32 slot) {
    const std::string directory = FileUtil::GetUserPath(FileUtil::UserPath::StatesDir);
    if (movie_id != 0) {
        return fmt::format("{}{:016X}.movie{:016X}.{:02d}.cst", directory, program_id, movie_id,
                           slot);
    }
    return fmt::format("{}{:016X}.{:02d}.cst", directory, program_id, slot);
}

/// Takes a picture of the screens as they are now and writes it, small, as a JPEG.
void SaveStateThumbnail(const std::string& path) {
    Core::System& system = Core::System::GetInstance();
    auto& renderer = system.GPU().Renderer();
    const auto layout = Layout::FrameLayoutFromResolutionScale(renderer.GetResolutionScaleFactor());
    const u32 width = layout.width;
    const u32 height = layout.height;
    if (width == 0 || height == 0) {
        return;
    }
    auto pixels = std::make_shared<std::vector<u8>>(static_cast<std::size_t>(width) * height * 4);
    renderer.RequestScreenshot(
        pixels->data(),
        [pixels, width, height, path](bool) {
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
              CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
              CGDataProviderRef provider =
                  CGDataProviderCreateWithData(nullptr, pixels->data(), pixels->size(), nullptr);
              // The renderer writes BGRA bytes.
              CGImageRef full = CGImageCreate(
                  width, height, 8, 32, static_cast<std::size_t>(width) * 4, color_space,
                  kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, provider, nullptr,
                  false, kCGRenderingIntentDefault);
              if (full != nullptr) {
                  // Small: the longer side is 240 points.
                  const double scale = std::min(1.0, 240.0 / std::max(width, height));
                  const std::size_t target_width = std::max<std::size_t>(
                      1, static_cast<std::size_t>(std::lround(width * scale)));
                  const std::size_t target_height = std::max<std::size_t>(
                      1, static_cast<std::size_t>(std::lround(height * scale)));
                  CGContextRef context = CGBitmapContextCreate(
                      nullptr, target_width, target_height, 8, 0, color_space,
                      kCGImageAlphaNoneSkipLast);
                  if (context != nullptr) {
                      CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
                      CGContextDrawImage(context, CGRectMake(0, 0, target_width, target_height),
                                         full);
                      CGImageRef small = CGBitmapContextCreateImage(context);
                      if (small != nullptr) {
                          NSURL* url = [NSURL fileURLWithPath:ToNSString(path)];
                          CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
                              (__bridge CFURLRef)url, CFSTR("public.jpeg"), 1, nullptr);
                          if (destination != nullptr) {
                              NSDictionary* options = @{
                                  (__bridge NSString*)kCGImageDestinationLossyCompressionQuality :
                                      @0.75
                              };
                              CGImageDestinationAddImage(destination, small,
                                                         (__bridge CFDictionaryRef)options);
                              CGImageDestinationFinalize(destination);
                              CFRelease(destination);
                          }
                          CGImageRelease(small);
                      }
                      CGContextRelease(context);
                  }
                  CGImageRelease(full);
              }
              CGDataProviderRelease(provider);
              CGColorSpaceRelease(color_space);
            });
        },
        layout);
}
} // namespace

- (NSArray<AZSaveStateSlot*>*)saveStateSlots {
    const u64 program_id = RunningProgramId();
    if (program_id == 0) {
        return @[];
    }
    const u64 movie_id = Core::System::GetInstance().Movie().GetCurrentMovieID();
    const auto states = Core::ListSaveStates(program_id, movie_id);
    NSMutableArray<AZSaveStateSlot*>* slots = [NSMutableArray array];
    for (const auto& state : states) {
        if (state.slot >= Core::SaveStateSlotCount) {
            continue;
        }
        const bool loadable = state.status != Core::SaveStateInfo::ValidationStatus::BuildMismatch;
        const std::string thumbnail = StateThumbnailPath(program_id, movie_id, state.slot);
        NSString* thumbnail_path = FileUtil::Exists(thumbnail) ? ToNSString(thumbnail) : nil;
        [slots addObject:[[AZSaveStateSlot alloc]
                             initWithSlot:state.slot
                                     date:[NSDate dateWithTimeIntervalSince1970:state.time]
                                 loadable:loadable
                            thumbnailPath:thumbnail_path]];
    }
    [slots sortUsingComparator:^NSComparisonResult(AZSaveStateSlot* a, AZSaveStateSlot* b) {
      return [b.date compare:a.date];
    }];
    return slots;
}

- (BOOL)saveStateToSlot:(NSInteger)slot {
    if (RunningProgramId() == 0 || slot < 0 || slot >= Core::SaveStateSlotCount) {
        return NO;
    }
    const bool requested = Core::System::GetInstance().SendSignal(Core::System::Signal::Save,
                                                                  static_cast<u32>(slot));
    if (requested) {
        [self beginStateOperation];
        SaveStateThumbnail(StateThumbnailPath(
            RunningProgramId(), Core::System::GetInstance().Movie().GetCurrentMovieID(),
            static_cast<u32>(slot)));
    }
    return requested;
}

- (BOOL)loadStateFromSlot:(NSInteger)slot {
    if (RunningProgramId() == 0 || slot < 0 || slot >= Core::SaveStateSlotCount) {
        return NO;
    }
    const bool requested = Core::System::GetInstance().SendSignal(Core::System::Signal::Load,
                                                                  static_cast<u32>(slot));
    if (requested) {
        [self beginStateOperation];
    }
    return requested;
}

- (BOOL)deleteSaveStateInSlot:(NSInteger)slot {
    const u64 program_id = RunningProgramId();
    if (program_id == 0 || slot < 0 || slot >= Core::SaveStateSlotCount) {
        return NO;
    }
    const u64 movie_id = Core::System::GetInstance().Movie().GetCurrentMovieID();
    return [AZEmulator deleteSaveStateForProgram:program_id
                                           movie:movie_id
                                            slot:slot];
}

+ (NSArray<AZSaveStateEntry*>*)allSaveStates {
    NSString* directory = ToNSString(FileUtil::GetUserPath(FileUtil::UserPath::StatesDir));
    NSFileManager* manager = [NSFileManager defaultManager];
    NSArray<NSString*>* names = [manager contentsOfDirectoryAtPath:directory error:nil];
    // <program>.<slot>.cst, or <program>.movie<movie>.<slot>.cst
    NSRegularExpression* pattern = [NSRegularExpression
        regularExpressionWithPattern:@"^([0-9A-Fa-f]{16})(?:\\.movie([0-9A-Fa-f]{16}))?\\.([0-9]{2})\\.cst$"
                             options:0
                               error:nil];
    NSMutableArray<AZSaveStateEntry*>* entries = [NSMutableArray array];
    for (NSString* name in names) {
        NSTextCheckingResult* match =
            [pattern firstMatchInString:name options:0 range:NSMakeRange(0, name.length)];
        if (match == nil) {
            continue;
        }
        const u64 program_id = strtoull([name substringWithRange:[match rangeAtIndex:1]].UTF8String,
                                        nullptr, 16);
        u64 movie_id = 0;
        if ([match rangeAtIndex:2].location != NSNotFound) {
            movie_id = strtoull([name substringWithRange:[match rangeAtIndex:2]].UTF8String,
                                nullptr, 16);
        }
        const u32 slot = static_cast<u32>(
            [[name substringWithRange:[match rangeAtIndex:3]] integerValue]);

        const std::string state_path = StatePath(program_id, movie_id, slot);
        NSString* state = ToNSString(state_path);
        NSDictionary* attributes = [manager attributesOfItemAtPath:state error:nil];
        unsigned long long size = [attributes fileSize];
        NSDate* date = [attributes fileModificationDate] ?: [NSDate distantPast];
        BOOL loadable = NO;
        const Core::SaveStateInfo info = Core::GetSaveStateInfo(program_id, movie_id, slot);
        if (info.slot != std::numeric_limits<u32>::max()) {
            date = [NSDate dateWithTimeIntervalSince1970:info.time];
            loadable = info.status != Core::SaveStateInfo::ValidationStatus::BuildMismatch;
        }
        const std::string thumbnail = StateThumbnailPath(program_id, movie_id, slot);
        NSString* thumbnail_path = nil;
        if (FileUtil::Exists(thumbnail)) {
            thumbnail_path = ToNSString(thumbnail);
            size += [[manager attributesOfItemAtPath:thumbnail_path error:nil] fileSize];
        }
        [entries addObject:[[AZSaveStateEntry alloc] initWithProgramID:program_id
                                                               movieID:movie_id
                                                                  slot:slot
                                                                  date:date
                                                              loadable:loadable
                                                              fileSize:size
                                                         thumbnailPath:thumbnail_path]];
    }
    [entries sortUsingComparator:^NSComparisonResult(AZSaveStateEntry* a, AZSaveStateEntry* b) {
      return [b.date compare:a.date];
    }];
    return entries;
}

+ (BOOL)deleteSaveStateForProgram:(uint64_t)programID movie:(uint64_t)movieID slot:(NSInteger)slot {
    if (slot < 0 || slot >= Core::SaveStateSlotCount) {
        return NO;
    }
    const std::string state = StatePath(programID, movieID, static_cast<u32>(slot));
    const bool removed = !FileUtil::Exists(state) || FileUtil::Delete(state);
    const std::string thumbnail = StateThumbnailPath(programID, movieID, static_cast<u32>(slot));
    if (FileUtil::Exists(thumbnail)) {
        FileUtil::Delete(thumbnail);
    }
    return removed;
}

// MARK: Cheats

- (NSArray<AZCheat*>*)cheats {
    if (RunningProgramId() == 0) {
        return @[];
    }
    NSMutableArray<AZCheat*>* result = [NSMutableArray array];
    for (const auto& cheat : Core::System::GetInstance().CheatEngine().GetCheats()) {
        AZCheat* item = [[AZCheat alloc] init];
        item.name = ToNSString(cheat->GetName());
        item.code = ToNSString(cheat->GetCode());
        item.comments = ToNSString(cheat->GetComments());
        item.enabled = cheat->IsEnabled();
        [result addObject:item];
    }
    return result;
}

- (void)setCheatEnabled:(BOOL)enabled atIndex:(NSInteger)index {
    auto cheats = Core::System::GetInstance().CheatEngine().GetCheats();
    if (index < 0 || static_cast<std::size_t>(index) >= cheats.size()) {
        return;
    }
    cheats[index]->SetEnabled(enabled);
    SaveCheats();
}

- (void)addCheat:(AZCheat*)cheat {
    if (RunningProgramId() == 0) {
        return;
    }
    Core::System::GetInstance().CheatEngine().AddCheat(
        MakeCheat(cheat));
    SaveCheats();
}

- (void)replaceCheatAtIndex:(NSInteger)index with:(AZCheat*)cheat {
    auto& engine = Core::System::GetInstance().CheatEngine();
    if (RunningProgramId() == 0 || index < 0 ||
        static_cast<std::size_t>(index) >= engine.GetCheats().size()) {
        return;
    }
    engine.UpdateCheat(static_cast<std::size_t>(index), MakeCheat(cheat));
    SaveCheats();
}

- (void)removeCheatAtIndex:(NSInteger)index {
    auto& engine = Core::System::GetInstance().CheatEngine();
    if (RunningProgramId() == 0 || index < 0 ||
        static_cast<std::size_t>(index) >= engine.GetCheats().size()) {
        return;
    }
    engine.RemoveCheat(static_cast<std::size_t>(index));
    SaveCheats();
}

+ (NSArray<NSNumber*>*)invalidCheatLinesInCode:(NSString*)code {
    NSMutableArray<NSNumber*>* invalid = [NSMutableArray array];
    NSArray<NSString*>* lines = [code componentsSeparatedByCharactersInSet:
                                          [NSCharacterSet newlineCharacterSet]];
    NSInteger number = 0;
    for (NSString* line in lines) {
        number++;
        NSString* trimmed = [line stringByTrimmingCharactersInSet:
                                      [NSCharacterSet whitespaceCharacterSet]];
        if (trimmed.length == 0) {
            continue;
        }
        const Cheats::GatewayCheat::CheatLine parsed{std::string{trimmed.UTF8String}};
        if (!parsed.valid) {
            [invalid addObject:@(number)];
        }
    }
    return invalid;
}

// MARK: Camera

+ (void)setCameraOrientationLandscapeLeft:(BOOL)landscapeLeft {
    Camera::SetIOSCameraOrientation(landscapeLeft);
}

// MARK: Amiibo

- (AZAmiiboStatus)loadAmiiboAtPath:(NSString*)path {
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return AZAmiiboStatusNotRunning;
    }
    auto nfc = system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return AZAmiiboStatusFailed;
    }

    std::scoped_lock lock{system.Kernel().GetHLELock()};
    if (nfc->IsTagActive()) {
        return AZAmiiboStatusTagInUse;
    }
    if (!nfc->IsSearchingForAmiibos()) {
        return AZAmiiboStatusNotSearching;
    }
    return nfc->LoadAmiibo(std::string{path.UTF8String}) ? AZAmiiboStatusLoaded
                                                         : AZAmiiboStatusFailed;
}

- (void)removeAmiibo {
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }
    auto nfc = system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }
    std::scoped_lock lock{system.Kernel().GetHLELock()};
    nfc->RemoveAmiibo();
}

// MARK: Screenshot and speed

- (void)takeScreenshotToDirectory:(NSString*)directory
                       completion:(void (^)(NSString* _Nullable))completion {
    Core::System& system = Core::System::GetInstance();
    const u64 program_id = RunningProgramId();
    if (program_id == 0) {
        completion(nil);
        return;
    }
    auto& renderer = system.GPU().Renderer();
    const auto layout = Layout::FrameLayoutFromResolutionScale(renderer.GetResolutionScaleFactor());
    const u32 width = layout.width;
    const u32 height = layout.height;
    auto pixels = std::make_shared<std::vector<u8>>(static_cast<std::size_t>(width) * height * 4);

    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyy-MM-dd HH.mm.ss";
    formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    NSString* name = [NSString stringWithFormat:@"%016llX %@.png", program_id,
                                                [formatter stringFromDate:[NSDate date]]];
    NSString* path = [directory stringByAppendingPathComponent:name];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    // The renderer calls back from the emulation thread once the image is in the buffer, or never
    // when it can't take it. Encode off that thread and give up after a while.
    auto finished = std::make_shared<std::atomic<bool>>(false);
    renderer.RequestScreenshot(
        pixels->data(),
        [pixels, width, height, path, completion, finished](bool) {
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
              bool saved = false;
              CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
              CGDataProviderRef provider = CGDataProviderCreateWithData(
                  nullptr, pixels->data(), pixels->size(), nullptr);
              // The renderer writes BGRA bytes.
              CGImageRef image = CGImageCreate(
                  width, height, 8, 32, static_cast<std::size_t>(width) * 4, color_space,
                  kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, provider, nullptr,
                  false, kCGRenderingIntentDefault);
              if (image != nullptr) {
                  NSURL* url = [NSURL fileURLWithPath:path];
                  CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
                      (__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
                  if (destination != nullptr) {
                      CGImageDestinationAddImage(destination, image, nullptr);
                      saved = CGImageDestinationFinalize(destination);
                      CFRelease(destination);
                  }
                  CGImageRelease(image);
              }
              CGDataProviderRelease(provider);
              CGColorSpaceRelease(color_space);
              if (!finished->exchange(true)) {
                  dispatch_async(dispatch_get_main_queue(), ^{
                    completion(saved ? path : nil);
                  });
              }
            });
        },
        layout);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      if (!finished->exchange(true)) {
          completion(nil);
      }
    });
}

- (void)setFastForward:(BOOL)enabled {
    Settings::is_temporary_frame_limit = enabled;
    Settings::temporary_frame_limit = enabled ? Settings::values.turbo_limit.GetValue() : 0.0;
}

// MARK: Console profile

+ (AZConsoleProfile*)readConsoleProfile {
    AZConsoleProfile* profile = [[AZConsoleProfile alloc] init];
    auto& system = Core::System::GetInstance();
    auto cfg = Service::CFG::GetModule(system);
    profile.username = [NSString stringWithUTF8String:Common::UTF16ToUTF8(cfg->GetUsername()).c_str()] ?: @"";
    const auto [month, day] = cfg->GetBirthday();
    profile.birthMonth = month;
    profile.birthDay = day;
    profile.language = static_cast<NSInteger>(cfg->GetSystemLanguage());
    profile.country = cfg->GetCountryCode();
    profile.soundMode = static_cast<NSInteger>(cfg->GetSoundOutputMode());
    profile.playCoins = Service::PTM::Module::GetPlayCoins();
    return profile;
}

+ (BOOL)writeConsoleProfile:(AZConsoleProfile*)profile {
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        return NO;
    }
    auto cfg = Service::CFG::GetModule(system);
    cfg->SetUsername(Common::UTF8ToUTF16(profile.username.UTF8String));
    cfg->SetBirthday(static_cast<u8>(std::clamp<NSInteger>(profile.birthMonth, 1, 12)),
                     static_cast<u8>(std::clamp<NSInteger>(profile.birthDay, 1, 31)));
    cfg->SetSystemLanguage(static_cast<Service::CFG::SystemLanguage>(
        std::clamp<NSInteger>(profile.language, 0, 11)));
    cfg->SetSoundOutputMode(static_cast<Service::CFG::SoundOutputMode>(
        std::clamp<NSInteger>(profile.soundMode, 0, 2)));
    cfg->SetCountryCode(static_cast<u8>(std::clamp<NSInteger>(profile.country, 0, 255)));
    cfg->UpdateConfigNANDSavegame();
    Service::PTM::Module::SetPlayCoins(
        static_cast<u16>(std::clamp<NSInteger>(profile.playCoins, 0, 300)));
    return YES;
}

// MARK: Movies

static void (^g_movie_finished)(void) = nil;

- (void)setOnMovieFinished:(void (^)(void))block {
    g_movie_finished = [block copy];
}

- (void (^)(void))onMovieFinished {
    return g_movie_finished;
}

- (void)recordMovieToPath:(NSString*)path author:(NSString*)author {
    auto& pending = GetPendingMovie();
    std::scoped_lock lock{pending.mutex};
    pending.path = path.UTF8String;
    pending.author = author.UTF8String;
    pending.mode = 1;
}

- (void)playMovieFromPath:(NSString*)path {
    auto& pending = GetPendingMovie();
    std::scoped_lock lock{pending.mutex};
    pending.path = path.UTF8String;
    pending.author.clear();
    pending.mode = 2;
}

- (void)clearPendingMovie {
    auto& pending = GetPendingMovie();
    std::scoped_lock lock{pending.mutex};
    pending.mode = 0;
}

- (AZMovieMode)movieMode {
    switch (Core::System::GetInstance().Movie().GetPlayMode()) {
    case Core::Movie::PlayMode::Recording:
        return AZMovieModeRecording;
    case Core::Movie::PlayMode::Playing:
        return AZMovieModePlaying;
    case Core::Movie::PlayMode::MovieFinished:
        return AZMovieModeFinished;
    default:
        return AZMovieModeNone;
    }
}

- (void)stopMovie {
    Core::System::GetInstance().Movie().Shutdown();
}

- (void)saveMovie {
    auto& movie = Core::System::GetInstance().Movie();
    if (movie.GetPlayMode() == Core::Movie::PlayMode::Recording) {
        movie.SaveMovie();
    }
}

+ (AZMovieInfo*)infoOfMovieAtPath:(NSString*)path {
    return [[AZMovieInfo alloc] initWithPath:path];
}

// MARK: Artic

+ (BOOL)areOld3DSSystemFilesInstalled {
    return Core::AreSystemTitlesInstalled().first;
}

+ (BOOL)areNew3DSSystemFilesInstalled {
    return Core::AreSystemTitlesInstalled().second;
}

+ (void)uninstallSystemFilesForOld3DS:(BOOL)old3DS {
    Core::UninstallSystemFiles(old3DS ? Core::SystemTitleSet::Old3ds
                                      : Core::SystemTitleSet::New3ds);
}

// MARK: CIA installation

+ (void)compressGameAtPath:(NSString*)path
               toDirectory:(NSString*)directory
                  progress:(void (^)(double))progress
                completion:(void (^)(AZCompressStatus, NSString* _Nullable))completion {
    const std::string source{path.UTF8String};
    NSString* base = [[path lastPathComponent] stringByDeletingPathExtension];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      const auto finish = ^(AZCompressStatus status, NSString* output) {
        dispatch_async(dispatch_get_main_queue(), ^{
          completion(status, output);
        });
      };
      const auto info = Loader::GetLoaderFileInfo(source);
      if (!info.is_valid || !info.is_compress_supported) {
          finish(AZCompressStatusNotSupported, nil);
          return;
      }
      if (info.is_compressed) {
          finish(AZCompressStatusAlreadyCompressed, nil);
          return;
      }

      NSString* name = [base stringByAppendingPathExtension:
                                 [NSString stringWithUTF8String:
                                               info.recommended_compressed_extension.c_str()]];
      NSString* output = [directory stringByAppendingPathComponent:name];
      NSString* partial = [output stringByAppendingPathExtension:@"partial"];
      const std::string partial_path{partial.UTF8String};
      FileUtil::Delete(partial_path);

      auto last_percent = std::make_shared<std::atomic<int>>(-1);
      const bool ok = FileUtil::CompressZ3DSFile(
          source, partial_path, info.underlying_magic, info.default_compress_frame_size,
          [progress, last_percent](std::size_t written, std::size_t total) {
              if (total == 0) {
                  return;
              }
              const int percent = static_cast<int>(100.0 * static_cast<double>(written) /
                                                   static_cast<double>(total));
              if (last_percent->exchange(percent) == percent) {
                  return;
              }
              dispatch_async(dispatch_get_main_queue(), ^{
                progress(static_cast<double>(percent) / 100.0);
              });
          },
          info.default_metadata);
      NSFileManager* files = [NSFileManager defaultManager];
      if (!ok) {
          FileUtil::Delete(partial_path);
          finish(AZCompressStatusFailed, nil);
          return;
      }
      [files removeItemAtPath:output error:nil];
      NSError* error = nil;
      if (![files moveItemAtPath:partial toPath:output error:&error]) {
          FileUtil::Delete(partial_path);
          finish(AZCompressStatusFailed, nil);
          return;
      }
      finish(AZCompressStatusSuccess, output);
    });
}

+ (void)installCIAAtPath:(NSString*)path
                progress:(void (^)(double))progress
              completion:(void (^)(AZInstallStatus))completion {
    const std::string cia_path{path.UTF8String};
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      auto last_percent = std::make_shared<std::atomic<int>>(-1);
      const auto status = Service::AM::InstallCIA(
          cia_path, [progress, last_percent](std::size_t written, std::size_t total) {
              if (total == 0) {
                  return;
              }
              const int percent = static_cast<int>(100.0 * static_cast<double>(written) /
                                                   static_cast<double>(total));
              if (last_percent->exchange(percent) == percent) {
                  return;
              }
              dispatch_async(dispatch_get_main_queue(), ^{
                progress(static_cast<double>(percent) / 100.0);
              });
          });
      dispatch_async(dispatch_get_main_queue(), ^{
        completion(ToInstallStatus(status));
      });
    });
}

@end

@implementation AZMovieInfo
- (instancetype)initWithPath:(NSString*)path {
    if ((self = [super init])) {
        _author = @"";
        const auto& movie = Core::System::GetInstance().Movie();
        const std::string file{path.UTF8String};
        if (movie.ValidateMovie(file) != Core::Movie::ValidationResult::Invalid) {
            const auto metadata = movie.GetMovieMetadata(file);
            _isValid = YES;
            _programID = metadata.program_id;
            _author = [NSString stringWithUTF8String:metadata.author.c_str()] ?: @"";
            _rerecordCount = metadata.rerecord_count;
            _inputCount = metadata.input_count;
        }
    }
    return self;
}
@end

@implementation AZConsoleProfile
- (instancetype)init {
    if ((self = [super init])) {
        _username = @"";
    }
    return self;
}
@end
