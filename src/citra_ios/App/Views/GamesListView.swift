// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import SwiftUI
import UniformTypeIdentifiers

/// The game library: a list or grid of the imported games.
struct GamesListView: View {
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var settings: AppSettings
    @EnvironmentObject private var session: EmulationSession
    @EnvironmentObject private var compressor: GameCompressor

    @State private var searchText = ""
    @State private var gamePendingDeletion: Game?
    @State private var selectedGameInfo: Game?
    @State private var showingArtic = false

    /// Shows the file picker. The picked files are copied by the system first, so a failure can
    /// only happen before the import starts.
    private func importGames() {
        let presented = DocumentImporter.present { urls in
            library.importFiles(urls)
        }
        if !presented {
            library.importError = "The file picker could not be opened."
        }
    }

    @ObservedObject private var stats = GameStatsStore.shared

    private var filteredGames: [Game] {
        var games = library.games
        if !searchText.isEmpty {
            games = games.filter {
                $0.title.localizedCaseInsensitiveContains(searchText)
                    || $0.publisher.localizedCaseInsensitiveContains(searchText)
            }
        }
        // The library arrives sorted by title; the other orders fall back to it.
        let sorted: [Game]
        switch settings.librarySort {
        case 1:
            sorted = games.sorted {
                (stats.entry(for: $0).lastPlayed ?? .distantPast)
                    > (stats.entry(for: $1).lastPlayed ?? .distantPast)
            }
        case 2:
            sorted = games.sorted {
                stats.entry(for: $0).playSeconds > stats.entry(for: $1).playSeconds
            }
        case 3:
            sorted = games.sorted { compatibilityRank($0) < compatibilityRank($1) }
        default:
            sorted = games
        }
        guard settings.favoritesFirst else { return sorted }
        return sorted.filter { stats.isFavorite($0) } + sorted.filter { !stats.isFavorite($0) }
    }

    /// Best rated first, the games without a rating last.
    private func compatibilityRank(_ game: Game) -> Int {
        guard let rating = CompatibilityList.rating(of: game) else { return 99 }
        return ["Perfect", "Great", "Okay", "Bad", "Intro/Menu", "Won't Boot"]
            .firstIndex(of: rating.text) ?? 99
    }

    var body: some View {
        NavigationStack {
            Group {
                if library.games.isEmpty && !library.isScanning {
                    EmptyLibraryView { importGames() }
                } else if settings.libraryStyle == .grid {
                    gridView
                } else {
                    listView
                }
            }
            .navigationTitle("Games")
            .searchable(text: $searchText, prompt: "Search games")
            .refreshable { library.refresh() }
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Menu {
                        Picker("Library Style", selection: $settings.libraryStyle) {
                            ForEach(LibraryStyle.allCases) { style in
                                Label(style.title,
                                      systemImage: style == .list ? "list.bullet" : "square.grid.2x2")
                                    .tag(style)
                            }
                        }
                        Picker("Sort By", selection: $settings.librarySort) {
                            Text("Title").tag(0)
                            Text("Recently Played").tag(1)
                            Text("Most Played").tag(2)
                            Text("Compatibility").tag(3)
                        }
                        Toggle("Favorites First", isOn: $settings.favoritesFirst)
                    } label: {
                        Image(systemName: settings.libraryStyle == .list
                              ? "list.bullet" : "square.grid.2x2")
                    }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        showingArtic = true
                    } label: {
                        Image(systemName: "antenna.radiowaves.left.and.right")
                    }
                    .accessibilityLabel("Artic: stream from a 3DS")
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        importGames()
                    } label: {
                        Image(systemName: "plus")
                    }
                    .accessibilityLabel("Import games or install CIAs")
                }
            }
            .overlay {
                if compressor.isRunning, let name = compressor.currentName {
                    VStack(spacing: 10) {
                        Text("Compressing")
                            .font(.headline)
                        Text(name)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        ProgressView(value: compressor.progress)
                            .frame(width: 220)
                        Text("\(Int((compressor.progress * 100).rounded()))%")
                            .font(.caption.monospacedDigit())
                    }
                    .padding(24)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
                } else if let name = library.installName {
                    VStack(spacing: 10) {
                        Text("Installing")
                            .font(.headline)
                        Text(name)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        ProgressView(value: library.installProgress)
                            .frame(width: 220)
                        Text("\(Int((library.installProgress * 100).rounded()))%")
                            .font(.caption.monospacedDigit())
                    }
                    .padding(24)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
                } else if library.isImporting {
                    ProgressView("Importing…")
                        .padding(24)
                        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
                } else if library.isScanning && library.games.isEmpty {
                    ProgressView("Scanning…")
                }
            }
        }
        .alert("Delete Game?", isPresented: Binding(
            get: { gamePendingDeletion != nil },
            set: { if !$0 { gamePendingDeletion = nil } })
        ) {
            Button("Delete", role: .destructive) {
                if let game = gamePendingDeletion { library.delete(game) }
                gamePendingDeletion = nil
            }
            Button("Cancel", role: .cancel) { gamePendingDeletion = nil }
        } message: {
            Text("\(gamePendingDeletion?.title ?? "") will be removed from this device.")
        }
        .alert("Import Failed", isPresented: Binding(
            get: { library.importError != nil },
            set: { if !$0 { library.importError = nil } })
        ) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(library.importError ?? "")
        }
        .alert("Installed", isPresented: Binding(
            get: { library.installSummary != nil },
            set: { if !$0 { library.installSummary = nil } })
        ) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(library.installSummary ?? "")
        }
        .sheet(isPresented: $showingArtic) {
            ArticView()
        }
        .sheet(item: $selectedGameInfo) { game in
            GameInfoView(game: game)
        }
    }

    private var listView: some View {
        List(filteredGames) { game in
            Button {
                session.launch(game, settings: settings)
            } label: {
                GameRowView(game: game)
            }
            .buttonStyle(.plain)
            .contextMenu { contextMenu(for: game) }
        }
        .listStyle(.plain)
    }

    private var gridView: some View {
        ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 150, maximum: 200), spacing: 16)],
                      spacing: 16) {
                ForEach(filteredGames) { game in
                    Button {
                        session.launch(game, settings: settings)
                    } label: {
                        GameCardView(game: game)
                    }
                    .buttonStyle(LGTiltButtonStyle())
                    .contextMenu { contextMenu(for: game) }
                }
            }
            .padding()
        }
        .onAppear { TiltMotion.shared.start() }
        .onDisappear { TiltMotion.shared.stop() }
    }

    @ViewBuilder
    private func contextMenu(for game: Game) -> some View {
        Button {
            session.launch(game, settings: settings)
        } label: {
            Label("Play", systemImage: "play.fill")
        }
        Button {
            stats.toggleFavorite(game)
        } label: {
            Label(stats.isFavorite(game) ? "Remove from Favorites" : "Add to Favorites",
                  systemImage: stats.isFavorite(game) ? "star.slash" : "star")
        }
        Button {
            selectedGameInfo = game
        } label: {
            Label("Game Info", systemImage: "info.circle")
        }
        if let compressed = game.compressedExtension {
            Button {
                compressor.compress([game.url],
                                    deleteSources: !settings.keepOriginalsWhenCompressing,
                                    library: library)
            } label: {
                Label("Convert to .\(compressed)", systemImage: "arrow.down.right.and.arrow.up.left")
            }
            .disabled(compressor.isRunning)
        }
        Button(role: .destructive) {
            gamePendingDeletion = game
        } label: {
            Label("Delete", systemImage: "trash")
        }
    }
}

private struct EmptyLibraryView: View {
    let onImport: () -> Void

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "gamecontroller")
                .font(.system(size: 56))
                .foregroundStyle(.secondary)
            Text("No Games Found")
                .font(.title2.bold())
            Text("Tap the + button to add your legally dumped and decrypted 3DS games, "
                 + "or copy them into the ROMs folder in the Files app.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 32)
            Button(action: onImport) {
                Label("Import Games", systemImage: "plus")
            }
            .buttonStyle(.borderedProminent)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

extension GameInfoView {
    fileprivate func backUp() {
        do {
            let url = try SaveManager.export(game)
            backups = SaveManager.backups(of: game)
            saveMessage = "Saved as \(url.lastPathComponent) in Files › Azahar › saves."
        } catch {
            saveMessage = error.localizedDescription
        }
    }

    fileprivate func restore(from folder: URL) {
        do {
            try SaveManager.restore(game, from: folder)
            backups = SaveManager.backups(of: game)
            saveMessage = "The save data was restored."
        } catch {
            saveMessage = error.localizedDescription
        }
    }
}

struct GameInfoView: View {
    let game: Game
    @ObservedObject private var stats = GameStatsStore.shared
    @State private var backups: [URL] = []
    @State private var pendingRestore: URL?
    @State private var saveMessage: String?
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack(spacing: 16) {
                        GameIconView(icon: game.icon, size: 72)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(game.title).font(.headline)
                            Text(game.publisher).font(.subheadline).foregroundStyle(.secondary)
                        }
                    }
                    .padding(.vertical, 4)
                }
                Section {
                    NavigationLink {
                        GameSettingsView(game: game)
                    } label: {
                        Label("Game Settings", systemImage: "slider.horizontal.3")
                    }
                }
                Section("Played") {
                    LabeledContent("Play Time", value: GameStatsStore.playTimeText(
                        stats.entry(for: game).playSeconds))
                    if let last = stats.entry(for: game).lastPlayed {
                        LabeledContent("Last Played",
                                       value: last.formatted(date: .abbreviated, time: .shortened))
                    }
                    if let rating = CompatibilityList.rating(of: game) {
                        HStack {
                            Text("Compatibility")
                            Spacer()
                            Circle().fill(rating.color).frame(width: 10, height: 10)
                            Text(rating.text).foregroundStyle(.secondary)
                        }
                    }
                    Button(stats.isFavorite(game) ? "Remove from Favorites" : "Add to Favorites") {
                        stats.toggleFavorite(game)
                    }
                }
                if !game.isInstalled || SaveManager.hasSave(of: game) {
                    Section {
                        Button("Back Up Save Data") { backUp() }
                            .disabled(!SaveManager.hasSave(of: game))
                        Button("Restore Save Data from a Folder…") {
                            FolderPicker.present { folder in
                                if let folder { pendingRestore = folder }
                            }
                        }
                        ForEach(backups, id: \.self) { backup in
                            Button {
                                pendingRestore = backup
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text("Restore").foregroundStyle(.primary)
                                    Text(backup.lastPathComponent)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                        .truncationMode(.middle)
                                }
                            }
                        }
                        Button("Show Saves Folder in Files") {
                            try? FileManager.default.createDirectory(
                                at: SaveManager.backupsDirectory, withIntermediateDirectories: true)
                            FileLocations.showInFiles(SaveManager.backupsDirectory)
                        }
                    } header: {
                        Text("Save Data")
                    } footer: {
                        Text("A backup is a copy of the game's save in Files › Azahar › saves. "
                             + "Restoring replaces the save of the game with a backup (or any "
                             + "folder with a save) and keeps the save it had as a backup first.")
                    }
                }
                Section("Details") {
                    LabeledContent("Title ID", value: game.programIDString)
                    LabeledContent("Region", value: game.regions)
                    LabeledContent("File Type", value: game.fileType)
                    LabeledContent("File", value: game.url.lastPathComponent)
                }
            }
            .navigationTitle("Game Info")
            .navigationBarTitleDisplayMode(.inline)
            .onAppear { backups = SaveManager.backups(of: game) }
            .confirmationDialog(
                "Replace the save data?",
                isPresented: Binding(get: { pendingRestore != nil },
                                     set: { if !$0 { pendingRestore = nil } }),
                titleVisibility: .visible
            ) {
                Button("Restore", role: .destructive) {
                    if let folder = pendingRestore { restore(from: folder) }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("The save data the game has now is kept as a backup first.")
            }
            .alert("Save Data", isPresented: Binding(
                get: { saveMessage != nil }, set: { if !$0 { saveMessage = nil } })
            ) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(saveMessage ?? "")
            }
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
