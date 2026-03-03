// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <unordered_map>
#include <QCamera>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMetaObject>
#include <QThread>
#include <QVideoSink>
#include "citra_qt/camera/camera_util.h"
#include "citra_qt/camera/qt_camera_base.h"
#include "core/frontend/camera/interface.h"

namespace Camera {

// NOTE: Must be created on the Qt thread. QtMultimediaCameraHandlerFactory ensures this.
class QtMultimediaCameraHandler final : public QObject {
    Q_OBJECT

public:
    explicit QtMultimediaCameraHandler(const std::string& camera_name);
    ~QtMultimediaCameraHandler();

    Q_INVOKABLE void StartCapture();
    Q_INVOKABLE void StopCapture();

    QImage QtReceiveFrame() {
        return camera_surface->videoFrame().toImage();
    }

    bool IsPreviewAvailable() {
        return camera->isAvailable();
    }

    bool IsActive() {
        return camera->isActive();
    }

    [[nodiscard]] bool IsPaused() {
        return paused;
    }

    void PauseCapture() {
        StopCapture();
        paused = true;
    }

private:
    std::unique_ptr<QCamera> camera;
    std::unique_ptr<QVideoSink> camera_surface;
    QMediaCaptureSession capture_session{};
    bool paused = false; // was previously started but was paused, to be resumed
};

// NOTE: Must be created on the Qt thread.
class QtMultimediaCameraHandlerFactory final : public QObject {
    Q_OBJECT

public:
    Q_INVOKABLE std::shared_ptr<QtMultimediaCameraHandler> Create(const std::string& camera_name);
    void PauseCameras();
    void ResumeCameras();

private:
    std::unordered_map<std::string, std::weak_ptr<QtMultimediaCameraHandler>> handlers;
};

/// This class is only an interface. It just calls QtMultimediaCameraHandler.
class QtMultimediaCamera final : public QtCameraInterface {
public:
    QtMultimediaCamera(const std::shared_ptr<QtMultimediaCameraHandler>& handler,
                       const Service::CAM::Flip& flip)
        : QtCameraInterface(flip), handler(handler) {}

    void StartCapture() override {
        if (handler->thread() == QThread::currentThread()) {
            handler->StartCapture();
        } else {
            QMetaObject::invokeMethod(handler.get(), "StartCapture", Qt::BlockingQueuedConnection);
        }
    }

    void StopCapture() override {
        if (handler->thread() == QThread::currentThread()) {
            handler->StopCapture();
        } else {
            QMetaObject::invokeMethod(handler.get(), "StopCapture", Qt::BlockingQueuedConnection);
        }
    }

    void SetFrameRate(Service::CAM::FrameRate frame_rate) override {}

    QImage QtReceiveFrame() override {
        if (handler->thread() == QThread::currentThread()) {
            return handler->QtReceiveFrame();
        }

        QImage frame;
        QMetaObject::invokeMethod(
            handler.get(), [&]() { frame = handler->QtReceiveFrame(); },
            Qt::BlockingQueuedConnection);
        return frame;
    }

    bool IsPreviewAvailable() override {
        return handler->IsPreviewAvailable();
    }

private:
    std::shared_ptr<QtMultimediaCameraHandler> handler;
};

/// This class is only an interface. It just calls QtMultimediaCameraHandlerFactory.
class QtMultimediaCameraFactory final : public QtCameraFactory {
public:
    QtMultimediaCameraFactory(
        const std::shared_ptr<QtMultimediaCameraHandlerFactory>& handler_factory)
        : handler_factory(handler_factory) {}

    std::unique_ptr<CameraInterface> Create(const std::string& config,
                                            const Service::CAM::Flip& flip) override {
        return std::make_unique<QtMultimediaCamera>(handler_factory->Create(config), flip);
    }

private:
    std::shared_ptr<QtMultimediaCameraHandlerFactory> handler_factory;
};

} // namespace Camera
