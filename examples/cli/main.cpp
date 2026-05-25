#include "galahad/LiveController.h"
#include "galahad/OscUdpTransport.h"
#include "galahad/RealtimeEvent.h"
#include "galahad/SpscQueue.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    auto transport = std::make_unique<galahad::OscUdpTransport>("127.0.0.1", 11000, 11001);
    galahad::LiveController controller(std::move(transport));
    galahad::SpscQueue<galahad::RealtimeEvent, 256> realtimeEvents;

    controller.onEvent([&realtimeEvents](const galahad::Command& command) {
        std::cout << "Received: " << command.address << " (" << command.arguments.size() << " args)" << std::endl;

        if (auto event = galahad::toRealtimeEvent(command))
            realtimeEvents.tryPush(*event);
    });

    if (!controller.connect())
    {
        std::cerr << "Could not start Galahad transport" << std::endl;
        return 1;
    }

    std::cout << "Sending /live/song/start_playing" << std::endl;
    controller.startSong();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Sending /live/clip/fire 0 0" << std::endl;
    controller.fireClip(0, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Sending /live/track/set/volume 0 0.75" << std::endl;
    controller.setTrackVolume(0, 0.75f);

    std::cout << "Sending /live/song/set/tempo 128" << std::endl;
    controller.setTempo(128.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    galahad::RealtimeEvent event;
    while (realtimeEvents.tryPop(event))
        std::cout << "Realtime event: " << galahad::toString(event.type) << std::endl;

    controller.disconnect();

    return 0;
}
