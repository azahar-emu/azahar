// Copyright 2024-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::ACT {

namespace ErrDescriptions {
enum {
    MySuccess = 0,
    MailAddressNotConfirmed = 1,

    // Library errors
    LibraryError = 100,
    NotInitialized = 101,
    AlreadyInitialized = 102,
    AcStatusDisconnected = 103,
    ErrDesc104 = 104,
    Busy = 111,
    ErrDesc112 = 112,
    NotImplemented = 191,
    Deprecated = 192,
    DevelopmentOnly = 193,

    InvalidArgument = 200,
    InvalidPointer = 201,
    OutOfRange = 202,
    InvalidSize = 203,
    InvalidFormat = 204,
    InvalidHandle = 205,
    InvalidValue = 206,

    InternalError = 300,
    EndOfStream = 301,
    FileError = 310,
    FileNotFound = 311,
    FileVersionMismatch = 312,
    FileIOError = 313,
    FileTypeMismatch = 314,
    ErrDesc315 = 315,

    OutOfResource = 330,
    ShortOfBuffer = 331,
    OutOfMemory = 340,
    OutOfGlobalHeap = 341,

    ErrDesc350 = 350,
    ErrDesc351 = 351,
    ErrDesc352 = 352,
    ErrDesc360 = 360,
    ErrDesc361 = 361,
    ErrDesc362 = 362,
    ErrDesc363 = 363,

    // Account management errors
    AccountManagementError = 400,
    AccountNotFound = 401,
    SlotsFull = 402,
    AccountNotLoaded = 411,
    AccountAlreadyLoaded = 412,
    AccountLocked = 413,
    NotNetworkAccount = 421,
    NotLocalAccount = 422,
    AccountNotCommited = 423,

    ErrDesc431 = 431,
    ErrDesc432 = 432,
    ErrDesc433 = 433,

    ErrDesc451 = 451,

    AuthenticationError = 500,

    // HTTP errors
    HttpError = 501,
    ErrDesc502 = 502,
    ErrDesc503 = 503,
    ErrDesc504 = 504,
    ErrDesc505 = 505,
    ErrDesc506 = 506,
    ErrDesc507 = 507,
    ErrDesc508 = 508,
    ErrDesc509 = 509,
    ErrDesc510 = 510,
    ErrDesc511 = 511,
    ErrDesc512 = 512,
    ErrDesc513 = 513,
    ErrDesc514 = 514,
    ErrDesc515 = 515,
    ErrDesc516 = 516,
    ErrDesc517 = 517,
    ErrDesc518 = 518,
    ErrDesc519 = 519,
    ErrDesc520 = 520,
    ErrDesc521 = 521,
    ErrDesc522 = 522,
    ErrDesc523 = 523,
    ErrDesc524 = 524,
    ErrDesc525 = 525,
    ErrDesc526 = 526,
    ErrDesc527 = 527,
    ErrDesc528 = 528,
    ErrDesc529 = 529,
    ErrDesc530 = 530,
    ErrDesc531 = 531,
    ErrDesc532 = 532,
    ErrDesc533 = 533,
    ErrDesc534 = 534,
    ErrDesc535 = 535,
    ErrDesc536 = 536,
    ErrDesc537 = 537,
    ErrDesc538 = 538,
    ErrDesc539 = 539,
    ErrDesc540 = 540,
    ErrDesc541 = 541,
    ErrDesc542 = 542,
    ErrDesc543 = 543,
    ErrDesc544 = 544,
    ErrDesc545 = 545,
    ErrDesc546 = 546,
    ErrDesc547 = 547,
    ErrDesc548 = 548,
    ErrDesc549 = 549,
    ErrDesc550 = 550,
    ErrDesc551 = 551,
    ErrDesc552 = 552,
    ErrDesc553 = 553,

    // Request errors
    RequestError = 600,
    BadFormatParameter = 601,
    BadFormatRequest = 602,
    RequestParameterMissing = 603,
    WrongHttpMethod = 604,

    // Response errors
    ResponseError = 620,
    BadFormatResponse = 621,
    ResponseItemMissing = 622,
    ResponseTooLarge = 623,

    // Invalid parameter errors
    InvalidCommonParameter = 650,
    InvalidPlatformId = 651,
    UnauthorizedDevice = 652,
    InvalidSerialId = 653,
    InvalidMacAddress = 654,
    InvalidRegion = 655,
    InvalidCountry = 656,
    InvalidLanguage = 657,
    UnauthorizedClient = 658,
    DeviceIdEmpty = 659,
    SerialIdEmpty = 660,
    PlatformIdEmpty = 661,

    InvalidUniqueId = 671,
    InvalidClientId = 672,
    InvalidClientKey = 673,

    InvalidNexClientId = 681,
    InvalidGameServerId = 682,
    GameServerIdEnvironmentNotFound = 683,
    GameServerIdUniqueIdNotLinked = 684,
    ClientIdUniqueIdNotLinked = 685,

    DeviceMismatch = 701,
    CountryMismatch = 702,
    EulaNotAccepted = 703,

    // Update required errors
    UpdateRequired = 710,
    SystemUpdateRequired = 711,
    ApplicationUpdateRequired = 712,

    UnauthorizedRequest = 720,
    RequestForbidden = 722,

    // Resource not found errors
    ResourceNotFound = 730,
    PidNotFound = 731,
    NexAccountNotFound = 732,
    GenerateTokenFailure = 733,
    RequestNotFound = 734,
    MasterPinNotFound = 735,
    MailTextNotFound = 736,
    SendMailFailure = 737,
    ApprovalIdNotFound = 738,

    // EULA errors
    InvalidEulaParameter = 740,
    InvalidEulaCountry = 741,
    InvalidEulaCountryAndVersion = 742,
    EulaNotFound = 743,

    // Not acceptable errors
    PhraseNotAcceptable = 770,
    AccountIdAlreadyExists = 771,
    AccountIdNotAcceptable = 772,
    AccountPasswordNotAcceptable = 773,
    MiiNameNotAcceptable = 774,
    MailAddressNotAcceptable = 775,
    AccountIdFormatInvalid = 776,
    AccountIdPasswordSame = 777,
    AccountIdCharNotAcceptable = 778,
    AccountIdSuccessiveSymbol = 779,
    AccountIdSymbolPositionNotAcceptable = 780,
    AccountIdTooManyDigit = 781,
    AccountPasswordCharNotAcceptable = 782,
    AccountPasswordTooFewCharTypes = 783,
    AccountPasswordSuccessiveSameChar = 784,
    MailAddressDomainNameNotAcceptable = 785,
    MailAddressDomainNameNotResolved = 786,
    ErrDesc787 = 787,

    ReachedAssociationLimit = 791,
    ReachedRegistrationLimit = 792,
    CoppaNotAccepted = 793,
    ParentalControlsRequired = 794,
    MiiNotRegistered = 795,
    DeviceEulaCountryMismatch = 796,
    PendingMigration = 797,

    // Wrong user input errors
    WrongUserInput = 810,
    WrongAccountPassword = 811,
    WrongMailAddress = 812,
    WrongAccountPasswordOrMailAddress = 813,
    WrongConfirmationCode = 814,
    WrongBirthDateOrMailAddress = 815,
    WrongAccountMail = 816,

    AccountAlreadyDeleted = 831,
    AccountIdChanged = 832,
    AuthenticationLocked = 833,
    DeviceInactive = 834,
    CoppaAgreementCanceled = 835,
    DomainAccountAlreadyExists = 836,

    AccountTokenExpired = 841,
    InvalidAccountToken = 842,
    AuthenticationRequired = 843,
    ErrDesc844 = 844,

    ConfirmationCodeExpired = 851,

    MailAddressNotValidated = 861,
    ExcessiveMailSendRequest = 862,

    // Credit card errors
    CreditCardError = 870,
    CreditCardGeneralFailure = 871,
    CreditCardDeclined = 872,
    CreditCardBlacklisted = 873,
    InvalidCreditCardNumber = 874,
    InvalidCreditCardDate = 875,
    InvalidCreditCardPin = 876,
    InvalidPostalCode = 877,
    InvalidLocation = 878,
    CreditCardDateExpired = 879,
    CreditCardNumberWrong = 880,
    CreditCardPinWrong = 881,

    // Ban errors
    Banned = 900,
    BannedAccount = 901,
    BannedAccountAll = 902,
    BannedAccountInApplication = 903,
    BannedAccountInNexService = 904,
    BannedAccountInIndependentService = 905,
    BannedDevice = 911,
    BannedDeviceAll = 912,
    BannedDeviceInApplication = 913,
    BannedDeviceInNexService = 914,
    BannedDeviceInIndependentService = 915,
    BannedAccountTemporarily = 921,
    BannedAccountAllTemporarily = 922,
    BannedAccountInApplicationTemporarily = 923,
    BannedAccountInNexServiceTemporarily = 924,
    BannedAccountInIndependentServiceTemporarily = 925,
    BannedDeviceTemporarily = 931,
    BannedDeviceAllTemporarily = 932,
    BannedDeviceInApplicationTemporarily = 933,
    BannedDeviceInNexServiceTemporarily = 934,
    BannedDeviceInIndependentServiceTemporarily = 935,

    // Service not provided errors
    ServiceNotProvided = 950,
    UnderMaintenance = 951,
    ServiceClosed = 952,
    NintendoNetworkClosed = 953,
    NotProvidedCountry = 954,

    // Restriction errors
    RestrictionError = 970,
    RestrictedByAge = 971,
    RestrictedByParentalControls = 980,
    OnGameInternetCommunicationRestricted = 981,

    InternalServerError = 991,
    UnknownServerError = 992,

    UnauthenticatedAfterSalvage = 998,
    AuthenticationFailureUnknown = 999,
};
}

namespace ErrCodes {
enum {
    MySuccess = 220000,               // 022-0000
    MailAddressNotConfirmed = 220001, // 022-0001

    // Library errors
    LibraryError = 220500,         // 022-0500
    NotInitialized = 220501,       // 022-0501
    AlreadyInitialized = 220502,   // 022-0502
    AcStatusDisconnected = 225103, // 022-5103
    ErrCode225104 = 225104,        // 022-5104
    Busy = 220511,                 // 022-0511
    ErrCode225112 = 225112,        // 022-5112
    NotImplemented = 220591,       // 022-0591
    Deprecated = 220592,           // 022-0592
    DevelopmentOnly = 220593,      // 022-0593

    InvalidArgument = 220600, // 022-0600
    InvalidPointer = 220601,  // 022-0601
    OutOfRange = 220602,      // 022-0602
    InvalidSize = 220603,     // 022-0603
    InvalidFormat = 220604,   // 022-0604
    InvalidHandle = 220605,   // 022-0605
    InvalidValue = 220606,    // 022-0606

    InternalError = 220700,       // 022-0700
    EndOfStream = 220701,         // 022-0701
    FileError = 220710,           // 022-0710
    FileNotFound = 220711,        // 022-0711
    FileVersionMismatch = 220712, // 022-0712
    FileIOError = 220713,         // 022-0713
    FileTypeMismatch = 220714,    // 022-0714
    ErrCode225315 = 225315,       // 022-5315

    OutOfResource = 220730,   // 022-0730
    ShortOfBuffer = 220731,   // 022-0731
    OutOfMemory = 220740,     // 022-0740
    OutOfGlobalHeap = 220741, // 022-0741

    ErrCode225350 = 225350, // 022-5350
    ErrCode225351 = 225351, // 022-5351
    ErrCode225352 = 225352, // 022-5352
    ErrCode225360 = 225360, // 022-5360
    ErrCode225361 = 225361, // 022-5361
    ErrCode225362 = 225362, // 022-5362
    ErrCode225363 = 225363, // 022-5363

    // Account management errors
    AccountManagementError = 221000, // 022-1000
    AccountNotFound = 221001,        // 022-1001
    SlotsFull = 221002,              // 022-1002
    AccountNotLoaded = 221011,       // 022-1011
    AccountAlreadyLoaded = 221012,   // 022-1012
    AccountLocked = 221013,          // 022-1013
    NotNetworkAccount = 221021,      // 022-1021
    NotLocalAccount = 221022,        // 022-1022
    AccountCommited = 221023,        // 022-1023

    ErrCode225431 = 225431, // 022-5431
    ErrCode225432 = 225432, // 022-5432
    ErrCode225433 = 225433, // 022-5433

    ErrCode221101 = 221101, // 022-1101

    AuthenticationError = 222000, // 022-2000

    // HTTP errors
    HttpError = 222100,     // 022-2100
    ErrCode225502 = 225502, // 022-5502
    ErrCode225503 = 225503, // 022-5503
    ErrCode225504 = 225504, // 022-5504
    ErrCode225505 = 225505, // 022-5505
    ErrCode225506 = 225506, // 022-5506
    ErrCode225507 = 225507, // 022-5507
    ErrCode225508 = 225508, // 022-5508
    ErrCode225509 = 225509, // 022-5509
    ErrCode225510 = 225510, // 022-5510
    ErrCode225511 = 225511, // 022-5511
    ErrCode225512 = 225512, // 022-5512
    ErrCode225513 = 225513, // 022-5513
    ErrCode225514 = 225514, // 022-5514
    ErrCode225515 = 225515, // 022-5515
    ErrCode225516 = 225516, // 022-5516
    ErrCode225517 = 225517, // 022-5517
    ErrCode225518 = 225518, // 022-5518
    ErrCode225519 = 225519, // 022-5519
    ErrCode225520 = 225520, // 022-5520
    ErrCode225521 = 225521, // 022-5521
    ErrCode225522 = 225522, // 022-5522
    ErrCode225523 = 225523, // 022-5523
    ErrCode225524 = 225524, // 022-5524
    ErrCode225525 = 225525, // 022-5525
    ErrCode225526 = 225526, // 022-5526
    ErrCode225527 = 225527, // 022-5527
    ErrCode225528 = 225528, // 022-5528
    ErrCode225529 = 225529, // 022-5529
    ErrCode225530 = 225530, // 022-5530
    ErrCode225531 = 225531, // 022-5531
    ErrCode225532 = 225532, // 022-5532
    ErrCode225533 = 225533, // 022-5533
    ErrCode225534 = 225534, // 022-5534
    ErrCode225535 = 225535, // 022-5535
    ErrCode225536 = 225536, // 022-5536
    ErrCode225537 = 225537, // 022-5537
    ErrCode225538 = 225538, // 022-5538
    ErrCode225539 = 225539, // 022-5539
    ErrCode225540 = 225540, // 022-5540
    ErrCode225541 = 225541, // 022-5541
    ErrCode225542 = 225542, // 022-5542
    ErrCode225543 = 225543, // 022-5543
    ErrCode225544 = 225544, // 022-5544
    ErrCode225545 = 225545, // 022-5545
    ErrCode225546 = 225546, // 022-5546
    ErrCode225547 = 225547, // 022-5547
    ErrCode225548 = 225548, // 022-5548
    ErrCode225549 = 225549, // 022-5549
    ErrCode225550 = 225550, // 022-5550
    ErrCode225551 = 225551, // 022-5551
    ErrCode225552 = 225552, // 022-5552
    ErrCode225553 = 225553, // 022-5553

    // Request errors
    RequestError = 222400,            // 022-2400
    BadFormatParameter = 222401,      // 022-2401
    BadFormatRequest = 222402,        // 022-2402
    RequestParameterMissing = 222403, // 022-2403
    WrongHttpMethod = 222404,         // 022-2404

    // Response errors
    ResponseError = 222420,       // 022-2420
    BadFormatResponse = 222421,   // 022-2421
    ResponseItemMissing = 222422, // 022-2422
    ResponseTooLarge = 222423,    // 022-2423

    // Invalid parameter errors
    InvalidCommonParameter = 222450, // 022-2450
    InvalidPlatformId = 222451,      // 022-2451
    UnauthorizedDevice = 222452,     // 022-2452
    InvalidSerialId = 222453,        // 022-2453
    InvalidMacAddress = 222454,      // 022-2454
    InvalidRegion = 222455,          // 022-2455
    InvalidCountry = 222456,         // 022-2456
    InvalidLanguage = 222457,        // 022-2457
    UnauthorizedClient = 222458,     // 022-2458
    DeviceIdEmpty = 222459,          // 022-2459
    SerialIdEmpty = 222460,          // 022-2460
    PlatformIdEmpty = 222461,        // 022-2461

    InvalidUniqueId = 222471,  // 022-2471
    InvalidClientId = 222472,  // 022-2472
    InvalidClientKey = 222473, // 022-2473

    InvalidNexClientId = 222481,              // 022-2481
    InvalidGameServerId = 222482,             // 022-2482
    GameServerIdEnvironmentNotFound = 222483, // 022-2483
    GameServerIdUniqueIdNotLinked = 222484,   // 022-2484
    ClientIdUniqueIdNotLinked = 222485,       // 022-2485

    DeviceMismatch = 222501,  // 022-2501
    CountryMismatch = 222502, // 022-2502
    EulaNotAccepted = 222503, // 022-2503

    // Update required errors
    UpdateRequired = 222510,            // 022-2510
    SystemUpdateRequired = 222511,      // 022-2511
    ApplicationUpdateRequired = 222512, // 022-2512

    UnauthorizedRequest = 222520, // 022-2520
    RequestForbidden = 222522,    // 022-2522

    // Resource not found errors
    ResourceNotFound = 222530,     // 022-2530
    PidNotFound = 222531,          // 022-2531
    NexAccountNotFound = 222532,   // 022-2532
    GenerateTokenFailure = 222533, // 022-2533
    RequestNotFound = 222534,      // 022-2534
    MasterPinNotFound = 222535,    // 022-2535
    MailTextNotFound = 222536,     // 022-2536
    SendMailFailure = 222537,      // 022-2537
    ApprovalIdNotFound = 222538,   // 022-2538

    // EULA errors
    InvalidEulaParameter = 222540,         // 022-2540
    InvalidEulaCountry = 222541,           // 022-2541
    InvalidEulaCountryAndVersion = 222542, // 022-2542
    EulaNotFound = 222543,                 // 022-2543

    // Not acceptable errors
    PhraseNotAcceptable = 222570,                  // 022-2570
    AccountIdAlreadyExists = 222571,               // 022-2571
    AccountIdNotAcceptable = 222572,               // 022-2572
    AccountPasswordNotAcceptable = 222573,         // 022-2573
    MiiNameNotAcceptable = 222574,                 // 022-2574
    MailAddressNotAcceptable = 222575,             // 022-2575
    AccountIdFormatInvalid = 222576,               // 022-2576
    AccountIdPasswordSame = 222577,                // 022-2577
    AccountIdCharNotAcceptable = 222578,           // 022-2578
    AccountIdSuccessiveSymbol = 222579,            // 022-2579
    AccountIdSymbolPositionNotAcceptable = 222580, // 022-2580
    AccountIdTooManyDigit = 222581,                // 022-2581
    AccountPasswordCharNotAcceptable = 222582,     // 022-2582
    AccountPasswordTooFewCharTypes = 222583,       // 022-2583
    AccountPasswordSuccessiveSameChar = 222584,    // 022-2584
    MailAddressDomainNameNotAcceptable = 222585,   // 022-2585
    MailAddressDomainNameNotResolved = 222586,     // 022-2586
    ErrCode222587 = 222587,                        // 022-2587

    ReachedAssociationLimit = 222591,   // 022-2591
    ReachedRegistrationLimit = 222592,  // 022-2592
    CoppaNotAccepted = 222593,          // 022-2593
    ParentalControlsRequired = 222594,  // 022-2594
    MiiNotRegistered = 222595,          // 022-2595
    DeviceEulaCountryMismatch = 222596, // 022-2596
    PendingMigration = 222597,          // 022-2597

    // Wrong user input errors
    WrongUserInput = 222610,                    // 022-2610
    WrongAccountPassword = 222611,              // 022-2611
    WrongMailAddress = 222612,                  // 022-2612
    WrongAccountPasswordOrMailAddress = 222613, // 022-2613
    WrongConfirmationCode = 222614,             // 022-2614
    WrongBirthDateOrMailAddress = 222615,       // 022-2615
    WrongAccountMail = 222616,                  // 022-2616

    AccountAlreadyDeleted = 222631,      // 022-2631
    AccountIdChanged = 222632,           // 022-2632
    AuthenticationLocked = 222633,       // 022-2633
    DeviceInactive = 222634,             // 022-2634
    CoppaAgreementCanceled = 222635,     // 022-2635
    DomainAccountAlreadyExists = 222636, // 022-2636

    AccountTokenExpired = 222641,    // 022-2641
    InvalidAccountToken = 222642,    // 022-2642
    AuthenticationRequired = 222643, // 022-2643
    ErrCode225844 = 225844,          // 022-5844

    ConfirmationCodeExpired = 222651, // 022-2651

    MailAddressNotValidated = 222661,  // 022-2661
    ExcessiveMailSendRequest = 222662, // 022-2662

    // Credit card errors
    CreditCardError = 222670,          // 022-2670
    CreditCardGeneralFailure = 222671, // 022-2671
    CreditCardDeclined = 222672,       // 022-2672
    CreditCardBlacklisted = 222673,    // 022-2673
    InvalidCreditCardNumber = 222674,  // 022-2674
    InvalidCreditCardDate = 222675,    // 022-2675
    InvalidCreditCardPin = 222676,     // 022-2676
    InvalidPostalCode = 222677,        // 022-2677
    InvalidLocation = 222678,          // 022-2678
    CreditCardDateExpired = 222679,    // 022-2679
    CreditCardNumberWrong = 222680,    // 022-2680
    CreditCardPinWrong = 222681,       // 022-2681

    // Ban errors
    Banned = 222800,                                       // 022-2800
    BannedAccount = 222801,                                // 022-2801
    BannedAccountAll = 222802,                             // 022-2802
    BannedAccountInApplication = 222803,                   // 022-2803
    BannedAccountInNexService = 222804,                    // 022-2804
    BannedAccountInIndependentService = 222805,            // 022-2805
    BannedDevice = 222811,                                 // 022-2811
    BannedDeviceAll = 222812,                              // 022-2812
    BannedDeviceInApplication = 222813,                    // 022-2813
    BannedDeviceInNexService = 222814,                     // 022-2814
    BannedDeviceInIndependentService = 222815,             // 022-2815
    BannedAccountTemporarily = 222821,                     // 022-2821
    BannedAccountAllTemporarily = 222822,                  // 022-2822
    BannedAccountInApplicationTemporarily = 222823,        // 022-2823
    BannedAccountInNexServiceTemporarily = 222824,         // 022-2824
    BannedAccountInIndependentServiceTemporarily = 222825, // 022-2825
    BannedDeviceTemporarily = 222831,                      // 022-2831
    BannedDeviceAllTemporarily = 222832,                   // 022-2832
    BannedDeviceInApplicationTemporarily = 222833,         // 022-2833
    BannedDeviceInNexServiceTemporarily = 222834,          // 022-2834
    BannedDeviceInIndependentServiceTemporarily = 222835,  // 022-2835

    // Service not provided errors
    ServiceNotProvided = 222880,    // 022-2880
    UnderMaintenance = 222881,      // 022-2881
    ServiceClosed = 222882,         // 022-2882
    NintendoNetworkClosed = 222883, // 022-2883
    NotProvidedCountry = 222884,    // 022-2884

    // Restriction errors
    RestrictionError = 222900,                      // 022-2900
    RestrictedByAge = 222901,                       // 022-2901
    RestrictedByParentalControls = 222910,          // 022-2910
    OnGameInternetCommunicationRestricted = 222911, // 022-2911

    InternalServerError = 222931, // 022-2931
    UnknownServerError = 222932,  // 022-2932

    UnauthenticatedAfterSalvage = 222998,  // 022-2998
    AuthenticationFailureUnknown = 222999, // 022-2999
    Unknown = 229999,                      // 022-9999
};
}

/// Gets the ACT error code for the given result
u32 GetACTErrorCode(Result result);

} // namespace Service::ACT
