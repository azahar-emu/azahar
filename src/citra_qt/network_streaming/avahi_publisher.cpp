// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <unistd.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>

#include "citra_qt/network_streaming/avahi_publisher.h"
#include "common/logging/log.h"

namespace NetworkStreaming {

namespace {
std::string MakeServiceName() {
    char host[256] = "azahar";
    gethostname(host, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    return std::string("Azahar-") + host;
}

void EntryGroupCallback(AvahiEntryGroup* group, AvahiEntryGroupState state, void* userdata) {
    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        LOG_INFO(Frontend, "AvahiPublisher: service established");
        break;
    case AVAHI_ENTRY_GROUP_COLLISION:
    case AVAHI_ENTRY_GROUP_FAILURE:
        LOG_ERROR(Frontend, "AvahiPublisher: entry group failure");
        break;
    default:
        break;
    }
}

void ClientCallback(AvahiClient* client, AvahiClientState state, void* userdata) {
    auto* self = static_cast<AvahiPublisher*>(userdata);
    // avahi_client_new() can invoke this callback synchronously, before it has returned and
    // before Start() has stored its result in self->client - use the client pointer the
    // callback itself received instead of relying on that member being set yet.
    self->client = client;
    if (state == AVAHI_CLIENT_S_RUNNING) {
        self->RegisterService();
    } else if (state == AVAHI_CLIENT_FAILURE) {
        LOG_ERROR(Frontend, "AvahiPublisher: client failure: {}",
                 avahi_strerror(avahi_client_errno(client)));
    }
}
} // namespace

AvahiPublisher::AvahiPublisher() = default;

AvahiPublisher::~AvahiPublisher() {
    Stop();
}

void AvahiPublisher::RegisterService() {
    if (!group) {
        group = avahi_entry_group_new(client, EntryGroupCallback, this);
    }
    if (!group) {
        LOG_ERROR(Frontend, "AvahiPublisher: could not create entry group");
        return;
    }
    const std::string name = MakeServiceName();
    const int ret =
        avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                                      static_cast<AvahiPublishFlags>(0), name.c_str(),
                                      service_type.c_str(), nullptr, nullptr, port, nullptr);
    if (ret < 0) {
        LOG_ERROR(Frontend, "AvahiPublisher: could not add service: {}", avahi_strerror(ret));
        return;
    }
    avahi_entry_group_commit(group);
}

void AvahiPublisher::Start(const std::string& service_type_, u16 port_) {
    if (threaded_poll) {
        return;
    }
    service_type = service_type_;
    port = port_;

    threaded_poll = avahi_threaded_poll_new();
    if (!threaded_poll) {
        LOG_ERROR(Frontend, "AvahiPublisher: could not create poll");
        return;
    }

    int error = 0;
    client = avahi_client_new(avahi_threaded_poll_get(threaded_poll),
                             static_cast<AvahiClientFlags>(0), ClientCallback, this, &error);
    if (!client) {
        LOG_ERROR(Frontend, "AvahiPublisher: could not create client: {}", avahi_strerror(error));
        avahi_threaded_poll_free(threaded_poll);
        threaded_poll = nullptr;
        return;
    }

    avahi_threaded_poll_start(threaded_poll);
}

void AvahiPublisher::Stop() {
    if (!threaded_poll) {
        return;
    }
    avahi_threaded_poll_stop(threaded_poll);
    if (group) {
        avahi_entry_group_free(group);
        group = nullptr;
    }
    if (client) {
        avahi_client_free(client);
        client = nullptr;
    }
    avahi_threaded_poll_free(threaded_poll);
    threaded_poll = nullptr;
}

} // namespace NetworkStreaming
