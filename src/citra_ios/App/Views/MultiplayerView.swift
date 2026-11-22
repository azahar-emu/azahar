// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import SwiftUI

/// Joins, hosts and shows a multiplayer room. Games with local wireless play over the room.
struct MultiplayerView: View {
    @EnvironmentObject private var multiplayer: MultiplayerSession
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if multiplayer.isInRoom {
                    RoomView()
                } else {
                    ConnectView()
                }
            }
            .navigationTitle("Multiplayer")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

private enum ConnectMode: String, CaseIterable, Identifiable {
    case join = "Join"
    case host = "Host"
    var id: String { rawValue }
}

private struct ConnectView: View {
    @EnvironmentObject private var multiplayer: MultiplayerSession

    @AppStorage("multiplayer.address") private var address = ""
    @AppStorage("multiplayer.port") private var port = "24872"
    @AppStorage("multiplayer.nickname") private var nickname = ""
    @AppStorage("multiplayer.roomName") private var roomName = "Neroli Room"
    @AppStorage("multiplayer.maxMembers") private var maxMembers = 4
    @State private var password = ""
    @State private var mode = ConnectMode.join

    private var portNumber: Int { Int(port) ?? 0 }

    var body: some View {
        Form {
            Section {
                Picker("Mode", selection: $mode) {
                    ForEach(ConnectMode.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
            } footer: {
                Text(mode == .join
                     ? "Join a room that somebody hosts, with the address they give you."
                     : "Host a room on this device. Others on the same network join it with this "
                         + "device's address.")
            }

            Section("You") {
                TextField("Nickname", text: $nickname)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                if !nickname.isEmpty && !AZMultiplayer.isValidNickname(nickname) {
                    Text("4 to 20 letters, digits, spaces or . _ -")
                        .font(.caption)
                        .foregroundStyle(.red)
                }
            }

            Section("Room") {
                if mode == .join {
                    TextField("Address", text: $address)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                } else {
                    TextField("Room Name", text: $roomName)
                    Stepper("Players: \(maxMembers)", value: $maxMembers, in: 2...16)
                }
                TextField("Port", text: $port)
                    .keyboardType(.numberPad)
                SecureField("Password (optional)", text: $password)
            }

            Section {
                Button {
                    connect()
                } label: {
                    HStack {
                        Text(mode == .join ? "Join Room" : "Create Room")
                        if multiplayer.isWorking {
                            Spacer()
                            ProgressView()
                        }
                    }
                }
                .disabled(!canConnect || multiplayer.isWorking)
                if let error = multiplayer.errorMessage {
                    Text(error)
                        .font(.callout)
                        .foregroundStyle(.red)
                }
            } footer: {
                Text("Rooms work while a game runs, and need this app to be allowed to use the "
                     + "local network (iOS Settings). Everybody in a room appears to be in "
                     + "wireless range of each other, so games with local wireless can play "
                     + "together. All players need the same game.")
            }
        }
        .onAppear {
            if nickname.isEmpty { nickname = multiplayer.suggestedNickname }
        }
    }

    private var canConnect: Bool {
        guard AZMultiplayer.isValidNickname(nickname), (1...65535).contains(portNumber) else {
            return false
        }
        return mode == .host ? roomName.count >= 3 : !address.isEmpty
    }

    private func connect() {
        if mode == .join {
            multiplayer.join(address: address.trimmingCharacters(in: .whitespaces),
                             port: portNumber, nickname: nickname, password: password)
        } else {
            multiplayer.host(name: roomName, port: portNumber, nickname: nickname,
                             password: password, maxMembers: maxMembers)
        }
    }
}

private struct RoomView: View {
    @EnvironmentObject private var multiplayer: MultiplayerSession
    @State private var draft = ""

    var body: some View {
        VStack(spacing: 0) {
            List {
                Section {
                    LabeledContent("Room", value: multiplayer.roomName)
                    if multiplayer.maxMembers > 0 {
                        LabeledContent(
                            "Players",
                            value: "\(multiplayer.members.count) of \(multiplayer.maxMembers)")
                    }
                    ForEach(multiplayer.members, id: \.self) { member in
                        Text(member)
                            .swipeActions {
                                if multiplayer.isHosting || multiplayer.state == .moderator {
                                    Button("Remove", role: .destructive) {
                                        multiplayer.kick(member)
                                    }
                                }
                            }
                    }
                } header: {
                    Text(multiplayer.isHosting ? "Hosting" : "Connected")
                }

                Section("Chat") {
                    if multiplayer.messages.isEmpty {
                        Text("No messages yet.").foregroundStyle(.secondary)
                    }
                    ForEach(Array(multiplayer.messages.enumerated()), id: \.offset) { _, line in
                        Text(line).font(.callout)
                    }
                }

                Section {
                    Button("Leave Room", role: .destructive) { multiplayer.leave() }
                }
            }

            HStack {
                TextField("Message", text: $draft)
                    .textFieldStyle(.roundedBorder)
                    .submitLabel(.send)
                    .onSubmit(send)
                Button("Send", action: send)
                    .disabled(draft.isEmpty)
            }
            .padding(10)
            .background(.bar)
        }
    }

    private func send() {
        guard !draft.isEmpty else { return }
        multiplayer.send(draft)
        draft = ""
    }
}
