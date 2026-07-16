# Azahar Architecture

This document describes the high-level architecture of the Azahar 3DS emulator.

## Dependency Graph

```
citra_qt / citra_cli / citra_libretro / citra_room_standalone
    |
    v
citra_core
    |---> citra_common
    |---> audio_core
    |---> video_core
    |---> network
    |---> web_service (optional)
    |
input_common (used by frontends)
    |
    v
externals (SDL2, Qt6, dynarmic, sirit, vulkan-headers, etc.)
```

All emulator logic lives in static libraries under `src/`. Frontends are thin wrappers that provide platform-specific windowing, input, and audio output.

---

## Core (`src/core/`)

The central subsystem. `Core::System` is a singleton that owns all other subsystems.

**Key files:**
- `core.h` / `core.cpp` -- `System` singleton, init/shutdown, main loop
- `memory.h` / `memory.cpp` -- Virtual/physical memory, page table
- `core_timing.h` -- Event scheduler, per-core timers

**Entry points:**
- `System::Load()` -- loads a ROM, creates the kernel process, initializes GPU/audio
- `System::RunLoop()` -- runs all CPU cores in slices, fires timing events
- `System::SaveState()` / `LoadState()` -- Boost.Serialization-based state snapshots

### CPU Emulation (`src/core/arm/`)

The 3DS has 4 ARM11 cores + 1 ARM9. Emulation backends:

| Backend | Architecture | Type |
|---------|-------------|------|
| **Dynarmic** | x86_64, ARM64 | JIT recompiler (primary) |
| **DynCom** | All | Interpreter (fallback) |
| **SkyEye** | All | Shared ARM state definitions |

`ARM_Interface` is the abstract base. Each core gets its own `Timing::Timer` with an event queue. The main loop calls `Run()` on each core per slice.

### Memory System (`memory.h`, `memory.cpp`)

#### Page Table

The page table covers the full 32-bit address space with 1M entries (one per 4 KiB page):

- **`pointers.raw`** -- raw `u8*` for fast access by dynarmic JIT
- **`pointers.refs`** -- `MemoryRef` objects for Boost.Serialization
- **`attributes`** -- `PageType` enum per page

Fast-path read: load `page_pointer = page_table->pointers[vaddr >> 12]`, if non-null, `memcpy`. Single indexed fetch + null check, mimicking hardware TLB.

#### PageType Values

| Value | Description |
|-------|-------------|
| `Unmapped` | Page not mapped; access causes error |
| `Memory` | Normal mapped memory; only type with valid pointer |
| `RasterizerCachedMemory` | Memory tracked by rasterizer cache; pointer is NULL (forces cache lookup) |
| `MemoryWatchpoint` | Memory with debug read/write watchpoint |
| `RasterizerCachedMemoryWatchpoint` | Rasterizer-cached memory with watchpoint |

When a page becomes `RasterizerCachedMemory`, the pointer is set to NULL. This forces all CPU accesses through the slow path in `Read<T>`, which calls `RasterizerFlushVirtualRegion` to sync GPU-modified data back to CPU memory before reading.

#### Physical Memory Regions

| Region | Physical Address | Size |
|--------|-----------------|------|
| IO registers | `0x10100000` | 4 MB |
| MPCore internal RAM | `0x17E00000` | 8 KB |
| VRAM | `0x18000000` | 6 MB |
| N3DS extra RAM | `0x1F000000` | 4 MB |
| DSP memory | `0x1FF00000` | 512 KB |
| AXI WRAM | `0x1FF80000` | 512 KB |
| FCRAM (O3DS) | `0x20000000` | 128 MB |
| FCRAM (N3DS) | `0x20000000` | 256 MB |

#### Virtual Address Space

| Region | Virtual Address | Size |
|--------|----------------|------|
| PROCESS_IMAGE | `0x00100000` | 64 MB (text/data/bss) |
| HEAP | `0x08000000` | 128 MB |
| LINEAR_HEAP | `0x14000000` | 128 MB (1:1 FCRAM mapping) |
| VRAM | `0x1F000000` | 6 MB (1:1 VRAM mapping) |

#### Memory Mapping

`MapPages()` iterates page-by-page: sets `page_table.attributes[base] = type` and `page_table.pointers[base] = memory`. If the page is already rasterizer-cached, it downgrades to `RasterizerCachedMemory` with a NULL pointer.

`UnmapRegion()` calls `MapPages` with `nullptr` memory and `PageType::Unmapped`.

`RasterizerCacheMarker` maintains per-page boolean arrays for the four rasterizer-accessible virtual regions (VRAM, LINEAR_HEAP, NEW_LINEAR_HEAP, PLUGIN_3GX_FB). `RasterizerMarkRegionCached` toggles page table entries between `Memory` and `RasterizerCachedMemory`.

#### Exclusive Monitor (LDREX/STREX)

Uses `AtomicCompareAndSwap` directly rather than traditional address-monitoring. `STREX` always succeeds if the memory value still equals the `expected` value from `LDREX`. No local/global monitor emulation -- a simplification that works for most single-core scenarios.

#### Luma3DS Custom Mapping

Addresses with bit 31 set bypass the page table entirely, enabling direct FCRAM and MMIO access via physical address aliasing.

### Kernel (`src/core/hle/kernel/`)

Full HLE kernel emulation:

- **Threads** -- `Thread` class with states (Running/Ready/WaitArb/WaitIPC/etc.), per-core `ThreadManager` with priority scheduling
- **Processes** -- `Process` with virtual address space, handle table
- **SVCs** -- `svc.cpp` dispatches supervisor calls (CreateThread, SendSyncRequest, etc.)
- **IPC** -- `hle_ipc.h` handles command buffer at TLS+0x80, with handle/buffer descriptor translation
- **Sync primitives** -- Events, mutexes, semaphores, address arbiters, timers
- **Memory** -- `vm_manager.h` maps virtual regions (HEAP, SHARED_MEMORY, LINEAR_HEAP, etc.)
- **Shared memory** -- `shared_memory.h`, `shared_page.h` (backlight, HID values, etc.)

### HLE Services (`src/core/hle/service/`)

41 service modules registered in `service.cpp`. Each implements the 3DS IPC interface.

#### Service Registration

`ServiceFrameworkBase` (CRTP base) provides:
- `service_name` -- the string games use to connect (e.g., `"gsp::Gpu"`, `"hid:USER"`)
- `max_sessions` -- max concurrent sessions (default 10)
- `handlers` -- `flat_map<u32, FunctionInfoBase>` mapping command IDs to handler functions
- `handler_invoker` -- type-erased function pointer that up-casts to derived class (avoids virtual dispatch)

Two installation paths:
1. **`InstallAsService()`** -- registers with `ServiceManager`, creates kernel port pair, stores `ClientPort`
2. **`InstallAsNamedPort()`** -- registers directly in kernel's named port registry (used for `srv:` itself)

#### Service Manager (SM)

`ServiceManager` owns `registered_services` (unordered_map of name -> ClientPort). `InstallInterfaces()` creates the `SRV` service as a named port. `ConnectToService()` looks up a `ClientPort` by name and creates a session.

The SRV service handles `GetServiceHandle` -- the core service discovery path. Games call this with a service name. If the service exists, it calls `client_port->Connect()`. If not yet registered and `wait_until_available` is set, the thread sleeps until the service appears.

#### IPC Command Buffer Layout

Command buffer is 64 u32 words (0x100 bytes) at TLS+0x80:

```
[Header (1 word)] [NormalParams...] [TranslateParams...]
```

Header: bits 0-5 = translate_params_size, bits 6-11 = normal_params_size, bits 16-31 = command_id.

#### IPC Descriptor Types

| Type | Value | Purpose |
|------|-------|---------|
| CopyHandle | `0x00` | Copy handle(s) to other process |
| MoveHandle | `0x10` | Move handle(s) to other process |
| CallingPid | `0x20` | Insert caller's PID |
| StaticBuffer | `0x02` | Copy data buffer (in/out) |
| PXIBuffer | `0x04` | Physical address buffer for PXI |
| MappedBuffer | `0x08` | Memory-mapped shared buffer |

#### IPC Translation Flow

1. `ServerSession::HandleSyncRequest()` reads raw command buffer from guest TLS
2. `PopulateFromIncomingCommandBuffer()` iterates translate params:
   - **Handle descriptors**: resolve kernel handles from source process, store object pointers, patch command buffer with HLE-local IDs
   - **StaticBuffer**: read data from source process memory into `std::vector<u8>`
   - **MappedBuffer**: create wrapper capturing address, size, permissions, process
3. Service handler processes the request
4. `WriteToOutgoingCommandBuffer()` reverses translation for the response

#### IPC Helpers

- **`RequestParser`**: reads incoming command buffer. `Pop<T>()` for normal params, `PopObject<T>()` for handle descriptors, `PopStaticBuffer()` / `PopMappedBuffer()` for data
- **`RequestBuilder`**: writes outgoing command buffer. `Push()` for normal params, `PushCopyObjects()` / `PushMoveObjects()` for handles, `PushStaticBuffer()` for data

#### Service Handler Patterns

**GSP_GPU** -- command list submission:
```cpp
void GSP_GPU::WriteHWRegs(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 reg_addr = rp.Pop<u32>();
    const u32 size = rp.Pop<u32>();
    const auto src_data = rp.PopStaticBuffer();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(GSP::WriteHWRegs(reg_addr, size, src_data, system.GPU()));
}
```

**HID** -- timing-driven input updates:
Uses a `Module` class owning shared memory and events. Pad updates run as timing events (`BASE_CLOCK_RATE_ARM11 / 234` Hz), writing directly to HID shared memory and signaling pad/touch events.

**FS_USER** -- async I/O pattern:
Uses `RunOnThreadWorker()` to run blocking I/O on a separate thread, with result function back on the emulator thread to write the IPC response.

#### All Service Modules

| Module | Purpose |
|--------|---------|
| FS | Filesystem, archive I/O (80+ commands) |
| GSP | GPU command list submission, display transfer |
| HID | Pad, circle pad, touch, accelerometer, gyro |
| DSP | Audio processing, binary loading |
| APT (NS) | Applet lifecycle, home menu |
| CFG | System settings (language, region) |
| CAM | Camera capture, Y2R color conversion |
| NFC | Amiibo read/write |
| HTTP/SOC/SSL | Network stack |
| BOSS | SpotPass background downloads |
| CECD | StreetPass data |
| FRD | Friend list, presence |
| AM | Title installation, CIA management |
| LDR | CRO/CRS dynamic module loading |
| PXI | Firmware crypto operations |
| PLGLDR | 3GX plugin loading |
| PM | Process manager |
| ERR | Error dialog applet |
| AC | WiFi connection management |
| ACT | Nintendo Network account |
| DLP | Local multiplayer game sharing |
| IR | IR service, C-stick, ZL/ZR |
| MIC | Microphone capture |
| MVD | Hardware video decoding |
| NDM | Network state management |
| NEWS | System notifications |
| NIM | Title download |
| NWM | Local wireless, UDS |
| PTM | System time, play history |
| QTM | Face detection |
| CSND | Legacy sound channel |
| MCU | MCU hardware (RTC, LED) |

Stubbed (no HLE): CDC, GPIO, I2C, MP, PDN, SPI.

### Timing Model (`core_timing.h`, `core_timing.cpp`)

#### Base Clock

```cpp
constexpr u64 BASE_CLOCK_RATE_ARM11 = 268111856; // Hz
```

Conversion functions: `msToCycles()`, `usToCycles()`, `nsToCycles()`, `cyclesToNs()`, etc.

#### Event System

`TimingEventType` -- registered event with callback and name. Created via `RegisterEvent()`, returns a stable pointer into an `unordered_map`.

`Event` -- scheduled event with `time` (absolute tick count), `fifo_order` (tiebreaker), `user_data`, and `type` pointer.

#### Per-Core Timer

Each CPU core gets a `Timing::Timer` with:
- `event_queue` -- min-heap of Events sorted by `(time, fifo_order)`
- `ts_queue` -- `MPSCQueue<Event>` for thread-safe scheduling from non-emulator threads
- `slice_length` -- current time slice length
- `downcount` -- cycles remaining until next event or slice end
- `executed_ticks` -- total cycles executed
- `cpu_clock_scale` -- scaling factor for under/overclocking

#### Slice Management

`MAX_SLICE_LENGTH = BASE_CLOCK_RATE_ARM11 / 234` (~1,145,777 cycles, ~4.27 ms). This is the HID pad update interval -- the smallest regularly-scheduled event.

`SetNextSlice()`: `slice_length = min(event_queue.front().time - executed_ticks, MAX_SLICE_LENGTH)`. `downcount = slice_length`.

`AddTicks()`: called by CPU after each instruction block. `downcount -= ticks * cpu_clock_scale`. When `downcount` reaches 0, the CPU dispatcher calls `Advance()`.

#### Advance()

1. Moves events from MPSC queue to event queue
2. Computes `cycles_executed = slice_length - downcount`
3. Updates `executed_ticks`, resets `slice_length = 0`, `downcount = 0`
4. Fires all events where `time <= executed_ticks`, passing `cycles_late`
5. Sets `is_timer_sane = false`

#### ForceExceptionCheck()

Shortens the current slice so the CPU breaks out sooner and fires a newly-scheduled event. `downcount = min(downcount, cycles)`.

#### Idle()

When CPU has nothing to execute: `idled_cycles += downcount; downcount = 0`. Immediately ends the slice.

#### Core-Timer Relationship

One `Timer` per CPU core (2 cores for 3DS: core 0 = application, core 1 = system). `current_timer` is switched via `SetCurrentTimer(core_id)` before executing each core. `GetGlobalTicks()` returns the maximum tick across all timers.

### Filesystem (`src/core/file_sys/`)

Archive-based I/O system. `ArchiveManager` maps archive IDs to backends.

**Archive types:** SaveData, ExtSaveData, SDMC, NAND, NCCH (game content), SelfNCCH (running game's RomFS), SystemSaveData, Artic (remote).

**Container parsers:** NCCH/CXI/CCI, CIA, TMD, Ticket, SeedDB, OTP.

**Other:** LayeredFS (mod support), IVFC archive (RomFS integrity), plugin_3gx, patch system.

### Loaders (`src/core/loader/`)

`GetLoader()` inspects magic bytes, returns the appropriate `AppLoader` subclass.

Supported formats: CCI/CXI (.3ds/.cxi), CIA, ELF, 3DSX (homebrew), Artic (remote).

### Save States (`savestate.h`, `savestate.cpp`)

#### CSTHeader

```
[4B "CST"+0x1B] [8B program_id] [20B revision hash] [8B timestamp] [20B build_name] [4B zero] [192B reserved]
```

Total: 256 bytes, followed by ZSTD-compressed serialized data.

#### Serialization Flow

**Save:**
1. `System::SaveState()` creates `std::ostringstream`
2. Serializes entire `System` object via Boost.Serialization: `oa &* this`
3. Compresses with ZSTD
4. Writes CSTHeader + compressed data to file

**Load:**
1. Validates header (magic, program_id, revision)
2. Decompresses with ZSTD
3. Deserializes entire `System` object

#### Versioning

`hash_to_version` map in `savestate_data.h` maps 40-char SHA1 git hashes to build names. During validation:
- If revision matches current build: status = `OK`
- Otherwise: looks up hash, sets `RevisionDismatch`

#### Limitations

- Cross-version loading NOT supported (only exact revision matches are safe)
- Cannot load while connected to multiplayer
- Only games whose loader supports save states can use this feature
- Maximum 11 slots (0-10)
- Any structural change to serialized members breaks compatibility

### Other Core Components

- **GDB stub** (`gdbstub/`) -- Remote debugging via GDB protocol, conditional on `ENABLE_GDBSTUB`
- **RPC server** (`rpc/`) -- TCP/UDP for external tools to read/write memory, conditional on `ENABLE_SCRIPTING`
- **Cheat engine** (`cheats/`) -- Gateway/ActionReplay format, runs on timing callbacks
- **Movie** (`movie.h`) -- Input recording/playback (TAS), CTM format
- **Tracer** (`tracer/`) -- IPC call recording for debugging
- **Hardware** (`hw/`) -- AES, RSA, ECC crypto, Y2R color conversion

---

## Video Core (`src/video_core/`)

GPU emulation. The 3DS GPU is called PICA200.

### GPU (`gpu.h`)

`VideoCore::GPU` owns the PICA state, renderer, and debug tools. Called by the GSP service for command list submission, memory fill, and display transfer.

### PICA200 (`pica/`)

Register-based command processor. Key components:

- **Command list processing** -- Reads GPU commands from memory, applies delays
- **Vertex loading** -- Packed attribute formats, per-vertex
- **Geometry pipeline** -- Vertex shader + optional geometry shader
- **Primitive assembly** -- Triangle list/strip/fan
- **Fragment processing** -- TEV combiner stages, texturing, lighting (4 lights, 24 LUTs), fog, procedural textures

**Register files** (`regs_*.h`): internal, external, framebuffer, texturing, lighting, rasterizer, pipeline, shader.

### Shader Pipeline

How 3DS PICA shader programs become host GPU code:

#### Vertex/Geometry Shaders (CPU-side execution)

1. PICA binary loaded from memory
2. JIT compiled to native x86_64/ARM64 via `shader_jit_x64_compiler` / `shader_jit_a64_compiler`
3. Fallback: `shader_interpreter` (portable, slower)

**JIT Compilation Flow (`shader_jit_x64_compiler.cpp`):**

1. `Compile()` stores pointers to program code and swizzle data
2. `FindReturnOffsets()` pre-scans for all `CALL` instructions to identify return locations
3. Pushes callee-saved registers, sets up 16-byte-aligned stack
4. Loads `UNIFORMS` (r9), `STATE` (r15) from ABI parameters
5. Loads address registers, loop counter, conditional code from `ShaderUnit` state
6. Loads constant vectors: `ONE = xmm14` ([1,1,1,1]), `NEGBIT = xmm15` ([-0,-0,-0,-0])
7. Jumps to entry point, compiles entire program via `Compile_Block()`
8. Appends LOG2/EXP2 subroutines if used
9. Calls `ready()` to finalize JIT memory

**Register Allocation (fixed):**
- r9 (UNIFORMS): pointer to float/bool/int uniform arrays
- r10/r11 (ADDROFFS_REG_0/1): address offset registers from MOVA
- r12d (LOOPCOUNT_REG): loop iteration counter
- r13/r14 (COND0/COND1): CMP results
- r15 (STATE): pointer to ShaderUnit
- xmm14 (ONE), xmm15 (NEGBIT): constants
- xmm0 (SCRATCH), xmm1/xmm2/xmm3 (SRC1/SRC2/SRC3): operands

**Special Operations:**
- `Compile_SanitizedMul`: handles PICA's `0 * inf = 0` semantics via AVX-512 or SSE fallback
- `Compile_RCP/RSQ`: uses `vrcp14ss`/`vrsqrt14ss` on AVX-512 (14-bit), otherwise `rcpss`/`rsqrtss` (12-bit)
- `Compile_Log2/Exp2`: software minimax polynomial approximation with FMA

#### Fragment Shaders (GPU-side execution)

1. `FSConfig` built from current PICA state
2. OpenGL: `GLSLFragmentModule` generates GLSL string
3. Vulkan: `SPIRV::FragmentModule` generates SPIR-V bytecode via `sirit`
4. `ShaderManager` caches compiled programs keyed by `FSConfig`

**FSConfig** (`pica_fs_config.h`): master configuration struct capturing all PICA state affecting fragment shader generation. Contains:
- `FramebufferConfig`: alpha test, scissor, depth, logic op, blending
- `TextureConfig`: texture types, 6 TEV stages, fog, wrap modes
- `LightConfig`: enable, source count, bump mode, 7 LUT configs, 8 light configs
- `ProcTexConfig`: enable, coordinate source, LOD range, combiners

Constructed directly from `Pica::RegsInternal`, extracting exactly the fields that affect shader output. Trivially copyable, with compile-time `StructHash()` for cache key generation.

**GLSL Generation Flow (`glsl_fs_shader_gen.cpp`):**
1. Emit `main()` with `rounded_primary_color = byteround(primary_color)`
2. Early-out if `alpha_test_func == Never`
3. `WriteScissor()` -- scissor test discard
4. `WriteDepth()` -- PICA depth transformation
5. `WriteLighting()` -- full PICA lighting (up to 8 lights)
6. Loop over 6 TEV stages via `WriteTevStage(index)`:
   - Compute color/alpha results from sources + modifiers
   - Apply combiner operation (Replace/Modulate/Add/Lerp/Subtract/Dot3/etc.)
   - Byte-round and scale
7. `WriteAlphaTestCondition()` -- alpha test discard
8. `WriteFog()` / `WriteGas()`
9. Shadow rendering or `gl_FragDepth` output
10. `WriteBlending()` / `WriteLogicOp()`

**SPIR-V Generation** (`spv_fs_shader_gen.cpp`): same logical flow but constructs SPIR-V 1.3 bytecode programmatically using the Sirit library. Each intermediate value has a typed `Id`. Uses `OpKill()` for discard, explicit control flow for scissor/alpha test.

**Disk caching:** Both GL and Vulkan persist compiled shaders to disk for faster startup.

### Renderer Backends

| Backend | Requirement | Notes |
|---------|------------|-------|
| **OpenGL** | 4.3+ / ES 3.2 | Most mature, driver bug workarounds for AMD/Intel/Mali |
| **Vulkan** | 1.1 | Production-ready, threaded pipeline compilation, zstd-compressed cache, geometry shaders disabled on Android (tile-based GPU cost) |
| **Software** | None | CPU rasterizer, incomplete (no min/mag filtering, no mipmaps) |

All backends implement `RasterizerInterface` and share the rasterizer cache.

### Rasterizer Cache (`rasterizer_cache/`)

Template-based surface/texture cache shared by GL and Vulkan.

#### Interval-Based Page Tracking

Uses **Boost.ICL (Interval Container Library)** for efficient range operations:

- **`dirty_regions`** (`SurfaceMap`): maps physical address intervals to the `SurfaceId` that owns the dirty data. Enables efficient range-based dirty tracking.
- **`cached_pages`** (`PageMap`): reference-counted interval map tracking which physical pages are rasterizer-cached (256 KiB granularity, coarser than memory system's 4 KiB).
- **Page table**: `robin_pg_map<u64, vector<SurfaceId>>` maps page numbers (physical address >> 18) to lists of surfaces covering that page.

#### Surface Management

`SurfaceParams` describes a surface: `addr`, `end`, `size`, `width`, `height`, `stride`, `levels`, `res_scale`, `is_tiled`, `pixel_format`, `texture_type`, `type` (Color/Depth/Texture/Fill).

`SurfaceBase` extends with runtime state: `flags` (Registered/Picked/Tracked/Custom/ShadowSource/RenderTarget), `invalid_regions` (interval set), `modification_tick`.

Invalidation/validation:
- `MarkInvalid(interval)` inserts into `invalid_regions`
- `MarkValid(interval)` erases from `invalid_regions`
- `IsRegionValid(interval)` checks `invalid_regions.find(interval) == end()`

#### Dirty Tracking Flow

1. **Surface creation**: `CreateSurface()` creates with `MarkInvalid(full_interval)` -- entire surface starts invalid
2. **Registration**: `RegisterSurface()` adds to page table, calls `UpdatePagesCachedCount(+1)` which triggers `RasterizerMarkRegionCached(true)` to switch page types
3. **Validation** (`ValidateSurface`): when GPU needs a surface region:
   - Finds invalid sub-intervals intersecting the requested range
   - Tries copy from another valid surface (`FindMatch<MatchFlags::Copy>`)
   - Tries reinterpretation from different-format surface
   - Falls back to uploading from CPU memory
   - Generates mipmaps if rescaled
4. **GPU write** (`InvalidateRegion`): when GPU renders to a surface:
   - Marks that surface valid, adds to `dirty_regions`
   - All other overlapping surfaces get intersecting regions marked invalid
   - Fully-invalid surfaces are unregistered (sentenced for GC)
5. **CPU read** (`FlushRegion`): when CPU needs to read rasterizer-cached memory:
   - Iterates dirty regions in flush range
   - Downloads each from GPU to CPU memory
   - Removes flushed intervals from `dirty_regions`

#### Garbage Collection

`RunGarbageCollector()` iterates the `sentenced` list. If a surface has been sentenced for more than `RemoveThreshold()` frames, it is fully deleted. Prevents thrashing when surfaces are rapidly created/destroyed.

#### Acceleration Paths

Three PICA hardware operations are intercepted:
1. **`AccelerateTextureCopy`** -- GPU-side texture copy with gap handling
2. **`AccelerateDisplayTransfer`** -- framebuffer copy/transform with format conversion, tiling, flipping
3. **`AccelerateFill`** -- memory fill creates a Fill-type surface that lazily writes fill values on download

All operate purely on the GPU side, avoiding expensive CPU<->GPU transfers.

### Custom Textures (`custom_textures/`)

#### Texture Hashing and Loading

`CustomTexManager::FindCustomTextures()`:
1. Gets `title_id` from current process's codeset
2. Scans `load/textures/<title_id_16hex>/` directory
3. Reads `pack.json` config file
4. Parses filenames: `tex1_<width>x<height>_<hash>_<format>.<extension>`
5. Creates `Material` objects keyed by hash in `material_map`

**Supported formats:** PNG, DDS, KTX
**Material types:** Color (diffuse/albedo) and Normal map (`.norm` suffix)

#### Material System

`Material` groups up to 2 textures (color + normal) by hash. States: `None`, `Pending`, `Decoded`, `Failed`. Thread-safe with `std::mutex decode_mutex`.

#### Async Loading

`Decode()` queues material loading on worker threads if `async_custom_loading` is enabled. `TickFrame()` processes up to `MAX_UPLOADS_PER_TICK = 8` uploads per frame.

#### Preloading

Reserves up to `sys_mem / 2` or `sys_mem - 2GiB` memory. Iterates all materials, calling `material->LoadFromDisk()`. Respects `stop_run` atomic for cancellation.

#### Supported Custom Formats

`CustomPixelFormat`: RGBA8, BC1, BC3, BC5, BC7, ASTC4, ASTC6, ASTC8.

---

## Audio Core (`src/audio_core/`)

### DSP Emulation

- **HLE** (`hle/`) -- High-level emulation, 48 voices, mixing, biquad filter, AAC decoder. Default mode.
- **LLE** (`lle/`) -- Loads real DSP firmware binary and runs it in an interpreter. More accurate but slower.

### Backends

| Backend | Type | Notes |
|---------|------|-------|
| **Cubeb** | Output + Input | Default, cross-platform |
| **OpenAL** | Output + Input | Fallback |
| **SDL2** | Output only | |
| **LibRetro** | Output + Input | For RetroArch |
| **Null/Static** | No-op / Test | |

`Sink` (output) and `Input` (microphone) are abstract interfaces. `TimeStretcher` handles audio-video sync.

---

## Networking (`src/network/`)

### Multiplayer

`Room` (server) and `RoomMember` (client) communicate over TCP/UDP with a custom protocol. Features: chat, game info sync, kick/ban, room browser via web service announcement.

Default port: 24872. Max 254 concurrent connections.

### Artic Base Protocol

Allows running 3DS games on a real 3DS console while rendering on PC.

#### Transport

- Raw TCP sockets with `connect()` and timeout via `poll()`
- `SERVER_VERSION = 2`
- Non-blocking mode via `SetNonBlock()`

#### Request/Response Model

**Request**: wraps a `RequestPacket` with parameters:
- `requestID` (u32) + 32-byte method name + `parameterCount`
- Parameter types: `IN_INTEGER_8/16/32/64`, `IN_SMALL_BUFFER` (inline, <= 0x1C bytes), `IN_BIG_BUFFER` (out-of-band via handler thread)

**Response**: holds `ArticResult` (SUCCESS, METHOD_NOT_FOUND, METHOD_ERROR, PROVIDE_INPUT) and result data. Can extract typed values: `GetResponseS32()`, `GetResponseS64()`, `GetResponseFloat()`, `GetResponseBuffer()`.

#### UDP Streaming

`UDPStream` class creates a UDP socket for high-frequency data (GPU framebuffers). Configurable `buffer_size` and `read_interval`. `GetLastPacket()` returns the most recent packet.

#### Keepalive

Background `ping_thread` sends periodic pings. Enable/disable via `SetPingEnabled()`.

#### What It Proxies

- **Filesystem**: HLE service calls serialized and sent to server
- **GPU commands**: Framebuffer data streamed via UDP for low latency
- **Input**: Controller/touch data sent to server for injection into 3DS input system

---

## Frontends

### Qt Desktop (`src/citra_qt/`)

Full-featured Qt6 GUI: game list, configuration dialogs, debugger (registers, memory, breakpoints, callstack), multiplayer lobby/chat, camera config, movie recording, video dumping, hotkeys, Discord integration, update checker.

#### Main Event Flow

`GMainWindow` (QMainWindow subclass) is the central hub:

1. Constructor: logging init, command-line parsing, translation loading, UI setup
2. `InitializeWidgets()`: creates `render_window`, `secondary_window`, `game_list`, `loading_screen`, status bar
3. `InitializeDebugWidgets()`: registers dock widgets for registers, GPU commands, breakpoints, vertex shader, tracing, wait tree, IPC recorder, LLE services
4. `InitializeHotkeys()`: loads hotkeys from registry, links QActions and QShortcuts
5. Connects signals: `EmulationStarting`, `EmulationStopping`, etc.

#### GPU Render Loop

`EmuThread` (QThread subclass):
- `run()`: acquires graphics context, preloads custom textures, enters main loop calling `system.RunLoop()`
- Thread-safe control: `SetRunning()`, `IsRunning()`, `RequestStop()`, `ExecStep()`

`GRenderWindow` (QWidget + Frontend::EmuWindow dual inheritance):
- Bridges Qt widget system with emulator frontend interface
- `InitRenderTarget()`: creates dummy RenderWidget, initializes GL/Vulkan/Software based on settings, extracts native window handles (HWND, Metal layer, Wayland/X11)
- OpenGL: `OpenGLRenderWidget::Present()` makes context current, calls `renderer.TryPresent()`, swaps buffers, calls `glFinish()`
- Paint loop: `paintEvent` calls `Present()` then `update()` for continuous rendering
- Shared contexts via `QOpenGLContext::setShareContext()` for dual-screen rendering

#### Game List

`GameList` widget with `QTreeView` and `QStandardItemModel`. Columns: name, compatibility, region, file type, size, play time.

`GameListWorker` (QRunnable on thread pool):
- Recursively scans directories using `FileUtil::ScanDirectoryTree()`
- Validates games via `Loader::GetLoader()` and `IsExecutable()`
- Emits `EntryReady` for each found game
- Emits `Finished` with directories to watch via `QFileSystemWatcher`
- Can be cancelled via `std::atomic_bool stop_processing`

### Android (`src/android/`)

Kotlin UI with JNI bridge to C++ core. MVVM architecture, Jetpack Navigation.

#### JNI Bridge

All JNI functions follow `Java_org_citra_citra_1emu_NativeLibrary_<method>` pattern.

**Surface management:** `surfaceChanged`/`surfaceDestroyed` (ANativeWindow lifecycle), `doFrame` (per-frame presentation), `swapScreens`, `updateFramebuffer`.

**Emulation control:** `run(path)` (main entry), `stopEmulation`, `pauseEmulation`/`unPauseEmulation`, `isRunning`.

**Input:** `onGamePadEvent`, `onGamePadMoveEvent`, `onGamePadAxisEvent`, `onTouchEvent`/`onTouchMoved`, `onSecondaryTouchEvent`.

**File management:** `compressFileNative`/`decompressFileNative`, `getInstalledGamePathsImpl`, `installCIA`, `uninstallTitle`.

**Save states:** `getSavestateInfo`, `saveState`/`loadState` (via `Core::System::SendSignal()`).

**Multiplayer:** `initMultiplayer`, `netPlayGetPublicRooms`, `netPlayCreateRoom`, `netPlayJoinRoom`, etc.

**GPU driver:** `initializeGpuDriver` (custom Adreno drivers via adrenotools), `supportsCustomDriverLoading`.

#### JNI Method ID Caching

`IDCache` namespace provides static accessors for JNI class references and method IDs. Uses `SharedGlobalRef<T>` smart pointer with custom deleter calling `DeleteGlobalRef()`.

#### Android EmuWindow

`EmuWindow_Android` extends `Frontend::EmuWindow`:
- Holds `ANativeWindow*` render and host windows
- `OnSurfaceChanged()`: updates dimensions, stops presenting, calls `OnFramebufferSizeChanged()`
- OpenGL variant: adds EGL context management (`EGLConfig`, `EGLSurface`, `EGLContext`, `EGLDisplay`)
- `PresentingState` enum: `Initial`, `Running`, `Stopped`
- Supports shared contexts for dual-screen rendering

### CLI (`src/citra_cli/`)

Headless emulation, compression tools. Minimal, no GUI.

### LibRetro (`src/citra_libretro/`)

RetroArch core implementation. Provides `retro_*` API. Vulkan support included. Built with `ENABLE_LIBRETRO=ON`, disables SDL2/Qt/Room/GDB/Scripting.

### Room Server (`src/citra_room/` + `src/citra_room_standalone/`)

Standalone multiplayer room server. Can run independently or embedded in the Qt frontend.

---

## Common (`src/common/`)

Shared utilities used across all subsystems:

- **Types** -- `common_types.h`: u8-u64, s8-s64, VAddr, PAddr
- **Settings** -- `settings.h`: all emulator configuration enums and structs
- **File I/O** -- `file_util.h`: IOFile, directory scanning, path manipulation
- **Logging** -- `logging/`: category-based logging framework
- **Threading** -- `thread_worker.h`, `threadsafe_queue.h`, `ring_buffer.h`
- **Memory** -- `memory_ref.h`: safe memory references for serialization
- **Math** -- `vector_math.h`, `math_util.h`
- **Bit manipulation** -- `bit_field.h`: `BitField` template for register access
- **Compression** -- `zstd_compression.h`
- **Hash** -- `cityhash.h`
- **Serialization** -- `serialization/`: Boost serialization helpers

---

## Build System

CMake 3.25+, C++20.

### Library Targets

| Target | Type | Dependencies |
|--------|------|-------------|
| `citra_common` | Static lib | None (leaf library) |
| `citra_core` | Static lib | citra_common, audio_core, video_core, network |
| `video_core` | Static lib | citra_common, externals (sirit, glad, vulkan-headers) |
| `audio_core` | Static lib | citra_common, cubeb (optional), openal (optional) |
| `network` | Static lib | citra_common, enet |
| `input_common` | Static lib | citra_common, SDL2 (optional) |
| `web_service` | Static lib | citra_common, httplib, json |

### Executable Targets

| Target | Description |
|--------|------------|
| `citra_qt` | Qt6 desktop frontend |
| `citra_cli` | CLI frontend |
| `citra_libretro` | RetroArch core (.so/.dll) |
| `citra_room_standalone` | Standalone room server |
| `citra-android` | Android JNI library |

### Key Options

`ENABLE_QT`, `ENABLE_SDL2`, `ENABLE_OPENGL`, `ENABLE_VULKAN`, `ENABLE_SOFTWARE_RENDERER`, `ENABLE_LIBRETRO`, `ENABLE_ROOM`, `ENABLE_WEB_SERVICE`, `ENABLE_SCRIPTING`, `ENABLE_GDBSTUB`, `ENABLE_CUBEB`, `ENABLE_OPENAL`, `ENABLE_LTO`, `ENABLE_DEVELOPER_OPTIONS`.

---

## Data Flow (Simplified)

```
ROM File
  |
  v
AppLoader (ncch/3dsx/elf) --> Kernel::Process (memory layout, threads)
  |
  v
Core::System::RunLoop()
  |
  +---> ARM CPU cores (Dynarmic JIT)
  |       |
  |       +---> SVCs --> Kernel (thread sync, IPC)
  |       |
  |       +---> Service IPC --> HLE Services
  |                               |
  |                               +---> GSP --> GPU::Execute()
  |                               |               |
  |                               |               +---> PICA200 command list
  |                               |               +---> Vertex/Fragment shaders
  |                               |               +---> Renderer (GL/VK/SW)
  |                               |               +---> Rasterizer Cache
  |                               |               +---> Framebuffer --> Screen
  |                               |
  |                               +---> DSP --> Audio mixing --> Sink (Cubeb/OpenAL)
  |                               |
  |                               +---> HID --> Input from host
  |                               |
  |                               +---> FS --> Archive I/O --> Disk
  |
  +---> Timing events (timers, cheats, GPU sync)
```

---

## Known Issues and Technical Debt

The codebase contains 614 TODO, 23 HACK, and 17 WORKAROUND comments. Key areas:

- **GPU accuracy**: early depth testing, GAS mode, LOD bias, ClampToEdge2/ClampToBorder2 wrap modes not fully implemented
- **Software renderer**: min/mag filtering and mipmaps not implemented
- **Shader precision**: RCP/RSQRT are rough approximations
- **PICA timing**: delay constants are estimated, not calibrated to real hardware
- **Rasterizer cache**: stride reinterpretation needs proper GPU shader implementation
- **Doxyfile**: `PROJECT_NAME` still says "Citra" (line 35)
