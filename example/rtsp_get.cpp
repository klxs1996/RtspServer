#include "xop/RtspPusher.h"
#include "net/Timer.h"
#include <thread>
#include <memory>
#include <iostream>
#include <string>
#include "../example/rtsp_h264_file.h"

#define PORT "6666"

int main()
{
	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
	server->SetServerType(xop::RtspServer::RTSP_SERVER);

	if (!server->Start("0.0.0.0", atoi(PORT))) {
		printf("RTSP Server listen on %s failed.\n", PORT);
		return 0;
	}

//	server->OpenUrl("rtsp://192.168.1.156:554/cam/realmonitor?channel=1&subtype=0");
	server->OpenUrl("rtsp://127.0.0.1:554/live");

	while (1)
	{
		xop::Timer::Sleep(1000);
	}

	return 0;
}