# The Azahar RPC Protocol

The RPC protocol enables third-party programs to directly call procedures in
the Azahar emulator. This document outlines the details of the protocol and how
to properly communicate with Azahar's RPC server.

<details open="open">
<summary>Table of Contents</summary>

1. [Introduction]
2. [RPC Packet Structure]
    1. [General Format]
    2. [Header Structure]
    3. [Invalid RPC Packet]
        1. [Example Invalid Response Packet]
    4. [Protocol Version]
3. [Request Types]
    1. [ReadMemory]
        1. [ReadMemory Request Body]
        2. [Example ReadMemory Request Packet]
        3. [ReadMemory Response Body]
        5. [Example ReadMemory Response Packet]
        4. [Invalid ReadMemory Response]
    2. [WriteMemory]
        1. [Writeable Memory Regions]
        2. [WriteMemory Request Body]
        3. [Example WriteMemory Request Packet]
        4. [WriteMemory Response Body]
        6. [Example WriteMemory Response Packet]
        5. [Invalid WriteMemory Response]
4. [Glossary]
5. [Copyright Notice]

</details>

## Introduction

The server allows third-party programs to interact with Azahar via Remote
Procedure Calls (RPC), enabling actions such as reading and modifying memory
with minimal setup required.

> [!NOTE]
> It is possible to disable the RPC server or change the server's port number
> in the **Debug** configuration menu.

- The server listens for UDP packets on `127.0.0.1` (localhost).
    - The port number is `45987` by default.
- Communication is stateless and always initiated by the client.
- Each request triggers a corresponding response from the server.
- All RPC packets are structured with a header and an optional payload (body).

```
+--------+          Request      +--------+------------+
|        |>---(A)-- Packet ----->| RPC    > (B)        |
| Client |                       | Server | Internal   |
|        |<---(C)-- Response ---<|        < Processing |
+--------+          Packet       +--------+------------+

        Figure 1: Communication Code Flow
```

Refer to [`dist/scripting/citra.py`][citra.py] for a Python implementation of
a client interacting with the RPC server.

## RPC Packet Structure

Each RPC packet sent between the client and the server must be structured in a
specific format, as detailed below.

Relevant source code: [`src/core/rpc/packet.h`][packet.h]

### General Format

Packets use the machines native byte order. Each packet consists of a 16-byte
header followed by a 0–32 byte body (maximum 48 bytes total).

> [!TIP]
> Due to the small packet size, larger operations must be divided in chunks.

### Header Structure

The header consists of four 4-byte fields, all of which are required. The total
header size must equal 16 bytes.

| Field        | Offset | Size | Type  | Valid Values                | Description                                      |
|--------------|--------|------|-------|-----------------------------|--------------------------------------------------|
| Version      | `0x0`  | `4`  | `u32` | `1`                         | The protocol version.                            |
| Request ID   | `0x4`  | `4`  | `u32` | `0x00000000` - `0xFFFFFFFF` | Random, unique identifier for tracking requests. |
| Request Type | `0x08` | `4`  | `u32` | `1`, `2`                    | The request type (see [Request Types]).          |
| Body Size    | `0xC`  | `4`  | `u32` | `0` - `32`                  | The number of bytes in the request body.         |

Response packets from the server are expected to echo the `Version`,
`Request ID`, and `Request Type` of the client's request packet.

### Invalid RPC Packet

If the server cannot validate an RPC packet, it responds with a packet whose
`Body Size` is zero.

Future revisions may include explicit error codes in response packets.

#### Example Invalid Response Packet

This example constructs an invalid response packet for request type 1.

| Field        | Value       | Hexadecimal  |
|--------------|-------------|--------------|
| Version      | `1`         | `0x00000001` |
| Request ID   | `305419896` | `0x12345678` |
| Request Type | `1`         | `0x00000001` |
| Body Size    | `0`         | `0x00000000` |

Below is the hexdump (little-endian) representation of the packet, which
developers can use to verify the data structure:

```
01 00 00 00   78 56 34 12   01 00 00 00   00 00 00 00
```

### Protocol Version

The protocol version is specified in the first four bytes of the RPC packet
header. This version determines what request the server expects from the
client, and what response the client expects from the server.

The protocol version should be bumped any time backwards-incompatible changes
are made.

> [!NOTE]
> If the version of a client request exceeds the version of the RPC server,
> the request is **not** processed and an [invalid RPC packet] is sent in
> response.

## Request Types

Relevant source code:
[`src/core/rpc/rpc_server.cpp`][rpc_server.cpp]

The request type determines what data must be sent in the request body and what
response to expect from the RPC server.

| Type | Name          | Description                                       |
|------|---------------|---------------------------------------------------|
| 1    | [ReadMemory]  | Read data from a memory address.                  |
| 2    | [WriteMemory] | Write data to a memory address.                   |
<!--
These request types need to be merged from a PR.
| 3    | [SendKey]     | Emulate a key press.                              |
| 4    | [SendSignal]  | Call `system.SendSignal` to control system state. |
-->

### ReadMemory

Retrieves a specified number of bytes from a given memory address.

> [!NOTE]
> Memory read occurs asynchronously from the state of the emulator.

#### ReadMemory Request Body

The request body is 8 bytes:

- 4 bytes for the read address.
- 4 bytes for the read size.

| Field        | Offset in Body | Size | Type  | Valid Values                    | Description                                   |
|--------------|----------------|------|-------|---------------------------------|-----------------------------------------------|
| Read Address | `0x0`          | `4`  | `u32` | Any valid mapped memory address | The memory address to start reading from.     |
| Read Size    | `0x4`          | `4`  | `u32` | `0` - `32`                      | The number of bytes to read from the address. |

#### Example ReadMemory Request Packet

This example constructs a `ReadMemory` request to read **6 bytes** from the
memory address `0xC0FFEE00`.

| Field        | Value        | Hexadecimal  |
|--------------|--------------|--------------|
| Version      | `1`          | `0x00000001` |
| Request ID   | `305419896`  | `0x12345678` |
| Request Type | `1`          | `0x00000001` |
| Body Size    | `8`          | `0x00000008` |
|              |              |              |
| Read Address | `3237998080` | `0xC0FFEE00` |
| Read Size    | `6`          | `0x00000006` |

Below is the hexdump (little-endian) representation of the packet, which
developers can use to verify the data structure:

```
01 00 00 00   78 56 34 12   01 00 00 00   08 00 00 00
00 EE FF C0   06 00 00 00
```

#### ReadMemory Response Body

The body consists of one field of `Read Size` bytes.

| Field     | Offset in Body | Size        | Type   | Valid Values      | Description                    |
|-----------|----------------|-------------|--------|-------------------|--------------------------------|
| Read Data | `0x0`          | `Read Size` | `u8[]` | Any byte sequence | The requested memory contents. |

#### Example ReadMemory Response Packet

This example constructs a `ReadMemory` response to the above request.

| Field        | Value                         | Hexadecimal      |
|--------------|-------------------------------|------------------|
| Version      | `1`                           | `0x00000001`     |
| Request ID   | `305419896`                   | `0x12345678`     |
| Request Type | `1`                           | `0x00000001`     |
| Body Size    | `6`                           | `0x00000006`     |
|              |                               |                  |
| Read Data    | `b"\xde\xc0\xde\xde\xc0\xde"` | `0xDEC0DEDEC0DE` |

Below is the hexdump (little-endian) representation of the packet, which
developers can use to verify the data structure:

```
01 00 00 00   78 56 34 12   01 00 00 00   06 00 00 00
DE C0 DE DE   C0 DE
```

#### Invalid ReadMemory Response

The RPC packet will have a `Body Size` of zero if `Read Size` exceeds 32 bytes.
See [invalid RPC packet] for details.

### WriteMemory

Writes a specified number of bytes to a given memory address.

> [!NOTE]
> Memory write occurs asynchronously from the state of the emulator.

#### Writeable Memory Regions

> [!IMPORTANT]
> Only specific virtual user-space memory regions can be written to. \
> Relevant code: [`src/core/memory.h`][memory.h]

| Memory Region  | Start Address | End Address  | Region Size | Description                                                                                    |
|----------------|---------------|--------------|-------------|------------------------------------------------------------------------------------------------|
| Process Image  | `0x00100000`  | `0x04000000` | 63 MiB      | Where the application text, data and bss reside.                                               |
| Heap           | `0x08000000`  | `0x10000000` | 128 MiB     | Application heap (includes stack).                                                             |
| N3DS Extra RAM | `0x1E800000`  | `0x1EC00000` | 4 MiB       | New 3DS additional memory. Used by applications and system modules if mapped via the ExHeader. |

#### WriteMemory Request Body

The request body ranges from 9 to 32 bytes:

- 4 bytes for the write address.
- 4 bytes for the write size.
- At least 1 byte for the write data (up to 24 bytes).

| Field         | Offset in Body | Size         | Type   | Valid Values                   | Description                          |
|---------------|----------------|--------------|--------|--------------------------------|--------------------------------------|
| Write Address | `0x0`          | `4`          | `u32`  | See [Writeable Memory Regions] | The memory address to write to.      |
| Write Size    | `0x4`          | `4`          | `u32`  | `1` - `24`                     | The number of bytes in `Write Data`. |
| Write Data    | `0x8`          | `Write Size` | `u8[]` | Any byte sequence              | The data to write to the address.    |

#### Example WriteMemory Request Packet

This example constructs a `WriteMemory` request to write `0xDEC0DEDEC0DE` to
the memory address `0xC0FFEE00`.

| Field         | Value                         | Hexadecimal      |
|---------------|-------------------------------|------------------|
| Version       | `1`                           | `0x00000001`     |
| Request ID    | `305419896`                   | `0x12345678`     |
| Request Type  | `2`                           | `0x00000002`     |
| Body Size     | `10`                          | `0x0000000A`     |
|               |                               |                  |
| Write Address | `3237998080`                  | `0xC0FFEE00`     |
| Write Size    | `6`                           | `0x00000006`     |
| Write Data    | `b"\xde\xc0\xde\xde\xc0\xde"` | `0xDEC0DEDEC0DE` |

Below is the hexdump (little-endian) representation of the packet, which
developers can use to verify the data structure:

```
01 00 00 00   78 56 34 12   02 00 00 00   0A 00 00 00
00 EE FF C0   06 00 00 00   DE C0 DE DE   C0 DE
```

#### WriteMemory Response Body

The response body of a `WriteMemory` call is always empty.
See [invalid RPC packet] for details.

> [!TIP]
> In order to verify that a write operation is successful, an additional
> `ReadMemory` call may be required. However, results can be inconsistent due
> to the emulator's asynchronous request handling.


#### Example WriteMemory Response Packet

This example constructs a `WriteMemory` response to the above request.

| Field        | Value       | Hexadecimal  |
|--------------|-------------|--------------|
| Version      | `1`         | `0x00000001` |
| Request ID   | `305419896` | `0x12345678` |
| Request Type | `2`         | `0x00000002` |
| Body Size    | `0`         | `0x00000000` |

Below is the hexdump (little-endian) representation of the packet, which
developers can use to verify the data structure:

```
01 00 00 00   78 56 34 12   02 00 00 00   00 00 00 00
```

#### Invalid WriteMemory Response

> [!WARNING]
> There is currently no distinction between an valid and invalid response. The
> response packet only acknowledges that a request packet was received,
> regardless of the success of the write operation. This would need to be
> addressed in a future version of the protocol.

A request is technically invalid if the `Write Data` is empty zero or exceeds
24 bytes, or if the `Write Address` is not within one of the
[Writeable Memory Regions].

## Glossary

| Abbreviation | Meaning                 |
|--------------|-------------------------|
| RPC          | Remote Procedure Call   |
| UDP          | User Datagram Protocol  |
| u32          | Unsigned 32-bit integer |
| u8[]         | Array of bytes          |
| MiB          | Mebibyte                |

## Copyright Notice

Copyright Citra Emulator Project / Azahar Emulator Project \
Licensed under GPLv2 or any later version \
Refer to the [license.txt] file included.

[🔼 Back to top][title]

<!-- References -->

[title]: #the-azahar-rpc-protocol "The Azahar RPC Protocol"
[introduction]: #introduction "Introduction"
[rpc packet structure]: #rpc-packet-structure "RPC Packet Structure"
[general format]: #general-format "General Format"
[header structure]: #header-structure "Header Structure"
[invalid rpc packet]: #invalid-rpc-packet "Invalid RPC Packet"
[example invalid response packet]: #example-invalid-response-packet "Example Invalid Response Packet"
[protocol version]: #protocol-version "Protocol Version"
[request types]: #request-types "Request Types"
[readmemory]: #readmemory "ReadMemory"
[readmemory request body]: #readmemory-request-body "ReadMemory Request Body"
[example readmemory request packet]: #example-readmemory-request-packet "Example ReadMemory Request Packet"
[readmemory response body]: #readmemory-response-body "ReadMemory Response Body"
[invalid readmemory response]: #invalid-readmemory-response "Invalid ReadMemory Response"
[example readmemory response packet]: #example-readmemory-response-packet "Example ReadMemory Response Packet"
[writememory]: #writememory "WriteMemory"
[writeable memory regions]: #writeable-memory-regions "Writeable Memory Regions"
[writememory request body]: #writememory-request-body "WriteMemory Request Body"
[example writememory request packet]: #example-writememory-request-packet "Example WriteMemory Request Packet"
[writememory response body]: #writememory-response-body "WriteMemory Response Body"
[invalid writememory response]: #invalid-writememory-response "Invalid WriteMemory Response"
[example writememory response packet]: #example-writememory-response-packet "Example WriteMemory Response Packet"
[glossary]: #glossary "Glossary"
[copyright notice]: #copyright-notice "Copyright Notice"
[license.txt]: https://github.com/azahar-emu/azahar/blob/master/license.txt "license.txt"
[citra.py]: https://github.com/azahar-emu/azahar/blob/master/dist/scripting/citra.py "citra.py"
[packet.h]: https://github.com/azahar-emu/azahar/blob/master/src/core/rpc/packet.h "packet.h"
[rpc_server.cpp]: https://github.com/azahar-emu/azahar/blob/master/src/core/rpc/rpc_server.cpp "rpc_server.cpp"
[memory.h]: https://github.com/azahar-emu/azahar/blob/master/src/core/memory.h "memory.h"
