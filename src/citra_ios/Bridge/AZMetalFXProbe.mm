// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// Lists what this device's MetalFX offers for frame interpolation, read from the Objective-C
// runtime: the classes and protocols whose names start with MTLFX, with their properties and
// methods. The documentation and the SDK headers are not at hand when the code is written, so
// the device itself tells which properties and calls the interpolator takes.

#import <AzaharCore/AZMetalFXProbe.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>

static void AppendMethods(NSMutableString* out, Protocol* protocol, BOOL required, BOOL instance) {
    unsigned int count = 0;
    struct objc_method_description* list =
        protocol_copyMethodDescriptionList(protocol, required, instance, &count);
    for (unsigned int i = 0; i < count; i++) {
        [out appendFormat:@"    %@ %c %s  [%s]\n", required ? @"required" : @"optional",
                          instance ? '-' : '+', sel_getName(list[i].name),
                          list[i].types ? list[i].types : ""];
    }
    free(list);
}

static void AppendProtocol(NSMutableString* out, Protocol* protocol) {
    [out appendFormat:@"protocol %s\n", protocol_getName(protocol)];
    unsigned int count = 0;
    Protocol* __unsafe_unretained* adopted = protocol_copyProtocolList(protocol, &count);
    for (unsigned int i = 0; i < count; i++) {
        [out appendFormat:@"    adopts %s\n", protocol_getName(adopted[i])];
    }
    free(adopted);
    count = 0;
    objc_property_t* properties = protocol_copyPropertyList(protocol, &count);
    for (unsigned int i = 0; i < count; i++) {
        [out appendFormat:@"    property %s  (%s)\n", property_getName(properties[i]),
                          property_getAttributes(properties[i])];
    }
    free(properties);
    AppendMethods(out, protocol, YES, YES);
    AppendMethods(out, protocol, NO, YES);
    AppendMethods(out, protocol, YES, NO);
    AppendMethods(out, protocol, NO, NO);
}

static void AppendClass(NSMutableString* out, Class cls) {
    [out appendFormat:@"class %s  (super %s)\n", class_getName(cls),
                      class_getSuperclass(cls) ? class_getName(class_getSuperclass(cls)) : "-"];
    for (int meta = 0; meta < 2; meta++) {
        Class target = meta ? object_getClass(cls) : cls;
        unsigned int count = 0;
        Method* methods = class_copyMethodList(target, &count);
        for (unsigned int i = 0; i < count; i++) {
            [out appendFormat:@"    %c %s\n", meta ? '+' : '-',
                              sel_getName(method_getName(methods[i]))];
        }
        free(methods);
    }
}

@implementation AZMetalFXProbe

+ (NSString*)report {
    NSMutableString* out = [NSMutableString string];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    [out appendFormat:@"Device: %@\nOS: %@\n", device.name, NSProcessInfo.processInfo.operatingSystemVersionString];

    // Does the descriptor class exist and does it support this device?
    Class descriptor = NSClassFromString(@"MTLFXFrameInterpolatorDescriptor");
    [out appendFormat:@"MTLFXFrameInterpolatorDescriptor: %@\n", descriptor ? @"present" : @"missing"];
    SEL supports = NSSelectorFromString(@"supportsDevice:");
    if (descriptor != Nil && [descriptor respondsToSelector:supports]) {
        const BOOL yes = ((BOOL(*)(Class, SEL, id))objc_msgSend)(descriptor, supports, device);
        [out appendFormat:@"supportsDevice: %@\n", yes ? @"YES" : @"NO"];
    }
    for (NSString* name in @[ @"MTLFXTemporalScalerDescriptor", @"MTLFXSpatialScalerDescriptor",
                              @"MTLFXTemporalDenoisedScalerDescriptor" ]) {
        Class other = NSClassFromString(name);
        [out appendFormat:@"%@: %@", name, other ? @"present" : @"missing"];
        if (other != Nil && [other respondsToSelector:supports]) {
            const BOOL yes = ((BOOL(*)(Class, SEL, id))objc_msgSend)(other, supports, device);
            [out appendFormat:@", supportsDevice %@", yes ? @"YES" : @"NO"];
        }
        [out appendString:@"\n"];
    }
    [out appendString:@"\n"];

    // Everything MetalFX in the runtime that has to do with interpolation, scalers and denoising.
    NSMutableArray<NSString*>* sections = [NSMutableArray array];
    unsigned int count = 0;
    Protocol* __unsafe_unretained* protocols = objc_copyProtocolList(&count);
    for (unsigned int i = 0; i < count; i++) {
        NSString* name = [NSString stringWithUTF8String:protocol_getName(protocols[i])];
        if ([name hasPrefix:@"MTLFX"]) {
            NSMutableString* section = [NSMutableString string];
            AppendProtocol(section, protocols[i]);
            [sections addObject:section];
        }
    }
    free(protocols);
    count = 0;
    Class* classes = objc_copyClassList(&count);
    for (unsigned int i = 0; i < count; i++) {
        NSString* name = [NSString stringWithUTF8String:class_getName(classes[i])];
        if ([name hasPrefix:@"MTLFX"] || [name containsString:@"FrameInterpolat"]) {
            NSMutableString* section = [NSMutableString string];
            AppendClass(section, classes[i]);
            [sections addObject:section];
        }
    }
    free(classes);
    [sections sortUsingSelector:@selector(compare:)];
    [out appendString:[sections componentsJoinedByString:@"\n"]];
    return out;
}

@end
