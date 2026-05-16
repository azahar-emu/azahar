// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/serialization/array.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/string.hpp>
#include "common/archives.h"
#include "common/assert.h"
#include "common/settings.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/thread.h"

SERIALIZE_EXPORT_IMPL(Kernel::ResourceLimit)
SERIALIZE_EXPORT_IMPL(Kernel::ResourceLimitList)

namespace Kernel {

ResourceLimit::ResourceLimit(KernelSystem& _kernel) : Object(_kernel), kernel(_kernel) {}

ResourceLimit::~ResourceLimit() = default;

std::shared_ptr<ResourceLimit> ResourceLimit::Create(KernelSystem& kernel,
                                                     ResourceLimitCategory category,
                                                     std::string name) {
    auto resource_limit = std::make_shared<ResourceLimit>(kernel);
    resource_limit->m_category = category;
    resource_limit->m_name = std::move(name);
    return resource_limit;
}

s32 ResourceLimit::GetCurrentValue(ResourceLimitType type) const {
    const auto index = static_cast<std::size_t>(type);
    return m_current_values[index];
}

s32 ResourceLimit::GetLimitValue(ResourceLimitType type) const {
    const auto index = static_cast<std::size_t>(type);
    return m_limit_values[index];
}

void ResourceLimit::SetCurrentValue(ResourceLimitType type, s32 value) {
    const auto index = static_cast<std::size_t>(type);
    m_current_values[index] = value;
}

void ResourceLimit::SetLimitValue(ResourceLimitType type, s32 value) {
    const auto index = static_cast<std::size_t>(type);
    m_limit_values[index] = value;
}

bool ResourceLimit::Reserve(ResourceLimitType type, s32 amount) {
    const auto index = static_cast<std::size_t>(type);
    const s32 limit = m_limit_values[index];
    const s32 new_value = m_current_values[index] + amount;
    // TODO(PabloMK7): Fix all resource limit bugs and return an error, instead of ignoring it.
    if (new_value > limit) {
        LOG_ERROR(Kernel, "New value {} exceeds limit {} for resource type {}", new_value, limit,
                  type);
    }
    m_current_values[index] = new_value;
    return true;
}

bool ResourceLimit::Release(ResourceLimitType type, s32 amount) {
    const auto index = static_cast<std::size_t>(type);
    const s32 value = m_current_values[index];
    // TODO(PabloMK7): Fix all resource limit bugs and return an error, instead of ignoring it.
    if (amount > value) {
        LOG_ERROR(Kernel, "Amount {} exceeds current value {} for resource type {}", amount, value,
                  type);
    }
    m_current_values[index] = value - amount;
    return true;
}

void ResourceLimit::ApplyAppMaxCPUSetting(std::shared_ptr<Kernel::Process>& process, u8 exh_mode,
                                          u8 exh_cpu_limit) {

    // List of 1.0 titles with max CPU time overrides. This is not a "hack" list,
    // these values actually exist in the 3DS kernel.
    static constexpr std::array<std::pair<u32, Core1CpuTime>, 16> cpu_time_overrides = {{
        // 3DS sound (JPN, USA, EUR)
        {0x205, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x215, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x225, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // Star Fox 64 3D (JPN)
        {0x304, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // Super Monkey Ball 3D (JPN)
        {0x32E, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // The Legend of Zelda: Ocarina of Time 3D (JPN, USA, EUR)
        {0x334, 30},
        {0x335, 30},
        {0x336, 30},

        // Doctor Lautrec to Boukyaku no Kishidan (JPN, USA, EUR)
        {0x348, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x368, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x562, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // Pro Yakyuu Spirits 2011 (JPN)
        {0x349, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // Super Monkey Ball 3D (USA, EUR)
        {0x370, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x389, Core1CpuTime::PREEMPTION_EXCEMPTED},

        // Star Fox 64 3D (USA, EUR)
        {0x490, Core1CpuTime::PREEMPTION_EXCEMPTED},
        {0x491, Core1CpuTime::PREEMPTION_EXCEMPTED},

    }};

    Core1ScheduleMode final_mode{};
    Core1CpuTime limit_cpu_time{};
    Core1CpuTime set_cpu_time = Core1CpuTime::PREEMPTION_DISABLED;

    if (exh_mode == 0 && exh_cpu_limit == 0) {
        // This is a 1.0 app with the CPU time value not set. Use default or hardcoded value.

        final_mode = Core1ScheduleMode::Single;
        limit_cpu_time = 80;
        set_cpu_time = 25;

        u32 uID = static_cast<u32>(((process->codeset->program_id >> 8) & 0xFFFFF));

        auto it = std::find_if(cpu_time_overrides.begin(), cpu_time_overrides.end(),
                               [uID](auto& v) { return v.first == uID; });

        if (it != cpu_time_overrides.end()) {
            const Core1CpuTime& time = it->second;
            if (static_cast<u32>(time) > 100 && static_cast<u32>(time) < 200) {
                // This code path is actually unused
                final_mode = Core1ScheduleMode::Multi;
                set_cpu_time = limit_cpu_time = static_cast<u32>(time) - 100;
            } else {
                final_mode = Core1ScheduleMode::Single;
                set_cpu_time = limit_cpu_time = time;
            }
        }
    } else {
        final_mode = (exh_mode == 0) ? Core1ScheduleMode::Single : Core1ScheduleMode::Multi;
        limit_cpu_time = exh_cpu_limit;
    }

    // Normally done by PM through svcSetKernelState and svcSetResourceLimitValues.
    SetLimitValue(ResourceLimitType::CpuTime, limit_cpu_time);
    SetCurrentValue(ResourceLimitType::CpuTime, set_cpu_time);
    kernel.SetCore1ScheduleMode(final_mode);
    kernel.UpdateCore1AppCpuLimit();
}

template <class Archive>
void ResourceLimit::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<Object>(*this);
    ar & m_category;
    ar & m_name;
    ar & m_limit_values;
    ar & m_current_values;
}
SERIALIZE_IMPL(ResourceLimit)

ResourceLimitList::ResourceLimitList(KernelSystem& kernel) {
    // PM makes APPMEMALLOC always match app RESLIMIT_COMMIT.
    // See: https://github.com/LumaTeam/Luma3DS/blob/e2778a45/sysmodules/pm/source/reslimit.c#L275
    const bool is_new_3ds = Settings::values.is_new_3ds.GetValue();
    const auto& appmemalloc = kernel.GetMemoryRegion(MemoryRegion::APPLICATION);

    // Create the Application resource limit
    auto resource_limit =
        ResourceLimit::Create(kernel, ResourceLimitCategory::Application, "Applications");
    resource_limit->SetLimitValue(ResourceLimitType::Priority, 0x18);
    resource_limit->SetLimitValue(ResourceLimitType::Commit, appmemalloc->size);
    resource_limit->SetLimitValue(ResourceLimitType::Thread, 0x20);
    resource_limit->SetLimitValue(ResourceLimitType::Event, 0x20);
    resource_limit->SetLimitValue(ResourceLimitType::Mutex, 0x20);
    resource_limit->SetLimitValue(ResourceLimitType::Semaphore, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::Timer, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::SharedMemory, 0x10);
    resource_limit->SetLimitValue(ResourceLimitType::AddressArbiter, 0x2);
    resource_limit->SetLimitValue(ResourceLimitType::CpuTime, Core1CpuTime::PREEMPTION_DISABLED);
    resource_limits[static_cast<u8>(resource_limit->GetCategory())] = resource_limit;

    // Create the SysApplet resource limit
    resource_limit =
        ResourceLimit::Create(kernel, ResourceLimitCategory::SysApplet, "System Applets");
    resource_limit->SetLimitValue(ResourceLimitType::Priority, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::Commit, is_new_3ds ? 0x5E06000 : 0x2606000);
    resource_limit->SetLimitValue(ResourceLimitType::Thread, is_new_3ds ? 0x1D : 0xE);
    resource_limit->SetLimitValue(ResourceLimitType::Event, is_new_3ds ? 0xB : 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::Mutex, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::Semaphore, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::Timer, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::SharedMemory, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::AddressArbiter, 0x3);
    resource_limit->SetLimitValue(ResourceLimitType::CpuTime, Core1CpuTime::PREEMPTION_EXCEMPTED);
    resource_limits[static_cast<u8>(resource_limit->GetCategory())] = resource_limit;

    // Create the LibApplet resource limit
    resource_limit =
        ResourceLimit::Create(kernel, ResourceLimitCategory::LibApplet, "Library Applets");
    resource_limit->SetLimitValue(ResourceLimitType::Priority, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::Commit, 0x602000);
    resource_limit->SetLimitValue(ResourceLimitType::Thread, 0xE);
    resource_limit->SetLimitValue(ResourceLimitType::Event, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::Mutex, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::Semaphore, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::Timer, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::SharedMemory, 0x8);
    resource_limit->SetLimitValue(ResourceLimitType::AddressArbiter, 0x1);
    resource_limit->SetLimitValue(ResourceLimitType::CpuTime, Core1CpuTime::PREEMPTION_EXCEMPTED);
    resource_limits[static_cast<u8>(resource_limit->GetCategory())] = resource_limit;

    // Create the Other resource limit
    resource_limit = ResourceLimit::Create(kernel, ResourceLimitCategory::Other, "Others");
    resource_limit->SetLimitValue(ResourceLimitType::Priority, 0x4);
    resource_limit->SetLimitValue(ResourceLimitType::Commit, is_new_3ds ? 0x2182000 : 0x1682000);
    resource_limit->SetLimitValue(ResourceLimitType::Thread, is_new_3ds ? 0xE1 : 0xCA);
    resource_limit->SetLimitValue(ResourceLimitType::Event, is_new_3ds ? 0x108 : 0xF8);
    resource_limit->SetLimitValue(ResourceLimitType::Mutex, is_new_3ds ? 0x25 : 0x23);
    resource_limit->SetLimitValue(ResourceLimitType::Semaphore, is_new_3ds ? 0x43 : 0x40);
    resource_limit->SetLimitValue(ResourceLimitType::Timer, is_new_3ds ? 0x2C : 0x2B);
    resource_limit->SetLimitValue(ResourceLimitType::SharedMemory, is_new_3ds ? 0x1F : 0x1E);
    resource_limit->SetLimitValue(ResourceLimitType::AddressArbiter, is_new_3ds ? 0x2D : 0x2B);
    resource_limit->SetLimitValue(ResourceLimitType::CpuTime, Core1CpuTime::PREEMPTION_SYSMODULE);
    resource_limits[static_cast<u8>(resource_limit->GetCategory())] = resource_limit;
}

ResourceLimitList::~ResourceLimitList() = default;

std::shared_ptr<ResourceLimit> ResourceLimitList::GetForCategory(ResourceLimitCategory category) {
    switch (category) {
    case ResourceLimitCategory::Application:
    case ResourceLimitCategory::SysApplet:
    case ResourceLimitCategory::LibApplet:
    case ResourceLimitCategory::Other:
        return resource_limits[static_cast<u8>(category)];
    default:
        UNREACHABLE_MSG("Unknown resource limit category");
    }
}

template <class Archive>
void ResourceLimitList::serialize(Archive& ar, const unsigned int) {
    ar & resource_limits;
}
SERIALIZE_IMPL(ResourceLimitList)

} // namespace Kernel
