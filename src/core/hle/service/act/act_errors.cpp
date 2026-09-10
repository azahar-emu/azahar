// Copyright 2024-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/act/act_errors.h"

namespace Service::ACT {

u32 GetACTErrorCode(Result result) {
    u32 error_code = ErrCodes::Unknown;
    if (result.module == ErrorModule::ACT) {
        switch (result.description) {
        case ErrDescriptions::MySuccess:
            error_code = ErrCodes::MySuccess;
            break;
        case ErrDescriptions::MailAddressNotConfirmed:
            error_code = ErrCodes::MailAddressNotConfirmed;
            break;
        case ErrDescriptions::LibraryError:
            error_code = ErrCodes::LibraryError;
            break;
        case ErrDescriptions::NotInitialized:
            error_code = ErrCodes::NotInitialized;
            break;
        case ErrDescriptions::AlreadyInitialized:
            error_code = ErrCodes::AlreadyInitialized;
            break;
        case ErrDescriptions::AcStatusDisconnected:
            error_code = ErrCodes::AcStatusDisconnected;
            break;
        case ErrDescriptions::ErrDesc104:
            error_code = ErrCodes::ErrCode225104;
            break;
        case ErrDescriptions::Busy:
            error_code = ErrCodes::Busy;
            break;
        case ErrDescriptions::ErrDesc112:
            error_code = ErrCodes::ErrCode225112;
            break;
        case ErrDescriptions::NotImplemented:
            error_code = ErrCodes::NotImplemented;
            break;
        case ErrDescriptions::Deprecated:
            error_code = ErrCodes::Deprecated;
            break;
        case ErrDescriptions::DevelopmentOnly:
            error_code = ErrCodes::DevelopmentOnly;
            break;
        case ErrDescriptions::InvalidArgument:
            error_code = ErrCodes::InvalidArgument;
            break;
        case ErrDescriptions::InvalidPointer:
            error_code = ErrCodes::InvalidPointer;
            break;
        case ErrDescriptions::OutOfRange:
            error_code = ErrCodes::OutOfRange;
            break;
        case ErrDescriptions::InvalidSize:
            error_code = ErrCodes::InvalidSize;
            break;
        case ErrDescriptions::InvalidFormat:
            error_code = ErrCodes::InvalidFormat;
            break;
        case ErrDescriptions::InvalidHandle:
            error_code = ErrCodes::InvalidHandle;
            break;
        case ErrDescriptions::InvalidValue:
            error_code = ErrCodes::InvalidValue;
            break;
        case ErrDescriptions::InternalError:
            error_code = ErrCodes::InternalError;
            break;
        case ErrDescriptions::EndOfStream:
            error_code = ErrCodes::EndOfStream;
            break;
        case ErrDescriptions::FileError:
            error_code = ErrCodes::FileError;
            break;
        case ErrDescriptions::FileNotFound:
            error_code = ErrCodes::FileNotFound;
            break;
        case ErrDescriptions::FileVersionMismatch:
            error_code = ErrCodes::FileVersionMismatch;
            break;
        case ErrDescriptions::FileIOError:
            error_code = ErrCodes::FileIOError;
            break;
        case ErrDescriptions::FileTypeMismatch:
            error_code = ErrCodes::FileTypeMismatch;
            break;
        case ErrDescriptions::ErrDesc315:
            error_code = ErrCodes::ErrCode225315;
            break;
        case ErrDescriptions::OutOfResource:
            error_code = ErrCodes::OutOfResource;
            break;
        case ErrDescriptions::ShortOfBuffer:
            error_code = ErrCodes::ShortOfBuffer;
            break;
        case ErrDescriptions::OutOfMemory:
            error_code = ErrCodes::OutOfMemory;
            break;
        case ErrDescriptions::OutOfGlobalHeap:
            error_code = ErrCodes::OutOfGlobalHeap;
            break;
        case ErrDescriptions::ErrDesc350:
            error_code = ErrCodes::ErrCode225350;
            break;
        case ErrDescriptions::ErrDesc351:
            error_code = ErrCodes::ErrCode225351;
            break;
        case ErrDescriptions::ErrDesc352:
            error_code = ErrCodes::ErrCode225352;
            break;
        case ErrDescriptions::ErrDesc360:
            error_code = ErrCodes::ErrCode225360;
            break;
        case ErrDescriptions::ErrDesc361:
            error_code = ErrCodes::ErrCode225361;
            break;
        case ErrDescriptions::ErrDesc362:
            error_code = ErrCodes::ErrCode225362;
            break;
        case ErrDescriptions::ErrDesc363:
            error_code = ErrCodes::ErrCode225363;
            break;
        case ErrDescriptions::AccountManagementError:
            error_code = ErrCodes::AccountManagementError;
            break;
        case ErrDescriptions::AccountNotFound:
            error_code = ErrCodes::AccountNotFound;
            break;
        case ErrDescriptions::SlotsFull:
            error_code = ErrCodes::SlotsFull;
            break;
        case ErrDescriptions::AccountNotLoaded:
            error_code = ErrCodes::AccountNotLoaded;
            break;
        case ErrDescriptions::AccountAlreadyLoaded:
            error_code = ErrCodes::AccountAlreadyLoaded;
            break;
        case ErrDescriptions::AccountLocked:
            error_code = ErrCodes::AccountLocked;
            break;
        case ErrDescriptions::NotNetworkAccount:
            error_code = ErrCodes::NotNetworkAccount;
            break;
        case ErrDescriptions::NotLocalAccount:
            error_code = ErrCodes::NotLocalAccount;
            break;
        case ErrDescriptions::AccountNotCommited:
            error_code = ErrCodes::AccountCommited;
            break;
        case ErrDescriptions::ErrDesc431:
            error_code = ErrCodes::ErrCode225431;
            break;
        case ErrDescriptions::ErrDesc432:
            error_code = ErrCodes::ErrCode225432;
            break;
        case ErrDescriptions::ErrDesc433:
            error_code = ErrCodes::ErrCode225433;
            break;
        case ErrDescriptions::ErrDesc451:
            error_code = ErrCodes::ErrCode221101;
            break;
        case ErrDescriptions::AuthenticationError:
            error_code = ErrCodes::AuthenticationError;
            break;
        case ErrDescriptions::HttpError:
            error_code = ErrCodes::HttpError;
            break;
        case ErrDescriptions::ErrDesc502:
            error_code = ErrCodes::ErrCode225502;
            break;
        case ErrDescriptions::ErrDesc503:
            error_code = ErrCodes::ErrCode225503;
            break;
        case ErrDescriptions::ErrDesc504:
            error_code = ErrCodes::ErrCode225504;
            break;
        case ErrDescriptions::ErrDesc505:
            error_code = ErrCodes::ErrCode225505;
            break;
        case ErrDescriptions::ErrDesc506:
            error_code = ErrCodes::ErrCode225506;
            break;
        case ErrDescriptions::ErrDesc507:
            error_code = ErrCodes::ErrCode225507;
            break;
        case ErrDescriptions::ErrDesc508:
            error_code = ErrCodes::ErrCode225508;
            break;
        case ErrDescriptions::ErrDesc509:
            error_code = ErrCodes::ErrCode225509;
            break;
        case ErrDescriptions::ErrDesc510:
            error_code = ErrCodes::ErrCode225510;
            break;
        case ErrDescriptions::ErrDesc511:
            error_code = ErrCodes::ErrCode225511;
            break;
        case ErrDescriptions::ErrDesc512:
            error_code = ErrCodes::ErrCode225512;
            break;
        case ErrDescriptions::ErrDesc513:
            error_code = ErrCodes::ErrCode225513;
            break;
        case ErrDescriptions::ErrDesc514:
            error_code = ErrCodes::ErrCode225514;
            break;
        case ErrDescriptions::ErrDesc515:
            error_code = ErrCodes::ErrCode225515;
            break;
        case ErrDescriptions::ErrDesc516:
            error_code = ErrCodes::ErrCode225516;
            break;
        case ErrDescriptions::ErrDesc517:
            error_code = ErrCodes::ErrCode225517;
            break;
        case ErrDescriptions::ErrDesc518:
            error_code = ErrCodes::ErrCode225518;
            break;
        case ErrDescriptions::ErrDesc519:
            error_code = ErrCodes::ErrCode225519;
            break;
        case ErrDescriptions::ErrDesc520:
            error_code = ErrCodes::ErrCode225520;
            break;
        case ErrDescriptions::ErrDesc521:
            error_code = ErrCodes::ErrCode225521;
            break;
        case ErrDescriptions::ErrDesc522:
            error_code = ErrCodes::ErrCode225522;
            break;
        case ErrDescriptions::ErrDesc523:
            error_code = ErrCodes::ErrCode225523;
            break;
        case ErrDescriptions::ErrDesc524:
            error_code = ErrCodes::ErrCode225524;
            break;
        case ErrDescriptions::ErrDesc525:
            error_code = ErrCodes::ErrCode225525;
            break;
        case ErrDescriptions::ErrDesc526:
            error_code = ErrCodes::ErrCode225526;
            break;
        case ErrDescriptions::ErrDesc527:
            error_code = ErrCodes::ErrCode225527;
            break;
        case ErrDescriptions::ErrDesc528:
            error_code = ErrCodes::ErrCode225528;
            break;
        case ErrDescriptions::ErrDesc529:
            error_code = ErrCodes::ErrCode225529;
            break;
        case ErrDescriptions::ErrDesc530:
            error_code = ErrCodes::ErrCode225530;
            break;
        case ErrDescriptions::ErrDesc531:
            error_code = ErrCodes::ErrCode225531;
            break;
        case ErrDescriptions::ErrDesc532:
            error_code = ErrCodes::ErrCode225532;
            break;
        case ErrDescriptions::ErrDesc533:
            error_code = ErrCodes::ErrCode225533;
            break;
        case ErrDescriptions::ErrDesc534:
            error_code = ErrCodes::ErrCode225534;
            break;
        case ErrDescriptions::ErrDesc535:
            error_code = ErrCodes::ErrCode225535;
            break;
        case ErrDescriptions::ErrDesc536:
            error_code = ErrCodes::ErrCode225536;
            break;
        case ErrDescriptions::ErrDesc537:
            error_code = ErrCodes::ErrCode225537;
            break;
        case ErrDescriptions::ErrDesc538:
            error_code = ErrCodes::ErrCode225538;
            break;
        case ErrDescriptions::ErrDesc539:
            error_code = ErrCodes::ErrCode225539;
            break;
        case ErrDescriptions::ErrDesc540:
            error_code = ErrCodes::ErrCode225540;
            break;
        case ErrDescriptions::ErrDesc541:
            error_code = ErrCodes::ErrCode225541;
            break;
        case ErrDescriptions::ErrDesc542:
            error_code = ErrCodes::ErrCode225542;
            break;
        case ErrDescriptions::ErrDesc543:
            error_code = ErrCodes::ErrCode225543;
            break;
        case ErrDescriptions::ErrDesc544:
            error_code = ErrCodes::ErrCode225544;
            break;
        case ErrDescriptions::ErrDesc545:
            error_code = ErrCodes::ErrCode225545;
            break;
        case ErrDescriptions::ErrDesc546:
            error_code = ErrCodes::ErrCode225546;
            break;
        case ErrDescriptions::ErrDesc547:
            error_code = ErrCodes::ErrCode225547;
            break;
        case ErrDescriptions::ErrDesc548:
            error_code = ErrCodes::ErrCode225548;
            break;
        case ErrDescriptions::ErrDesc549:
            error_code = ErrCodes::ErrCode225549;
            break;
        case ErrDescriptions::ErrDesc550:
            error_code = ErrCodes::ErrCode225550;
            break;
        case ErrDescriptions::ErrDesc551:
            error_code = ErrCodes::ErrCode225551;
            break;
        case ErrDescriptions::ErrDesc552:
            error_code = ErrCodes::ErrCode225552;
            break;
        case ErrDescriptions::ErrDesc553:
            error_code = ErrCodes::ErrCode225553;
            break;
        case ErrDescriptions::RequestError:
            error_code = ErrCodes::RequestError;
            break;
        case ErrDescriptions::BadFormatParameter:
            error_code = ErrCodes::BadFormatParameter;
            break;
        case ErrDescriptions::BadFormatRequest:
            error_code = ErrCodes::BadFormatRequest;
            break;
        case ErrDescriptions::RequestParameterMissing:
            error_code = ErrCodes::RequestParameterMissing;
            break;
        case ErrDescriptions::WrongHttpMethod:
            error_code = ErrCodes::WrongHttpMethod;
            break;
        case ErrDescriptions::ResponseError:
            error_code = ErrCodes::ResponseError;
            break;
        case ErrDescriptions::BadFormatResponse:
            error_code = ErrCodes::BadFormatResponse;
            break;
        case ErrDescriptions::ResponseItemMissing:
            error_code = ErrCodes::ResponseItemMissing;
            break;
        case ErrDescriptions::ResponseTooLarge:
            error_code = ErrCodes::ResponseTooLarge;
            break;
        case ErrDescriptions::InvalidCommonParameter:
            error_code = ErrCodes::InvalidCommonParameter;
            break;
        case ErrDescriptions::InvalidPlatformId:
            error_code = ErrCodes::InvalidPlatformId;
            break;
        case ErrDescriptions::UnauthorizedDevice:
            error_code = ErrCodes::UnauthorizedDevice;
            break;
        case ErrDescriptions::InvalidSerialId:
            error_code = ErrCodes::InvalidSerialId;
            break;
        case ErrDescriptions::InvalidMacAddress:
            error_code = ErrCodes::InvalidMacAddress;
            break;
        case ErrDescriptions::InvalidRegion:
            error_code = ErrCodes::InvalidRegion;
            break;
        case ErrDescriptions::InvalidCountry:
            error_code = ErrCodes::InvalidCountry;
            break;
        case ErrDescriptions::InvalidLanguage:
            error_code = ErrCodes::InvalidLanguage;
            break;
        case ErrDescriptions::UnauthorizedClient:
            error_code = ErrCodes::UnauthorizedClient;
            break;
        case ErrDescriptions::DeviceIdEmpty:
            error_code = ErrCodes::DeviceIdEmpty;
            break;
        case ErrDescriptions::SerialIdEmpty:
            error_code = ErrCodes::SerialIdEmpty;
            break;
        case ErrDescriptions::PlatformIdEmpty:
            error_code = ErrCodes::PlatformIdEmpty;
            break;
        case ErrDescriptions::InvalidUniqueId:
            error_code = ErrCodes::InvalidUniqueId;
            break;
        case ErrDescriptions::InvalidClientId:
            error_code = ErrCodes::InvalidClientId;
            break;
        case ErrDescriptions::InvalidClientKey:
            error_code = ErrCodes::InvalidClientKey;
            break;
        case ErrDescriptions::InvalidNexClientId:
            error_code = ErrCodes::InvalidNexClientId;
            break;
        case ErrDescriptions::InvalidGameServerId:
            error_code = ErrCodes::InvalidGameServerId;
            break;
        case ErrDescriptions::GameServerIdEnvironmentNotFound:
            error_code = ErrCodes::GameServerIdEnvironmentNotFound;
            break;
        case ErrDescriptions::GameServerIdUniqueIdNotLinked:
            error_code = ErrCodes::GameServerIdUniqueIdNotLinked;
            break;
        case ErrDescriptions::ClientIdUniqueIdNotLinked:
            error_code = ErrCodes::ClientIdUniqueIdNotLinked;
            break;
        case ErrDescriptions::DeviceMismatch:
            error_code = ErrCodes::DeviceMismatch;
            break;
        case ErrDescriptions::CountryMismatch:
            error_code = ErrCodes::CountryMismatch;
            break;
        case ErrDescriptions::EulaNotAccepted:
            error_code = ErrCodes::EulaNotAccepted;
            break;
        case ErrDescriptions::UpdateRequired:
            error_code = ErrCodes::UpdateRequired;
            break;
        case ErrDescriptions::SystemUpdateRequired:
            error_code = ErrCodes::SystemUpdateRequired;
            break;
        case ErrDescriptions::ApplicationUpdateRequired:
            error_code = ErrCodes::ApplicationUpdateRequired;
            break;
        case ErrDescriptions::UnauthorizedRequest:
            error_code = ErrCodes::UnauthorizedRequest;
            break;
        case ErrDescriptions::RequestForbidden:
            error_code = ErrCodes::RequestForbidden;
            break;
        case ErrDescriptions::ResourceNotFound:
            error_code = ErrCodes::ResourceNotFound;
            break;
        case ErrDescriptions::PidNotFound:
            error_code = ErrCodes::PidNotFound;
            break;
        case ErrDescriptions::NexAccountNotFound:
            error_code = ErrCodes::NexAccountNotFound;
            break;
        case ErrDescriptions::GenerateTokenFailure:
            error_code = ErrCodes::GenerateTokenFailure;
            break;
        case ErrDescriptions::RequestNotFound:
            error_code = ErrCodes::RequestNotFound;
            break;
        case ErrDescriptions::MasterPinNotFound:
            error_code = ErrCodes::MasterPinNotFound;
            break;
        case ErrDescriptions::MailTextNotFound:
            error_code = ErrCodes::MailTextNotFound;
            break;
        case ErrDescriptions::SendMailFailure:
            error_code = ErrCodes::SendMailFailure;
            break;
        case ErrDescriptions::ApprovalIdNotFound:
            error_code = ErrCodes::ApprovalIdNotFound;
            break;
        case ErrDescriptions::InvalidEulaParameter:
            error_code = ErrCodes::InvalidEulaParameter;
            break;
        case ErrDescriptions::InvalidEulaCountry:
            error_code = ErrCodes::InvalidEulaCountry;
            break;
        case ErrDescriptions::InvalidEulaCountryAndVersion:
            error_code = ErrCodes::InvalidEulaCountryAndVersion;
            break;
        case ErrDescriptions::EulaNotFound:
            error_code = ErrCodes::EulaNotFound;
            break;
        case ErrDescriptions::PhraseNotAcceptable:
            error_code = ErrCodes::PhraseNotAcceptable;
            break;
        case ErrDescriptions::AccountIdAlreadyExists:
            error_code = ErrCodes::AccountIdAlreadyExists;
            break;
        case ErrDescriptions::AccountIdNotAcceptable:
            error_code = ErrCodes::AccountIdNotAcceptable;
            break;
        case ErrDescriptions::AccountPasswordNotAcceptable:
            error_code = ErrCodes::AccountPasswordNotAcceptable;
            break;
        case ErrDescriptions::MiiNameNotAcceptable:
            error_code = ErrCodes::MiiNameNotAcceptable;
            break;
        case ErrDescriptions::MailAddressNotAcceptable:
            error_code = ErrCodes::MailAddressNotAcceptable;
            break;
        case ErrDescriptions::AccountIdFormatInvalid:
            error_code = ErrCodes::AccountIdFormatInvalid;
            break;
        case ErrDescriptions::AccountIdPasswordSame:
            error_code = ErrCodes::AccountIdPasswordSame;
            break;
        case ErrDescriptions::AccountIdCharNotAcceptable:
            error_code = ErrCodes::AccountIdCharNotAcceptable;
            break;
        case ErrDescriptions::AccountIdSuccessiveSymbol:
            error_code = ErrCodes::AccountIdSuccessiveSymbol;
            break;
        case ErrDescriptions::AccountIdSymbolPositionNotAcceptable:
            error_code = ErrCodes::AccountIdSymbolPositionNotAcceptable;
            break;
        case ErrDescriptions::AccountIdTooManyDigit:
            error_code = ErrCodes::AccountIdTooManyDigit;
            break;
        case ErrDescriptions::AccountPasswordCharNotAcceptable:
            error_code = ErrCodes::AccountPasswordCharNotAcceptable;
            break;
        case ErrDescriptions::AccountPasswordTooFewCharTypes:
            error_code = ErrCodes::AccountPasswordTooFewCharTypes;
            break;
        case ErrDescriptions::AccountPasswordSuccessiveSameChar:
            error_code = ErrCodes::AccountPasswordSuccessiveSameChar;
            break;
        case ErrDescriptions::MailAddressDomainNameNotAcceptable:
            error_code = ErrCodes::MailAddressDomainNameNotAcceptable;
            break;
        case ErrDescriptions::MailAddressDomainNameNotResolved:
            error_code = ErrCodes::MailAddressDomainNameNotResolved;
            break;
        case ErrDescriptions::ErrDesc787:
            error_code = ErrCodes::ErrCode222587;
            break;
        case ErrDescriptions::ReachedAssociationLimit:
            error_code = ErrCodes::ReachedAssociationLimit;
            break;
        case ErrDescriptions::ReachedRegistrationLimit:
            error_code = ErrCodes::ReachedRegistrationLimit;
            break;
        case ErrDescriptions::CoppaNotAccepted:
            error_code = ErrCodes::CoppaNotAccepted;
            break;
        case ErrDescriptions::ParentalControlsRequired:
            error_code = ErrCodes::ParentalControlsRequired;
            break;
        case ErrDescriptions::MiiNotRegistered:
            error_code = ErrCodes::MiiNotRegistered;
            break;
        case ErrDescriptions::DeviceEulaCountryMismatch:
            error_code = ErrCodes::DeviceEulaCountryMismatch;
            break;
        case ErrDescriptions::PendingMigration:
            error_code = ErrCodes::PendingMigration;
            break;
        case ErrDescriptions::WrongUserInput:
            error_code = ErrCodes::WrongUserInput;
            break;
        case ErrDescriptions::WrongAccountPassword:
            error_code = ErrCodes::WrongAccountPassword;
            break;
        case ErrDescriptions::WrongMailAddress:
            error_code = ErrCodes::WrongMailAddress;
            break;
        case ErrDescriptions::WrongAccountPasswordOrMailAddress:
            error_code = ErrCodes::WrongAccountPasswordOrMailAddress;
            break;
        case ErrDescriptions::WrongConfirmationCode:
            error_code = ErrCodes::WrongConfirmationCode;
            break;
        case ErrDescriptions::WrongBirthDateOrMailAddress:
            error_code = ErrCodes::WrongBirthDateOrMailAddress;
            break;
        case ErrDescriptions::WrongAccountMail:
            error_code = ErrCodes::WrongAccountMail;
            break;
        case ErrDescriptions::AccountAlreadyDeleted:
            error_code = ErrCodes::AccountAlreadyDeleted;
            break;
        case ErrDescriptions::AccountIdChanged:
            error_code = ErrCodes::AccountIdChanged;
            break;
        case ErrDescriptions::AuthenticationLocked:
            error_code = ErrCodes::AuthenticationLocked;
            break;
        case ErrDescriptions::DeviceInactive:
            error_code = ErrCodes::DeviceInactive;
            break;
        case ErrDescriptions::CoppaAgreementCanceled:
            error_code = ErrCodes::CoppaAgreementCanceled;
            break;
        case ErrDescriptions::DomainAccountAlreadyExists:
            error_code = ErrCodes::DomainAccountAlreadyExists;
            break;
        case ErrDescriptions::AccountTokenExpired:
            error_code = ErrCodes::AccountTokenExpired;
            break;
        case ErrDescriptions::InvalidAccountToken:
            error_code = ErrCodes::InvalidAccountToken;
            break;
        case ErrDescriptions::AuthenticationRequired:
            error_code = ErrCodes::AuthenticationRequired;
            break;
        case ErrDescriptions::ErrDesc844:
            error_code = ErrCodes::ErrCode225844;
            break;
        case ErrDescriptions::ConfirmationCodeExpired:
            error_code = ErrCodes::ConfirmationCodeExpired;
            break;
        case ErrDescriptions::MailAddressNotValidated:
            error_code = ErrCodes::MailAddressNotValidated;
            break;
        case ErrDescriptions::ExcessiveMailSendRequest:
            error_code = ErrCodes::ExcessiveMailSendRequest;
            break;
        case ErrDescriptions::CreditCardError:
            error_code = ErrCodes::CreditCardError;
            break;
        case ErrDescriptions::CreditCardGeneralFailure:
            error_code = ErrCodes::CreditCardGeneralFailure;
            break;
        case ErrDescriptions::CreditCardDeclined:
            error_code = ErrCodes::CreditCardDeclined;
            break;
        case ErrDescriptions::CreditCardBlacklisted:
            error_code = ErrCodes::CreditCardBlacklisted;
            break;
        case ErrDescriptions::InvalidCreditCardNumber:
            error_code = ErrCodes::InvalidCreditCardNumber;
            break;
        case ErrDescriptions::InvalidCreditCardDate:
            error_code = ErrCodes::InvalidCreditCardDate;
            break;
        case ErrDescriptions::InvalidCreditCardPin:
            error_code = ErrCodes::InvalidCreditCardPin;
            break;
        case ErrDescriptions::InvalidPostalCode:
            error_code = ErrCodes::InvalidPostalCode;
            break;
        case ErrDescriptions::InvalidLocation:
            error_code = ErrCodes::InvalidLocation;
            break;
        case ErrDescriptions::CreditCardDateExpired:
            error_code = ErrCodes::CreditCardDateExpired;
            break;
        case ErrDescriptions::CreditCardNumberWrong:
            error_code = ErrCodes::CreditCardNumberWrong;
            break;
        case ErrDescriptions::CreditCardPinWrong:
            error_code = ErrCodes::CreditCardPinWrong;
            break;
        case ErrDescriptions::Banned:
            error_code = ErrCodes::Banned;
            break;
        case ErrDescriptions::BannedAccount:
            error_code = ErrCodes::BannedAccount;
            break;
        case ErrDescriptions::BannedAccountAll:
            error_code = ErrCodes::BannedAccountAll;
            break;
        case ErrDescriptions::BannedAccountInApplication:
            error_code = ErrCodes::BannedAccountInApplication;
            break;
        case ErrDescriptions::BannedAccountInNexService:
            error_code = ErrCodes::BannedAccountInNexService;
            break;
        case ErrDescriptions::BannedAccountInIndependentService:
            error_code = ErrCodes::BannedAccountInIndependentService;
            break;
        case ErrDescriptions::BannedDevice:
            error_code = ErrCodes::BannedDevice;
            break;
        case ErrDescriptions::BannedDeviceAll:
            error_code = ErrCodes::BannedDeviceAll;
            break;
        case ErrDescriptions::BannedDeviceInApplication:
            error_code = ErrCodes::BannedDeviceInApplication;
            break;
        case ErrDescriptions::BannedDeviceInNexService:
            error_code = ErrCodes::BannedDeviceInNexService;
            break;
        case ErrDescriptions::BannedDeviceInIndependentService:
            error_code = ErrCodes::BannedDeviceInIndependentService;
            break;
        case ErrDescriptions::BannedAccountTemporarily:
            error_code = ErrCodes::BannedAccountTemporarily;
            break;
        case ErrDescriptions::BannedAccountAllTemporarily:
            error_code = ErrCodes::BannedAccountAllTemporarily;
            break;
        case ErrDescriptions::BannedAccountInApplicationTemporarily:
            error_code = ErrCodes::BannedAccountInApplicationTemporarily;
            break;
        case ErrDescriptions::BannedAccountInNexServiceTemporarily:
            error_code = ErrCodes::BannedAccountInNexServiceTemporarily;
            break;
        case ErrDescriptions::BannedAccountInIndependentServiceTemporarily:
            error_code = ErrCodes::BannedAccountInIndependentServiceTemporarily;
            break;
        case ErrDescriptions::BannedDeviceTemporarily:
            error_code = ErrCodes::BannedDeviceTemporarily;
            break;
        case ErrDescriptions::BannedDeviceAllTemporarily:
            error_code = ErrCodes::BannedDeviceAllTemporarily;
            break;
        case ErrDescriptions::BannedDeviceInApplicationTemporarily:
            error_code = ErrCodes::BannedDeviceInApplicationTemporarily;
            break;
        case ErrDescriptions::BannedDeviceInNexServiceTemporarily:
            error_code = ErrCodes::BannedDeviceInNexServiceTemporarily;
            break;
        case ErrDescriptions::BannedDeviceInIndependentServiceTemporarily:
            error_code = ErrCodes::BannedDeviceInIndependentServiceTemporarily;
            break;
        case ErrDescriptions::ServiceNotProvided:
            error_code = ErrCodes::ServiceNotProvided;
            break;
        case ErrDescriptions::UnderMaintenance:
            error_code = ErrCodes::UnderMaintenance;
            break;
        case ErrDescriptions::ServiceClosed:
            error_code = ErrCodes::ServiceClosed;
            break;
        case ErrDescriptions::NintendoNetworkClosed:
            error_code = ErrCodes::NintendoNetworkClosed;
            break;
        case ErrDescriptions::NotProvidedCountry:
            error_code = ErrCodes::NotProvidedCountry;
            break;
        case ErrDescriptions::RestrictionError:
            error_code = ErrCodes::RestrictionError;
            break;
        case ErrDescriptions::RestrictedByAge:
            error_code = ErrCodes::RestrictedByAge;
            break;
        case ErrDescriptions::RestrictedByParentalControls:
            error_code = ErrCodes::RestrictedByParentalControls;
            break;
        case ErrDescriptions::OnGameInternetCommunicationRestricted:
            error_code = ErrCodes::OnGameInternetCommunicationRestricted;
            break;
        case ErrDescriptions::InternalServerError:
            error_code = ErrCodes::InternalServerError;
            break;
        case ErrDescriptions::UnknownServerError:
            error_code = ErrCodes::UnknownServerError;
            break;
        case ErrDescriptions::UnauthenticatedAfterSalvage:
            error_code = ErrCodes::UnauthenticatedAfterSalvage;
            break;
        case ErrDescriptions::AuthenticationFailureUnknown:
            error_code = ErrCodes::AuthenticationFailureUnknown;
            break;
        }
    }

    return error_code;
}

} // namespace Service::ACT
