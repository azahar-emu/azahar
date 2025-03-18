// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once
#include <common/common_types.h>
#include <string>
#include <vector>
#include <network/network.h>

namespace Core {
    class System;
}

namespace Network {
    class AnnounceMultiplayerSession;
}

enum class NetPlayStatus : s32 {
    NO_ERROR,

    NETWORK_ERROR,
    LOST_CONNECTION,
    NAME_COLLISION,
    MAC_COLLISION,
    CONSOLE_ID_COLLISION,
    WRONG_VERSION,
    WRONG_PASSWORD,
    COULD_NOT_CONNECT,
    ROOM_IS_FULL,
    HOST_BANNED,
    PERMISSION_DENIED,
    NO_SUCH_USER,
    ALREADY_IN_ROOM,
    CREATE_ROOM_ERROR,
    HOST_KICKED,
    UNKNOWN_ERROR,

    ROOM_UNINITIALIZED,
    ROOM_IDLE,
    ROOM_JOINING,
    ROOM_JOINED,
    ROOM_MODERATOR,

    MEMBER_JOIN,
    MEMBER_LEAVE,
    MEMBER_KICKED,
    MEMBER_BANNED,
    ADDRESS_UNBANNED,

    CHAT_MESSAGE,
};

class AndroidMultiplayer {
public:
    explicit AndroidMultiplayer(Core::System& system,
                                std::shared_ptr<Network::AnnounceMultiplayerSession> session);
    ~AndroidMultiplayer();

    bool NetworkInit();

    void AddNetPlayMessage(int status, const std::string& msg);
    void AddNetPlayMessage(jint type, jstring msg);

    void ClearChat();

    NetPlayStatus NetPlayCreateRoom(const std::string &ipaddress, int port,
                                    const std::string &username, const std::string &preferedGameName,const u64 &preferedGameId , const std::string &password,
                                    const std::string &room_name, int max_players);

    NetPlayStatus NetPlayJoinRoom(const std::string &ipaddress, int port,
                                  const std::string &username, const std::string &password);

    std::vector<std::string> NetPlayRoomInfo();

    bool NetPlayIsJoined();

    bool NetPlayIsHostedRoom();

    bool NetPlayIsModerator();

    void NetPlaySendMessage(const std::string &msg);

    void NetPlayKickUser(const std::string &username);

    void NetPlayBanUser(const std::string &username);

    void NetPlayLeaveRoom();

    static void NetworkShutdown();

    std::vector<std::string> NetPlayGetBanList();

    void NetPlayUnbanUser(const std::string &username);

    std::vector<std::string> NetPlayGetPublicRooms();

private:
    Core::System& system;
    static std::unique_ptr<Network::VerifyUser::Backend> CreateVerifyBackend(bool use_validation) ;
    std::weak_ptr<Network::AnnounceMultiplayerSession> announce_multiplayer_session;


};
