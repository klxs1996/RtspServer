//#include <thread>
//#include <memory>
//#include <iostream>
//#include <string>
//#include "../example/rtsp_h264_file.h"
//
//bool new_connect = false;
//
//int main(int argc, char **argv)
//{
//	if (argc != 2) {
//		printf("Usage: %s test.h264\n", argv[0]);
//		return 0;
//	}
//
//	H264File h264_file;
//	if (!h264_file.Open("D:/test/Video/1080p.h264")) {
//		printf("Open %s failed.\n", argv[1]);
//		return 0;
//	}
//
//	std::string suffix = "live";
//	std::string ip = "127.0.0.1";
//	std::string port = "6666";
//	std::string rtsp_url = "rtsp://" + ip + ":" + port + "/" + suffix;
//
//	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
//	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
//
//	if (!server->Start("0.0.0.0", atoi(port.c_str()))) {
//		printf("RTSP Server listen on %s failed.\n", port.c_str());
//		return 0;
//	}
//
//#ifdef AUTH_CONFIG
//	server->SetAuthConfig("-_-", "admin", "12345");
//#endif
//
//	std::cout << "Play URL: " << rtsp_url << std::endl;
//
//	while (1) {
//		xop::Timer::Sleep(100);
//	}
//
//	getchar();
//	return 0;
//}
